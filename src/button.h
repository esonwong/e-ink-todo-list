#ifndef __BUTTON_H__
#define __BUTTON_H__

#include <OneButton.h>
#include "network.h"

OneButton button(BUTTON_PIN);

void buttonClick()
{
  Serial.println("Button clicked");
  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.println("force update todo");
    strcpy(runningValue.todoLastModified, "");
    saveRunningValue(runningValue);
    updating = true;
    downloadAndDrawTodo();
    updating = false;
  }
  else
  {
    Serial.println("WiFi not connected");
  }
}

void buttonDoubleClick()
{
  Serial.println("Button double clicked");
}

void buttonLongPress()
{
  Serial.println("Button long pressed");
  // wifiManager.startConfigPortal(AP_SSID.c_str(), AP_PASSWORD.c_str());
}

void initButton()
{

  Serial.println("Initializing button...");

  button.attachClick(buttonClick);
  button.attachDoubleClick(buttonDoubleClick);
  button.attachLongPressStart(buttonLongPress);
}

#endif