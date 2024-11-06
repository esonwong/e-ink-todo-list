#include "display.h"

#ifndef _CLOCK_H_
#define _CLOCK_H_

#define TZ 8                    // 时区偏移值，以 UTC+8 为例
#define DST_MN 0                // 夏令时持续时间（分钟）
#define TZ_SEC ((TZ) * 3600)    // 时区偏移值（秒）
#define DST_SEC ((DST_MN) * 60) // 夏令时持续时间（秒）

void setClock()
{
  configTime(TZ_SEC, DST_SEC, "time.asia.apple.com", "cn.pool.ntp.org", "pool.ntp.org");

  Serial.println("Waiting for time");
  time_t nowSecs = time(nullptr);
  int i = 0;
  while (nowSecs < 8 * 3600 * 2 && i++ < 10)
  {
    delay(1000);
    Serial.print(".");
    nowSecs = time(nullptr);
  }

  struct tm timeinfo;
  gmtime_r(&nowSecs, &timeinfo);
  Serial.print("Current time: ");
  Serial.println(asctime(&timeinfo));
  delay(100);
}

void drawCurrentTime()
{
  // partial update display
  // time_t now = time(nullptr);
  // struct tm *timeinfo = localtime(&now);
  // char timeStr[20];
  // strftime(timeStr, 20, "%Y-%m-%d %H:%M:%S", timeinfo);
  // Serial.println(timeStr);

  // display.setTextColor(GxEPD_BLACK);
  // display.setTextSize(1);

  // int16_t tbx, tby;
  // uint16_t tbw, tbh; // boundary box window
  // // align with centered HelloWorld
  // display.getTextBounds(timeStr, 0, 0, &tbx, &tby, &tbw, &tbh);

  // int x = display.width() - tbw - 10;
  // int y = display.height() - tbh - 10;

  // display.setCursor(x, y);
  // display.print(timeStr);
}

#endif