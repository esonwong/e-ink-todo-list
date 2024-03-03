#ifndef _NETWORK_H_
#define _NETWORK_H_

#include <WiFiManager.h>
#include "config.h"
#include "display.h"
#include "store.h"

WiFiManager wifiManager;

WiFiManagerParameter apiKey("apiKey", "API Key", "", 41);
WiFiManagerParameter apiUrl("apiUrl", "API URL", "https://www.einktodo.com/api/display", 200);

void configModeCallback(WiFiManager *myWiFiManager)
{

  displayLogln("Entered config mode");
  displayLogln("Please connect to the AP: " + String(myWiFiManager->getConfigPortalSSID()));
  displayLogln("Password: " + AP_PASSWORD);
  displayLogln("Config Web Server: http://" + WiFi.softAPIP().toString());

#ifdef E_INK_750
  initDisplay();
  do
  {
    display.fillScreen(GxEPD_WHITE);
    display.setTextColor(GxEPD_BLACK);
    display.setCursor(0, 0);
    display.println("Entered config mode");
    display.println("Please connect to the AP: " + String(myWiFiManager->getConfigPortalSSID()));
    display.println("Password: " + AP_PASSWORD);
    display.println("Config Web Server: http://" + WiFi.softAPIP().toString());
  } while (display.nextPage());

#else
  display.display(true);
#endif

  display.powerOff();
}

void saveConfigCallback()
{
  Serial.println("Should save config");
  Serial.print("WiFi SSID:");
  Serial.println(wifiManager.getWiFiSSID());
  Serial.print("WiFi Password:");
  Serial.println(wifiManager.getWiFiPass());
  Serial.print("API Key:");
  Serial.println(apiKey.getValue());
  saveToStore("api key", apiKey.getValue());
  saveToStore("api url", apiUrl.getValue());
  // wifiManager.reboot();
}

void initWifiWithManager()
{

  Serial.println("Connecting to WiFi...");
  displayLogln("Connecting to WiFi...");
  Serial.println(WiFi.waitForConnectResult());

  String apiKeyValue = getFromStore("api key");
  String apiUrlValue = getFromStore("api url");

  if (apiKeyValue.length() > 0)
  {
    apiKey.setValue(apiKeyValue.c_str(), apiKeyValue.length());
  }

  if (apiUrlValue.length() > 0)
  {
    apiUrl.setValue(apiUrlValue.c_str(), apiUrlValue.length());
  }

  wifiManager.addParameter(&apiKey);
  wifiManager.addParameter(&apiUrl);

  wifiManager.setConfigPortalTimeout(600);
  wifiManager.setConnectTimeout(30);
  wifiManager.setConnectRetries(3);

  wifiManager.setAPCallback(configModeCallback);
  wifiManager.setSaveConfigCallback(saveConfigCallback);
  wifiManager.setConfigPortalTimeoutCallback([]()
                                             { displayLogln("Config portal closed"); });
  // wifiManager.setSaveParamsCallback([]()
  // { displayLogln("Parameters saved"); });

  wifiManager.setTitle("E-ink Todo List");
  wifiManager.autoConnect(AP_SSID.c_str(), AP_PASSWORD.c_str());

  Serial.println("Connected to WiFi");
  Serial.println(WiFi.waitForConnectResult());
  displayLogln("Connected to WiFi: " + WiFi.SSID());
  displayLogln("IP: " + WiFi.localIP().toString());
}

#endif