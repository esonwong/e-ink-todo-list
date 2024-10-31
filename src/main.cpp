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
#include "UpdateFiles.h"
#include "UpdateFirmware.h"
#include <ESP8266mDNS.h>

void setup()
{

  Serial.begin(115200);
  delay(3000);
  // Serial.setDebugOutput(true);
  Serial.println();
  Serial.println("Steup Start");
  delay(500);

#ifdef GIT_VERSION
  Serial.print("Version: ");
  Serial.println(GIT_VERSION);
#endif

#ifdef DEBUG

  Serial.print("Sketch MD5: ");
  Serial.println(ESP.getSketchMD5());

  if (!LittleFS.begin())
  {
    Serial.println("An Error has occurred while mounting LittleFS");
    return;
  }

  DeviceID = String(ESP.getChipId());

  // Print chip information
  Serial.print("Device ID: ");
  Serial.println(DeviceID);
  Serial.println();

  Serial.println("List files");
  Dir dir = LittleFS.openDir("/");
  while (dir.next())
  {
    Serial.print(dir.fileName());
    Serial.print("   - ");
    File f = dir.openFile("r");
    Serial.println(f.size());
  }
  Serial.println("End list files \n");

  delay(3000);

#ifdef DEBUG_PERSISTENT_STORAGE
  savePersistentValue("text", "Hello World");
  savePersistentValue("number", 123);
  savePersistentValue("float", 123.456);
  savePersistentValue("bool", true);
  savePersistentValue("bool", false);
#endif

#endif // DEBUG

  Serial.print("API URL: ");
  Serial.println(setting.apiUrl);

  delay(500);
  showLaunchScreen();
  delay(3000);

  initStore();
  initButton();

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
    // showTextOnScreenCenter("Network Connected");
  }

#ifdef ENABLE_PCB_TEST
  String testInfo = "";
  testInfo += "Test Mode\n";
  testInfo += "Version: " + String(GIT_VERSION) + "\n";
  testInfo += "Device ID: " + DeviceID + "\n";
  testInfo += "Sketch MD5: " + String(ESP.getSketchMD5()) + "\n";
  testInfo += "Free Heap: " + String(ESP.getFreeHeap()) + "\n";
  testInfo += "Flash Chip ID: " + String(ESP.getFlashChipId()) + "\n";

  // network info
  testInfo += "WiFi SSID: " + String(WIFI_SSID) + "\n";
  testInfo += "WiFi ip: " + WiFi.localIP().toString() + "\n";
  testInfo += "WiFi mac: " + WiFi.macAddress() + "\n";

  // time
  time_t now = time(nullptr);
  struct tm *timeinfo = localtime(&now);
  char timeStr[20];
  strftime(timeStr, 20, "%Y-%m-%d %H:%M:%S", timeinfo);

  // persistent value
  savePersistentValue("PCB_TEST", "PCB test persistent value is OK!");
  testInfo += "Persistent Value: " + String(getPersistentValue("PCB_TEST", "PCB test persistent value is not OK!")) + "\n";

  showTextOnScreenCenter(testInfo);

  delay(5 * 60 * 1000);
#endif // ENABLE_PCB_TEST
}

void loop()
{
  buttonLoop();
  wifiManager.process();

  if (wifiManager.getConfigPortalActive())
  {
    return;
  }

  // Network reconnecting
  if (WiFi.status() != WL_CONNECTED)
  {
    if (initWifiWithManager())
    {
      setClock();
    }
    else
    {
      return;
    }
  }

  // every 60 seconds
  time_t now = time(nullptr);
  if (now - runningValue.lastCheck > 60 && !updating)
  {

#ifdef ENABLE_OTA
    updateFiles();
    updateFireWare();
#endif

    updating = true;
    downloadAndDrawTodo();
    updating = false;
  }
}