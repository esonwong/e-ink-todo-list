#include <Arduino.h>
#include "config.h"
#include "network.h"
#include "led.h"

void setup()
{
  Serial.begin(115200);
  initLED();
  turnOnLed();
  initWifiWithManager();
  turnOffLed();
}

void loop()
{
  // put your main code here, to run repeatedly:
  Serial.println("loop!");
  delay(1000);
}