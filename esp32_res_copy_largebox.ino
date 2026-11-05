
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
#include "src/reading_log.h"
#include "src/reading_queue.h"
#include "src/sensor_math.h"
#include "src/wifi_setup.h"


#define totalcount 200

const char *setupApName = "EnvMonitor-Setup";
const unsigned long setupPortalTimeout = 300000;  //5 min before retrying saved network
volatile bool wifiSaved = false;

String title = "Server";
unsigned long interval0 = 300000;  //min between readings 60000 for 1
const unsigned long holdTimeout = 900000;  //15 min before an abandoned wizard resumes sampling


const char *ntpServer = "pool.ntp.org";
const long gmtOffset_sec = -18000;    //Replace with your GMT offset (seconds)
const int daylightOffset_sec = 3600;  //Replace with your daylight offset (seconds)

DS18B20 ds(8);
const int relay = 1;
const int sensorPin = 3;  //pH and EC share this input, in the D1 mini A0 position

String programlog;

float phstdev = 0;

enum class SettleStep { Idle, Holding, EcState, PhState };
volatile bool settleRequested = false;
SettleStep settleStep = SettleStep::Idle;
String settleLog;
unsigned long settleStart = 0;
unsigned long lastSettleRead = 0;
const unsigned long settleEcTime = 10000;
const unsigned long settlePhTime = 90000;
const int settlePins[] = { 0, 2, 3, 4, 5 };  //every free analog pin; GPIO 1 drives the relay

ReadingLog<totalcount> readings;

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
    readings.append(reading.timestamp, reading.ph, reading.ec, reading.wt);
    phstdev = reading.phUncertainty;
    readings.save(readingStore);
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
    readings.clear();
    readings.save(readingStore);
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

String processor(const String &var) {

  if (var == "interval0_html")
    return String(interval0 / 60000);

  if (var == "title_html")
    return title;

  if (var == "ph_html")
    return String(readings.ph().currentValue);

  if (var == "phstd_html")
    return String(phstdev);

  if (var == "phmean_html")
    return String(readings.ph().mean);

  if (var == "phmeanstd_html")
    return String(sqrt(readings.ph().sVar));

  if (var == "ec_html")
    return String(readings.ec().currentValue);

  if (var == "ecmean_html")
    return String(readings.ec().mean);

  if (var == "ecmeanstd_html")
    return String(sqrt(readings.ec().sVar));

  if (var == "wt_html")
    return String(readings.wt().currentValue);

  if (var == "wtmean_html")
    return String(readings.wt().mean);

  if (var == "wtmeanstd_html")
    return String(sqrt(readings.wt().sVar));

  if (var == "log_html")
    return programlog;

  if (var == "datatable0_html")
    return readings.chartRows().c_str();

  return String();
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
  Serial.begin(115200);

  Serial.println("Starting the LittleFS Webserver..");

  // Begin LittleFS
  if (!LittleFS.begin()) {
    Serial.println("An Error has occurred while mounting LittleFS");
    return;
  }

   restoreAllData();

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


  // Route for root index.html
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/index.html", "text/html", false, processor);
  });

  // Route for root index.css
  server.on("/index.css", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/index.css", "text/css");
  });

  // Route for root index.js
  server.on("/index.js", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/index.js", "text/html", false, processor);
  });

  // Route for update
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
    Preferences apiPrefs;
    apiPrefs.begin("api", false);
    if (request->hasParam("url", true)) {
      String url = request->getParam("url", true)->value();
      while (url.endsWith("/")) {
        url.remove(url.length() - 1);
      }
      if (url.length() && !url.startsWith("http://")) {
        apiPrefs.end();
        request->send(400, "text/plain", "url must start with http://");
        return;
      }
      apiPrefs.putString("url", url);
    }
    if (request->hasParam("token", true)) {
      apiPrefs.putString("token", request->getParam("token", true)->value());
    }
    if (request->hasParam("device", true) && request->getParam("device", true)->value().length()) {
      apiPrefs.putString("device", request->getParam("device", true)->value());
    }
    apiPrefs.end();
    apiConfigChanged = true;
    request->send(200, "text/plain", "saved\n");
  });

  server.on("/update.html", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/update.html", "text/html", false, processor);
  });


  // Send a GET request to <ESP_IP>/get?input1=<inputMessage>
  server.on("/get", HTTP_GET, [](AsyncWebServerRequest *request) {
    String inputMessage;
    String inputParam;
    // GET input1 value on <ESP_IP>/get?input1=<inputMessage>
    if (request->hasParam("interval0")) {
      inputMessage = request->getParam("interval0")->value();
      inputParam = "interval0";
      inputMessage = inputMessage.toInt() * 60000;
      Serial.println(inputMessage);
      writeFile("/interval0.txt", inputMessage.c_str());
      interval0 = readFile("/interval0.txt").toInt();
      cycle.setInterval(interval0);
    }
    if (request->hasParam("title")) {
      String inputMessage = request->getParam("title")->value();
      writeFile("/title.txt", inputMessage.c_str());
      title = readFile("/title.txt");
    }
    if (request->hasParam("restart")) {
      cycle.request(CycleCommand::Restart);
      request->redirect("/");
    }

    if (request->hasParam("reset")) {
      cycle.request(CycleCommand::WipeAndRestart);
      request->redirect("/");
    }
    if (request->hasParam("calibrate0")) {
      cycle.request(CycleCommand::Hold);
     request->send(LittleFS, "/calibrate0.html", "text/html");
    }

    if (request->hasParam("calibrate1")) {
      cycle.request(CycleCommand::MeasurePh4);
      request->redirect("/get?calibrate2=");
     }

    if (request->hasParam("calibrate2")) {

      if (cycle.status() == CycleStatus::Busy) {
     request->send(LittleFS, "/waiting.html", "text/html");
      } else {
     request->send(LittleFS, "/calibrate2.html", "text/html");
      }
    }


    if (request->hasParam("calibrate3")) {
      if (cycle.status() == CycleStatus::Holding){
        cycle.request(CycleCommand::MeasurePh7);
      }
      if (cycle.status() == CycleStatus::Busy) {
       request->send(LittleFS, "/waiting.html", "text/html");
      } else {
        request->send(200, "text/html", "ph4 updated to " + String(cycle.ph4()) + " and ph7 updated " + "to " + String(cycle.ph7()) + "<br><a href=\"/\">Return to Home Page</a>");
      }
    }

    if (request->hasParam("takeph0")) {
      cycle.request(CycleCommand::Hold);
      request->send(200, "text/html", "To take pH reading: Make sure cleaned probe is in the solution to be measured and click to continue.<form action=\"/get\"><button name=\"takeph1\" type=\"submit\">Continue</button></form>");
    }

    if (request->hasParam("takeph1")) {
      cycle.request(CycleCommand::MeasureManual);
      request->redirect("/get?takeph2=");
         }

    if (request->hasParam("takeph2")) {
      if (cycle.status() == CycleStatus::Busy) {
         request->send(LittleFS, "/waiting.html", "text/html");
      } else {
        const Reading &manual = cycle.manualReading();
        request->send(200, "text/html", "Current pH is " + String(manual.ph) +
        "\nCurrent ec is " + String(manual.ec) + "\nCurrent water temperature is " + String(manual.wt) + "<br><a href=\"/\">Return to Home Page</a>");
      }
    }

    if (request->hasParam("takenewreading0")) {
      cycle.request(CycleCommand::TakeReading);
      request->redirect("/get?takenewreading1=");
         }

    if (request->hasParam("takenewreading1")) {
      if (cycle.status() != CycleStatus::Idle) {
        request->send(200, "text/html", "<meta http-equiv='refresh' content='10'>Please wait...");
      } else {
        request->redirect("/");
         }
      }

    else {
      inputMessage = "No message sent";
      inputParam = "none";
    }
    Serial.println(inputMessage);
    request->redirect("/");
  });



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


  //reset the interval if it is outside of the range
  if (interval0 < 12000 || interval0 > 720000) { interval0 = 600000; }
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
  delay(2);  //single core: loop() outranks loop2, so it must yield or sampling starves
}

// the loop2 function also runs forver but as a parallel task
void loop2 (void* pvParameters) {
    while (1) {
    cycle.tick(millis());
  }
}