#include <Arduino.h>
#include <GxEPD2_BW.h>
#include <U8g2_for_Adafruit_GFX.h>
#include "todoList.h"
#include "config.h"
#include "network.h"
#include "led.h"
#include "display.h"
#include "clock.h"
#include "button.h"

OneButton button;

void setup()
{

  Serial.begin(115200);
  // Serial.setDebugOutput(true);
  Serial.println();
  Serial.println("Steup Start");
  Serial.print("Version: ");
  Serial.println(GIT_VERSION);

  // Print chip information
  Serial.print("Device ID: ");
  Serial.println(ESP.getChipId());

  initStore();

  button = initButton();

  if (initWifiWithManager())
  {

    setClock();

    delay(1000);

    updating = true;
    downloadAndDrawTodo();
    updating = false;
  }
}

void loop()
{
  button.tick();
  wifiManager.process();

  // 每 1 分钟检查一次是否需要更新 todo
  time_t now = time(nullptr);
  if (now - runningValue.lastCheck > 60 && !updating && WiFi.status() == WL_CONNECTED)
  {
    updating = true;
    downloadAndDrawTodo();
    updating = false;
  }
}