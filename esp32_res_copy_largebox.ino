
#include <Arduino.h>
#include "WiFi.h"
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoOTA.h>
#include <DS18B20.h>
#include <Preferences.h>
#include <HTTPClient.h>
#include "time.h"
#include "src/reading_cycle.h"
#include "src/api_commands.h"
#include "src/api_json.h"
#include "src/pi_cache.h"
#include "src/ranges.h"
#include "src/reading_log.h"
#include "src/reading_queue.h"
#include "src/sensor_math.h"
#include "src/wifi_setup.h"
#include "src/web_assets.h"


#define totalcount 200

const char *setupApName = "EnvMonitor-Setup";
const unsigned long setupPortalTimeout = 300000;  //5 min before retrying saved network
volatile bool wifiSaved = false;

// Title and interval are changed by POST /api/settings and read by handlers; loop() writes the
// pending values to LittleFS because writeFile() blocks. settingsLock guards all six.
String title = "Server";
unsigned long interval0 = 300000;  //min between readings 60000 for 1
SemaphoreHandle_t settingsLock;
bool hasPendingTitle = false;
bool hasPendingInterval = false;
String pendingTitle;
unsigned long pendingInterval = 0;
const unsigned long holdTimeout = 900000;  //15 min before an abandoned wizard resumes sampling


const char *ntpServer = "pool.ntp.org";
const long gmtOffset_sec = -18000;    //Replace with your GMT offset (seconds)
const int daylightOffset_sec = 3600;  //Replace with your daylight offset (seconds)

DS18B20 ds(8);
const int relay = 1;
const int sensorPin = 3;  //pH and EC share this input, in the D1 mini A0 position

enum class SettleStep { Idle, Holding, EcState, PhState };
volatile bool settleRequested = false;
SettleStep settleStep = SettleStep::Idle;
String settleLog;
unsigned long settleStart = 0;
unsigned long lastSettleRead = 0;
const unsigned long settleEcTime = 10000;
const unsigned long settlePhTime = 90000;
const int settlePins[] = { 0, 2, 3, 4, 5 };  //every free analog pin; GPIO 1 drives the relay

// readingsLock guards readings and lastPhUncertainty: the sampling task appends, handlers read
ReadingLog<totalcount> readings;
float lastPhUncertainty = 0;  //of the newest reading, not persisted
SemaphoreHandle_t readingsLock;

// Last replies from the Pi's /stats, /reports/latest and /thresholds, fetched one per loop() pass
PiCache piCache;
SemaphoreHandle_t piCacheLock;

// Readings API on the LAN: finished readings are queued by the sampling task and sent from loop().
// The URL, token and device name live in NVS (Preferences namespace "api"), set through /apiconfig.
ReadingQueue<24> apiQueue;  //4 hours at a 10 min interval
SemaphoreHandle_t apiQueueLock;
String apiUrl;
String apiToken;
String apiDevice;
volatile bool apiConfigChanged = true;
int apiFailures = 0;
unsigned long apiNextAttempt = 0;
int apiLastCode = 0;
unsigned long apiDelivered = 0;
unsigned long apiRejected = 0;
unsigned long apiNoClock = 0;

// Webserver
AsyncWebServer server(80);
String PARAM_MESSAGE = "status";


void notFound(AsyncWebServerRequest *request) {
  request->send(404, "text/plain", "Not found");
}


class LittleFsStore {

public:

  std::string read(const char *path) {
    return readFile(path).c_str();
  }

  //Written to a temporary file first so a power cut leaves the last good save
  void write(const char *path, const std::string &text) {
    const char *tempPath = "/readings.tmp";
    File file = LittleFS.open(tempPath, "w");
    if (!file) {
      Serial.println("Failed to open file for writing");
      return;
    }
    size_t written = file.print(text.c_str());
    file.close();
    if (written != text.length()) {
      Serial.println("Write failed");
      return;
    }
    if (!LittleFS.rename(tempPath, path)) {
      LittleFS.remove(path);
      LittleFS.rename(tempPath, path);
    }
  }
};

LittleFsStore readingStore;

String getDateTime() {

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
	return "";
  }
  char timeStringBuff[22];  // Buffer for formatted time string
  strftime(timeStringBuff, sizeof(timeStringBuff), "%Y,%m-1,%d,%H,%M,%S", &timeinfo);

  return timeStringBuff;
}

float lastEcAdc = 0;

float takeec() {
  float ecadc = 0;
  for (int i = 0; i < 20; i++) {
    delay(100);
    ecadc += analogRead(sensorPin);  //same input as pH, the relay selects which sensor is powered
  }
  ecadc = ecadc / 20.0;
  lastEcAdc = ecadc;
  return ecFromAdc(ecadc);
}

//LittleFS functions below here
String readFile(const char *path) {
  String result = "";
  Serial.printf("Reading file: %s\n", path);
  File file = LittleFS.open(path, "r");
  if (!file) {
    Serial.println("Failed to open file for reading");
    return result;
  }
  while (file.available()) {
    result += (char)file.read();
  }
  file.close();
  Serial.println("\nRead complete");
  return result;
}



void writeFile(const char *path, const char *message) {
  Serial.printf("Writing file: %s\n", path);

  File file = LittleFS.open(path, "w");
  if (!file) {
    Serial.println("Failed to open file for writing");
    return;
  }
  if (file.print(message)) {
    Serial.println("File written");
  } else {
    Serial.println("Write failed");
  }
  delay(2000);  // Make sure the CREATE and LASTWRITE times are different
  file.close();
}
//LittleFS above here

class ReadingCycle;
extern ReadingCycle cycle;

class DeviceHost : public CycleHost {

public:

  float readTemperature() override {
    return ds.getTempF();
  }

  float readEc() override {
    return takeec();
  }

  float readPhAdc() override {
    return analogRead(sensorPin);
  }

  void setPhPower(bool on) override {
    digitalWrite(relay, on ? HIGH : LOW);
    delay(500);
  }

  std::string timestamp() override {
    return getDateTime().c_str();
  }

  void recordReading(const Reading &reading) override {
    xSemaphoreTake(readingsLock, portMAX_DELAY);
    readings.append(reading.timestamp, reading.ph, reading.ec, reading.wt);
    lastPhUncertainty = reading.phUncertainty;
    readings.save(readingStore);
    xSemaphoreGive(readingsLock);
    queueForApi(reading);
  }

  //Only queues; loop() does the network call so sampling never waits on the API
  void queueForApi(const Reading &reading) {
    QueuedReading queued;
    queued.epoch = time(nullptr);
    if (!clockIsSet(queued.epoch)) {
      apiNoClock++;
      return;
    }
    queued.ph = reading.ph;
    queued.phUncertainty = reading.phUncertainty;
    queued.phAdc = cycle.lastPhAdc();
    queued.ec = reading.ec;
    queued.ecAdc = lastEcAdc;
    queued.wt = reading.wt;
    xSemaphoreTake(apiQueueLock, portMAX_DELAY);
    apiQueue.push(queued);
    xSemaphoreGive(apiQueueLock);
  }

  void saveCalibration(float ph4, float ph7) override {
    writeFile("/ph4.txt", String(ph4).c_str());
    writeFile("/ph7.txt", String(ph7).c_str());
  }

  void wipeReadings() override {
    xSemaphoreTake(readingsLock, portMAX_DELAY);
    readings.clear();
    lastPhUncertainty = 0;
    readings.save(readingStore);
    xSemaphoreGive(readingsLock);
  }

  void restart() override {
    ESP.restart();
  }
};

DeviceHost deviceHost;
ReadingCycle cycle(deviceHost, 2368., 1880., interval0, holdTimeout);

void loadApiConfig() {
  Preferences apiPrefs;
  apiPrefs.begin("api", true);
  apiUrl = apiPrefs.getString("url", "");
  apiToken = apiPrefs.getString("token", "");
  apiDevice = apiPrefs.getString("device", "envmonitor-1");
  apiPrefs.end();
}

String apiStatus() {
  xSemaphoreTake(apiQueueLock, portMAX_DELAY);
  int queued = apiQueue.size();
  int dropped = apiQueue.dropped();
  xSemaphoreGive(apiQueueLock);
  return "api url=" + (apiUrl.length() ? apiUrl : String("(not set)")) + " token=" + (apiToken.length() ? "set" : "not set") +
         " device=" + apiDevice + "\napi queued=" + String(queued) + " delivered=" + String(apiDelivered) +
         " rejected=" + String(apiRejected) + " dropped=" + String(dropped) + " skipped_no_clock=" + String(apiNoClock) +
         " last_http=" + String(apiLastCode) + " failures=" + String(apiFailures) + "\n";
}

//Sends the oldest queued reading, one per call, backing off while the API is unreachable
void sendQueuedReading() {
  if (apiConfigChanged) {
    apiConfigChanged = false;
    loadApiConfig();
    apiFailures = 0;
    apiNextAttempt = 0;
    //Start the Pi fetch over, so a new URL or device is fetched now and the old replies are dropped
    xSemaphoreTake(piCacheLock, portMAX_DELAY);
    piCache = PiCache();
    xSemaphoreGive(piCacheLock);
  }
  if (apiUrl.length() == 0 || apiToken.length() == 0 || WiFi.status() != WL_CONNECTED) {
    return;
  }
  if (apiNextAttempt != 0 && (long)(millis() - apiNextAttempt) < 0) {
    return;
  }
  xSemaphoreTake(apiQueueLock, portMAX_DELAY);
  bool empty = apiQueue.size() == 0;
  QueuedReading reading;
  if (!empty) {
    reading = apiQueue.front();
  }
  xSemaphoreGive(apiQueueLock);
  if (empty) {
    return;
  }

  HTTPClient http;
  http.setConnectTimeout(3000);
  http.setTimeout(3000);
  http.begin(apiUrl + "/readings");
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", "Bearer " + apiToken);
  apiLastCode = http.POST(readingJson(apiDevice.c_str(), reading).c_str());
  http.end();

  SendOutcome outcome = outcomeFor(apiLastCode);
  if (outcome == SendOutcome::RetryLater) {
    apiFailures++;
    apiNextAttempt = millis() + retryDelayMs(apiFailures);
    return;
  }
  if (outcome == SendOutcome::Delivered) {
    apiDelivered++;
  } else {
    apiRejected++;
  }
  apiFailures = 0;
  apiNextAttempt = 0;
  xSemaphoreTake(apiQueueLock, portMAX_DELAY);
  //The queue may have dropped this reading while it was being sent, if it filled up meanwhile
  if (apiQueue.size() > 0 && apiQueue.front().epoch == reading.epoch) {
    apiQueue.pop();
  }
  xSemaphoreGive(apiQueueLock);
}

//Fetches one Pi endpoint per call, every PiCache::ROUND_MS. The lock is never held across the GET.
void fetchPiCache() {
  if (apiUrl.length() == 0 || apiToken.length() == 0) {
    xSemaphoreTake(piCacheLock, portMAX_DELAY);
    piCache.markUnconfigured();
    xSemaphoreGive(piCacheLock);
    return;
  }
  xSemaphoreTake(piCacheLock, portMAX_DELAY);
  bool due = piCache.due(millis());
  PiEndpoint endpoint = PiEndpoint::Stats;
  if (due) {
    endpoint = piCache.next();
  }
  xSemaphoreGive(piCacheLock);
  if (!due) {
    return;
  }

  HTTPClient http;
  http.setConnectTimeout(3000);
  http.setTimeout(3000);
  http.begin(apiUrl + piPath(endpoint, apiDevice.c_str()).c_str());
  http.addHeader("Authorization", "Bearer " + apiToken);
  int code = http.GET();
  String body = code == 200 ? http.getString() : String();
  http.end();

  xSemaphoreTake(piCacheLock, portMAX_DELAY);
  piCache.record(endpoint, code, body.c_str(), millis());
  xSemaphoreGive(piCacheLock);
}

//Writes settings queued by POST /api/settings. Copies them out first: writeFile blocks for 2 s.
void savePendingSettings() {
  xSemaphoreTake(settingsLock, portMAX_DELAY);
  bool saveTitle = hasPendingTitle;
  bool saveInterval = hasPendingInterval;
  String newTitle = pendingTitle;
  unsigned long newInterval = pendingInterval;
  hasPendingTitle = false;
  hasPendingInterval = false;
  xSemaphoreGive(settingsLock);
  if (saveInterval) {
    cycle.setInterval(newInterval);
    writeFile("/interval0.txt", String(newInterval).c_str());
  }
  if (saveTitle) {
    writeFile("/title.txt", newTitle.c_str());
  }
}

//Saves readings API settings to NVS. A field whose has* flag is false is left unchanged.
//Returns an error message, or "" when saved.
String saveApiConfig(bool hasUrl, String url, bool hasToken, const String &token, bool hasDevice, const String &device) {
  while (url.endsWith("/")) {
    url.remove(url.length() - 1);
  }
  if (hasUrl && url.length() && !url.startsWith("http://")) {
    return "url must start with http://";
  }
  Preferences apiPrefs;
  apiPrefs.begin("api", false);
  if (hasUrl) {
    apiPrefs.putString("url", url);
  }
  if (hasToken) {
    apiPrefs.putString("token", token);
  }
  if (hasDevice) {
    apiPrefs.putString("device", device);
  }
  apiPrefs.end();
  apiConfigChanged = true;
  return "";
}

//Form body value, or "" when the field is missing
String formValue(AsyncWebServerRequest *request, const char *name) {
  return request->hasParam(name, true) ? request->getParam(name, true)->value() : String();
}

void sendJson(AsyncWebServerRequest *request, int code, const std::string &json) {
  request->send(code, "application/json", json.c_str());
}

void sendError(AsyncWebServerRequest *request, int code, const std::string &message) {
  sendJson(request, code, "{\"error\":" + jsonString(message) + "}");
}

void sendOk(AsyncWebServerRequest *request, int code = 200) {
  sendJson(request, code, "{\"ok\":true}");
}

const char *stepName(CycleStep step) {
  switch (step) {
    case CycleStep::Holding: return "holding";
    case CycleStep::Warmup: return "warmup";
    case CycleStep::Sampling: return "sampling";
    case CycleStep::Restarting: return "restarting";
    default: return "waiting";
  }
}

const char *jobName(CycleJob job) {
  switch (job) {
    case CycleJob::Ph4: return "ph4";
    case CycleJob::Ph7: return "ph7";
    case CycleJob::Manual: return "manual";
    default: return "reading";
  }
}

const char *stateName(CycleStatus status) {
  switch (status) {
    case CycleStatus::Busy: return "busy";
    case CycleStatus::Holding: return "holding";
    default: return "idle";
  }
}

std::string statusText() {
  StatusView s;
  xSemaphoreTake(settingsLock, portMAX_DELAY);
  s.title = title.c_str();
  s.intervalMin = interval0 / 60000;
  xSemaphoreGive(settingsLock);
  s.uptimeS = millis() / 1000;
  s.freeHeap = ESP.getFreeHeap();

  xSemaphoreTake(readingsLock, portMAX_DELAY);
  s.count = readings.size();
  if (s.count > 0) {
    s.lastTime = readings.timestampAt(s.count - 1);
    s.ph = readings.ph().currentValue;
    s.ec = readings.ec().currentValue;
    s.wt = readings.wt().currentValue;
    s.phPm = lastPhUncertainty;
  }
  s.means[0] = readings.ph().mean;
  s.means[1] = readings.ec().mean;
  s.means[2] = readings.wt().mean;
  //Rounding can leave a variance a hair below zero
  s.stds[0] = sqrt(max(readings.ph().sVar, 0.0f));
  s.stds[1] = sqrt(max(readings.ec().sVar, 0.0f));
  s.stds[2] = sqrt(max(readings.wt().sVar, 0.0f));
  xSemaphoreGive(readingsLock);

  unsigned long now = millis();
  s.state = stateName(cycle.status());
  s.step = stepName(cycle.step());
  s.job = jobName(cycle.job());
  s.warmupElapsedMs = cycle.warmupElapsed(now);
  s.samplesTaken = cycle.samplesTaken();
  s.ph4Ready = cycle.ph4Ready();
  s.pendingPh4 = cycle.pendingPh4();
  s.manualSeq = cycle.manualSeq();
  const Reading &manual = cycle.manualReading();
  s.manualPh = manual.ph;
  s.manualEc = manual.ec;
  s.manualWt = manual.wt;
  s.calibrationSeq = cycle.calibrationSeq();
  s.ph4Adc = cycle.ph4();
  s.ph7Adc = cycle.ph7();
  s.lastPhAdc = cycle.lastPhAdc();
  s.phSampleCount = cycle.lastPhSampleCount();
  if (s.phSampleCount > 20) {
    s.phSampleCount = 20;
  }
  for (int i = 0; i < s.phSampleCount; i++) {
    s.phSamples[i] = cycle.lastPhSample(i);
  }
  s.lastEcAdc = lastEcAdc;

  xSemaphoreTake(apiQueueLock, portMAX_DELAY);
  s.apiQueued = apiQueue.size();
  xSemaphoreGive(apiQueueLock);
  s.apiLastHttp = apiLastCode;
  s.apiFailures = apiFailures;
  s.apiDelivered = apiDelivered;
  return statusJson(s);
}

//Device ranges saved by POST /api/ranges, as rangesJson text in NVS
bool loadDeviceRanges(Ranges &out) {
  Preferences rangePrefs;
  if (!rangePrefs.begin("ranges", true)) {
    return false;  //namespace not created yet
  }
  String text = rangePrefs.getString("json", "");
  rangePrefs.end();
  std::string error;
  return text.length() && parseThresholds(text.c_str(), out, error);
}

std::string rangesText() {
  Ranges device, pi, chosen;
  bool hasDevice = loadDeviceRanges(device);
  std::string piText;
  xSemaphoreTake(piCacheLock, portMAX_DELAY);
  bool hasPiText = piCache.thresholdsBody(piText);
  xSemaphoreGive(piCacheLock);
  std::string error;
  bool hasPi = hasPiText && parseThresholds(piText, pi, error);
  RangeSource source = chooseRanges(hasDevice, device, hasPi, pi, chosen);
  return std::string("{\"source\":") + jsonString(rangeSourceName(source)) + ",\"ranges\":" + rangesJson(chosen) +
         ",\"pi\":" + (hasPi ? rangesJson(pi) : std::string("null")) + "}";
}

//The JSON API used by the embedded app. Bodies are form-urlencoded, replies are JSON.
void registerApiRoutes() {
  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    sendJson(request, 200, statusText());
  });

  server.on("/api/readings", HTTP_GET, [](AsyncWebServerRequest *request) {
    xSemaphoreTake(readingsLock, portMAX_DELAY);
    std::string json = readingsJson(readings);
    xSemaphoreGive(readingsLock);
    sendJson(request, 200, json);
  });

  server.on("/api/settings", HTTP_GET, [](AsyncWebServerRequest *request) {
    xSemaphoreTake(settingsLock, portMAX_DELAY);
    std::string json = settingsJson(interval0 / 60000, title.c_str());
    xSemaphoreGive(settingsLock);
    sendJson(request, 200, json);
  });

  server.on("/api/settings", HTTP_POST, [](AsyncWebServerRequest *request) {
    String interval = formValue(request, "interval");
    String newTitle = formValue(request, "title");
    std::string error;
    if (!validateSettings(interval.c_str(), newTitle.c_str(), error)) {
      sendError(request, 400, error);
      return;
    }
    //Live values change now so the next GET shows them; loop() persists them
    xSemaphoreTake(settingsLock, portMAX_DELAY);
    if (interval.length()) {
      interval0 = interval.toInt() * 60000UL;
      pendingInterval = interval0;
      hasPendingInterval = true;
    }
    if (newTitle.length()) {
      title = newTitle;
      pendingTitle = newTitle;
      hasPendingTitle = true;
    }
    xSemaphoreGive(settingsLock);
    sendOk(request);
  });

  server.on("/api/command", HTTP_POST, [](AsyncWebServerRequest *request) {
    CommandResult result = dispatchCommand(formValue(request, "cmd").c_str(), cycle);
    if (result.httpCode == 202) {
      sendOk(request, 202);
    } else {
      sendError(request, result.httpCode, result.message);
    }
  });

  server.on("/api/pi", HTTP_GET, [](AsyncWebServerRequest *request) {
    xSemaphoreTake(piCacheLock, portMAX_DELAY);
    std::string json = piCache.json(millis());
    xSemaphoreGive(piCacheLock);
    sendJson(request, 200, json);
  });

  server.on("/api/ranges", HTTP_GET, [](AsyncWebServerRequest *request) {
    sendJson(request, 200, rangesText());
  });

  server.on("/api/ranges", HTTP_POST, [](AsyncWebServerRequest *request) {
    std::string fields[RANGE_FIELD_COUNT];
    for (int i = 0; i < RANGE_FIELD_COUNT; i++) {
      fields[i] = formValue(request, rangeFieldName(i)).c_str();
    }
    Ranges ranges;
    std::string error;
    if (!rangesFromForm(fields, ranges, error)) {
      sendError(request, 400, error);
      return;
    }
    Preferences rangePrefs;
    rangePrefs.begin("ranges", false);
    size_t written = rangePrefs.putString("json", rangesJson(ranges).c_str());
    rangePrefs.end();
    if (written == 0) {
      sendError(request, 500, "could not save ranges");
      return;
    }
    sendOk(request);
  });

  server.on("/api/ranges", HTTP_DELETE, [](AsyncWebServerRequest *request) {
    Preferences rangePrefs;
    rangePrefs.begin("ranges", false);
    rangePrefs.remove("json");
    rangePrefs.end();
    sendOk(request);
  });

  //Reads NVS rather than apiUrl and friends, which loop() reloads
  server.on("/api/apiconfig", HTTP_GET, [](AsyncWebServerRequest *request) {
    Preferences apiPrefs;
    apiPrefs.begin("api", true);
    String url = apiPrefs.getString("url", "");
    String device = apiPrefs.getString("device", "envmonitor-1");
    bool tokenSet = apiPrefs.getString("token", "").length() > 0;
    apiPrefs.end();
    sendJson(request, 200, "{\"url\":" + jsonString(url.c_str()) + ",\"device\":" + jsonString(device.c_str()) +
                               ",\"token_set\":" + jsonBool(tokenSet) + "}");
  });

  //An empty field means "not provided": the app always sends url and device
  server.on("/api/apiconfig", HTTP_POST, [](AsyncWebServerRequest *request) {
    String url = formValue(request, "url");
    String token = formValue(request, "token");
    String device = formValue(request, "device");
    String error = saveApiConfig(url.length() > 0, url, token.length() > 0, token, device.length() > 0, device);
    if (error.length()) {
      sendError(request, 400, error.c_str());
      return;
    }
    sendOk(request);
  });
}

void restoreAllData() {
  readings.restore(readingStore);
  //settings variables below
  interval0 = readFile("/interval0.txt").toInt();
  title = readFile("/title.txt");
}

// Serves a Wi-Fi form on an access point, never returns: restarts once credentials
// are saved, or after setupPortalTimeout if there is a saved network to retry
void runWifiSetupPortal(bool hasSavedNetwork) {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(setupApName);
  Serial.print("Join ");
  Serial.print(setupApName);
  Serial.print(" and open http://");
  Serial.println(WiFi.softAPIP());

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html",
                  "<!DOCTYPE html><html><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
                  "<title>Wi-Fi Setup</title></head><body><h1>Wi-Fi Setup</h1>"
                  "<form action=\"/savewifi\" method=\"post\">"
                  "<p>Network name <input name=\"ssid\" maxlength=\"32\" required>"
                  "<p>Password <input name=\"password\" type=\"password\" maxlength=\"63\">"
                  "<p><button type=\"submit\">Save and connect</button></form></body></html>");
  });

  server.on("/savewifi", HTTP_POST, [](AsyncWebServerRequest *request) {
    String newSsid = request->hasParam("ssid", true) ? request->getParam("ssid", true)->value() : "";
    String newPassword = request->hasParam("password", true) ? request->getParam("password", true)->value() : "";
    if (!validWifiCredentials(newSsid.c_str(), newPassword.c_str())) {
      request->redirect("/");
      return;
    }
    Preferences wifiPrefs;
    wifiPrefs.begin("wifi", false);
    wifiPrefs.putString("ssid", newSsid);
    wifiPrefs.putString("password", newPassword);
    wifiPrefs.end();
    wifiSaved = true;
    request->send(200, "text/plain", "Saved. Restarting to connect...");
  });

  server.onNotFound(notFound);
  server.begin();

  unsigned long portalStart = millis();
  while (!setupPortalFinished(wifiSaved, hasSavedNetwork, millis() - portalStart, setupPortalTimeout)) {
    delay(100);
  }
  delay(1000);  //let the response finish sending
  ESP.restart();
}

void setup() {
  apiQueueLock = xSemaphoreCreateMutex();
  readingsLock = xSemaphoreCreateMutex();
  piCacheLock = xSemaphoreCreateMutex();
  settingsLock = xSemaphoreCreateMutex();
  Serial.begin(115200);

  Serial.println("Starting the LittleFS Webserver..");

  // Begin LittleFS
  if (!LittleFS.begin()) {
    Serial.println("An Error has occurred while mounting LittleFS");
    return;
  }

   restoreAllData();
  //reset the interval if it is outside of the range; done before the server starts, as handlers change it
  if (interval0 < 120000 || interval0 > 43200000) { interval0 = 600000; }

  // Connect to WIFI, credentials are kept in NVS so LittleFS uploads don't erase them
  Preferences wifiPrefs;
  wifiPrefs.begin("wifi", true);
  String ssid = wifiPrefs.getString("ssid", "");
  String password = wifiPrefs.getString("password", "");
  wifiPrefs.end();

  if (ssid.length() == 0) {
    runWifiSetupPortal(false);
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), password.c_str());
  if (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Starting setup portal...");
    runWifiSetupPortal(true);
  }
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  //DateTime
  delay(1000);
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);


  // The embedded Preact app. Assets keep fixed names and are cache-busted by ?v=<etag> in index.html.
  for (size_t i = 0; i < webAssetCount; i++) {
    const WebAsset *asset = &webAssets[i];
    server.on(asset->path, HTTP_GET, [asset](AsyncWebServerRequest *request) {
      String etag = String("\"") + asset->etag + "\"";
      if (request->hasHeader("If-None-Match") && request->getHeader("If-None-Match")->value() == etag) {
        request->send(304);
        return;
      }
      AsyncWebServerResponse *response = request->beginResponse(200, asset->mime, asset->data, asset->length);
      response->addHeader("Content-Encoding", "gzip");
      response->addHeader("ETag", etag);
      response->addHeader("Cache-Control", asset->path[1] == '\0' ? "no-cache" : "max-age=31536000");
      request->send(response);
    });
  }

  // Raw sensor values for troubleshooting
  server.on("/diag", HTTP_GET, [](AsyncWebServerRequest *request) {
    String text = "calibration ph4=" + String(cycle.ph4()) + " ph7=" + String(cycle.ph7()) + "\n";
    text += "last pH adc average=" + String(cycle.lastPhAdc()) + "\nlast pH samples:";
    for (int i = 0; i < cycle.lastPhSampleCount(); i++) {
      text += " " + String(cycle.lastPhSample(i), 0);
    }
    text += "\nlast EC adc average=" + String(lastEcAdc) + "\n";
    text += "now relay=" + String(digitalRead(relay)) + " sensor back-to-back:";
    for (int i = 0; i < 10; i++) {
      text += " " + String(analogRead(sensorPin));
    }
    text += " (" + String(analogReadMilliVolts(sensorPin)) + " mV)\n";
    if (request->hasParam("settle")) {
      settleRequested = true;
    }
    text += apiStatus();
    text += settleLog;
    request->send(200, "text/plain", text);
  });

  // Readings API settings, stored in NVS. GET shows the status, POST saves url, token and device.
  // The token is never shown back.
  server.on("/apiconfig", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", apiStatus());
  });

  server.on("/apiconfig", HTTP_POST, [](AsyncWebServerRequest *request) {
    //Unlike /api/apiconfig, an empty url or token here clears it
    String error = saveApiConfig(request->hasParam("url", true), formValue(request, "url"), request->hasParam("token", true),
                                 formValue(request, "token"), formValue(request, "device").length() > 0,
                                 formValue(request, "device"));
    if (error.length()) {
      request->send(400, "text/plain", error);
      return;
    }
    request->send(200, "text/plain", "saved\n");
  });

  registerApiRoutes();



  server.onNotFound(notFound);
  server.begin();



  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else {  // U_FS
      type = "filesystem";
    }

    // NOTE: if updating FS this would be the place to unmount FS using FS.end()
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    }
  });
  ArduinoOTA.begin();
  pinMode(relay, OUTPUT);
  Serial.println("Ready");


  cycle.configure(readFile("/ph4.txt").toFloat(), readFile("/ph7.txt").toFloat(), interval0);

    xTaskCreatePinnedToCore (
    loop2,     // Function to implement the task
    "loop2",   // Name of the task
    10000,      // Stack size in bytes
    NULL,      // Task input parameter
    0,         // Priority of the task
    NULL,      // Task handle.
    0          // Core where the task should run
  );


}





// Settle test, started from /diag?settle=: holds sampling, powers EC for a while,
// switches to pH and logs the analog pins so the warmup the pH probe needs can be read off
void logSettleRead(unsigned long now, long sincePhSwitch) {
  if (now - lastSettleRead >= 500) {
    lastSettleRead = now;
    settleLog += String(sincePhSwitch);
    for (int pin : settlePins) {
      settleLog += " " + String(analogRead(pin));
    }
    settleLog += "\n";
  }
}

void runSettleTest() {
  unsigned long now = millis();
  switch (settleStep) {
    case SettleStep::Idle:
      if (settleRequested) {
        settleRequested = false;
        settleLog = "settle test starting\n";
        cycle.request(CycleCommand::Hold);
        settleStep = SettleStep::Holding;
      }
      break;

    case SettleStep::Holding:
      if (cycle.status() == CycleStatus::Holding) {
        digitalWrite(relay, LOW);
        settleStart = now;
        lastSettleRead = 0;
        settleLog = "ms_since_ph_switch GPIO0 GPIO2 GPIO3 GPIO4 GPIO5\n";
        settleStep = SettleStep::EcState;
      }
      break;

    case SettleStep::EcState:
      logSettleRead(now, (long)(now - settleStart) - (long)settleEcTime);
      if (now - settleStart >= settleEcTime) {
        digitalWrite(relay, HIGH);
        settleStart = now;
        settleStep = SettleStep::PhState;
      }
      break;

    case SettleStep::PhState:
      logSettleRead(now, now - settleStart);
      if (now - settleStart >= settlePhTime) {
        digitalWrite(relay, LOW);
        settleLog += "done\n";
        cycle.request(CycleCommand::TakeReading);
        settleStep = SettleStep::Idle;
      }
      break;
  }
}

void loop() {
  ArduinoOTA.handle();
  runSettleTest();
  sendQueuedReading();
  fetchPiCache();
  savePendingSettings();
  delay(2);  //single core: loop() outranks loop2, so it must yield or sampling starves
}

// the loop2 function also runs forver but as a parallel task
void loop2 (void* pvParameters) {
    while (1) {
    cycle.tick(millis());
  }
}