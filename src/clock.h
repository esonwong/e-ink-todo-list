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

  Serial.print(F("Waiting for NTP time sync: "));
  displayLog("Waiting for NTP time sync: ");
  time_t nowSecs = time(nullptr);
  int i = 0;
  while (nowSecs < 8 * 3600 * 2 && i++ < 10)
  {
    delay(500);
    Serial.print(F("."));
    displayLog(".");
    nowSecs = time(nullptr);
  }

  Serial.println();
  displayLogln("");
  struct tm timeinfo;
  gmtime_r(&nowSecs, &timeinfo);
  Serial.print(asctime(&timeinfo));
  displayLogln(asctime(&timeinfo));
}

void displayTime()
{
  time_t nowSecs = time(nullptr);
  struct tm timeinfo;
  gmtime_r(&nowSecs, &timeinfo);
  String time = String(timeinfo.tm_hour + 8) + ":" + (timeinfo.tm_min < 10 ? "0" : "") + String(timeinfo.tm_min) + ":" + (timeinfo.tm_sec < 10 ? "0" : "") + String(timeinfo.tm_sec);

  int width = u8g2Fonts.getUTF8Width(time.c_str());
  int height = u8g2Fonts.getFontAscent();

  display.fillScreen(GxEPD_WHITE);
  u8g2Fonts.setCursor((display.width() - width) / 2, (display.height() + height) / 2);
  u8g2Fonts.print(time);

  if (timeinfo.tm_min % 10 == 0 && timeinfo.tm_sec == 0)
  {
    display.display(false);
  }
  else
  {
    display.display(true);
  }
}

#endif