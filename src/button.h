#ifndef __BUTTON_H__
#define __BUTTON_H__

#include <OneButton.h>
#include "network.h"
#include "gpio16.h"

void onWakeUp()
{
  Serial.println("onWakeUp");
}

void goToSleep(u32_t time)
{
  Serial.println("goToSleep");
  wifi_set_opmode(NULL_MODE);
  wifi_fpm_set_sleep_type(LIGHT_SLEEP_T);
  wifi_fpm_open();
  // gpio_pin_wakeup_enable(16, GPIO_PIN_INTR_LOLEVEL);
  wifi_fpm_set_wakeup_cb(onWakeUp);
  wifi_fpm_do_sleep(time);
}

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
  goToSleep(10e6);
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