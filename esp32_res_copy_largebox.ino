
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
#include "src/event_api.h"
#include "src/event_sync.h"
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
const unsigned long settleEcTime = 10000;
const unsigned long settlePhTime = 180000;
const int settleBurstReads = 200;
SampleSchedule settleSchedule;  //same hum-cancelling 105 ms grid the readings use
float settleSum = 0;
int settleMin = 0;
int settleMax = 0;
int settleReads = 0;

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

// Tank events: detected top-ups and feedings plus events the owner logs. eventsLock guards events and
// eventsSavePending: the sampling task offers detections, handlers edit, and loop() saves /events.txt and
// syncs one changed event per pass to <url>/events/<uid>, sharing the readings upload's backoff.
EventLog<100> events;
SemaphoreHandle_t eventsLock;
bool eventsSavePending = false;
int eventsLastHttp = 0;
unsigned long savedNextSeq = 0;  //the seq high-water mark in NVS "events"; loop() only
// Detection settings, guarded by settingsLock and saved to NVS namespace "events" by loop()
DetectLimits detectLimits;
bool hasPendingDetect = false;
// EC temperature coefficient, % per deg C (0 = off), guarded by settingsLock, saved to NVS "sensor" by
// loop(). The reading log keeps raw EC; status, readings, detection and the upload refer it to 25 C.
const float defaultEcTempCoef = 2.0f;
float ecTempCoef = defaultEcTempCoef;
bool hasPendingEcCoef = false;

//Saved API settings as handlers see them; defined up here because the .ino prototypes need it
struct SavedApiConfig {
  String url;
  String device;
  bool tokenSet;
};

//The first function in the file: the .ino prototypes go above it, so every type they use is defined above here
float currentEcTempCoef() {
  xSemaphoreTake(settingsLock, portMAX_DELAY);
  float coef = ecTempCoef;
  xSemaphoreGive(settingsLock);
  return coef;
}

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

  //Written to a temporary file first so a power cut leaves the last good save. Each path has its own
  //temporary file: readings are saved by the sampling task and events by loop(), possibly at once.
  void write(const char *path, const std::string &text) {
    String temp = String(path) + ".tmp";
    const char *tempPath = temp.c_str();
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

//UTC seconds of a reading log timestamp (local time, converted with the TZ configTime set), or 0
long long epochFromLog(const char *text) {
  LogTime t;
  if (!parseLogTimestamp(text, t)) {
    return 0;
  }
  struct tm local = {};
  local.tm_year = t.year - 1900;
  local.tm_mon = t.month - 1;
  local.tm_mday = t.day;
  local.tm_hour = t.hour;
  local.tm_min = t.minute;
  local.tm_sec = t.second;
  local.tm_isdst = -1;  //let the TZ rules decide
  return (long long)mktime(&local);
}

// Used only by the sampling task, in detectEvents(); static so they stay off its stack
DetectSample detectSamples[totalcount];
char detectTimes[totalcount][24];
CandidateHold candidateHold;  //keeps a step's strongest candidate until 3 later windows haven't beaten it

//Runs the detection rule over the reading log after each recorded reading, and fills before/after
//values of events that lack them. Copies the log under readingsLock, then changes events under eventsLock.
void detectEvents() {
  long long now = time(nullptr);
  if (!clockIsSet(now)) {
    return;  //no event gets a timestamp from an unsynced clock
  }
  float coef = currentEcTempCoef();
  xSemaphoreTake(readingsLock, portMAX_DELAY);
  int count = readings.size();
  for (int i = 0; i < count; i++) {
    snprintf(detectTimes[i], sizeof(detectTimes[i]), "%s", readings.timestampAt(i));
    detectSamples[i].ec = ecAt25(readings.ec().getOrderedValue(i), readings.wt().getOrderedValue(i), coef);
    detectSamples[i].ph = readings.ph().getOrderedValue(i);
  }
  xSemaphoreGive(readingsLock);
  if (count == 0 || !clockIsSet(epochFromLog(detectTimes[count - 1]))) {
    return;  //the newest reading has no usable time
  }
  int usable = 0;
  for (int i = 0; i < count; i++) {
    long long epoch = epochFromLog(detectTimes[i]);
    if (!clockIsSet(epoch)) {
      continue;  //taken before NTP sync
    }
    detectSamples[usable] = detectSamples[i];
    detectSamples[usable].epoch = epoch;
    usable++;
  }
  xSemaphoreTake(settingsLock, portMAX_DELAY);
  DetectLimits limits = detectLimits;
  xSemaphoreGive(settingsLock);
  Candidate window;
  if (usable >= DETECT_WINDOW) {
    window = detectAt(detectSamples + usable - DETECT_WINDOW, limits);
  }
  Candidate candidate = candidateHold.push(window);  //every window counts, found or not
  xSemaphoreTake(eventsLock, portMAX_DELAY);
  bool changed = events.offerDetected(candidate, now);
  changed = events.fillDeltas(detectSamples, usable, now) || changed;
  if (changed) {
    eventsSavePending = true;
  }
  xSemaphoreGive(eventsLock);
}

float lastEcAdc = 0;

float takeec() {
  float ecadc = 0;
  for (int i = 0; i < 20; i++) {
    delay(105);  //not 100: that's a whole number of 60 Hz cycles, so mains hum would alias to an offset
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
    std::string text = readings.saveText();
    xSemaphoreGive(readingsLock);
    readingStore.write(readings.path(), text);  //the flash write happens outside the lock
    queueForApi(reading);
    detectEvents();
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
    queued.ec = ecAt25(reading.ec, reading.wt, currentEcTempCoef());  //ec_adc stays raw
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
    std::string text = readings.saveText();
    xSemaphoreGive(readingsLock);
    readingStore.write(readings.path(), text);
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

//Read from NVS so handlers never touch apiUrl and friends, which loop() reloads
SavedApiConfig readSavedApiConfig() {
  SavedApiConfig saved;
  Preferences apiPrefs;
  apiPrefs.begin("api", true);
  saved.url = apiPrefs.getString("url", "");
  saved.device = apiPrefs.getString("device", "envmonitor-1");
  saved.tokenSet = apiPrefs.getString("token", "").length() > 0;
  apiPrefs.end();
  return saved;
}

String apiStatus() {
  xSemaphoreTake(apiQueueLock, portMAX_DELAY);
  int queued = apiQueue.size();
  int dropped = apiQueue.dropped();
  xSemaphoreGive(apiQueueLock);
  SavedApiConfig saved = readSavedApiConfig();
  return "api url=" + (saved.url.length() ? saved.url : String("(not set)")) + " token=" + (saved.tokenSet ? "set" : "not set") +
         " device=" + saved.device + "\napi queued=" + String(queued) + " delivered=" + String(apiDelivered) +
         " rejected=" + String(apiRejected) + " dropped=" + String(dropped) + " skipped_no_clock=" + String(apiNoClock) +
         " last_http=" + String(apiLastCode) + " failures=" + String(apiFailures) + "\n" + eventsStatus();
}

String eventsStatus() {
  long long now = time(nullptr);
  xSemaphoreTake(eventsLock, portMAX_DELAY);
  String text = "events count=" + String(events.size()) + " unsynced=" + String(events.dirtyCount()) +
                " dropped_unsynced=" + String(events.droppedUnsynced()) +
                " clock_ahead=" + String(events.futureCount(now)) + " refused=" + String(events.refusedCount());
  xSemaphoreGive(eventsLock);
  return text + " last_http=" + String(eventsLastHttp) + "\n";
}

//Writes /events.txt when events changed. Returns true when it wrote.
//The seq high-water mark goes to NVS first: a lost or re-seeded /events.txt must never let a new
//event reuse a seq, which would overwrite an unrelated record on the Pi.
bool saveEvents() {
  xSemaphoreTake(eventsLock, portMAX_DELAY);
  bool pending = eventsSavePending;
  std::string text;
  unsigned long nextSeq = events.nextSeqValue();
  if (pending) {
    text = events.saveText();
    eventsSavePending = false;
  }
  xSemaphoreGive(eventsLock);
  if (!pending) {
    return false;
  }
  if (nextSeq > savedNextSeq) {
    Preferences eventPrefs;
    eventPrefs.begin("events", false);
    eventPrefs.putULong("next_seq", nextSeq);
    eventPrefs.end();
    savedNextSeq = nextSeq;
  }
  readingStore.write(events.path(), text);
  return true;
}

//PUTs one changed event to <url>/events/<uid>, sharing the readings upload's backoff.
//An event changed while its PUT was in flight stays dirty. Returns true when it made an HTTP request.
bool syncNextEvent() {
  if (apiUrl.length() == 0 || apiToken.length() == 0 || WiFi.status() != WL_CONNECTED) {
    return false;
  }
  if (apiNextAttempt != 0 && (long)(millis() - apiNextAttempt) < 0) {
    return false;
  }
  long long now = time(nullptr);
  if (!clockIsSet(now)) {
    return false;  //can't tell an updated_at from a fast clock without NTP time
  }
  xSemaphoreTake(eventsLock, portMAX_DELAY);
  const TankEvent *next = events.nextDirty(now);
  TankEvent event;
  if (next) {
    event = *next;
  }
  xSemaphoreGive(eventsLock);
  if (!next) {
    return false;
  }

  HTTPClient http;
  http.setConnectTimeout(3000);
  http.setTimeout(3000);
  http.begin(apiUrl + eventSyncPath(apiDevice.c_str(), event).c_str());
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", "Bearer " + apiToken);
  int code = http.PUT(String(eventSyncJson(apiDevice.c_str(), event).c_str()));
  http.end();
  eventsLastHttp = code;

  SendOutcome outcome = eventSyncOutcome(code);
  if (outcome == SendOutcome::RetryLater) {
    apiFailures++;
    apiNextAttempt = millis() + retryDelayMs(apiFailures);
    return true;
  }
  apiFailures = 0;
  apiNextAttempt = 0;
  xSemaphoreTake(eventsLock, portMAX_DELAY);
  if (outcome == SendOutcome::Rejected) {
    events.markRefused(event.seq, event.dirtyGen);  //not resent, shown as not backed up; an edit retries
  } else {
    events.markSynced(event.seq, event.dirtyGen);
  }
  eventsSavePending = true;  //the synced state is saved too, so a reboot doesn't resend everything
  xSemaphoreGive(eventsLock);
  return true;
}

//Sends the oldest queued reading, one per call, backing off while the API is unreachable.
//Returns true when it made an HTTP request.
bool sendQueuedReading() {
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
    return false;
  }
  if (apiNextAttempt != 0 && (long)(millis() - apiNextAttempt) < 0) {
    return false;
  }
  xSemaphoreTake(apiQueueLock, portMAX_DELAY);
  bool empty = apiQueue.size() == 0;
  QueuedReading reading;
  if (!empty) {
    reading = apiQueue.front();
  }
  xSemaphoreGive(apiQueueLock);
  if (empty) {
    return false;
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
    return true;
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
  return true;
}

const unsigned long piBodyDeadline = 3000;  //ms to read a whole Pi reply body

//Reads a Pi reply body of `size` bytes (-1 when unknown: read until the server closes), keeping at
//most cap + 1 bytes in memory. Returns false when the body is longer than cap.
bool readPiBody(HTTPClient &http, int size, size_t cap, std::string &body) {
  WiFiClient *stream = http.getStreamPtr();
  size_t limit = (size >= 0 && (size_t)size <= cap) ? (size_t)size : cap + 1;
  body.reserve(limit);
  uint8_t buffer[256];
  unsigned long start = millis();
  while (stream && body.size() < limit && millis() - start < piBodyDeadline) {
    int available = stream->available();
    if (available <= 0) {
      if (!stream->connected()) {
        break;
      }
      delay(1);
      continue;
    }
    size_t want = min(min((size_t)available, sizeof(buffer)), limit - body.size());
    int got = stream->read(buffer, want);
    if (got > 0) {
      body.append((const char *)buffer, got);
    }
  }
  return body.size() <= cap;
}

//Fetches one Pi endpoint per call, every PiCache::ROUND_MS. The lock is never held across the GET.
void fetchPiCache() {
  bool configured = apiUrl.length() > 0 && apiToken.length() > 0;
  xSemaphoreTake(piCacheLock, portMAX_DELAY);
  bool due = piCache.due(millis());
  PiEndpoint endpoint = PiEndpoint::Stats;
  if (due) {
    endpoint = piCache.next();
    if (!configured) {
      piCache.markUnconfigured();
    }
  }
  xSemaphoreGive(piCacheLock);
  if (!due || !configured) {
    return;
  }

  HTTPClient http;
  http.setConnectTimeout(3000);
  http.setTimeout(3000);
  http.useHTTP10(true);  //no chunked encoding, so the raw stream is the body
  http.begin(apiUrl + piPath(endpoint, apiDevice.c_str()).c_str());
  http.addHeader("Authorization", "Bearer " + apiToken);
  int code = http.GET();
  bool tooLarge = false;
  std::string body;
  if (code == 200) {
    size_t cap = PiCache::bodyCap(endpoint);
    int size = http.getSize();
    tooLarge = size > (int)cap || !readPiBody(http, size, cap, body);
  }
  http.end();

  xSemaphoreTake(piCacheLock, portMAX_DELAY);
  if (tooLarge) {
    piCache.recordTooLarge(endpoint, millis());
  } else {
    piCache.record(endpoint, code, body, millis());
  }
  xSemaphoreGive(piCacheLock);
}

//Writes settings queued by POST /api/settings. Copies them out first: writeFile blocks for 2 s.
//Returns true when it wrote a file.
bool savePendingSettings() {
  xSemaphoreTake(settingsLock, portMAX_DELAY);
  //One file per pass: the title first, the interval on the next pass
  bool saveTitle = hasPendingTitle;
  bool saveInterval = hasPendingInterval && !saveTitle;
  bool saveDetect = hasPendingDetect && !saveTitle && !saveInterval;
  bool saveEcCoef = hasPendingEcCoef && !saveTitle && !saveInterval && !saveDetect;
  String newTitle = pendingTitle;
  unsigned long newInterval = pendingInterval;
  DetectLimits newLimits = detectLimits;
  float newEcCoef = ecTempCoef;
  if (saveTitle) {
    hasPendingTitle = false;
  } else if (saveInterval) {
    hasPendingInterval = false;
  } else if (saveDetect) {
    hasPendingDetect = false;
  } else if (saveEcCoef) {
    hasPendingEcCoef = false;
  }
  xSemaphoreGive(settingsLock);
  if (saveTitle) {
    writeFile("/title.txt", newTitle.c_str());
  } else if (saveInterval) {
    cycle.setInterval(newInterval);
    writeFile("/interval0.txt", String(newInterval).c_str());
  } else if (saveDetect) {
    saveDetectLimits(newLimits);
  } else if (saveEcCoef) {
    Preferences sensorPrefs;
    sensorPrefs.begin("sensor", false);
    sensorPrefs.putFloat("ec_coef", newEcCoef);
    sensorPrefs.end();
  }
  return saveInterval || saveTitle || saveDetect || saveEcCoef;
}

//Called at boot only; the default when nothing was saved or the saved value is out of range
float loadEcTempCoef() {
  float coef = defaultEcTempCoef;
  Preferences sensorPrefs;
  if (sensorPrefs.begin("sensor", true)) {
    coef = sensorPrefs.getFloat("ec_coef", defaultEcTempCoef);
    sensorPrefs.end();
  }
  return (coef >= 0 && coef <= 5) ? coef : defaultEcTempCoef;
}

// NVS keys are at most 15 characters, so the keys are shorter than the API's field names
void saveDetectLimits(const DetectLimits &limits) {
  Preferences eventPrefs;
  eventPrefs.begin("events", false);
  eventPrefs.putFloat("topup_drop", limits.topupMinDrop);
  eventPrefs.putFloat("feed_rise", limits.feedingMinRise);
  eventPrefs.putBool("feed_on", limits.feedingEnabled);
  eventPrefs.end();
}

//Called at boot only; defaults when nothing was saved
DetectLimits loadDetectLimits() {
  DetectLimits limits;
  Preferences eventPrefs;
  if (eventPrefs.begin("events", true)) {
    limits.topupMinDrop = eventPrefs.getFloat("topup_drop", limits.topupMinDrop);
    limits.feedingMinRise = eventPrefs.getFloat("feed_rise", limits.feedingMinRise);
    limits.feedingEnabled = eventPrefs.getBool("feed_on", limits.feedingEnabled);
    eventPrefs.end();
  }
  return limits;
}

String withoutTrailingSlashes(String url) {
  while (url.endsWith("/")) {
    url.remove(url.length() - 1);
  }
  return url;
}

//Saves readings API settings to NVS. A null field is left unchanged; the url must already have
//its trailing slashes removed. Returns an error message, or "" when saved.
String saveApiConfig(const String *url, const String *token, const String *device) {
  if (url && url->length() && !url->startsWith("http://")) {
    return "url must start with http://";
  }
  Preferences apiPrefs;
  apiPrefs.begin("api", false);
  if (url) {
    apiPrefs.putString("url", *url);
  }
  if (token) {
    apiPrefs.putString("token", *token);
  }
  if (device) {
    apiPrefs.putString("device", *device);
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
  s.ecTempCoef = ecTempCoef;
  xSemaphoreGive(settingsLock);
  s.uptimeS = (unsigned long)(esp_timer_get_time() / 1000000);
  s.freeHeap = ESP.getFreeHeap();

  xSemaphoreTake(readingsLock, portMAX_DELAY);
  s.count = readings.size();
  if (s.count > 0) {
    s.lastTime = readings.timestampAt(s.count - 1);
    s.ph = readings.ph().currentValue;
    s.ec = ecAt25(readings.ec().currentValue, readings.wt().currentValue, s.ecTempCoef);
    s.wt = readings.wt().currentValue;
    s.phPm = lastPhUncertainty;
  }
  s.means[0] = readings.ph().mean;
  compensatedEcStats(readings, s.ecTempCoef, s.means[1], s.stds[1]);
  s.means[2] = readings.wt().mean;
  //Rounding can leave a variance a hair below zero
  s.stds[0] = sqrt(max(readings.ph().sVar, 0.0f));
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
  s.manualEc = ecAt25(manual.ec, manual.wt, s.ecTempCoef);
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
  s.warmupMs = ReadingCycle::warmupMs();
  s.lastWarmupMs = cycle.lastWarmupMs();

  xSemaphoreTake(apiQueueLock, portMAX_DELAY);
  s.apiQueued = apiQueue.size();
  xSemaphoreGive(apiQueueLock);
  s.apiLastHttp = apiLastCode;
  s.apiFailures = apiFailures;
  s.apiDelivered = apiDelivered;

  long long epoch = time(nullptr);
  xSemaphoreTake(eventsLock, portMAX_DELAY);
  s.eventsCount = events.size();
  s.eventsUnsynced = events.dirtyCount();
  s.eventsDropped = events.droppedUnsynced();
  s.eventsClockAhead = events.futureCount(epoch);
  s.eventsRefused = events.refusedCount();
  xSemaphoreGive(eventsLock);
  s.eventsLastHttp = eventsLastHttp;
  return statusJson(s);
}

//RAM copy of the device ranges, loaded once at boot and updated by POST and DELETE /api/ranges,
//so GET never opens NVS. Guarded by settingsLock.
Ranges deviceRanges;
bool hasDeviceRanges = false;

//Device ranges saved by POST /api/ranges, as rangesJson text in NVS. Called at boot only.
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
  xSemaphoreTake(settingsLock, portMAX_DELAY);
  bool hasDevice = hasDeviceRanges;
  if (hasDevice) {
    device = deviceRanges;
  }
  xSemaphoreGive(settingsLock);
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
    float coef = currentEcTempCoef();
    xSemaphoreTake(readingsLock, portMAX_DELAY);
    std::string json = readingsJson(readings, coef);
    xSemaphoreGive(readingsLock);
    sendJson(request, 200, json);
  });

  server.on("/api/settings", HTTP_GET, [](AsyncWebServerRequest *request) {
    xSemaphoreTake(settingsLock, portMAX_DELAY);
    std::string json = settingsJson(interval0 / 60000, title.c_str(), detectLimits, ecTempCoef);
    xSemaphoreGive(settingsLock);
    sendJson(request, 200, json);
  });

  server.on("/api/settings", HTTP_POST, [](AsyncWebServerRequest *request) {
    String interval = formValue(request, "interval");
    String newTitle = formValue(request, "title");
    String topupMinDrop = formValue(request, "topup_min_drop");
    String feedingMinRise = formValue(request, "feeding_min_rise");
    String feedingEnabled = formValue(request, "feeding_enabled");
    String ecCoefText = formValue(request, "ec_temp_coef");
    bool hasBasic = interval.length() || newTitle.length();
    bool hasDetect = topupMinDrop.length() || feedingMinRise.length() || feedingEnabled.length();
    bool hasEcCoef = ecCoefText.length() > 0;
    std::string error;
    if (!hasBasic && !hasDetect && !hasEcCoef) {
      sendError(request, 400, "nothing to save");
      return;
    }
    if (hasBasic && !validateSettings(interval.c_str(), newTitle.c_str(), error)) {
      sendError(request, 400, error);
      return;
    }
    float newEcCoef = 0;
    if (hasEcCoef && !parseEcTempCoef(ecCoefText.c_str(), newEcCoef, error)) {
      sendError(request, 400, error);
      return;
    }
    //Live values change now so the next GET shows them; loop() persists them
    xSemaphoreTake(settingsLock, portMAX_DELAY);
    DetectLimits limits = detectLimits;
    bool valid = !hasDetect || applyDetectSettings(topupMinDrop.c_str(), feedingMinRise.c_str(),
                                                   feedingEnabled.c_str(), limits, error);
    if (valid) {
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
      if (hasDetect) {
        detectLimits = limits;
        hasPendingDetect = true;
      }
      if (hasEcCoef) {
        ecTempCoef = newEcCoef;
        hasPendingEcCoef = true;
      }
    }
    xSemaphoreGive(settingsLock);
    if (!valid) {
      sendError(request, 400, error);
      return;
    }
    sendOk(request);
  });

  server.on("/api/events", HTTP_GET, [](AsyncWebServerRequest *request) {
    xSemaphoreTake(eventsLock, portMAX_DELAY);
    std::string json = events.listJson();
    xSemaphoreGive(eventsLock);
    sendJson(request, 200, json);
  });

  //Changes RAM only and replies at once; loop() saves and syncs
  server.on("/api/events", HTTP_POST, [](AsyncWebServerRequest *request) {
    EventForm form;
    form.op = formValue(request, "op").c_str();
    form.seq = formValue(request, "seq").c_str();
    form.kind = formValue(request, "kind").c_str();
    form.detail = formValue(request, "detail").c_str();
    form.occurredAt = formValue(request, "occurred_at").c_str();
    form.note = formValue(request, "note").c_str();
    form.status = formValue(request, "status").c_str();
    form.hasKind = request->hasParam("kind", true);
    form.hasDetail = request->hasParam("detail", true);
    form.hasOccurredAt = request->hasParam("occurred_at", true);
    form.hasNote = request->hasParam("note", true);
    form.hasStatus = request->hasParam("status", true);
    long long now = time(nullptr);
    xSemaphoreTake(eventsLock, portMAX_DELAY);
    EventApiResult result = applyEventForm(events, form, now);
    if (result.changed) {
      eventsSavePending = true;
    }
    xSemaphoreGive(eventsLock);
    sendJson(request, result.httpCode, result.json);
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
    xSemaphoreTake(settingsLock, portMAX_DELAY);
    deviceRanges = ranges;
    hasDeviceRanges = true;
    xSemaphoreGive(settingsLock);
    sendOk(request);
  });

  server.on("/api/ranges", HTTP_DELETE, [](AsyncWebServerRequest *request) {
    Preferences rangePrefs;
    if (!rangePrefs.begin("ranges", false)) {
      sendError(request, 500, "could not clear device ranges");
      return;
    }
    rangePrefs.remove("json");  //a missing key is fine: nothing to clear
    rangePrefs.end();
    xSemaphoreTake(settingsLock, portMAX_DELAY);
    hasDeviceRanges = false;
    xSemaphoreGive(settingsLock);
    sendOk(request);
  });

  server.on("/api/apiconfig", HTTP_GET, [](AsyncWebServerRequest *request) {
    SavedApiConfig saved = readSavedApiConfig();
    sendJson(request, 200, "{\"url\":" + jsonString(saved.url.c_str()) + ",\"device\":" + jsonString(saved.device.c_str()) +
                               ",\"token_set\":" + jsonBool(saved.tokenSet) + "}");
  });

  //An empty field means "not provided": the app always sends url and device.
  //Slashes are stripped first, so url "/" also counts as not provided.
  server.on("/api/apiconfig", HTTP_POST, [](AsyncWebServerRequest *request) {
    String url = withoutTrailingSlashes(formValue(request, "url"));
    String token = formValue(request, "token");
    String device = formValue(request, "device");
    String error = saveApiConfig(url.length() ? &url : nullptr, token.length() ? &token : nullptr,
                                 device.length() ? &device : nullptr);
    if (error.length()) {
      sendError(request, 400, error.c_str());
      return;
    }
    sendOk(request);
  });
}

void restoreAllData() {
  readings.restore(readingStore);
  //Saved-but-unsynced events come back dirty. Edits not yet saved when the power went are lost.
  if (!events.loadText(readingStore.read(events.path()))) {
    Serial.println("No saved events");
  }
  Preferences eventPrefs;
  if (eventPrefs.begin("events", true)) {
    savedNextSeq = eventPrefs.getULong("next_seq", 0);
    eventPrefs.end();
  }
  events.raiseNextSeq(savedNextSeq);
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
  eventsLock = xSemaphoreCreateMutex();
  hasDeviceRanges = loadDeviceRanges(deviceRanges);
  detectLimits = loadDetectLimits();
  ecTempCoef = loadEcTempCoef();
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
    text += "\nlast pH warmup ms=" + String(cycle.lastWarmupMs()) + " (fixed " + String(ReadingCycle::warmupMs()) +
            ")\nlast pH warmup window means:";
    for (int i = 0; i < cycle.lastWarmupWindowCount(); i++) {
      text += " " + String(cycle.lastWarmupWindowMean(i), 1);
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
    String url = withoutTrailingSlashes(formValue(request, "url"));
    String token = formValue(request, "token");
    String device = formValue(request, "device");
    String error = saveApiConfig(request->hasParam("url", true) ? &url : nullptr,
                                 request->hasParam("token", true) ? &token : nullptr,
                                 device.length() ? &device : nullptr);
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





// Settle test, started from /diag?settle=: holds sampling, powers EC for a while, then powers pH
// for settlePhTime and logs hum-free 2.1 s window averages of the sensor pin, so the warmup the pH
// board needs can be read off. It marks the window where the reading cycle's fixed warmup ends,
// and ends with a burst of reads 1 ms apart that shows the mains hum on the pH signal.
void resetSettleWindow() {
  settleSchedule.reset();
  settleSum = 0;
  settleReads = 0;
}

void logSettleWindow(unsigned long now, long sincePhOn, bool phOn) {
  if (!settleSchedule.due(now)) {
    return;
  }
  int value = analogRead(sensorPin);
  if (settleReads == 0 || value < settleMin) {
    settleMin = value;
  }
  if (settleReads == 0 || value > settleMax) {
    settleMax = value;
  }
  settleSum += value;
  settleReads++;
  if (settleReads < HUM_WINDOW) {
    return;
  }
  float mean = settleSum / HUM_WINDOW;
  settleLog += String(sincePhOn) + " " + String(mean, 1) + " " + String(settleMin) + " " + String(settleMax);
  if (phOn && sincePhOn >= (long)ReadingCycle::warmupMs() && sincePhOn < (long)(ReadingCycle::warmupMs() + HUM_WINDOW * HUM_SPACING_MS)) {
    settleLog += " <- readings sample from here";
  }
  settleLog += "\n";
  settleSum = 0;
  settleReads = 0;
}

void logHumBurst() {
  int burst[settleBurstReads];
  unsigned long began = micros();
  for (int i = 0; i < settleBurstReads; i++) {
    burst[i] = analogRead(sensorPin);
    delay(1);
  }
  unsigned long took = micros() - began;
  String line = "hum burst, " + String(settleBurstReads) + " reads over " + String(took / 1000) + " ms:";
  for (int i = 0; i < settleBurstReads; i++) {
    line += " " + String(burst[i]);
  }
  settleLog += line + "\n";
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
        resetSettleWindow();
        settleLog = "ms_since_ph_on mean min max (GPIO 3, 20 reads 105 ms apart per line)\n";
        settleStep = SettleStep::EcState;
      }
      break;

    case SettleStep::EcState:
      logSettleWindow(now, (long)(now - settleStart) - (long)settleEcTime, false);
      if (now - settleStart >= settleEcTime) {
        digitalWrite(relay, HIGH);
        settleStart = now;
        resetSettleWindow();
        settleStep = SettleStep::PhState;
      }
      break;

    case SettleStep::PhState:
      logSettleWindow(now, now - settleStart, true);
      if (now - settleStart >= settlePhTime) {
        logHumBurst();
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
  //No network or flash work while a probe is being read: Wi-Fi transmits disturb the ADC, and the
  //pH board should be on no longer than it must. A reading's sensors are on for about 35 s.
  CycleStep step = cycle.step();
  bool measuring = step == CycleStep::Warmup || step == CycleStep::Sampling || settleStep != SettleStep::Idle;
  //At most one slow step per pass, in this order: a readings upload, an events save, an event sync,
  //a settings write or one Pi fetch. A skipped fetch stays due, so it runs on a later pass.
  if (!measuring) {
    bool didSlowWork = sendQueuedReading();
    if (!didSlowWork) {
      didSlowWork = saveEvents();
    }
    if (!didSlowWork) {
      didSlowWork = syncNextEvent();
    }
    if (!didSlowWork) {
      didSlowWork = savePendingSettings();
    }
    if (!didSlowWork) {
      fetchPiCache();
    }
  }
  delay(2);  //single core: loop() outranks loop2, so it must yield or sampling starves
}

// the loop2 function also runs forver but as a parallel task
void loop2 (void* pvParameters) {
    while (1) {
    cycle.tick(millis());
  }
}