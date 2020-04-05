/*
This is a rework of the great design published at https://www.thingiverse.com/thing:4190016
I modified the code to fit Wemos D1 Mini and to use Wifi to fetch the time from NTP server.

It uses a ws2812b with 30 leds per meter.
*/


#define FASTLED_ALLOW_INTERRUPTS 0
#include <FastLED.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <ESP8266HTTPClient.h>

#include <TimeLib.h>
#include <PubSubClient.h>


#define LED_COUNT 32
#define LED_PIN D2


// TimeZoneDB Configuration
// replace your api key and location (longitude,latitude)
#define TIMEZONEDB_URL "http://api.timezonedb.com/v2.1/get-time-zone?key=YOUR_API_KEY&format=json&by=position&lat=38.329369&lng=39.856541"



// MQTT Configuration
//
#define MQTT_SERVER "10.0.0.100"
#define MQTT_PORT 1883
#define MQTT_USER "username"
#define MQTT_PASS "password"
#define STATION_NAME "ledclock1"
#define TELEMETRY_INTERVAL_MS 300000
#define TOPIC_CMND_INBOUND "cmnd/" STATION_NAME "/#"
#define TOPIC_GROUP_INBOUND "cmnd/sonoffs/#"
#define TOPIC_OUTBOUND "stat/" STATION_NAME "/POWER"



#define N 3

String ssidList[N] = { "ssid1", "ssid2", "ssid3" };
String passList[N] = { "pass1", "pass2", "pass3" };

// NTP Configuration
const unsigned long NTP_UPDATE_INTERVAL = 5 * 60000; // milliseconds
const unsigned long NTP_TIMEZONE_UPDATE_INTERVAL_MS = 12 * 60 * 60000; // 12 hours
const unsigned long NTP_PRINT_TIME_INTERVAL_MS = 1000L;




WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);
long lastNtpSyncTime = 0;
long lastTimeZoneUpdate = 0;

// buffer for printing the hour
char response[512];


//
// MQTT related variables
//
void callback(char* topic, byte* payload, unsigned int length);
WiFiClient wifiClient;
PubSubClient mqttclient(MQTT_SERVER, MQTT_PORT, callback, wifiClient);
unsigned long lastTelemetryTime = -TELEMETRY_INTERVAL_MS;


//
// LED related variables
//

const unsigned long LED_UPDATE_INTERVAL_MS = 250;
unsigned long lastLedUpdateTime = - LED_UPDATE_INTERVAL_MS;

CRGB leds[LED_COUNT];

byte brightness = 250;
byte saturation = 255;
byte digitColor = 0;
byte dotsColor = 0;
boolean use12H = true;

byte segGroups[14] = {
  // right (seen from front) digit
  2,   // top, a
  3,   // top right, b
  4,   // bottom right, c
  5,   // bottom, d
  6,   // bottom left, e
  1,   // top left, f
  0,   // center, g

  12,  // top, a
  13,  // top right, b
  8,   // bottom right, c
  9,   // bottom, d
  10,  // bottom left, e
  11,  // top left, f
  14   // center, g

};

byte digits[10][7] = {
  { 1, 1, 1, 1, 1, 1, 0 },  // 0
  { 0, 1, 1, 0, 0, 0, 0 },  // 1
  { 1, 1, 0, 1, 1, 0, 1 },  // 2
  { 1, 1, 1, 1, 0, 0, 1 },  // 3
  { 0, 1, 1, 0, 0, 1, 1 },  // 4
  { 1, 0, 1, 1, 0, 1, 1 },  // 5
  { 1, 0, 1, 1, 1, 1, 1 },  // 6
  { 1, 1, 1, 0, 0, 0, 0 },  // 7
  { 1, 1, 1, 1, 1, 1, 1 },  // 8
  { 1, 1, 1, 1, 0, 1, 1 },  // 9
};

void setup() {
  Serial.begin(115200);

  Serial.println("setup FastLED");

  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, LED_COUNT);
  FastLED.setMaxPowerInVoltsAndMilliamps(5, 1000);
  FastLED.setDither(0);
  FastLED.setBrightness(brightness);

  FastLED.clear();
  showDigit(0, 1, digitColor);
  FastLED.show();

  // Wifi init
  bool connected = false;
  int i = 0;

  while (!connected) {
    char b1[32];
    ssidList[i].toCharArray(b1, ssidList[i].length() + 1);
    char b2[32];
    passList[i].toCharArray(b2, passList[i].length() + 1);

    Serial.print("Connecting to ");
    Serial.println(b1);

    WiFi.begin(b1, b2);

    // try this network for some time and then try the next network
    int c = 30;
    while (WiFi.status() != WL_CONNECTED && c > 0) {
      delay(500);
      Serial.print(".");
      c--;
    }
    Serial.println("");

    if (c == 0) {
      // this mean we need to try the next network
      i = (i < N - 1) ? i + 1 : 0;
    }
    connected = WiFi.status() == WL_CONNECTED;
  }

  Serial.println("WiFi connected, IP: ");
  Serial.println(WiFi.localIP());

  // prevent the wifi to go to sleep
  // requires more power in total
  WiFi.setSleepMode(WIFI_NONE_SLEEP);

  
  // led shows '2' if wifi is connected
  FastLED.clear();
  showDigit(0, 2, digitColor);
  FastLED.show();


  reconnect();

  timeClient.begin();
  timeClient.setUpdateInterval(NTP_UPDATE_INTERVAL);

  // led shows '3' if timezone fetched
  FastLED.clear();
  showDigit(0, 3, digitColor);
  FastLED.show();
  
  fetchTimeZone();

  // led shows '4' if timezone fetched
  FastLED.clear();
  showDigit(0, 4, digitColor);
  FastLED.show();

  timeClient.update();

  delay(500);
}


void loop() {

  timeClient.update();


  // update timezone
  updateTimezone();

  //verify connection to wifi and mqtt
  if (!mqttclient.connected() || WiFi.status() != WL_CONNECTED) {
    reconnect();
  }

  //maintain MQTT connection
  mqttclient.loop();


  // send telemetry status message
  if (millis() - lastTelemetryTime >=  TELEMETRY_INTERVAL_MS) {
    sendTelemetry();
    lastTelemetryTime = millis();
  }


  if (millis() - lastLedUpdateTime >= LED_UPDATE_INTERVAL_MS) {
    showLedTime();
    lastLedUpdateTime = millis();
  }

  delay(2);

}

void showLedTime() {

  FastLED.clear();
  FastLED.setBrightness(brightness);

  // minutes
  int minute = timeClient.getMinutes();
  showDigit(0, minute % 10, digitColor);
  showDigit(1, minute / 10, digitColor);

  // hours
  int hours = timeClient.getHours();
  if (use12H) {
    hours = hours > 12 ? hours - 12 : hours;
  }
  showDigit(2, hours % 10, digitColor);
  if (hours > 9) {
    showDigit(3, hours / 10, digitColor);
  }

  // dots as seconds
  int seconds = timeClient.getSeconds();
  if (seconds % 2 == 0) {
    leds[15].setHSV(dotsColor, saturation, brightness);
    leds[16].setHSV(dotsColor, saturation, brightness);
  }

  FastLED.show();

}

void showSegment(byte digitPosition, byte segment, byte color) {
  byte index = digitPosition % 2 == 0 ? 0 : 7;
  byte stripIndex = segGroups[ index + segment ];
  if (digitPosition >= 2) {
    stripIndex += 17;
  }
  leds[stripIndex].setHSV(color, saturation, brightness);
}

void showDigit(byte digitPosition, byte digit, byte color) {
  for (byte i = 0; i < 7; i++) {
    if (digits[digit][i] != 0) {
      showSegment(digitPosition, i, color);
    }
  }
}


bool fetchTimeZone() {
  bool ok = false;

  HTTPClient http;
  http.begin(TIMEZONEDB_URL);
  int httpCode = http.GET();
  //Check the returning code
  if (httpCode > 0) {
    // Get the request response payload
    String payload = http.getString();
    Serial.println(payload);
    int start = payload.indexOf("gmtOffset");
    if (start > 0) {
      int finish = payload.indexOf(",", start);
      int offsetSeconds = payload.substring(start + 11, finish).toInt();
      Serial.print("detected offset of ");
      Serial.println(offsetSeconds);
      timeClient.setTimeOffset(offsetSeconds);
      ok = true;
    }
  } else {
    Serial.print("error with http status code: ");
    Serial.println(httpCode);
    timeClient.setTimeOffset(2 * 3600);
  }
  http.end();   //Close connection

  return ok;
}



void updateTimezone() {
  if (millis() - lastTimeZoneUpdate >= NTP_TIMEZONE_UPDATE_INTERVAL_MS) {
    if (fetchTimeZone()) {
      lastTimeZoneUpdate = millis();
    } else {
      Serial.println("will try to fetch time zone one more time in a minute");
      lastTimeZoneUpdate += 60000;
    }
  }
}



//////////////////////////////////
// MQTT related code
//////////////////////////////////

char* string2char(String command) {
  if (command.length() != 0) {
    char *p = const_cast<char*>(command.c_str());
    return p;
  }
}

void callback(char* topic, byte* payload, unsigned int length) {

  //convert topic to string to make it easier to work with
  String topicStr = topic;

  //Print out some debugging info
  Serial.print("Message received on topic ");
  Serial.println(topicStr);
  Serial.print("Message payload: ");
  //note payload must not be over 512 bytes (MQTT_MAX_PACKET_SIZE)
  payload[length] = '\0';
  String action = String((char*)payload);
  Serial.println(action);

  int idx1 = topicStr.indexOf('/');
  int idx2 = topicStr.indexOf('/', idx1 + 1);

  // check input
  if (idx1 == -1 || idx2 == -1) {
    Serial.println("bad request. ignore");
    return;
  }

  String device = topicStr.substring(idx1 + 1, idx2);
  Serial.print("device: "); Serial.println(device);

  // first check if this meesage is for me
  //String nodeId = String(STATION_NAME);
  if (device.equals(STATION_NAME)) {
    sendTelemetry();

    String command = topicStr.substring(idx2 + 1);
    command.toLowerCase();
    Serial.print("command: "); Serial.println(command);

    // color change command
    if (command.equals("color")) {
      Serial.print("change color to ");
      Serial.println(action.toInt());
      digitColor = action.toInt();
    }

    if (command.equals("dots")) {
      Serial.print("change dots color to ");
      Serial.println(action.toInt());
      dotsColor = action.toInt();
    }

    if (command.equals("use12h")) {
      Serial.print("use12H: ");
      Serial.println(action);
      use12H = action.equals("1");
    }
  }

  if (topicStr.equals("cmnd/sonoffs/Status")) {
    sendNetworkStatus();
  }
}


void sendTelemetry() {
  String topic;

  // publish STATE message with RSSI
  int quality = 2 * (WiFi.RSSI() + 100);
  sprintf(response,
          "{\"Time\":\"%s\",\"Wifi\":{\"SSId\":\"%s\",\"RSSI\":%d}}",
          (char *)timeClient.getFormattedTime().c_str(), WiFi.SSID().c_str(), quality);
  Serial.println(response);
  topic = "tele/";
  topic += STATION_NAME;
  topic += "/STATE";
  mqttclient.publish(string2char(topic), response);

}


void sendNetworkStatus() {
  sprintf(response, "{\"StatusNET\":{\"Hostname\":\"%s\",\"IPAddress\":\"%s\",\"Mac\":\"%s\"}}",
          STATION_NAME, WiFi.localIP().toString().c_str(), WiFi.macAddress().c_str());
  Serial.println(response);

  // send mqtt message
  String topic = "stat/";
  topic += STATION_NAME;
  topic += "/STATUS5";
  mqttclient.publish(string2char(topic), response);

}


void reconnect() {

  //attempt to connect to the wifi if connection is lost
  if (WiFi.status() != WL_CONNECTED) {
    Serial.print("Connecting to ");
    Serial.println(WiFi.SSID());
    //loop while we wait for connection
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
    }
    Serial.println("");
    Serial.println("WiFi connected");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  }

  //make sure we are connected to WIFI before attemping to reconnect to MQTT
  if (WiFi.status() == WL_CONNECTED) {
    // Loop until we're reconnected to the MQTT server
    while (!mqttclient.connected()) {
      Serial.print("Attempting MQTT connection...");
      //if connected, subscribe to the topic(s) we want to be notified about
      if (mqttclient.connect(STATION_NAME, MQTT_USER, MQTT_PASS)) {
        Serial.println("MQTT Connected");
        mqttclient.subscribe(TOPIC_CMND_INBOUND);
        mqttclient.subscribe(TOPIC_GROUP_INBOUND);
      } else {
        Serial.println("Failed.");
        delay(1000);
      }
    }
  }

}
