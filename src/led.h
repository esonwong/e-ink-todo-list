#ifndef LED_H
#define LED_H

#include <Arduino.h>
#include "config.h"

unsigned long lastBlinkTime = 0;

void initLED()
{
  pinMode(LED_PIN, OUTPUT);
}

void turnOnLed()
{
  digitalWrite(LED_PIN, HIGH);
}

void turnOffLed()
{
  digitalWrite(LED_PIN, LOW);
}

void blinkLed(int blinkInterval)
{
  unsigned long currentTime = millis();

  if (currentTime - lastBlinkTime >= blinkInterval)
  {
    digitalWrite(LED_PIN, !digitalRead(LED_PIN)); // 切换 LED 状态
    lastBlinkTime = currentTime;
  }
}

#endif