
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

#ifdef E_INK_290
  display.setRotation(1);
#endif

  display.fillScreen(GxEPD_WHITE);
  display.setTextColor(GxEPD_BLACK);

  Serial.println("初始化屏幕完成");
}

#endif