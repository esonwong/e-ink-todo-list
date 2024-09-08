#ifndef UPDATE_FIRMWARE_H
#define UPDATE_FIRMWARE_H

#include <ESP8266httpUpdate.h>
#include <ESP8266HTTPClient.h>
#include <CertStoreBearSSL.h>
#include <LittleFS.h>
#include "store.h"

BearSSL::WiFiClientSecure updateFireWareClient;

int updateFireWareProgress = 0;

void onStartUpdateFireWare()
{
  Serial.println("Update Firmware Start");
  showTextOnScreenCenter("Updating Firmware");
  Serial.printf("Update Firmware Progress: %d%%", updateFireWareProgress);
};

void onProgressUpdateFireWare(int current, int total)
{
  int newProgress = (current * 100) / total;
  if (newProgress != updateFireWareProgress)
  {
    updateFireWareProgress = newProgress;
    Serial.print("\r");
    Serial.print("                                                            ");
    Serial.printf("\rUpdate Firmware Progress: %d%%, %d/%d bytes", updateFireWareProgress, current, total);
  }
};

void onEndUpdateFireWare()
{
  Serial.println();
  Serial.println("Update Firmware End");
  showTextOnScreenCenter("Updated Firmware Success");
  delay(2000);
};

void updateFireWare(const char *url = FIRMWARE_UPDATE_URL)
{

  int lastFirmwareCheck = getPersistentValue("lastFirmwareCheck", 0);
  int now = time(nullptr);
  int diff = now - lastFirmwareCheck;

#ifdef DEBUG_UPDATE_FIREWARE
  int checkInterval = 60;
#else
  int checkInterval = 10 * 24 * 60 * 60;
#endif

  if (diff < checkInterval)
  {
    Serial.printf("Skip update Firmware, will check after %d hours\n", (checkInterval - diff) / 3600);
    return;
  }

  Serial.println("Update Firmware Start");

  savePersistentValue("lastFirmwareCheck", time(nullptr));

  BearSSL::CertStore certStore;

  LittleFS.begin();
  int numCerts = certStore.initCertStore(LittleFS, PSTR("/certs.idx"), PSTR("/certs.ar"));
  Serial.printf("Number of CA certs read: %d\n", numCerts);
  if (numCerts == 0)
  {
    updateFireWareClient.setInsecure();
  }

  updateFireWareClient.setCertStore(&certStore);
  ESPhttpUpdate.onStart(onStartUpdateFireWare);
  ESPhttpUpdate.onProgress(onProgressUpdateFireWare);
  ESPhttpUpdate.onEnd(onEndUpdateFireWare);
  t_httpUpdate_return ret = ESPhttpUpdate.update(updateFireWareClient, url);
  updateFireWareClient.stop();
  LittleFS.end();

  switch (ret)
  {
  case HTTP_UPDATE_FAILED:
    Serial.printf("UPDATE Firmware FAILD Error (%d): %s\n", ESPhttpUpdate.getLastError(), ESPhttpUpdate.getLastErrorString().c_str());
    break;
  case HTTP_UPDATE_NO_UPDATES:
    Serial.println("UPDATE Firmware NO_UPDATES");
    break;
  case HTTP_UPDATE_OK:
    Serial.println("UPDATE Firmware SUCCESS");
    break;
  }
  Serial.println("Update Firmware End");
}

#endif
