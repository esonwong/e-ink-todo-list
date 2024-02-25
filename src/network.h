#ifndef _NETWORK_H_
#define _NETWORK_H_

#include <WiFiManager.h>
#include "config.h"
#include "display.h"

WiFiManager wifiManager;

WiFiManagerParameter apiToken("apiToken", "API Token", "", 40);
WiFiManagerParameter apiHost("apiHost", "API Host", "", 40);

void configModeCallback(WiFiManager *myWiFiManager)
{

  Serial.println("Entered config mode");
  Serial.println(WiFi.softAPIP());
  Serial.println(myWiFiManager->getConfigPortalSSID());
  displayLogln("Entered config mode");
  displayLogln("Please connect to the AP: " + String(myWiFiManager->getConfigPortalSSID()));
}

void saveConfigCallback()
{
  Serial.println("Should save config");
  Serial.println(wifiManager.getWiFiSSID());
  Serial.println(wifiManager.getWiFiPass());
  displayLogln("Should save config");
  wifiManager.reboot();
}

void initWifiWithManager()
{

  Serial.println("Connecting to WiFi...");
  displayLogln("Connecting to WiFi...");
  Serial.println(WiFi.waitForConnectResult());

  wifiManager.addParameter(&apiToken);
  wifiManager.addParameter(&apiHost);

  wifiManager.setConfigPortalTimeout(600);
  wifiManager.setAPCallback(configModeCallback);
  wifiManager.setConnectTimeout(30);
  wifiManager.setSaveConfigCallback(saveConfigCallback);
  wifiManager.setConfigPortalTimeoutCallback([]()
                                             { displayLogln("Config portal closed"); });
  wifiManager.setSaveParamsCallback([]()
                                    { displayLogln("Parameters saved"); });

  wifiManager.setConnectRetries(3);
  wifiManager.setTitle("E-ink Todo List");
  wifiManager.autoConnect(AP_SSID.c_str(), AP_PASSWORD.c_str());

  Serial.println("Connected to WiFi");
  Serial.println(WiFi.waitForConnectResult());
  displayLogln("Connected to WiFi: " + WiFi.SSID());
  displayLogln("IP: " + WiFi.localIP().toString());
}

#endif