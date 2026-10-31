
#include <Arduino.h>
#include "WiFi.h"
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoOTA.h>
#include <DS18B20.h>
#include <Preferences.h>
#include "time.h"
#include "src/sensor_math.h"
#include "src/stats_file.h"
#include "src/wifi_setup.h"


#define totalcount 200

const char *setupApName = "EnvMonitor-Setup";
const unsigned long setupPortalTimeout = 300000;  //5 min before retrying saved network
volatile bool wifiSaved = false;

String title = "Server";
unsigned long interval0 = 300000;  //min between readings 60000 for 1
const long interval1 = 20000;      //20 seconds warmup before pH read


const char *ntpServer = "pool.ntp.org";
const long gmtOffset_sec = -18000;    //Replace with your GMT offset (seconds)
const int daylightOffset_sec = 3600;  //Replace with your daylight offset (seconds)

DS18B20 ds(10);
const int relay = 7;
float ph4 = 2368.;
float ph7 = 1880.;
char STATE = 'B';
char PHSTATE = 'A';

String programlog;

//number of ph samples each reading
float phadcarray[20];
int phcount = 0;
float phstdev, phadc = 0;
 

unsigned long previousTime = 0;
unsigned long phstarttime = 0;


String datetimearray[totalcount];

// Webserver
AsyncWebServer server(80);
String PARAM_MESSAGE = "status";


void notFound(AsyncWebServerRequest *request) {
  request->send(404, "text/plain", "Not found");
}


class dataset : public RingStats<totalcount> {

public:

  String id;

  dataset(String id_in) {
    id = id_in;
  }

  void saveDataset() {
    String path = "/" + id + ".txt";
    writeFile(path.c_str(), serializeStats(id.c_str(), *this).c_str());
  }
  void restoreDataset() {
    String path = "/" + id + ".txt";
    String data = readFile(path.c_str());
    if (!restoreStats(data.c_str(), *this)) {
      Serial.println("No valid saved data for " + id);
    }
  }
};

dataset phdata("phdata");
dataset ecdata("ecdata");
dataset wtdata("wtdata");




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

void saveDateTime() {
  String ptr;
  for (int i = 0; i < phdata.size(); i++) {
    ptr += datetimearray[i];
    ptr += " |";
  }

  writeFile("/datetime.txt", ptr.c_str());
}

void restoreDateTime() {
  String data0 = readFile("/datetime.txt");
  char data[data0.length() + 1];
  strcpy(data, data0.c_str());
  int n;
  char *p = data;
  char str[50];

  for (int i = 0; sscanf(p, "%s |%n", &str, &n) == 1; p += n, i++) {
    datetimearray[i] = str;
  }
}


String getarrayprint(int size) {
  String arrayptr;
  arrayptr += "[";
  for (int i = 0; i < size; i++) {
    arrayptr += "[new Date(";
    arrayptr += datetimearray[(phdata.index + i) % size];
    arrayptr += "),";
    arrayptr += String(phdata.getOrderedValue(i));
    arrayptr += ",";
    arrayptr += String(ecdata.getOrderedValue(i));
    arrayptr += ",";
    arrayptr += String(wtdata.getOrderedValue(i));
    arrayptr += "]";
    arrayptr += ",\n";
  }
  arrayptr += "]";
  return arrayptr;
}





float getStandardDeviation(float *sdarray, int sdarraysize, float sdmean) {

  float stdevtemp = 0;

  for (int i = 0; i < sdarraysize; i++) {
    stdevtemp += ((sdmean - sdarray[i]) * (sdmean - sdarray[i]));
  }
  stdevtemp = stdevtemp / sdarraysize;
  stdevtemp = sqrt(stdevtemp);
  return stdevtemp;
}

void calibrateph() {
  getphadc();
  if (PHSTATE == 'D') {
    if (STATE == 'F') {
      ph4 = phadc;
      PHSTATE = 'A';
      STATE = 'W';

    } else {
      ph7 = phadc;
      PHSTATE = 'A';
      STATE = 'A';

    }
  }
}

void takeph() {
  getphadc();
  if (PHSTATE == 'D') {
    float ph = phFromAdc(phadc, ph4, ph7);

    datetimearray[phdata.index] = getDateTime();
    phdata.updatedata(ph);
    saveAllData();

    float phadcstdev = getStandardDeviation(phadcarray, 20, phadc);
    phstdev = ph - phFromAdc(phadc + phadcstdev, ph4, ph7);
    PHSTATE = 'A';
    STATE = 'A';
  }
}


void getphadc() {
  const long currentphtime = millis();

  switch (PHSTATE) {
    case 'A':
      digitalWrite(relay, HIGH);
      delay(500);
      phstarttime = currentphtime;
      PHSTATE = 'B';
      break;

    case 'B':
      if (currentphtime - phstarttime > interval1) {
        PHSTATE = 'C';
      }
      break;

    case 'C':
      if (phcount < 20) {
        phadcarray[phcount] = analogRead(0);
        phcount++;
      } else {
        digitalWrite(relay, LOW);
        delay(500);
        phadc = 0;
        for (int i = 0; i < 20; i++) {
          phadc += phadcarray[i];
        }
        phcount = 0;
        phadc = phadc / 20;
        PHSTATE = 'D';
      }
      break;
  }
}

void resetState() {
  STATE = 'A';
  PHSTATE = 'A';
  previousTime = millis();
  phstarttime = 0;
  digitalWrite(relay, LOW);
}

float takeec() {
  digitalWrite(relay, LOW);
  float ecadc = 0;
  for (int i = 0; i < 20; i++) {
    delay(100);
    ecadc += analogRead(1);
  }
  ecadc = ecadc / 20.0;
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

String processor(const String &var) {

  if (var == "interval0_html")
    return String(interval0 / 60000);

  if (var == "title_html")
    return title;

  if (var == "ph_html")
    return String(phdata.currentValue);

  if (var == "phstd_html")
    return String(phstdev);

  if (var == "phmean_html")
    return String(phdata.mean);

  if (var == "phmeanstd_html")
    return String(sqrt(phdata.sVar));

  if (var == "ec_html")
    return String(ecdata.currentValue);

  if (var == "ecmean_html")
    return String(ecdata.mean);

  if (var == "ecmeanstd_html")
    return String(sqrt(ecdata.sVar));

  if (var == "wt_html")
    return String(wtdata.currentValue);

  if (var == "wtmean_html")
    return String(wtdata.mean);

  if (var == "wtmeanstd_html")
    return String(sqrt(wtdata.sVar));

  if (var == "log_html")
    return programlog;

  if (var == "datatable0_html") {
    if (!phdata.full) {
      return getarrayprint(phdata.index);
    } else {
      return getarrayprint(totalcount);
    }
  }

  return String();
}

void saveAllData() {
  phdata.saveDataset();
  ecdata.saveDataset();
  wtdata.saveDataset();
  saveDateTime();
}

void restoreAllData() {
  phdata.restoreDataset();
  ecdata.restoreDataset();
  wtdata.restoreDataset();
  restoreDateTime();
  //settings variables below
  interval0 = readFile("/interval0.txt").toInt();
  title = readFile("/title.txt");
  ph4 = readFile("/ph4.txt").toFloat();
  ph7 = readFile("/ph7.txt").toFloat();
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
      resetState();
      writeFile("/interval0.txt", inputMessage.c_str());
      interval0 = readFile("/interval0.txt").toInt();
    }
    if (request->hasParam("title")) {
      String inputMessage = request->getParam("title")->value();
      resetState();
      writeFile("/title.txt", inputMessage.c_str());
      title = readFile("/title.txt");
    }
    if (request->hasParam("restart")) {
      resetState();
      STATE = 'R';
      request->redirect("/");
    }

    if (request->hasParam("reset")) {
      resetState();
      STATE = 'E';
      request->redirect("/");
    }
    if (request->hasParam("calibrate0")) {
      resetState();
      STATE = 'W';
     request->send(LittleFS, "/calibrate0.html", "text/html");
    }

    if (request->hasParam("calibrate1")) {
      STATE = 'F';
      request->redirect("/get?calibrate2=");
     }

    if (request->hasParam("calibrate2")) {

      if (STATE == 'F') {
     request->send(LittleFS, "/waiting.html", "text/html");
      } else {
     request->send(LittleFS, "/calibrate2.html", "text/html");
      }
    }


    if (request->hasParam("calibrate3")) {
      if (STATE == 'W'){
        STATE = 'G';
      }
      if (STATE == 'G') {
       request->send(LittleFS, "/waiting.html", "text/html");
      } else {
        request->send(200, "text/html", "ph4 updated to " + String(ph4) + " and ph7 updated " + "to " + String(ph7) + "<br><a href=\"/\">Return to Home Page</a>");
      }
    }

    if (request->hasParam("takeph0")) {
      resetState();
      STATE = 'W';
      request->send(200, "text/html", "To take pH reading: Make sure cleaned probe is in the solution to be measured and click to continue.<form action=\"/get\"><button name=\"takeph1\" type=\"submit\">Continue</button></form>");
    }

    if (request->hasParam("takeph1")) {
      STATE = 'H';
      request->redirect("/get?takeph2=");
         }

    if (request->hasParam("takeph2")) {
      if (STATE == 'H') {
         request->send(LittleFS, "/waiting.html", "text/html");
      } else {
        float phmanual = phFromAdc(phadc, ph4, ph7);
        request->send(200, "text/html", "Current pH is " + String(phmanual) + 
        "\nCurrent ec is " + String(takeec()) + "\nCurrent water temperature is " + String(ds.getTempF()) + "<br><a href=\"/\">Return to Home Page</a>");
      }
    }

    if (request->hasParam("takenewreading0")) {
      resetState();
      STATE = 'B';
      request->redirect("/get?takenewreading1=");
         }

    if (request->hasParam("takenewreading1")) {
      if (STATE != 'A') {
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





void loop() {
  ArduinoOTA.handle();

}

// the loop2 function also runs forver but as a parallel task
void loop2 (void* pvParameters) {
    while (1) {
    //Current time in milliseconds
    unsigned long currentTime = millis();

    switch (STATE) {
  //Waiting in between readings
      case 'A':
        if (currentTime - previousTime > interval0) {
          STATE = 'B';
        }
        break;

  //to take temp and ec
      case 'B':
        wtdata.updatedata(ds.getTempF());
        ecdata.updatedata(takeec());

        STATE = 'C';
        previousTime = currentTime;
        break;

  //to take pH
      case 'C':
        takeph();

        break;

  //to restart
      case 'R':

        if (currentTime - previousTime > 2000) {
          ESP.restart();
        }

        break;

  //to reset data
      case 'E':
        phdata.resetDataset();
        ecdata.resetDataset();
        wtdata.resetDataset();
        saveAllData();
        STATE = 'R';

        break;

  //to calibrate the probe
      case 'F':
        calibrateph();
        break;

  //to calibrate the probe part2
      case 'G':
        calibrateph();
        if (STATE == 'A') {

          writeFile("/ph4.txt", String(ph4).c_str());
          writeFile("/ph7.txt", String(ph7).c_str());

        }
        break;

  //for manual ph readings
      case 'H':
        getphadc();
        if (PHSTATE == 'D') {
          PHSTATE = 'A';
          STATE = 'A';
        }
        break;
  //waiting
      case 'W':

        break;
    }
  }
}