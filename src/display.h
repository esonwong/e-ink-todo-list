
#ifndef _DISPLAY_H_
#define _DISPLAY_H_

#include <GxEPD2_3C.h>
// #include <GxEPD2_BW.h>
#include <U8g2_for_Adafruit_GFX.h>

U8G2_FOR_ADAFRUIT_GFX u8g2Fonts;

#ifdef ESP32
#define CS 15
#define DC 0
#define RST 2
#define BUSY 4
#define PAGES 2
#endif

#ifdef ESP8266
#define CS SS // 15
#define DC 4
#define RST 2
#define BUSY 5
#define PAGES 20
#endif

#ifdef GDEW075Z08
GxEPD2_3C<GxEPD2_750c_Z08, GxEPD2_750c_Z08::HEIGHT / PAGES> display(GxEPD2_750c_Z08(CS, DC, RST, BUSY));
#endif

#ifdef E_INK_750
// GxEPD2_BW<GxEPD2_750_T7, GxEPD2_750_T7::HEIGHT> display(GxEPD2_750_T7(CS, DC, RST, BUSY));
GxEPD2_3C<GxEPD2_750c_Z08, GxEPD2_750c_Z08::HEIGHT / PAGES> display(GxEPD2_750c_Z08(CS, DC, RST, BUSY));
#endif

#ifdef E_INK_750_V1
GxEPD2_BW<GxEPD2_750c, GxEPD2_750c::HEIGHT / PAGES> display(GxEPD2_750c(CS, DC, RST, BUSY));
#endif

#ifdef E_INK_290
GxEPD2_BW<GxEPD2_290, GxEPD2_290::HEIGHT> display(GxEPD2_290(CS, DC, RST, BUSY));
#endif

#ifdef E_INK_75Z90
GxEPD2_3C<GxEPD2_750c_Z90, GxEPD2_750c_Z90::HEIGHT / PAGES> display(GxEPD2_750c_Z90(CS, DC, RST, BUSY));
#endif

void initDisplay()
{
  Serial.println("Initializing display...");

#ifdef ESP32
  SPI.end();
  SPI.begin(13, 14, 14, 0);

  // for Waveshare boards with "clever" reset circuit
  display.init(115200, true, 2, false);
#else
  display.init(115200);
#endif

  display.setFullWindow();
  display.setRotation(0);
  display.setTextSize(2);

#ifdef E_INK_290
  display.setRotation(1);
  display.setTextSize(1);
#endif

  display.fillScreen(GxEPD_WHITE);
  display.setTextColor(GxEPD_BLACK);

  Serial.println("Display initialized");
}

void showLaunchScreen()
{
  initDisplay();
  do
  {
    display.fillScreen(GxEPD_WHITE);
    display.setCursor(0, 0);
    int16_t x, y;
    uint16_t w1, w2, h1, h2;
    const char *name = "E-Ink Todo List";
    const char *author = "by @Eson";
    display.setTextSize(3);
    display.getTextBounds(name, 0, 0, &x, &y, &w1, &h1);
    display.setCursor((display.width() - w1) / 2, display.height() / 2 - h1);
    display.println(name);
    display.setTextSize(2);
    display.getTextBounds(author, 0, 0, &x, &y, &w2, &h2);
    display.setCursor((display.width() - w2) / 2, display.height() / 2 + h1 / 2);
    display.println(author);
  } while (display.nextPage());
}

void showTextOnScreenCenter(const char *text, uint8_t textSize = 2)
{
  initDisplay();
  display.fillScreen(GxEPD_WHITE);
  display.setCursor(0, 0);
  int16_t x, y;
  uint16_t w, h;
  do
  {
    display.setTextSize(textSize);
    display.getTextBounds(text, 0, 0, &x, &y, &w, &h);
    display.setCursor((display.width() - w) / 2, (display.height() - h) / 2);
    display.println(text);
  } while (display.nextPage());
}

void showTextOnScreenCenter(String text, uint8_t textSize = 2)
{
  showTextOnScreenCenter(text.c_str(), textSize);
}

#endif