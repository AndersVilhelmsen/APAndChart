
#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include "SPIFFS.h"
#include <ArduinoJson.h>

// Libraries for SD card
#include "FS.h"
#include "SD.h"
#include <SPI.h>

//DS18B20 libraries
#include <OneWire.h>
#include <DallasTemperature.h>

// Libraries to get time from NTP Server
#include <WiFi.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

// GPIO where the sensor is connected to
#define oneWireBus 4    
// Define CS pin for the SD card module
#define SD_CS 5

// Setup a oneWire instance to communicate with any OneWire devices
OneWire oneWire(oneWireBus);

// Pass our oneWire reference to Dallas Temperature sensor 
DallasTemperature sensors(&oneWire);

// Temperature Sensor variables
String temperature;

// Save reading number on RTC memory
RTC_DATA_ATTR int readingID = 0;

String dataMessage;

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);

// Search for parameter in HTTP POST request
const char* PARAM_INPUT_1 = "ssid";
const char* PARAM_INPUT_2 = "pass";
const char* PARAM_INPUT_3 = "ip";
const char* PARAM_INPUT_4 = "gateway";

//Variables to save values from HTML form
String ssid;
String pass;
String ip;
String gateway;

// File paths to save input values permanently
const char* configPath = "/config.json";

IPAddress localIP;
//IPAddress localIP(192, 168, 0, 147); // hardcoded

// Set your Gateway IP address
IPAddress localGateway;
//IPAddress localGateway(192, 168, 0, 1); //hardcoded
IPAddress subnet(255, 255, 0, 0);
//DNS needed for NTPClient
IPAddress dns(8,8,8,8);

// Define NTP Client to get time
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);

// Timer variables
unsigned long previousMillis = 0;
// interval to wait for Wi-Fi connection (milliseconds)
const long interval = 10000;  

unsigned long lastTime = 0;
unsigned long timerDelay = 10000;

// Variables to save date and time
String formattedDate;
String dayStamp;
String timeStamp;

//forward declarations
void readDSTemperatureC();
void getTimeStamp();
void logSDCard();
void writeFile(fs::FS &fs, const char * path, const char * message);
void appendFile(fs::FS &fs, const char * path, const char * message);
void deleteFile(fs::FS &fs, const char * path);

// Initialize SPIFFS
void initSPIFFS() {
  if (!SPIFFS.begin(true)) {
    Serial.println("An error has occurred while mounting SPIFFS");
  }
  Serial.println("SPIFFS mounted successfully");
}

// Read File from SPIFFS
String readFile(fs::FS &fs, const char * path){
  Serial.printf("Reading file: %s\r\n", path);

  File file = fs.open(path);
  if(!file || file.isDirectory()){
    Serial.println("- failed to open file for reading");
    return String();
  }
  
  String fileContent;
  while(file.available()){
    fileContent = file.readStringUntil('\n');
    Serial.println(fileContent);
    break;     
  }
  return fileContent;
}

// Initialize WiFi
bool initWiFi() {
  if(ssid=="" || ip==""){
    Serial.println("Undefined SSID or IP address.");
    return false;
  }

  WiFi.mode(WIFI_STA);
  localIP.fromString(ip.c_str());
  localGateway.fromString(gateway.c_str());
  
  if (!WiFi.config(localIP, localGateway, subnet, dns)){
    Serial.println("STA Failed to configure");
    return false;
  }
  WiFi.begin(ssid.c_str(), pass.c_str());
  Serial.println("Connecting to WiFi...");

  unsigned long currentMillis = millis();
  previousMillis = currentMillis;

  while(WiFi.status() != WL_CONNECTED) {
    currentMillis = millis();
    if (currentMillis - previousMillis >= interval) {
      Serial.println("Failed to connect.");
      return false;
    }
  }

  Serial.println(WiFi.localIP());
  return true;
}

//html for wifi-manager
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>ESP Wi-Fi Manager</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <link rel="icon" href="data:,">
  <link rel="stylesheet" type="text/css" href="style.css">
</head>
<body>
  <div class="topnav">
    <h1>ESP Wi-Fi Manager *** PROGMEM ***</h1>
  </div>
  <div class="content">
    <div class="card-grid">
      <div class="card">
        <form action="/" method="POST">
          <p>
            <label for="ssid">SSID</label>
            <input type="text" id ="ssid" name="ssid"><br>
            <label for="pass">Password</label>
            <input type="text" id ="pass" name="pass"><br>
            <label for="ip">IP Address</label>
            <input type="text" id ="ip" name="ip" value="192.168.0.147"><br>
            <label for="gateway">Gateway Address</label>
            <input type="text" id ="gateway" name="gateway" value="192.168.0.1"><br>
            <input type ="submit" value ="Submit">
          </p>
        </form>
      </div>
    </div>
  </div>
</body>
</html>
)rawliteral";

// Replaces placeholder temperature
String processor(const String& var){
  Serial.println(var);
  if(var == "TEMPERATUREC"){
   return String(temperature);
  }
  return String();
}

void setup() {
  // Serial port for debugging purposes
  Serial.begin(115200);

  sensors.begin();
  temperature = sensors.getTempCByIndex(0);


  initSPIFFS();

  //Converting to json
  DynamicJsonDocument doc2(1024);
  deserializeJson(doc2, readFile (SPIFFS, configPath));
  const char* ssid2 = doc2["ssid"];
  const char* pass2 = doc2["pass"];
  const char* ip2 = doc2["ip"];
  const char* gateway2 = doc2["gateway"];
  
  ssid = ssid2;
  pass=pass2;
  ip=ip2;
  gateway=gateway2;

  Serial.println(ssid);
  Serial.println(pass);
  Serial.println(ip);
  Serial.println(gateway);

  if(initWiFi()) {
  
    server.serveStatic("/", SPIFFS, "/");
    
    // Route for root / web page
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
      request->send(SPIFFS, "/index.html", "text/html", false, processor);
    });
    
    server.on("/deleteData", HTTP_GET, [](AsyncWebServerRequest *request){
      deleteFile(SD, "/data.txt");
      writeFile(SD, "/data.txt", "Reading ID, Date, Hour, Temperature \r\n");
    request->send(SPIFFS, "/index.html", String(), false, processor);
    });
    
    server.on("/deleteConfig", HTTP_GET, [](AsyncWebServerRequest *request){
      deleteFile(SPIFFS, configPath);
    request->send_P(200, "text/html", index_html, processor);
    // request->send(SPIFFS, "/index.html", String(), false, processor);
    });

    server.on("/temperaturec", HTTP_GET, [](AsyncWebServerRequest *request){
      readDSTemperatureC();
      getTimeStamp();
      logSDCard();
    request->send_P(200, "text/plain", temperature.c_str());
    });

  // Initialize SD card
  SD.begin(SD_CS);  
  if(!SD.begin(SD_CS)) {
    Serial.println("Card Mount Failed");
    return;
  }
  uint8_t cardType = SD.cardType();
  if(cardType == CARD_NONE) {
    Serial.println("No SD card attached");
    return;
  }
  Serial.println("Initializing SD card...");
  if (!SD.begin(SD_CS)) {
    Serial.println("ERROR - SD card initialization failed!");
    return;    // init failed
  }

  // If the data.txt file doesn't exist
  // Create a file on the SD card and write the data labels
  File file = SD.open("/data.txt");
  if(!file) {
    Serial.println("File doens't exist");
    Serial.println("Creating file...");
    writeFile(SD, "/data.txt", "Reading ID, Date, Hour, Temperature \r\n");
  }
  else {
    Serial.println("File already exists");  
  }
  file.close();

  // Initialize a NTPClient to get time
  timeClient.begin();
  // Set offset time in seconds to adjust for your timezone, for example:
  // GMT +1 = 3600
  // GMT +8 = 28800
  // GMT -1 = -3600
  // GMT 0 = 0
  timeClient.setTimeOffset(3600);

  
    server.begin();

  }
  else {
    // Connect to Wi-Fi network with SSID and password
    Serial.println("Setting AP (Access Point)");
    // NULL sets an open Access Point
    WiFi.softAP("ESP-WIFI-MANAGER-Anders", NULL);

    IPAddress IP = WiFi.softAPIP();
    Serial.print("AP IP address: ");
    Serial.println(IP); 

    // Web Server Root URL
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
      // request->send(SPIFFS, "/wifimanager.html", "text/html");
      request->send_P(200, "text/html", index_html, processor);
    });
    
    server.serveStatic("/", SPIFFS, "/");
    
    server.on("/", HTTP_POST, [](AsyncWebServerRequest *request) {
      
      int params = request->params();
      for(int i=0;i<params;i++){
        AsyncWebParameter* p = request->getParam(i);
        if(p->isPost()){
          // HTTP POST ssid value
          if (p->name() == PARAM_INPUT_1) {
            ssid = p->value().c_str();
          }
          // HTTP POST pass value
          if (p->name() == PARAM_INPUT_2) {
            pass = p->value().c_str();
          }
          // HTTP POST ip value
          if (p->name() == PARAM_INPUT_3) {
            ip = p->value().c_str();
          }
          // HTTP POST gateway value
          if (p->name() == PARAM_INPUT_4) {
            gateway = p->value().c_str();
          }
        }
      }
        //convert config to json
        DynamicJsonDocument doc(1024);

          doc["ssid"] = ssid;
          doc["pass"]   = pass;
          doc["ip"] = ip;
          doc["gateway"] = gateway;
          
          String config;
          serializeJson(doc, config);
          
          Serial.println(config);
          writeFile(SPIFFS, configPath, config.c_str());
      
      request->send(200, "text/plain", "Done. ESP will restart, connect to your router and go to IP address: " + ip);
      delay(3000);
      ESP.restart();
    });
    server.begin();
  }
}

//collect data from sensor
void readDSTemperatureC() {
  // Call sensors.requestTemperatures() to issue a global temperature and Requests to all devices on the bus
  sensors.requestTemperatures(); 
  float tempC = sensors.getTempCByIndex(0);

  if(tempC == -127.00) {
    Serial.println("Failed to read from sensor");
  } else {
    Serial.print("Temperature Celsius: ");
    Serial.println(tempC); 
  }
  temperature = tempC;
}

void loop(){
  //Not needed since the webpage triggers the collection of data
}

// Function to get date and time from NTPClient
void getTimeStamp() {
  while(!timeClient.update()) {
    timeClient.forceUpdate();
  }
  // The formattedDate comes with the following format:
  // 2018-05-28T16:00:13Z
  // We need to extract date and time
  formattedDate = timeClient.getFormattedDate();
  Serial.println(formattedDate);

  // Extract date
  int splitT = formattedDate.indexOf("T");
  dayStamp = formattedDate.substring(0, splitT);
  Serial.println(dayStamp);
  // // Extract time
  timeStamp = formattedDate.substring(splitT+1, formattedDate.length()-1);
  Serial.println(timeStamp);
}

// Write the sensor readings on the SD card
void logSDCard() {
  dataMessage = String(readingID) + "," + String(dayStamp) + "," + String(timeStamp) + "," + 
                String(temperature) + "\r\n";
  Serial.print("Save data: ");
  Serial.println(dataMessage);
  appendFile(SD, "/data.txt", dataMessage.c_str());
}

// Write the first line to the SD card
void writeFile(fs::FS &fs, const char * path, const char * message) {
  Serial.printf("Writing file: %s\n", path);

  File file = fs.open(path, FILE_WRITE);
  if(!file) {
    Serial.println("Failed to open file for writing");
    return;
  }
  if(file.print(message)) {
    Serial.println("File written");
  } else {
    Serial.println("Write failed");
  }
  file.close();
}

// Append data to the SD card
void appendFile(fs::FS &fs, const char * path, const char * message) {
  Serial.printf("Appending to file: %s\n", path);

  File file = fs.open(path, FILE_APPEND);
  if(!file) {
    Serial.println("Failed to open file for appending");
    return;
  }
  if(file.print(message)) {
    Serial.println("Message appended\n");
  } else {
    Serial.println("Append failed");
  }
  file.close();
}

void deleteFile(fs::FS &fs, const char * path) {
  Serial.printf("deleting file: %s\n", path);
  fs.remove(path);
}