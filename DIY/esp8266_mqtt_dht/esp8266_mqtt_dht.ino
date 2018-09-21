/*
  MQTT Light for Home-Assistant - NodeMCU (ESP8266)
  https://home-assistant.io/components/switch.mqtt/

  Features:
    - WiFiManager
    - mDNS
    - OTA

  Libraries:
    - ESP8266 core for Arduino : https://github.com/esp8266/Arduino
    - WiFiManager: https://github.com/tzapu/WiFiManager
    - ESP8266mDNS: https://github.com/esp8266/Arduino/tree/master/libraries/ESP8266mDNS
    - ArduinoJson: https://github.com/bblanchon/ArduinoJson

  Sources:
    - File > Examples > WiFiManager > AutoConnectWithFSParameters
    - File > Examples > ArduinoOTA > BasicOTA

  Configuration (HA):
  Warning: 'F90D5F' corresponds to the chip ID of my ESP
  Your chip ID is visible in the logs and the initial WIFI AP has the same name
    light:
      platform: mqtt
      name: 'Office light'
      state_topic: 'F90D5F/light'
      command_topic: 'F90D5F/light/switch'
      optimistic: false

  Samuel M. - v1.0 - 08.2016
  If you like this example, please add a star! Thank you!
  https://github.com/mertenats/open-home-automation
*/

#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include "DHT.h"
#include <ArduinoJson.h>

/*
  WiFiManager
  Doc: https://github.com/tzapu/WiFiManager
  Moves the ESP8266 into AP mode and spins up a DNS and WebServer, IP 192.168.4.1
  Scans the available AP, asks the password for the wished AP and tries to connect to it
  Custom parameters:
    - MQTT username
    - MQTT password
  How-to:
    - Define #WIFI_MANAGER, or
    - Set manually the variables, lines 72-75
*/

#define WIFI_MANAGER

#ifdef WIFI_MANAGER
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <ArduinoJson.h>
#include <FS.h>

// Wifi AP: SSID: chip ID, passowrd: WIFI_PASSWORD
const char              WIFI_PASSWORD[]           = "password";
char                    MQTT_USERNAME[10]         = {0};
char                    MQTT_PASSWORD[10]         = {0};

// flag for saving data
bool                    m_shouldSaveConfig        = false;

void saveConfigCallback () {
  Serial.println(F("INFO: Should save config"));
  m_shouldSaveConfig = true;
}
#else
const PROGMEM char*     WIFI_SSID                 = "[Redacted]";
const PROGMEM char*     WIFI_PASSWORD             = "[Redacted]";
const PROGMEM char*     MQTT_USERNAME             = "[Redacted]";
const PROGMEM char*     MQTT_PASSWORD             = "[Redacted]";
#endif

/*
  DNS-SD - http://www.dns-sd.org
  Soft: Avahi (Linux) or Bonjour (OS X/Windows)
  Doc:  http://linux.die.net/man/5/avahi.service
  Create a service on your machine, which hosts the MQTT broker
  On Linux:
    - sudo apt-get install avahi-deamon (installed by default)
    - sudo nano /etc/avahi/services/mqtt.service
    - Copy/paste the following lines:
        <?xml version="1.0" standalone='no'?>
        <!DOCTYPE service-group SYSTEM "avahi-service.dtd">
        <service-group>
          <name replace-wildcards="yes">%h</name>
          <service>
            <type>_mqtt._tcp</type>
            <port>1883</port>
          </service>
        </service-group>
    - sudo service avahi-daemon restart
  How-to:
    - Define #MDNS_SD, or
    - Set manually the variables, lines 106-107
*/
#define MDNS_SD

#ifdef MDNS_SD
#include <ESP8266mDNS.h>
#else
const PROGMEM char*     MQTT_SERVER_IP            = "[Redacted]";
const PROGMEM uint16_t  MQTT_SERVER_PORT          = 1883;
#endif

/*
  OTA
  Password: "1234" (line 341)
*/

#define OTA
#ifdef OTA
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#endif

// MQTT
#define MQTT_VERSION MQTT_VERSION_3_1_1
char                    MQTT_CLIENT_ID[20]         = {0};
char                    WIFI_AP_ID[20]         = {0};

// topics: status:  <MQTT_CLIENT_ID>/light
//         command: <MQTT_CLIENT_ID>/sensor/dht/<MQTT_CLIENT_ID>
char                    MQTT_TOPIC[100]   = {0};

// default payload
const char*             LIGHT_ON                  = "ON";
const char*             LIGHT_OFF                 = "OFF";

// light is turned off by default
boolean                 m_light_state             = false;

// sleeping time
const PROGMEM uint16_t SLEEPING_TIME_IN_SECONDS = 600; // 10 minutes x 60 seconds

// DHT - D1/GPIO5
#define DHTPIN 2
#define DHTTYPE DHT22


WiFiClient wifiClient;
PubSubClient client(wifiClient);

DHT dht(DHTPIN, DHTTYPE);


float humidity = NAN;
float temperature = NAN;

// function called to publish the temperature and the humidity
void publishData(float p_temperature, float p_humidity) {
  // create a JSON object
  // doc : https://github.com/bblanchon/ArduinoJson/wiki/API%20Reference
  StaticJsonBuffer<200> jsonBuffer;
  JsonObject& root = jsonBuffer.createObject();
  // INFO: the data must be converted into a string; a problem occurs when using floats...
  root["temperature"] = (String)p_temperature;
  root["humidity"] = (String)p_humidity;
  root.prettyPrintTo(Serial);
  Serial.println("");
  /*
     {
        "temperature": "23.20" ,
        "humidity": "43.70"
     }
  */
  char data[200];
  root.printTo(data, root.measureLength() + 1);
  client.publish(MQTT_TOPIC, data, true);
  yield();
}

// function called when a MQTT message arrived
void callback(char* p_topic, byte* p_payload, unsigned int p_length) {
}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.println("INFO: Attempting MQTT connection...");
    if (client.connect(MQTT_CLIENT_ID, MQTT_USERNAME, MQTT_PASSWORD)) {
      Serial.println(F("INFO: Successfully connected to the MQTT broker"));
    } else {
      Serial.print("ERROR: failed, rc=");
      Serial.print(client.state());
      Serial.println("DEBUG: try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}


void setup() {
  Serial.begin(115200);

  dht.begin();
  measure();
  
  // get the chip ID and concat this ID with the MQTT topics
  sprintf(MQTT_CLIENT_ID, "%06X", ESP.getChipId());
  sprintf(WIFI_AP_ID, "ESP_%s", MQTT_CLIENT_ID);
  sprintf(MQTT_TOPIC, "sensor/dht/%s", MQTT_CLIENT_ID);

  Serial.print(F("MQTT id:\t\t "));
  Serial.println(MQTT_CLIENT_ID);

  Serial.print(F("WIFI ap id:\t\t "));
  Serial.println(WIFI_AP_ID);

  Serial.print(F("MQTT topic:\t "));
  Serial.println(MQTT_TOPIC);
#ifdef WIFI_MANAGER
  // clean FS, for testing
  SPIFFS.format();

  // read configuration from FS json
  Serial.println(F("INFO: mounting FS..."));

  if (SPIFFS.begin()) {
    Serial.println(F("INFO: mounted file system"));
    if (SPIFFS.exists("/config.json")) {
      Serial.println(F("INFO: reading config file"));
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        Serial.println(F("INFO: opened config file"));
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success()) {
          Serial.println(F("\nINFO: parsed json"));
          strcpy(MQTT_USERNAME, json["mqtt_username"]);
          strcpy(MQTT_PASSWORD, json["mqtt_password"]);
        } else {
          Serial.println(F("ERROR: failed to load json config"));
        }
      }
    }
  } else {
    Serial.println(F("ERROR: failed to mount FS"));
  }

  WiFiManagerParameter custom_mqtt_username("mqtt_username", "MQTT username", MQTT_USERNAME, 15);
  WiFiManagerParameter custom_mqtt_password("mqtt_password", "MQTT password", MQTT_PASSWORD, 15, "type=\"password\"");

  // WiFiManager, local intialization
  WiFiManager wifiManager;

  // set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  // add all the parameters
  wifiManager.addParameter(&custom_mqtt_username);
  wifiManager.addParameter(&custom_mqtt_password);

  //  // reset settings, for testing
  //  wifiManager.resetSettings();

  // disable the debug output
  wifiManager.setDebugOutput(true);

  // fetches ssid and pass and tries to connect
  // if it does not connect it starts an access point with the specified name
  if (!wifiManager.autoConnect(WIFI_AP_ID, WIFI_PASSWORD)) {
    Serial.println(F("Error: Failed to connect and hit timeout"));
    delay(3000);
    ESP.reset();
    delay(5000);
  }

  // if you get here you have connected to the WiFi
  Serial.println(F("INFO: Successfully connected to the Wifi AP"));

  // read updated parameters
  strcpy(MQTT_USERNAME, custom_mqtt_username.getValue());
  strcpy(MQTT_PASSWORD, custom_mqtt_password.getValue());

  // save the custom parameters to FS
  if (m_shouldSaveConfig) {
    Serial.println(F("INFO: saving config"));
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    json["mqtt_username"] = MQTT_USERNAME;
    json["mqtt_password"] = MQTT_PASSWORD;

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println(F("ERROR: failed to open config file for writing"));
    }
    json.printTo(Serial);
    json.printTo(configFile);
    Serial.println();
    configFile.close();
  }

  Serial.print(F("INFO: MQTT username: "));
  Serial.println(MQTT_USERNAME);

  Serial.print(F("INFO: MQTT password: "));
  Serial.println(MQTT_PASSWORD);

  Serial.print(F("INFO: Local IP address: "));
  Serial.println(WiFi.localIP());
#else
  WiFi.hostname(MQTT_CLIENT_ID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
  }
  Serial.println(F("INFO: Successfully connected to the Wifi AP"));
  Serial.print(F("INFO: Local IP address: "));
  Serial.println(WiFi.localIP());
#endif

  delay(500);

#ifdef OTA
  // set port to 8266
  //ArduinoOTA.setPort(8266);

  // set hostname
  sprintf(MQTT_CLIENT_ID, "%06X", ESP.getChipId());
  ArduinoOTA.setHostname(MQTT_CLIENT_ID);

  // set a password
  ArduinoOTA.setPassword((const char *)"1234");

  ArduinoOTA.onStart([]() {
    Serial.println(F("INFO: Start OTA"));
  });
  ArduinoOTA.onEnd([]() {
    Serial.println(F("INFO: End OTA"));
    ESP.reset();
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("INFO: Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println(F("ERROR: OTA auth Failed"));
    else if (error == OTA_BEGIN_ERROR) Serial.println(F("ERROR: OTA begin Failed"));
    else if (error == OTA_CONNECT_ERROR) Serial.println(F("ERROR: OTA connect Failed"));
    else if (error == OTA_RECEIVE_ERROR) Serial.println(F("ERROR: OTA receive Failed"));
    else if (error == OTA_END_ERROR) Serial.println(F("ERROR: OTA end Failed"));
  });
  ArduinoOTA.begin();
#endif

  delay(500);

#ifdef MDNS_SD
  if (!MDNS.begin(MQTT_CLIENT_ID)) {
    Serial.println(F("ERROR: Setting up MDNS responder!"));
  }
  Serial.println(F("INFO: mDNS responder started"));
  Serial.println(F("INFO: Sending mDNS query"));
  int n = MDNS.queryService("mqtt", "tcp"); // Send out query for the MQTT service
  Serial.println(F("INFO: mDNS query done"));
  if (n == 0) {
    Serial.println(F("ERROR: No services found"));
  } else if (n > 1) {
    Serial.println(F("ERROR: Multiple services found, remove the unnecessary mDNS services"));
  } else {
    Serial.print(F("INFO: MQTT broker IP address: "));
    Serial.print(MDNS.IP(0));
    Serial.print(F(":"));
    Serial.println(MDNS.port(0));
    client.setServer(MDNS.IP(0), int(MDNS.port(0)));
  }
#else
  client.setServer(MQTT_SERVER_IP, MQTT_SERVER_PORT);
#endif

  client.setCallback(callback);
}

void measure() {
    int attemtps = 5;
    while (attemtps) {
    attemtps -= 1;
    // Reading temperature or humidity takes about 250 milliseconds!
    // Sensor readings may also be up to 2 seconds 'old' (its a very slow sensor)
    humidity = dht.readHumidity();
    // Read temperature as Celsius (the default)
    temperature = dht.readTemperature();

    if (isnan(humidity) || isnan(temperature)) {
      Serial.println("ERROR: Failed to read from DHT sensor!");
    } else {
      //Serial.println(h);
      //Serial.println(t);
      break;
    }
  }
}

void loop() {

  // MQTT
//  if (!client.connected()) {
//    reconnect();
//  }
//  client.loop();

//  ArduinoOTA.handle();

    if (isnan(humidity) || isnan(temperature)) {
      Serial.println("ERROR: Failed to send DHT data!");
    } else {
      publishData(temperature, humidity);
    }

  Serial.println("INFO: Closing the MQTT connection");
  client.disconnect();

//  WiFi.persistent(false);
//  Serial.println("INFO: Closing the Wifi connection");
//  WiFi.disconnect();

  ESP.deepSleep(SLEEPING_TIME_IN_SECONDS * 1000000, WAKE_RF_DEFAULT);
  delay(1000); // wait for deep sleep to happen
}
