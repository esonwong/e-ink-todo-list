#ifndef _CLOCK_H_
#define _CLOCK_H_

#define TZ 8                    // 时区偏移值，以 UTC+8 为例
#define DST_MN 0                // 夏令时持续时间（分钟）
#define TZ_SEC ((TZ) * 3600)    // 时区偏移值（秒）
#define DST_SEC ((DST_MN) * 60) // 夏令时持续时间（秒）

void setClock()
{
  configTime(TZ_SEC, DST_SEC, "cn.pool.ntp.org",
             "ntp1.aliyun.com", "pool.ntp.org");

  Serial.print(F("Waiting for NTP time sync: "));
  time_t nowSecs = time(nullptr);
  while (nowSecs < 8 * 3600 * 2)
  {
    delay(500);
    Serial.print(F("."));
    yield();
    nowSecs = time(nullptr);
  }

  Serial.println();
  struct tm timeinfo;
  gmtime_r(&nowSecs, &timeinfo);
  Serial.print(asctime(&timeinfo));
}

#endif