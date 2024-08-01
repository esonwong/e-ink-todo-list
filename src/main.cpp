#include <Arduino.h>
#include <GxEPD2_BW.h>
#include <U8g2_for_Adafruit_GFX.h>
#include "todoList.h"
#include "config.h"
#include "network.h"
#include "led.h"
#include "display.h"
#include "clock.h"
#include "button.h"
#include "store.h"

OneButton button;

void setup()
{

  Serial.begin(115200);
  // Serial.setDebugOutput(true);
  Serial.println();
  Serial.println("Steup Start");
#ifdef GIT_VERSION
  Serial.print("Version: ");
  Serial.println(GIT_VERSION);
#endif
  Serial.print("API URL: ");
  Serial.println(setting.apiUrl);

#ifdef ESP32
  DeviceID = String((uint32_t)ESP.getEfuseMac(), HEX);
#else
  DeviceID = String(ESP.getChipId());
#endif

  // Print chip information
  Serial.print("Device ID: ");
  Serial.println(DeviceID);
  initDisplay();
  showLaunchScreen();

  initStore();

  button = initButton();

#if defined(WIFI_SSID) && defined(WIFI_PASS)
  // For debugging
  Serial.print("WiFi SSID: ");
  Serial.println(WIFI_SSID);
  Serial.print("WiFi Password: ");
  Serial.println(WIFI_PASS);
  wifiManager.preloadWiFi(WIFI_SSID, WIFI_PASS);
#endif

  if (initWifiWithManager())
  {

    setClock();

    delay(1000);

    updating = true;
    downloadAndDrawTodo();
    updating = false;
  }
}

void loop()
{
  button.tick();
  wifiManager.process();

  // 每 1 分钟检查一次是否需要更新 todo
  time_t now = time(nullptr);
  if (now - runningValue.lastCheck > 60 && !updating && WiFi.status() == WL_CONNECTED)
  {
    updating = true;
    downloadAndDrawTodo();
    updating = false;
  }
}