#ifndef __BUTTON_H__
#define __BUTTON_H__

#include <OneButton.h>
#include "network.h"
#include "gpio16.h"

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
  wifiManager.startConfigPortal(AP_SSID.c_str(), AP_PASSWORD.c_str());
}

OneButton initButton()
{

  Serial.println("Initializing button...");
  OneButton button;

  Serial.print("Button pin: ");
  Serial.println(BUTTON_PIN);

  if (BUTTON_PIN == 16)
  {
    Serial.println("Button pin is 16, configuring gpio16_output_conf...");
    gpio16_output_conf();
    gpio16_output_set(1);
    button = OneButton(BUTTON_PIN);
  }
  else
  {
    button = OneButton(BUTTON_PIN);
  }

  button.setPressMs(10000);

  button.attachClick(buttonClick);
  button.attachDoubleClick(buttonDoubleClick);
  button.attachLongPressStart(buttonLongPress);

  return button;
}

#endif