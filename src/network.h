#pragma once

#include <WiFiManager.h>
#include "config.h"
#include "display.h"
#include "store.h"
#include "clock.h"
#include "UpdateFiles.h"
#include "UpdateFirmware.h"

WiFiManager wifiManager;

WiFiManagerParameter apiKey("apiKey", "API Key", "", 41);
WiFiManagerParameter apiUrl("apiUrl", "API URL", "", 200);

void onConnectNetwork()
{
}

void configModeCallback(WiFiManager *myWiFiManager)
{
  display.display([myWiFiManager](BaseDisplayDriver &displayDriver)
                  { uint16_t y = displayDriver.drawString(displayDriver.width / 2, displayDriver.height / 5, "Entered config mode", &Font24, DISPLAY_COLOR_BLACK, TEXT_ALIGN_CENTER); 
                    displayDriver.drawString(displayDriver.width / 2, y, "Please connect to the AP: " , &Font20, DISPLAY_COLOR_BLACK, TEXT_ALIGN_RIGHT);
                    y= displayDriver.drawString(displayDriver.width / 2, y, myWiFiManager->getConfigPortalSSID(), &Font20, DISPLAY_COLOR_BLACK, TEXT_ALIGN_LEFT);
                    displayDriver.drawString(displayDriver.width / 2, y, "Password: " , &Font20, DISPLAY_COLOR_BLACK, TEXT_ALIGN_RIGHT);
                    y= displayDriver.drawString(displayDriver.width / 2, y, AP_PASSWORD, &Font20, DISPLAY_COLOR_BLACK, TEXT_ALIGN_LEFT); 
                    displayDriver.drawString(displayDriver.width / 2, y, "Config Web Server: ", &Font20, DISPLAY_COLOR_BLACK, TEXT_ALIGN_RIGHT); 
                    y= displayDriver.drawString(displayDriver.width / 2, y, "http://" + WiFi.softAPIP().toString(), &Font20, DISPLAY_COLOR_BLACK, TEXT_ALIGN_LEFT); });
}

void preSaveConfigCallback()
{
  Serial.println("Should save network config");
}

// This gets called when custom parameters have been set AND a connection has been established.
void saveConfigCallback()
{
  Serial.println("Should save network config");
  Serial.print("WiFi SSID:");
  Serial.println(wifiManager.getWiFiSSID());
  Serial.print("WiFi Password:");
  Serial.println(wifiManager.getWiFiPass());

  setClock();

  updateFiles();
  updateFireWare();
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

  cleanRunningValue();
}

void configPortalTimeoutCallback()
{
  // Serial.println("Config portal timeout");

  // Modem sleep
  WiFi.mode(WIFI_OFF);
  temporaryValue.isConfigTimeOut = true;
  display.display([](BaseDisplayDriver &displayDriver)
                  { uint16_t y = displayDriver.drawString(displayDriver.width / 2, displayDriver.height / 5, "Config portal timeout!", &Font24, DISPLAY_COLOR_BLACK, TEXT_ALIGN_CENTER); 
                    displayDriver.drawString(displayDriver.width / 2, y, "Press the button to restart", &Font20, DISPLAY_COLOR_BLACK, TEXT_ALIGN_CENTER); });
}

bool initWifiWithManager()
{

  wifiManager.setHostname("e-ink-todo-list-" + String(ESP.getChipId(), HEX));

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
  wifiManager.setConnectRetries(3);

  wifiManager.setAPCallback(configModeCallback);
  wifiManager.setPreSaveConfigCallback(preSaveConfigCallback);
  wifiManager.setSaveConfigCallback(saveConfigCallback);
  wifiManager.setSaveParamsCallback(setSaveParamsCallback);
  wifiManager.setConfigPortalTimeoutCallback(configPortalTimeoutCallback);

  std::vector<const char *> menu = {"wifi", "info", "restart", "exit"};
  wifiManager.setMenu(menu);

  wifiManager.setTitle("E-ink Todo List");

  // showTextOnScreenCenter("Connecting to WiFi " + String(wifiManager.getWiFiSSID()));

  if (wifiManager.autoConnect(AP_SSID.c_str(), AP_PASSWORD.c_str()))
  {

    Serial.println("Connected to WiFi");
    Serial.println(WiFi.waitForConnectResult());
    return true;
  }
  return false;
}
