#pragma once

#ifdef E_INK_750
#include "display_750_driver.h"
DisplayDriver750 display = DisplayDriver750();
#endif

#ifdef E_INK_290
#include "display_290_driver.h"
DisplayDriver290 display = DisplayDriver290();
#endif

void initDisplay()
{
  Serial.println("Initializing display...");
  display.initialize();
  Serial.println("Display width: " + String(display.width));
  Serial.println("Display height: " + String(display.height));
  Serial.println("Display initialized");
}

void showLaunchScreen()
{
  //   initDisplay();
  //   do
  //   {
  //     display.fillScreen(GxEPD_WHITE);
  //     display.setCursor(0, 0);
  //     int16_t x, y;
  //     uint16_t w1, w2, h1, h2;
  //     const char *name = "E-Ink Todo List";
  //     const char *author = "by @Eson";

  // #ifdef DEBUG
  //     display.setTextSize(1);
  //     display.setCursor(400, 400);
  //     display.println("DEBUG: width=" + String(display.width()) + ", height=" + String(display.height()));

  //     // draw a rectangle
  //     display.drawRect(40, 320, display.width() - 80, display.height() - 80, GxEPD_BLACK);
  // #endif

  //     display.setTextSize(3);
  //     display.getTextBounds(name, 0, 0, &x, &y, &w1, &h1);
  //     display.setCursor((display.width() - w1) / 2, display.height() / 2 - h1);
  //     display.println(name);
  //     display.setTextSize(2);
  //     display.getTextBounds(author, 0, 0, &x, &y, &w2, &h2);
  //     display.setCursor((display.width() - w2) / 2, display.height() / 2 + h1 / 2);
  //     display.println(author);
  // #ifdef GIT_VERSION
  //     uint16_t w3, h3;
  //     display.setTextSize(2);
  //     display.getTextBounds(GIT_VERSION, 0, 0, &x, &y, &w3, &h3);
  //     display.setCursor((display.width() - w3) / 2, display.height() - h3 - 10);
  //     display.println(GIT_VERSION);
  // #endif

  //   } while (display.nextPage());
}

void showTextOnScreenCenter(const char *text, uint8_t textSize = 2)
{
  // initDisplay();
  // display.fillScreen(GxEPD_WHITE);
  // display.setCursor(0, 0);
  // int16_t x, y;
  // uint16_t w, h;
  // do
  // {
  //   display.setTextSize(textSize);
  //   display.getTextBounds(text, 0, 0, &x, &y, &w, &h);
  //   display.setCursor((display.width() - w) / 2, (display.height() - h) / 2);
  //   display.println(text);
  // } while (display.nextPage());
}

void showTextOnScreenCenter(String text, uint8_t textSize = 2)
{
  showTextOnScreenCenter(text.c_str(), textSize);
}
