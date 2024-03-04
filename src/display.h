
#ifndef _DISPLAY_H_
#define _DISPLAY_H_

#include <GxEPD2_3C.h>
// #include <GxEPD2_BW.h>
#include <U8g2_for_Adafruit_GFX.h>

U8G2_FOR_ADAFRUIT_GFX u8g2Fonts;

#ifdef E_INK_750
// GxEPD2_BW<GxEPD2_750_T7, GxEPD2_750_T7::HEIGHT> display(GxEPD2_750_T7(/*CS=D8*/ 15, /*DC=D3*/ 0, /*RST=D4*/ 2, /*BUSY=D2*/ 4));
GxEPD2_3C<GxEPD2_750c_Z08, GxEPD2_750c_Z08::HEIGHT / 2> display(GxEPD2_750c_Z08(/*CS=D8*/ 15, /*DC=D3*/ 0, /*RST=D4*/ 2, /*BUSY=D2*/ 4));
#else
GxEPD2_BW<GxEPD2_290, GxEPD2_290::HEIGHT> display(GxEPD2_290(/*CS=5*/ SS, /*DC=*/4, /*RST=*/2, /*BUSY=*/5));
#endif

void initDisplay()
{
  Serial.println("Initializing display...");

#ifdef E_INK_750
  SPI.end();
  SPI.begin(13, 14, 14, 0);
  display.init(115200, true, 2, false);
#else
  display.init(115200);
#endif

  display.setFullWindow();

#ifdef E_INK_750
  display.fillScreen(GxEPD_BLACK);
  display.setRotation(0);
  display.setTextColor(GxEPD_WHITE);
  display.setTextSize(2);
  display.setCursor(20, 20);
#else
  display.setRotation(1);
  display.setCursor(0, 0);
  display.fillScreen(GxEPD_WHITE);
  display.setTextColor(GxEPD_BLACK);
#endif
  Serial.println("初始化屏幕完成");
}

void displayLogln(String log)
{

#ifdef E_INK_750
  Serial.println(log);
  return;
#endif

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

void displayLog(String log)
{

#ifdef E_INK_750
  Serial.print(log);
  return;
#endif

  if (display.getCursorY() > display.height() - 10)
  {
    display.fillScreen(GxEPD_WHITE);
    display.setCursor(0, 0);
    display.print(log);
    display.display(false);
  }
  else
  {
    display.print(log);
    display.display(true);
  }
}

#endif