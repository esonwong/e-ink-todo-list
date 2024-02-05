#ifndef _NETWORK_H_
#define _NETWORK_H_

#include <WiFiManager.h>
#include "config.h"

void configModeCallback(WiFiManager *myWiFiManager)
{

  Serial.println("Entered config mode");
  Serial.println(WiFi.softAPIP());
  Serial.println(myWiFiManager->getConfigPortalSSID());
}

void initWifiWithManager()
{
  WiFiManager wifiManager;

  Serial.println("Connecting to WiFi...");
  Serial.println(WiFi.waitForConnectResult());
  wifiManager.setConnectRetries(5);
  wifiManager.setConfigPortalTimeout(600);
  wifiManager.setAPCallback(configModeCallback);
  wifiManager.autoConnect(AP_SSID.c_str(), AP_PASSWORD.c_str());
  Serial.println("Connected to WiFi");
  Serial.println(WiFi.waitForConnectResult());
}

#endif