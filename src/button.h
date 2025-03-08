#ifndef __BUTTON_H__
#define __BUTTON_H__

#include <OneButton.h>
#include "network.h"
#include "gpio16.h"
#include "store.h"

OneButton button;
bool longPressActionDone = false; // 添加标志变量来防止长按操作重复执行

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
  if (wifiManager.getConfigPortalActive())
  {
    Serial.println("Close Config Portal");
    wifiManager.stopConfigPortal();
    display.clear();
    return;
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.println("force update todo");
    cleanRunningValue();
    updating = true;
    downloadAndDrawTodo();
    updating = false;
  }
  else
  {
    Serial.println("Start Config Portal");
    wifiManager.autoConnect(AP_SSID.c_str(), AP_PASSWORD.c_str());
  }
}

void buttonDoubleClick()
{
  Serial.println("Button double clicked");
  // TODO: Switch to the next mode
}

void buttonLongPressStart()
{
  Serial.println("Button long press start");
  longPressActionDone = false; // 重置标志变量
}

void buttonLongDuringPress()
{
  Serial.printf("\rButton long pressed for ");
  Serial.print(button.getPressedMs());
  Serial.print(" ms");

  if (button.getPressedMs() > 6000 && !longPressActionDone)
  {
    Serial.println("Button long pressed");
    Serial.println("Erasing configuration、persistent value and resetting WiFi");
    removePersistentValue();
    wifiManager.resetSettings();
    wifiManager.startConfigPortal(AP_SSID.c_str(), AP_PASSWORD.c_str());
    longPressActionDone = true; // 设置标志，防止重复执行

    Serial.println();
  }
}

void buttonLongPressStop()
{
  Serial.println("Button long press stop");
}

void buttonMultiClick()
{
  Serial.print("Button multi clicked: ");
  Serial.println(button.getNumberClicks());

  switch (button.getNumberClicks())
  {
  case 4:
    ESP.restart();
    break;
  case 5:
    removePersistentValue();
    break;
  case 6:
    if (WiFi.status() == WL_CONNECTED)
    {
      Serial.println("Disconnecting WiFi");
      wifiManager.disconnect();
    }
    else
    {
      Serial.println("Connecting WiFi");
      wifiManager.autoConnect(AP_SSID.c_str(), AP_PASSWORD.c_str());
    }
  default:
    break;
  }
}

OneButton initButton()
{

  Serial.println("Initializing button...");

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

  // button.setPressMs(8000);

  button.attachClick(buttonClick);
  button.attachDoubleClick(buttonDoubleClick);
  button.attachMultiClick(buttonMultiClick);
  button.attachLongPressStart(buttonLongPressStart);
  button.attachDuringLongPress(buttonLongDuringPress);
  button.attachLongPressStop(buttonLongPressStop);

  return button;
}

void buttonLoop()
{
  button.tick();
}

#endif