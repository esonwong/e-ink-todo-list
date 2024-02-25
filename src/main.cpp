#include <Arduino.h>
#include <GxEPD2_BW.h>
#include <U8g2_for_Adafruit_GFX.h>
#include "config.h"
#include "network.h"
#include "led.h"
#include "display.h"
#include "clock.h"

void setup()
{

  Serial.begin(115200);
  initDisplay();
  display.fillScreen(GxEPD_WHITE);
  display.setCursor(0, 0);
  displayLog("E Ink Todo List Starting...");
  initWifiWithManager();
  setClock();
}

void loop()
{
  Serial.println("loop!");
  displayLog("loop!");
  delay(1000);
}