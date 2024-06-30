#include <Arduino.h>
#include "display.h"

void setup()
{

  Serial.begin(115200);
  // Serial.setDebugOutput(true);
  Serial.println();
  Serial.println("Steup Start");

  // Print chip information

  Serial.print("Device ID: ");
  Serial.println(ESP.getChipId());

  // 配置 GPIO 16 用于深度休眠唤醒
  pinMode(16, WAKEUP_PULLUP);
  initDisplay();
  do
  {
    display.fillScreen(GxEPD_WHITE);
    display.setCursor(0, 0);
    display.setTextColor(GxEPD_BLACK);
    display.setTextSize(14);
    display.println("Hello World");
  } while (display.nextPage());

  // 进入深度休眠，设置唤醒时间为 10e6 微秒（10 秒）
  ESP.deepSleep(10e6);
}

void loop()
{
  // 这里的代码不会被执行
  Serial.println("Loop Start");
  initDisplay();
  do
  {
    display.fillScreen(GxEPD_WHITE);
    display.setCursor(0, 0);
    display.setTextColor(GxEPD_BLACK);
    display.setTextSize(14);
    display.println("Wake up!");
  } while (display.nextPage());
}