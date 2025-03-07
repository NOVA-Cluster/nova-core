#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <LittleFS.h>
#include "FS.h"

#include "OneButton.h"

#include <DNSServer.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include "ESPAsyncWebServer.h"
#include <ESPUI.h>

#include "configuration.h"
#include "NovaIO.h"
#include "main.h"
#include "Enable.h"
#include "Ambient.h"
#include "LightUtils.h"
#include "output/Star.h"
#include "output/StarSequence.h"
//#include "modes/Buttons.h"
#include "Web.h"
#include "utilities/PreferencesManager.h"
#include "fileSystemHelper.h"
#include "Ambient.h"
#include "Tasks.h"
#include "utilities.h"

#include "Simona.h"

#define FORMAT_LITTLEFS_IF_FAILED true

#define CONFIG_FILE "/config.json"

uint8_t buttons[4] = {BUTTON_RED_IN, BUTTON_GREEN_IN, BUTTON_BLUE_IN, BUTTON_YELLOW_IN};
uint8_t leds[4] = {BUTTON_RED_OUT, BUTTON_GREEN_OUT, BUTTON_BLUE_OUT, BUTTON_YELLOW_OUT};
const char *buttonColors[4] = {"RED", "GREEN", "BLUE", "YELLOW"};
const char *ledColors[4] = {"RED", "GREEN", "BLUE", "YELLOW"};

// PersistenceManager manager("/data.json", 4096);

void TaskLightUtils(void *pvParameters);
void TaskAmbient(void *pvParameters);
void TaskEnable(void *pvParameters);
void TaskMDNS(void *pvParameters);
void TaskModes(void *pvParameters);
//void TaskButtons(void *pvParameters);
void TaskWeb(void *pvParameters);
void TaskStarSequence(void *pvParameters);

DNSServer dnsServer;
AsyncWebServer webServer(80);

// Add WiFi event handler function
void WiFiEvent(WiFiEvent_t event)
{
    Serial.printf("[WiFi-event] event: %d\n", event);

    switch (event)
    {
    case SYSTEM_EVENT_WIFI_READY:
        Serial.println("WiFi interface ready");
        break;
    case SYSTEM_EVENT_SCAN_DONE:
        Serial.println("Completed scan for access points");
        break;
    case SYSTEM_EVENT_STA_START:
        Serial.println("WiFi client started");
        break;
    case SYSTEM_EVENT_AP_START:
        Serial.println("WiFi AP started");
        Serial.print("AP IP address: ");
        Serial.println(WiFi.softAPIP());
        Serial.print("AP MAC address: ");
        Serial.println(WiFi.softAPmacAddress());
        break;
    case SYSTEM_EVENT_AP_STOP:
        Serial.println("WiFi AP stopped");
        break;
    case SYSTEM_EVENT_AP_STACONNECTED:
        Serial.println("Client connected");
        break;
    case SYSTEM_EVENT_AP_STADISCONNECTED:
        Serial.println("Client disconnected");
        break;
    }
}

// Function to initialize LED with PWM
void initLedPWM(uint8_t pin, uint8_t channel) {
    ledcSetup(channel, LEDC_FREQ_HZ, LEDC_RESOLUTION);
    ledcAttachPin(pin, channel);
    ledcWrite(channel, LEDC_FULL_DUTY); // Initialize to full brightness (on state)
  }

void setup()
{
  Serial.begin(921600);
  delay(2000); // Give serial interface time to connect
  Serial.println("");
  Serial.println("NOVA: CORE");
  Serial.print("setup() is running on core ");
  Serial.println(xPortGetCoreID());

  Serial.setDebugOutput(true);

  if (!LittleFS.begin(FORMAT_LITTLEFS_IF_FAILED))
  {
    Serial.println("LITTLEFS Mount Failed");
    return;
  }
  else
  {
    Serial.println("LITTLEFS Mount Success");

    listDir(LittleFS, "/", 0);
  }
  
  Serial.println("Setting up Serial2");
  Serial2.begin(921600, SERIAL_8N1, UART2_RX, UART2_TX);

  Serial.println("Pin Directions");
  pinMode(ENABLE_DEVICE_PIN, INPUT_PULLDOWN);

  pinMode(BUTTON_RED_OUT, OUTPUT);
  pinMode(BUTTON_GREEN_OUT, OUTPUT);
  pinMode(BUTTON_BLUE_OUT, OUTPUT);
  pinMode(BUTTON_YELLOW_OUT, OUTPUT);
  pinMode(BUTTON_WHITE_OUT, OUTPUT);

  initLedPWM(BUTTON_RED_OUT, LEDC_CHANNEL_RED);
  initLedPWM(BUTTON_GREEN_OUT, LEDC_CHANNEL_GREEN);
  initLedPWM(BUTTON_BLUE_OUT, LEDC_CHANNEL_BLUE);
  initLedPWM(BUTTON_YELLOW_OUT, LEDC_CHANNEL_YELLOW);
  initLedPWM(BUTTON_WHITE_OUT, LEDC_CHANNEL_RESET);


  Serial.println("Set clock of I2C interface to 0.4mhz");
  Wire.begin();
  Wire.setClock(400000UL); // 400khz
  // Wire.setClock(600000UL); // 600khz
  // Wire.setClock(800000UL); // 800khz
  // Wire.setClock(1000000UL); // 1mhz

  Serial.println("new NovaIO");
  novaIO = new NovaIO();

  Serial.println("new Enable");
  enable = new Enable();

  Serial.println("new Star");
  star = new Star();

  Serial.println("new Ambient");
  ambient = new Ambient();

  Serial.println("new LightUtils");
  lightUtils = new LightUtils();

  //Serial.println("new Buttons");
  //buttons = new Buttons();

  Serial.println("new Star Sequence");
  starSequence = new StarSequence();

  String apName = "NovaCore_" + getLastFourOfMac();

  // Set WiFi mode to WIFI_AP_STA for simultaneous AP and STA mode
  WiFi.mode(WIFI_AP_STA);

  WiFi.softAP(apName.c_str(), "scubadandy");
  WiFi.setSleep(false); // Disable power saving on the wifi interface.

  WiFi.onEvent(WiFiEvent);

  // Print network information
  Serial.println("Device Information:");
  Serial.print("MAC Address: ");
  Serial.println(WiFi.macAddress());
  Serial.print("AP Name: ");
  Serial.println(apName);
  Serial.print("AP IP Address: ");
  Serial.println(WiFi.softAPIP());
  Serial.print("STA IP Address: ");
  Serial.println(WiFi.localIP());
  Serial.print("Free Heap: ");
  Serial.println(ESP.getFreeHeap());
  Serial.print("CPU Frequency (MHz): ");
  Serial.println(ESP.getCpuFreqMHz());
  Serial.print("SDK Version: ");
  Serial.println(ESP.getSdkVersion());

  dnsServer.start(53, "*", WiFi.softAPIP());
  // webServer.addHandler(new CaptiveRequestHandler()).setFilter(ON_AP_FILTER); // only when requested from AP
  //  more handlers...
  //  webServer.begin();

  Serial.println("Setting up Webserver");
  webSetup();
  Serial.println("Setting up Webserver - Done");

  // Initialize Simona singleton
  Simona::initInstance(buttons, leds, buttonColors, ledColors);

  Serial.println("Create gameTask");
  xTaskCreate(gameTask, "Game Task", 4096, NULL, 1, NULL);
  Serial.println("Create gameTask - Done");

  Serial.println("Create buttonTask");
  xTaskCreate(buttonTask, "Button Task", 4096, NULL, 1, NULL);
  Serial.println("Create buttonTask - Done");


  Serial.println("Create TaskEnable");
  xTaskCreate(&TaskEnable, "TaskEnable", 3 * 1024, NULL, 1, NULL);
  Serial.println("Create TaskEnable - Done");

  Serial.println("Create TaskWeb");
  xTaskCreate(&TaskWeb, "TaskWeb", 10 * 1024, NULL, 5, NULL);
  Serial.println("Create TaskWeb - Done");

  Serial.println("Create TaskModes");
  xTaskCreate(&TaskModes, "TaskModes", 4 * 1024, NULL, 10, NULL);
  Serial.println("Create TaskModes - Done");

  //Serial.println("Create TaskButtons");
  //xTaskCreate(&TaskButtons, "TaskButtons", 4 * 1024, NULL, 5, NULL);
  //Serial.println("Create TaskButtons - Done");

  Serial.println("Create TaskMDNS");
  xTaskCreate(&TaskMDNS, "TaskMDNS", 3 * 1024, NULL, 1, NULL);
  Serial.println("Create TaskMDNS - Done");

  Serial.println("Create TaskAmbient");
  xTaskCreate(&TaskAmbient, "TaskAmbient", 7 * 1024, NULL, 5, NULL);
  Serial.println("Create TaskAmbient - Done");

  Serial.println("Create LightUtils");
  xTaskCreate(&TaskLightUtils, "LightUtils", 3 * 1024, NULL, 5, NULL);
  Serial.println("Create LightUtils - Done");

  Serial.println("Create StarSequence");
  xTaskCreate(&TaskStarSequence, "StarSequence", 3 * 1024, NULL, 5, NULL);
  Serial.println("Create StarSequence - Done");

  PreferencesManager::begin();
  // ...existing code...
  PreferencesManager::end();

  Serial.println("Setup Complete");
}

void loop()
{
  /* Best not to have anything in this loop.
      Everything should be in freeRTOS tasks
  */
  delay(1);
}

// LED control function that other code can call
void setLedBrightness(uint8_t led, bool isOn) {
    uint8_t channel;
    switch(led) {
      case BUTTON_RED_OUT: channel = LEDC_CHANNEL_RED; break;
      case BUTTON_GREEN_OUT: channel = LEDC_CHANNEL_GREEN; break;
      case BUTTON_BLUE_OUT: channel = LEDC_CHANNEL_BLUE; break;
      case BUTTON_YELLOW_OUT: channel = LEDC_CHANNEL_YELLOW; break;
      case BUTTON_WHITE_OUT: channel = LEDC_CHANNEL_RESET; break;
      default: return;
    }
    ledcWrite(channel, isOn ? LEDC_FULL_DUTY : LEDC_DIM_DUTY);
  }