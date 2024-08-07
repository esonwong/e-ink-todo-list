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
#include "UpdateFS.h"

OneButton button;

void setup()
{

  Serial.begin(115200);
  // Serial.setDebugOutput(true);
  Serial.println();
  Serial.println("Steup Start");

#ifdef DEBUG
  delay(3000);
  if (!LittleFS.begin())
  {
    Serial.println("An Error has occurred while mounting LittleFS");
    return;
  }

  Serial.println("List files");
  Dir dir = LittleFS.openDir("/");
  while (dir.next())
  {
    Serial.print(dir.fileName());
    Serial.print(" - ");
    File f = dir.openFile("r");
    Serial.println(f.size());
  }
#endif

#ifdef GIT_VERSION
  Serial.print("Version: ");
  Serial.println(GIT_VERSION);
#endif

  Serial.print("API URL: ");
  Serial.println(setting.apiUrl);

  DeviceID = String(ESP.getChipId());

  // Print chip information
  Serial.print("Device ID: ");
  Serial.println(DeviceID);
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

    updateFS();
    saveSetting();

    updating = true;
    Serial.println("Update todo");
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