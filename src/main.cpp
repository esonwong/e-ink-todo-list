#include <WiFiManager.h>
#include "display.h"
#include <Arduino.h>

void setup()
{

    Serial.begin(115200);
    delay(3000);
    // Serial.setDebugOutput(true);
    Serial.println();
    Serial.println("Steup Start!!!!!");
    delay(2000);
    initDisplay();
    display.testDisplay();
}

void loop()
{
    Serial.println("Loop Start!!!!!");
    delay(1000);
    showLaunchScreen();
    delay(1000);

    Serial.println("Loop End!!!!!");
    delay(1000);
}