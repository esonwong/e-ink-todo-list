#ifndef _NETWORK_H_
#define _NETWORK_H_

#include <WiFiManager.h>
#include "config.h"
#include "display.h"
#include "store.h"
#include "clock.h"

WiFiManager wifiManager;

WiFiManagerParameter apiKey("apiKey", "API Key", "", 41);
WiFiManagerParameter apiUrl("apiUrl", "API URL", "", 200);

void configModeCallback(WiFiManager *myWiFiManager)
{

  strcpy(runningValue.todoLastModified, "");
  saveRunningValue(runningValue);

  initDisplay();

  displayLogln("Entered config mode");
  displayLogln("Please connect to the AP: " + String(myWiFiManager->getConfigPortalSSID()));
  displayLogln("Password: " + AP_PASSWORD);
  displayLogln("Config Web Server: http://" + WiFi.softAPIP().toString());

#ifdef E_INK_750
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

void savePreConfigCallback()
{
  Serial.println("Should save config");
  // Serial.print("WiFi SSID:");
  // Serial.println(wifiManager.getWiFiSSID());
  // Serial.print("WiFi Password:");
  // Serial.println(wifiManager.getWiFiPass());
}

void saveConfigCallback()
{
  Serial.println("Should save network config");
  Serial.print("WiFi SSID:");
  Serial.println(wifiManager.getWiFiSSID());
  Serial.print("WiFi Password:");
  Serial.println(wifiManager.getWiFiPass());

  setClock();
}

void setSaveParamsCallback()
{
  Serial.println("Should save params");
  Serial.print("API Key:");
  Serial.println(apiKey.getValue());
  Serial.print("API URL:");
  Serial.println(apiUrl.getValue());

  strcpy(setting.apiKey, apiKey.getValue());
  strcpy(setting.apiUrl, apiUrl.getValue());

  saveSetting(setting);
}

bool initWifiWithManager()
{

  Serial.println("Connecting to WiFi...");
  displayLogln("Connecting to WiFi...");
  Serial.println(WiFi.waitForConnectResult());

  Serial.println("API Key: " + String(setting.apiKey));
  Serial.println("API Key Length: " + String(strlen(setting.apiKey)));
  Serial.println("API URL: " + String(setting.apiUrl));
  Serial.println("API URL Length: " + String(strlen(setting.apiUrl)));

  apiKey.setValue(setting.apiKey, 41);
  apiUrl.setValue(setting.apiUrl, 200);

  wifiManager.addParameter(&apiKey);
  wifiManager.addParameter(&apiUrl);

  wifiManager.setConfigPortalTimeout(600);
  wifiManager.setConfigPortalBlocking(false);
  wifiManager.setConnectTimeout(30);
  wifiManager.setConnectRetries(5);

  wifiManager.setAPCallback(configModeCallback);
  wifiManager.setPreSaveConfigCallback(savePreConfigCallback);
  wifiManager.setSaveConfigCallback(saveConfigCallback);
  wifiManager.setSaveParamsCallback(setSaveParamsCallback);

  wifiManager.setTitle("E-ink Todo List");

  if (wifiManager.autoConnect(AP_SSID.c_str(), AP_PASSWORD.c_str()))
  {

    Serial.println("Connected to WiFi");
    Serial.println(WiFi.waitForConnectResult());
    displayLogln("Connected to WiFi: " + WiFi.SSID());
    displayLogln("IP: " + WiFi.localIP().toString());
    return true;
  }
  return false;
}

#endif