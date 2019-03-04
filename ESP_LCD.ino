//wifi enabled clock and info
#include <LiquidCrystal.h>
#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <WiFiUdp.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>

ESP8266WiFiMulti wifiMulti;

WiFiClient client;

// Open Weather Map API server name
const char server[] = "api.openweathermap.org";

//zip code
String zipCode = "#####"; //your zipcode

//open weather api
String apiKey = "#########################"; //your api key

int jsonend = 0;
boolean startJson = false;
int status = WL_IDLE_STATUS;

#define JSON_BUFF_DIMENSION 2500

String jsonResponse;
String currentTemp, weatherCondition, windDir, windSpeed;

unsigned long lastConnectionTime = 1 * 60 * 1000;     // last time you connected to the server, in milliseconds
const unsigned long postInterval = 1 * 60 * 1000;  // posting interval of 10 minutes  (10L * 1000L; 10 seconds delay for testing)

WiFiUDP UDP;

IPAddress timeServerIP;          // time.nist.gov NTP server address
const char* NTPServerName = "time.nist.gov";

const int NTP_PACKET_SIZE = 48;  // NTP time stamp is in the first 48 bytes of the message

byte NTPBuffer[NTP_PACKET_SIZE]; // buffer to hold incoming and outgoing packets

unsigned long intervalNTP = 60000; // Request NTP time every minute
unsigned long prevNTP = 0;
unsigned long lastNTPResponse = millis();
uint32_t timeUNIX = 0;

unsigned long prevActualTime = 0;

LiquidCrystal lcd(16, 5, 4, 0, 2, 14);

void setup() {
  Serial.begin(115200);

  WiFi.mode(WIFI_STA);
  wifiMulti.addAP("########", "##########"); //wifi credentials
  wifiMulti.addAP("########", "##########");

  Serial.println("Connecting WiFi...");
  while (wifiMulti.run() != WL_CONNECTED) {
    Serial.println(".");
    delay(500);
  }
    Serial.println("WiFi connected");
    Serial.println("IP address:  ");
    Serial.println(WiFi.localIP());

    startUDP();

    if(!WiFi.hostByName(NTPServerName, timeServerIP)) { // Get the IP address of the NTP server
    Serial.println("DNS lookup failed. Rebooting.");
    Serial.flush();
    ESP.reset();
  }
  Serial.print("Time server IP:\t");
  Serial.println(timeServerIP);
  
  Serial.println("\r\nSending NTP request ...");
  sendNTPpacket(timeServerIP);

  jsonResponse.reserve(JSON_BUFF_DIMENSION);
  
  
  lcd.begin(16, 2); //set number of rows and columns
  
}

void loop() {
  long current = millis();
  long offset = millis() + 10000;
  while (current < offset){
    displayTime();
    delay(100);
    current = millis();
  }
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print(weatherCondition);
  lcd.setCursor(0,1);
  lcd.print(currentTemp + " F");
  if (millis() - lastConnectionTime > postInterval) {
      // note the time that the connection was made:
      lastConnectionTime = millis();
      Serial.println("getting weather");
      getWeather();
    }
  delay(10000);
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print(windDir + " wind");
  lcd.setCursor(0,1);
  lcd.print(windSpeed + " mph");
  delay(10000);
  lcd.clear();
}

void displayTime() {
  int hours, minutes, seconds;
  String hoursStr, minutesStr, secondsStr;

   unsigned long currentMillis = millis();

  if (currentMillis - prevNTP > intervalNTP) { // If a minute has passed since last NTP request
    prevNTP = currentMillis;
    Serial.println("\r\nSending NTP request ...");
    sendNTPpacket(timeServerIP);               // Send an NTP request
  }

  uint32_t time = getTime();                   // Check if an NTP response has arrived and get the (UNIX) time
  if (time) {                                  // If a new timestamp has been received
    timeUNIX = time;
    Serial.print("NTP response:\t");
    Serial.println(timeUNIX);
    lastNTPResponse = currentMillis;
  } else if ((currentMillis - lastNTPResponse) > 3600000) {
    Serial.println("More than 1 hour since last NTP response. Rebooting.");
    Serial.flush();
    ESP.reset();
  }

  uint32_t actualTime = timeUNIX + (currentMillis - lastNTPResponse)/1000;
  if (actualTime != prevActualTime && timeUNIX != 0) { // If a second has passed since last print
    prevActualTime = actualTime;
    Serial.printf("\rUTC time:\t%d:%d:%d   ", getHours(actualTime), getMinutes(actualTime), getSeconds(actualTime));
    Serial.println("");
    
    hours = getHours(actualTime) - 6;
    if (hours < 1){
      hours = 24 + hours;
    }
    minutes = getMinutes(actualTime);
    seconds = getSeconds(actualTime);

    if (hours > 12)
      hours = (hours - 12);

    if (hours < 10)
      hoursStr = "0" + (String)hours;
    else
      hoursStr = hours;

    if (minutes < 10)
      minutesStr = "0" + (String)minutes;
    else
      minutesStr = minutes;

    if (seconds < 10)
      secondsStr = "0" + (String)seconds;
    else
      secondsStr = seconds;
    
    String date = getDate((time_t)timeUNIX);

    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print(date);
    lcd.setCursor(8,1);
    lcd.print(hoursStr + ":" + minutesStr + ":" + secondsStr);
  } 
}

void startUDP() {
  Serial.println("Starting UDP");
  UDP.begin(123);                          // Start listening for UDP messages on port 123
  Serial.print("Local port:\t");
  Serial.println(UDP.localPort());
  Serial.println();
}

uint32_t getTime() {
  if (UDP.parsePacket() == 0) { // If there's no response (yet)
    return 0;
  }
  UDP.read(NTPBuffer, NTP_PACKET_SIZE); // read the packet into the buffer
  // Combine the 4 timestamp bytes into one 32-bit number
  uint32_t NTPTime = (NTPBuffer[40] << 24) | (NTPBuffer[41] << 16) | (NTPBuffer[42] << 8) | NTPBuffer[43];
  // Convert NTP time to a UNIX timestamp:
  // Unix time starts on Jan 1 1970. That's 2208988800 seconds in NTP time:
  const uint32_t seventyYears = 2208988800UL;
  // subtract seventy years:
  uint32_t UNIXTime = NTPTime - seventyYears;
  return UNIXTime;
}

void sendNTPpacket(IPAddress& address) {
  memset(NTPBuffer, 0, NTP_PACKET_SIZE);  // set all bytes in the buffer to 0
  // Initialize values needed to form NTP request
  NTPBuffer[0] = 0b11100011;   // LI, Version, Mode
  // send a packet requesting a timestamp:
  UDP.beginPacket(address, 123); // NTP requests are to port 123
  UDP.write(NTPBuffer, NTP_PACKET_SIZE);
  UDP.endPacket();
}

inline int getSeconds(uint32_t UNIXTime) {
  return UNIXTime % 60;
}

inline int getMinutes(uint32_t UNIXTime) {
  return UNIXTime / 60 % 60;
}

inline int getHours(uint32_t UNIXTime) {
  return UNIXTime / 3600 % 24;
}

inline String getDate(time_t UNIXTime) {
  char buffer [80];
  struct tm * timeinfo;
  time (&UNIXTime);
  timeinfo = localtime (&UNIXTime);
  
  strftime(buffer, 80, "%x",timeinfo);
  return buffer;
}

void getWeather(){
  client.stop();

  if (client.connect(server, 80)){
    client.println("GET /data/2.5/weather?zip=" + zipCode + "&APPID=" + apiKey + "&units=imperial");
    client.println("Host: api.openweathermap.org");
    client.println("User-Agent: ArduinoWiFi/1.1");
    client.println("Connection: Close");
    client.println();

    unsigned long timeout = millis();
    while (client.available() == 0){
      if (millis() - timeout > 5000){
        Serial.println(">>> Client Timeout!");
        client.stop();
        return;
      }
    }
  

    char c = 0;
    while (client.available()){
      c = client.read();
  
      if (c == '{'){
        startJson = true;
        jsonend++;
      }
      if (c == '}'){
        jsonend--;
      }
      if (startJson == true){
        jsonResponse += c;
      }
      if (jsonend == 0 && startJson == true){
        parseJson(jsonResponse.c_str());
        jsonResponse = "";
        startJson = false;
      }
    }
  }
  else{
    Serial.println("connection failed");
    return;
  }
}


void parseJson(const char * jsonString) {
  //StaticJsonBuffer<4000> jsonBuffer;
  const size_t capacity = JSON_ARRAY_SIZE(3) + JSON_OBJECT_SIZE(1) + 2*JSON_OBJECT_SIZE(2) + 3*JSON_OBJECT_SIZE(4) + JSON_OBJECT_SIZE(5) + JSON_OBJECT_SIZE(6) + JSON_OBJECT_SIZE(12) + 510;
  DynamicJsonBuffer jsonBuffer(capacity);

  // FIND FIELDS IN JSON TREE
  JsonObject& root = jsonBuffer.parseObject(jsonString);
  if (!root.success()) {
    Serial.println("parseObject() failed");
    return;
  }

  JsonArray& list = root["list"];
  JsonObject& nowT = list[0];
  JsonObject& later = list[1];
  
  JsonObject& main = root["main"];
  float main_temp = main["temp"]; // 49.14
  int main_pressure = main["pressure"]; // 1015
  int main_humidity = main["humidity"]; // 100
  float main_temp_min = main["temp_min"]; // 46.94
  float main_temp_max = main["temp_max"]; // 51.8

  JsonArray& weather = root["weather"];

  JsonObject& weather0 = weather[0];
  int weather0_id = weather0["id"]; // 741
  const char* weather0_main = weather0["main"]; // "Fog"
  const char* weather0_description = weather0["description"]; // "fog"
  const char* weather0_icon = weather0["icon"]; // "50d"

  float wind_speed = root["wind"]["speed"];
  float wind_deg = root["wind"]["deg"];

  windSpeed = (String)wind_speed;

  if (wind_deg > 337.5)
    windDir = "north";
  else if (wind_deg > 292.5)
    windDir = "north west";
  else if (wind_deg > 247.5)
    windDir = "west";
  else if (wind_deg > 202.5)
    windDir = "south west";
  else if (wind_deg > 157.5)
    windDir = "south";
  else if (wind_deg > 122.5)
    windDir = "south east";
  else if (wind_deg > 67.5)
    windDir = "east";
  else if (wind_deg > 22.5)
    windDir = "north east";
  else
    windDir = "north";
  
  Serial.println(main_temp);
  currentTemp = (String)main_temp;
  weatherCondition = (String)weather0_description;
}
