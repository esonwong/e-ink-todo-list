#pragma once

#include <WiFiManager.h>
#include "config.h"
#include "display.h"
#include "store.h"
#include "clock.h"
#include "UpdateFiles.h"
#include "UpdateFirmware.h"

WiFiManager wifiManager;
bool isTringToConnectWiFi = false;

WiFiManagerParameter apiKey("apiKey", "API Key", "", 41);
WiFiManagerParameter apiUrl("apiUrl", "API URL", "", 200);

void configModeCallback(WiFiManager *myWiFiManager)
{
  Serial.println("[Network] Config mode callback");
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
  isTringToConnectWiFi = true;
  Serial.println("[Network] preSaveConfigCallback");

  String displayText = "Trying to connect to a new WiFi";
  Serial.println("[Network] Trying to connect to a new WiFi");
  showTextOnScreenCenter(displayText, &Font20, DISPLAY_COLOR_BLACK);
}

void onWifiConnected()
{
  Serial.println("[Network] onWifiConnected");
  isTringToConnectWiFi = false;
  String displayText = "Connected to WiFi";
  showTextOnScreenCenter(displayText, &Font20, DISPLAY_COLOR_BLACK);
  setClock();
}

void onWifiConnectFailed()
{
  Serial.println("[Network] onWifiConnectFailed");
  isTringToConnectWiFi = false;
  String displayText = "WiFi connect failed";
  showTextOnScreenCenter(displayText, &Font20, DISPLAY_COLOR_BLACK);

  if (wifiManager.getConfigPortalActive())
  {
    wifiManager.stopConfigPortal();
    delay(3000);
  }
  wifiManager.startConfigPortal(AP_SSID.c_str(), AP_PASSWORD.c_str());
}

// This gets called when custom parameters have been set AND a connection has been established.
void saveConfigCallback()
{
  Serial.println("[Network] saveConfigCallback");
  onWifiConnected();
}

void setSaveParamsCallback()
{
  Serial.println("[Network] setSaveParamsCallback");
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
  display.display([](BaseDisplayDriver &displayDriver)
                  { uint16_t y = displayDriver.drawString(displayDriver.width / 2, displayDriver.height / 5, "Config portal timeout!", &Font24, DISPLAY_COLOR_BLACK, TEXT_ALIGN_CENTER); 
                    displayDriver.drawString(displayDriver.width / 2, y, "Press the button to restart", &Font20, DISPLAY_COLOR_BLACK, TEXT_ALIGN_CENTER); });
}

void initWifiWithManager()
{

  Serial.println("[Network] initWifiWithManager");
  wifiManager.setHostname("e-ink-todo-list-" + String(ESP.getChipId(), HEX));

  Serial.println("API Key: " + String(setting.apiKey));
  Serial.println("API Key Length: " + String(strlen(setting.apiKey)));
  Serial.println("API URL: " + String(setting.apiUrl));
  Serial.println("API URL Length: " + String(strlen(setting.apiUrl)));

  apiKey.setValue(setting.apiKey, 41);
  apiUrl.setValue(setting.apiUrl, 200);

  wifiManager.addParameter(&apiKey);
  wifiManager.addParameter(&apiUrl);

  wifiManager.setConfigPortalBlocking(false);
  wifiManager.setConfigPortalTimeout(600);
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

  if (wifiManager.getWiFiIsSaved())
  {
    Serial.println("[Network] WiFi is saved");
    String displayText = "Trying to connect to WiFi";
    showTextOnScreenCenter(displayText, &Font20, DISPLAY_COLOR_BLACK);
    if (wifiManager.autoConnect(AP_SSID.c_str(), AP_PASSWORD.c_str()))
    {
      Serial.println("[Network] WiFi connected");
      onWifiConnected();
    }
  }
  else
  {
    Serial.println("[Network] WiFi not connected");
    String displayText = "WiFi not connected";
    showTextOnScreenCenter(displayText, &Font20, DISPLAY_COLOR_BLACK);
    if (wifiManager.getConfigPortalActive())
    {
      wifiManager.stopConfigPortal();
      delay(3000);
    }
    wifiManager.startConfigPortal(AP_SSID.c_str(), AP_PASSWORD.c_str());
  }

  Serial.println("[Network] initWifiWithManager done");
}

void wifiLoop()
{
  wifiManager.process();

  if (wifiManager.getConfigPortalActive() && isTringToConnectWiFi)
  {
    onWifiConnectFailed();
    return;
  }
}
