#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
//#include <DNSServer.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager WiFi Configuration Magic
#include <PubSubClient.h>
#include <EEPROM.h>
#include <DoubleResetDetector.h>
#include "FastLED.h"
#include <Ticker.h>

#include <string.h>


#define DATA_PIN 5


struct MQTT_LED_config {
  unsigned char config_version;
  char broker[40];
  char port[6] = "1883";
  char user[20];
  char password[20];
  char prefix[40];
  unsigned short num_leds = 10;
} MQTT_LED_config;

#define CONFIG_VERSION 1

#define DRD_TIMEOUT 10

#define DRD_ADDRESS 0

DoubleResetDetector drd(DRD_TIMEOUT, DRD_ADDRESS);

WiFiClient espClient;
PubSubClient client(espClient);
WiFiManager wifiManager;

WiFiManagerParameter cfg_mqtt_broker("server", "mqtt broker", "", 40);
WiFiManagerParameter cfg_mqtt_port("port", "mqtt port", "", 6);
WiFiManagerParameter cfg_mqtt_user("user", "mqtt user", "", 20);
WiFiManagerParameter cfg_mqtt_password("password", "mqtt password", "", 20);
WiFiManagerParameter cfg_mqtt_prefix("prefix", "mqtt prefix", "", 40);

// led stuff
CRGB* leds;

Ticker LEDcontroll;

#define LIGHT_OFF 0
#define LIGHT_ON 1
#define EFFECT_SOLID 1
#define EFFECT_RAINBOW 2
#define EFFECT_COLORLOOP 3
#define EFFECT_GRADIENT 4

struct light {
  unsigned char state = LIGHT_OFF;
  unsigned char effect = EFFECT_SOLID;
  unsigned char brightness = 255;
  unsigned char red = 255;
  unsigned char green = 255;
  unsigned char blue = 255;
  unsigned char red2 = 255;
  unsigned char green2 = 255;
  unsigned char blue2 = 255;
} light;

char topic_buffer[60];




//
void setup() {
  Serial.begin(115200);

  EEPROM.begin(sizeof(MQTT_LED_config));
  bool hasValidConfig = loadMQTTConfig();
  bool isDoubleReset = drd.detectDoubleReset();

  if (isDoubleReset || !hasValidConfig) {
    drd.stop();
    if(isDoubleReset)
      Serial.println("Double Reset Detected");
    if(!hasValidConfig)
      Serial.println("invalid config");
    Serial.println("Entering config mode");
    startConfig();
  }


  // init leds
  Serial.print("Reserve space for LEDs ");
  Serial.println(MQTT_LED_config.num_leds);
  leds = (CRGB*)malloc(sizeof(CRGB) * MQTT_LED_config.num_leds);
  yield();
  Serial.println("Create LEDs");
  LEDS.addLeds<WS2811, DATA_PIN, GRB>(leds, MQTT_LED_config.num_leds);
  yield();
  FastLED.setMaxPowerInVoltsAndMilliamps(5, 800);

  client.setCallback(mqttCallback);

  LEDcontroll.attach(0.05, LEDupdate);

  delay(5000);
  drd.stop();
  Serial.println("Setup complete");
}

void loop() {

  if (!client.connected()) {
    mqttReconnect();
  }
  client.loop();

  yield();
}

static uint8_t hue = 0;

void LEDupdate(void) {
   switch (light.state ? light.effect : LIGHT_OFF)
  {
    case LIGHT_OFF:
      fill_solid(leds, MQTT_LED_config.num_leds, CRGB::Black);
      break;
    case EFFECT_SOLID:
      fill_solid(leds, MQTT_LED_config.num_leds, CRGB(light.red, light.green, light.blue));
      break;
    case EFFECT_RAINBOW:
      // First slide the led in one direction
      fill_rainbow(leds, MQTT_LED_config.num_leds, hue++ * 5, 255 / MQTT_LED_config.num_leds);
      break;
    case EFFECT_COLORLOOP:
      fill_solid(leds, MQTT_LED_config.num_leds, CHSV(hue++ * 5, 255, 255));
      break;
    case EFFECT_GRADIENT:
//      fill_gradient(leds, 0, CRGB(light.red, light.green, light.blue), MQTT_LED_config.num_leds, CRGB(light.red2, light.green2, light.blue2), SHORTEST_HUES);
      break;
    default:
      Serial.println("non valid effect");
  }
  FastLED.setBrightness(light.brightness);
  FastLED.show();
}


void mqttCallback(char* topic, byte* payload, unsigned int length) {
  unsigned char prefix_lenght = strlen(MQTT_LED_config.prefix);
  char message_buffer[12];  // used for numeric to int conversion
  int i;
  Serial.println(topic);
  if (strcmp (topic + prefix_lenght, "/light/switch") == 0) {
    if (length == 3 and memcmp(payload, "OFF", 3) == 0) {
      light.state = LIGHT_OFF;
      client.publish(buildTopic("/light/status"), "OFF");
    }
    else if (length == 2 and memcmp(payload, "ON", 2) == 0) {
      light.state = LIGHT_ON;
      client.publish(buildTopic("/light/status"), "ON");
    } else {
      Serial.print("received invalid command to light/switch ");
      Serial.println(light.state);
    }
  }
  else if (strcmp (topic + prefix_lenght, "/brightness/set") == 0) {
    Serial.println("brightness");
    memcpy(message_buffer, payload, _min(3, length));
    message_buffer[_min(length, 11)] = '\0';
    light.brightness = (unsigned char)atoi(message_buffer);
    sprintf(message_buffer, "%d", light.brightness);
    client.publish(buildTopic("/brightness/status"), message_buffer); 
  }

  else if (strcmp (topic + prefix_lenght, "/rgb/set") == 0) {
    Serial.println("rgb");
    // store old color for multi color effects
    light.red2 = light.red;
    light.green2 = light.green;
    light.blue2 = light.blue;
    memcpy(message_buffer, payload, _min(sizeof(message_buffer) - 1, length));
    message_buffer[_min(length, 11)] = '\0';
    light.red = atoi(message_buffer);
    i = strcspn(message_buffer, ",") + 1;
    light.green = atoi(message_buffer + i);
    i = strcspn(&message_buffer[i], ",") + i + 1;
    light.blue = atoi(message_buffer + i);
    sprintf(message_buffer, "%d,%d,%d", light.red, light.green, light.blue);
    client.publish(buildTopic("/rgb/status"), message_buffer); 
  }

  else if (strcmp (topic + prefix_lenght, "/effect/set") == 0) {
    Serial.println("effect");
    if (length == 5 and memcmp(payload, "solid", 5) == 0) {
      light.effect = EFFECT_SOLID;
      client.publish(buildTopic("/effect/status"), "solid");
    }
    else if (length == 7 and memcmp(payload, "rainbow", 7) == 0) {
      light.effect = EFFECT_RAINBOW;
      client.publish(buildTopic("/effect/status"), "rainbow");
    }
    else if (length == 9 and memcmp(payload, "colorloop", 7) == 0) {
      light.effect = EFFECT_COLORLOOP;
      client.publish(buildTopic("/effect/status"), "colorloop");
    }
    else if (length == 9 and memcmp(payload, "gradient", 7) == 0) {
      light.effect = EFFECT_GRADIENT;
      client.publish(buildTopic("/effect/status"), "gradient");
    } else {
      Serial.print("received invalid command to light/effect ");
      Serial.println(light.state);
    }
  }
  
  else if (strcmp (topic + prefix_lenght, "/num_leds/set") == 0) {
    Serial.println("num_leds");
    memcpy(message_buffer, payload, _min(3, length));
    message_buffer[_min(length, 11)] = '\0';
    MQTT_LED_config.num_leds = (unsigned short)atoi(message_buffer);
    saveMQTTConfig();
  }

}

void mqttReconnect() {
  unsigned int attempt = 0;
  // Loop until we're reconnected
  while (!client.connected()) {
    if (attempt++ > 0) {
      Serial.println("try again in 5 seconds");
      delay(5000);
    }


    Serial.println("Connect");

    WiFi.mode(WIFI_STA);

    if (WiFi.status() != WL_CONNECTED) {
      ETS_UART_INTR_DISABLE();
      wifi_station_disconnect();
      ETS_UART_INTR_ENABLE();
      delay(1000);

      if (WiFi.begin() != WL_CONNECTED) {
        Serial.println("WiFi connection failed");
        continue;  // try again
      }
    }

    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());

    Serial.print("Attempting MQTT connection to ");
    Serial.print(MQTT_LED_config.broker);
    client.setServer(MQTT_LED_config.broker, 1883);
    // Create a random client ID
    String clientId = "ESP8266Client-";
    clientId += String(random(0xffff), HEX);
    // Attempt to connect
    if (!client.connect(clientId.c_str(), MQTT_LED_config.user, MQTT_LED_config.password, buildTopic("/light/status"),0 ,false, "OFF")) {
      Serial.print("failed, rc=");
      Serial.println(client.state());

      continue;
    }
    Serial.println("connected");
    // Once connected, publish an announcement...
    client.publish(buildTopic("/light/status"), "OFF");
    client.publish(buildTopic("/rgb/status"), "255,255,255");
    // ... and resubscribe
    char topic[sizeof(MQTT_LED_config.prefix) + 2];
    sprintf(topic, "%s/#", MQTT_LED_config.prefix);

    client.subscribe(topic);
    Serial.print("Subscribed to ");
    Serial.println(topic);
  }
}

char* buildTopic(const char* suffix) {
  strcpy(topic_buffer, MQTT_LED_config.prefix);
  strcat(topic_buffer, suffix);
  return topic_buffer;
}

bool loadMQTTConfig(void) {
  Serial.println("load config");
  for (unsigned int t = 0; t < sizeof(MQTT_LED_config); t++) {
    *((char*)&MQTT_LED_config + t) = EEPROM.read(t);
  }
  if(MQTT_LED_config.num_leds > 1000) {  // prevent too large numbers
    Serial.println("Setting leds to 1000");
    MQTT_LED_config.num_leds = 1000;
  }
  if (MQTT_LED_config.config_version == CONFIG_VERSION)
    return true;
  Serial.println("invalid configuration resetting it to 0");
  Serial.println(MQTT_LED_config.config_version);
  Serial.println(MQTT_LED_config.broker);

  for (unsigned int t = 0; t < sizeof(MQTT_LED_config); t++)
    *((char*)&MQTT_LED_config + t) = '0';
  return false;
}

void extractWiFiManagerConfig(void) {
  Serial.println("extract config");
  strcpy(MQTT_LED_config.broker, cfg_mqtt_broker.getValue());
  strcpy(MQTT_LED_config.port, cfg_mqtt_port.getValue());
  strcpy(MQTT_LED_config.user, cfg_mqtt_user.getValue());
  strcpy(MQTT_LED_config.password, cfg_mqtt_password.getValue());
  strcpy(MQTT_LED_config.prefix, cfg_mqtt_prefix.getValue());
  saveMQTTConfig();
}

void saveMQTTConfig(void) {
  Serial.println("save config");
  MQTT_LED_config.config_version = CONFIG_VERSION;
  for (unsigned int t = 0; t < sizeof(MQTT_LED_config); t++)
    EEPROM.write(t, *((char*)&MQTT_LED_config + t));
  EEPROM.commit();
}

void startConfig(void) {
  drd.stop();
  // add custom parameter
  wifiManager.addParameter(&cfg_mqtt_broker);
  wifiManager.addParameter(&cfg_mqtt_port);
  wifiManager.addParameter(&cfg_mqtt_user);
  wifiManager.addParameter(&cfg_mqtt_password);
  wifiManager.addParameter(&cfg_mqtt_prefix);

  wifiManager.setSaveConfigCallback(extractWiFiManagerConfig);
  strncpy(MQTT_LED_config.broker, cfg_mqtt_broker.getValue(), sizeof(MQTT_LED_config.broker));
  strncpy(MQTT_LED_config.port, cfg_mqtt_port.getValue(), sizeof(MQTT_LED_config.port));
  strncpy(MQTT_LED_config.user, cfg_mqtt_user.getValue(), sizeof(MQTT_LED_config.user));
  strncpy(MQTT_LED_config.password, cfg_mqtt_password.getValue(), sizeof(MQTT_LED_config.password));
  strncpy(MQTT_LED_config.prefix, cfg_mqtt_prefix.getValue(), sizeof(MQTT_LED_config.prefix));
  WiFi.disconnect();
  WiFi.mode(WIFI_AP_STA);
  wifiManager.startConfigPortal("ondemandap", NULL);

}

