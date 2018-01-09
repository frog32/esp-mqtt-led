#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#define MQTT_SOCKET_TIMEOUT 0
#include <PubSubClient.h>
#define FASTLED_INTERRUPT_RETRY_COUNT 0
#include <FastLED.h>
#include <ArduinoJson.h>

#include <string.h>

#define APPNAME "MQTT LED"
#define VERSION "V0.0.1"
// #define COMPDATE __DATE__ __TIME__
#define COMPDATE __DATE__
#define MODEBUTTON 0


#include <IOTAppStory.h>
IOTAppStory IAS(APPNAME, VERSION, COMPDATE, MODEBUTTON);

// switch this to 5 if using generic board
#define DATA_PIN 1

WiFiClient espClient;
PubSubClient client(espClient);

// led stuff
CRGB* leds;

#define LIGHT_OFF 0
#define LIGHT_ON 1

#define EFFECT_SOLID 0
#define EFFECT_RAINBOW 1
#define EFFECT_COLORLOOP 2
#define EFFECT_GRADIENT 3
#define EFFECT_JUNGLE 4
#define EFFECT_JUNGLE2 5
#define EFFECT_CONFETTI 6
#define EFFECT_LIGHTNING 7
#define EFFECT_TWINKLE 8
#define EFFECT_TWINKLE2 9

const char effectMap[10][10] = {
 "solid",
 "rainbow",
 "colorloop",
 "gradient",
 "jungle",
 "jungle2",
 "confetti",
 "lightning", // todo: implement
 "twinkle",
 "twinkle2",
};

struct light {
  unsigned char state = LIGHT_OFF;
  unsigned char effect = EFFECT_SOLID;
  unsigned short transition = 0;
  unsigned char brightness = 255;
  unsigned char speed = 100;
  unsigned char gain = 100;
  unsigned char red = 255;
  unsigned char green = 255;
  unsigned char blue = 255;
  unsigned char red2 = 255;
  unsigned char green2 = 255;
  unsigned char blue2 = 255;
} light;

char topic_buffer[60];


struct Config {
  char* broker = "1.1.1.1";
  char* port = "1883";
  char* user = "user";
  char* password = "password";
//  char root[3000] = "";
  char* prefix = "test";
  char* numLedsRaw = "16";
  unsigned short numLeds;
  char* colorOrder = "RGB";
  char* powerRaw = "1500";
  unsigned short power;
} Config;

unsigned long lastReconnectAttempt = 0;
unsigned long lastLedUpdate = 0;


//
void setup() {
  ESP.wdtDisable();
  ESP.wdtEnable(5500);

  IAS.serialdebug(true);
  strcpy(IAS.config.IOTappStory1, "static.frog32.ch");
  strcpy(IAS.config.IOTappStoryPHP1, "/ota/mqtt_led.bin");
  strcpy(IAS.config.IOTappStory2, "static.frog32.ch");
  strcpy(IAS.config.IOTappStoryPHP2, "/ota/mqtt_led.bin");

  IAS.addField(Config.broker, "mqtt_broker", "MQTT Broker IP", 39);
  IAS.addField(Config.port, "mqtt_port", "Port", 5);
//  IAS.addField(Config.root, "TLS", "mqtt_root", 2999);
  IAS.addField(Config.user, "mqtt_user", "User", 19);
  IAS.addField(Config.password, "mqtt_password", "Password", 19);
  IAS.addField(Config.prefix, "mqtt_prefix", "Prefix", 39);
  IAS.addField(Config.numLedsRaw, "num_leds", "Num LEDs", 3);
  IAS.addField(Config.colorOrder, "color_order", "Color Order", 3);
  IAS.addField(Config.powerRaw, "power", "LED Power (mW)", 5);

  IAS.begin(true, false);  // todo: check if erase all is needed once released

  // init leds
  Config.numLeds = atoi(Config.numLedsRaw);
  Serial.print("Reserve space for LEDs ");
  Serial.println(Config.numLeds);
  leds = (CRGB*)malloc(sizeof(CRGB) * Config.numLeds);
  yield();
  Serial.println("Create LEDs");
  if(strcmp(Config.colorOrder, "BRG") == 0) {
    LEDS.addLeds<WS2811, DATA_PIN, BRG>(leds, Config.numLeds);
  } else if(strcmp(Config.colorOrder, "GRB") == 0) {
    LEDS.addLeds<WS2811, DATA_PIN, GRB>(leds, Config.numLeds);
  } else {
    LEDS.addLeds<WS2811, DATA_PIN, RGB>(leds, Config.numLeds);    
  }
  yield();
  Config.power = atoi(Config.powerRaw);
  FastLED.setMaxPowerInMilliWatts(Config.power);

  client.setCallback(mqttCallback);

  Serial.println("Setup complete");
}

void loop() {
  IAS.buttonLoop();
  LEDupdate();
  if (client.connected()) {
      client.loop();
  } else if (millis() > lastReconnectAttempt + 20000 || millis() < lastReconnectAttempt) {
    mqttReconnect();
    lastReconnectAttempt = millis();
  }
  delay(2);
}

static uint8_t hue = 0, loopcounter = 0;

void LEDupdate(void) {
  if (millis() < lastLedUpdate + 20 && millis() > lastLedUpdate) {  // todo: make update interval changable
    return;
  }
  lastLedUpdate = millis();
  int i, j;  // used in some effects
  CRGB color1 = CRGB(light.red, light.green, light.blue);
  CRGB color2 = CRGB(light.red2, light.green2, light.blue2);
  if(light.state == LIGHT_OFF) {
    fadeToBlackBy( leds, Config.numLeds, 20);
  } else {
    switch (light.state ? light.effect : LIGHT_OFF)
    {
      case EFFECT_SOLID:
        fill_solid(leds, Config.numLeds, color1);
        break;
      case EFFECT_RAINBOW:
        fill_rainbow(leds, Config.numLeds, hue, light.gain / 5);
        if(loopcounter >= (255 - light.speed) % 128) {
          hue += light.speed / 128 + 1;
          loopcounter = 0;
        }
        break;
      case EFFECT_COLORLOOP:
        fill_solid(leds, Config.numLeds, CHSV(hue, 255, 255));
        if(loopcounter >= (255 - light.speed) % 128) {
          hue += light.speed / 128 + 1;
          loopcounter = 0;
        }
        break;
      case EFFECT_GRADIENT:
        fill_gradient_RGB(leds, 0, color1, Config.numLeds, color2);
        break;
      case EFFECT_JUNGLE:
      case EFFECT_JUNGLE2:
        fadeToBlackBy( leds, Config.numLeds, light.speed / 10 + 1);
        for ( i = 0; i < light.gain; i = i + 16) {
          if(light.effect == EFFECT_JUNGLE || i % 2)
            leds[beatsin16(light.speed / 10 + i, 0, Config.numLeds - 1)] |= color1;
          else
            leds[beatsin16(light.speed / 10 + i, 0, Config.numLeds - 1)] |= color2;
        }
        break;
      case EFFECT_CONFETTI:
      case EFFECT_TWINKLE:
      case EFFECT_TWINKLE2:
        fadeToBlackBy( leds, Config.numLeds, light.speed / 10 + 1);
        for(i = 0; i < 3 && random8() < light.gain; i++) {
          if(light.effect == EFFECT_CONFETTI)
            leds[random16(Config.numLeds)] += CRGB(random8(64), random8(64), random8(64));
          else if(light.effect == EFFECT_TWINKLE || random(2))
            leds[random16(Config.numLeds)] += color1;
          else
            leds[random16(Config.numLeds)] += color2;
        }
        
        break;
      default:
        Serial.println("non valid effect");
    }
  }
  FastLED.setBrightness(light.brightness);
  FastLED.show();
  loopcounter++;
}


void mqttCallback(char* topic, byte* payload, unsigned int length) {
  unsigned char prefix_lenght = strlen(Config.prefix);
  char message_buffer[12];  // used for numeric to int conversion
  int i;
  Serial.println(topic);
  if(strcmp (topic + prefix_lenght, "/set") == 0) {
    parseJSONCommand(payload, length);
    sendJSONStatus();
  }
}

void parseJSONCommand(byte* payload, unsigned int length) {
  // todo: check if input is of correct type http://arduinojson.org/api/jsonvariant/is/
  char message_buffer[512];
  memcpy(message_buffer, payload, _min(511, length));
  DynamicJsonBuffer inputJsonBuffer;
  JsonObject& input = inputJsonBuffer.parseObject(message_buffer);
  if(input.containsKey("state") && input["state"].is<char*>()) {
    const char* state = input["state"];
    if(strcmp(state, "ON") == 0) {
      light.state = LIGHT_ON;
    }
    else if(strcmp(state, "OFF") == 0) {
      light.state = LIGHT_OFF;
    }
  }
  if(input.containsKey("brightness") && input["brightness"].is<int>()) {
    light.brightness = input["brightness"];
  }
  if(input.containsKey("transition") && input["transition"].is<int>()) {
    light.transition = input["transition"];
  }
  if(input.containsKey("speed") && input["speed"].is<int>()) {
    light.speed = input["speed"];
  }
  if(input.containsKey("gain") && input["gain"].is<int>()) {
    light.gain = input["gain"];
  }
  if(input.containsKey("color") && input["color"].is<JsonObject>() && input["color"]["r"].is<int>()
      && input["color"]["g"].is<int>() && input["color"]["b"].is<int>()) {
    // store old color for multi color effects
    light.red2 = light.red;  // todo: find a better solution
    light.green2 = light.green;
    light.blue2 = light.blue;
    light.red = input["color"]["r"];
    light.green = input["color"]["g"];
    light.blue = input["color"]["b"];
  }
  if(input.containsKey("effect") && input["effect"].is<char*>()) {
    for(int i = 0; i < sizeof(effectMap); i++) {
      Serial.print("test ");
      Serial.print(effectMap[i]);
      if(strcmp(input["effect"], effectMap[i]) == 0) {
        Serial.print("found");
        light.effect = i;
        break;
      }
    }

  }
}

void sendJSONStatus() {
    // build reply
  char message_buffer[512];
  DynamicJsonBuffer outputJsonBuffer;
  JsonObject& output = outputJsonBuffer.createObject();
  output["state"] = light.state == LIGHT_ON ? "ON": "OFF";
  sprintf(message_buffer, "%d", light.brightness);
  output["brightness"] = light.brightness;
  JsonObject& color = output.createNestedObject("color");
  color["r"] = light.red;
  color["g"] = light.green;
  color["b"] = light.blue;
  output["effect"] = (char*)effectMap[light.effect];
  output.printTo(message_buffer);
  Serial.println(message_buffer);
  client.publish(buildTopic("/status"), message_buffer);

}

bool mqttReconnect() {
  Serial.println("Connect");

//  WiFi.mode(WIFI_STA);
//
//  if (WiFi.status() != WL_CONNECTED) {
//    ETS_UART_INTR_DISABLE();
////      wifi_station_disconnect();
//    ETS_UART_INTR_ENABLE();
//    delay(1000);
//
//    if (WiFi.begin() != WL_CONNECTED) {
//      Serial.println("WiFi connection failed");
//      return(false);  // try again
//    }
//  }

  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  Serial.print("Attempting MQTT connection to ");
  Serial.print(Config.broker);
  client.setServer(Config.broker, 1883);  // todo: use port from config
  // Create a unique client ID
  char clientId[13];
  sprintf(clientId, "MQTT_LED%4X", ESP.getChipId());
  // Attempt to connect
  if (!client.connect(clientId, Config.user, Config.password, buildTopic("/status"),0 ,false, "{\"state\": \"OFF\"}")) {
    Serial.print("failed, rc=");
    Serial.println(client.state());

    return(false);
  }
  Serial.println("connected");
  yield();
  // Once connected, publish an announcement...
  sendJSONStatus();
  // ... and resubscribe
  char topic[sizeof(Config.prefix) + 2];
  sprintf(topic, "%s/#", Config.prefix);
  yield();
  client.subscribe(topic);
  Serial.println("Subscribed to topic");
//  Serial.println(topic);
  return(true);
}

char* buildTopic(const char* suffix) {
  strcpy(topic_buffer, Config.prefix);
  strcat(topic_buffer, suffix);
  return topic_buffer;
}

bool sanitizeConfig(void) {
  if(Config.numLeds > 1000) {  // prevent too large numbers
    Serial.println("Setting leds to 1000");
    Config.numLeds = 1000;
  }
}

