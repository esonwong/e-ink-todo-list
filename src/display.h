
#ifndef _DISPLAY_H_
#define _DISPLAY_H_

// #include <GxEPD2_3C.h>
#include <GxEPD2_BW.h>
#include <U8g2_for_Adafruit_GFX.h>

U8G2_FOR_ADAFRUIT_GFX u8g2Fonts;
// GxEPD2_3C<GxEPD2_750c_Z08, GxEPD2_750c_Z08::HEIGHT / 2> display(GxEPD2_750c_Z08(/*CS=D8*/ 15, /*DC=D3*/ 0, /*RST=D4*/ 2, /*BUSY=D2*/ 4));
// GxEPD2_BW<GxEPD2_750_T7, GxEPD2_750_T7::HEIGHT> display(GxEPD2_750_T7(/*CS=D8*/ 15, /*DC=D3*/ 0, /*RST=D4*/ 2, /*BUSY=D2*/ 4));
GxEPD2_BW<GxEPD2_290, GxEPD2_290::HEIGHT> display(GxEPD2_290(/*CS=5*/ SS, /*DC=*/4, /*RST=*/2, /*BUSY=*/5));

void initDisplay()
{
  Serial.println("初始化屏幕");

  display.init(115200);
  display.fillScreen(GxEPD_WHITE);
  display.setRotation(1);
  display.setTextColor(GxEPD_BLACK);

  u8g2Fonts.begin(display);
  u8g2Fonts.setFont(u8g2_font_helvB08_tf);
}

void displayLog(String log)
{
  if (display.getCursorY() > display.height() - 10)
  {
    display.fillScreen(GxEPD_WHITE);
    display.setCursor(0, 0);
    display.println(log);
    display.display(false);
  }
  else
  {
    display.println(log);
    display.display(true);
  }
}

#endif