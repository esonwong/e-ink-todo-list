#ifndef _NETWORK_H_
#define _NETWORK_H_

#include <WiFiManager.h>
#include "config.h"

WiFiManager wifiManager;

WiFiManagerParameter apiToken("apiToken", "API Token", "", 40);
WiFiManagerParameter apiHost("apiHost", "API Host", "", 40);

void configModeCallback(WiFiManager *myWiFiManager)
{

  Serial.println("Entered config mode");
  Serial.println(WiFi.softAPIP());
  Serial.println(myWiFiManager->getConfigPortalSSID());
}

void saveConfigCallback()
{
  Serial.println("Should save config");
  Serial.println(wifiManager.getWiFiSSID());
  Serial.println(wifiManager.getWiFiPass());
  wifiManager.reboot();
}

void initWifiWithManager()
{

  Serial.println("Connecting to WiFi...");
  Serial.println(WiFi.waitForConnectResult());
  wifiManager.setConnectRetries(3);
  wifiManager.setConfigPortalTimeout(600);
  wifiManager.setAPCallback(configModeCallback);
  wifiManager.setConnectTimeout(30);
  wifiManager.setSaveConfigCallback(saveConfigCallback);
  wifiManager.setTitle("E-ink Todo List");
  wifiManager.autoConnect(AP_SSID.c_str(), AP_PASSWORD.c_str());
  Serial.println("Connected to WiFi");
  Serial.println(WiFi.waitForConnectResult());
}

#endif