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
  Serial.println("Update Fireware Start");
  showTextOnScreenCenter("Updating Fireware");
};

void onProgressUpdateFireWare(int current, int total)
{
  int newProgress = (current * 100) / total;
  if (newProgress != updateFireWareProgress)
  {
    updateFireWareProgress = newProgress;
    Serial.printf("Update Fireware Progress: %d%%\n", updateFireWareProgress);
  }
};

void onEndUpdateFireWare()
{
  Serial.println("Update Fireware End");
  showTextOnScreenCenter("Updated Fireware Success");
};

void updateFireWare(const char *url = FIREWARE_UPDATE_URL)
{

  Serial.println("Update Fireware Start");

  int lastFirewareCheck = getPersistentValue("lastFirewareCheck", 0);
  int now = time(nullptr);
  int diff = now - lastFirewareCheck;

  int checkInterval = 10 * 24 * 60 * 60;

  if (diff < checkInterval)
  {
    Serial.printf("Skip update fireware, will check again in %d hours\n", (checkInterval - diff) / 3600);
    return;
  }

  savePersistentValue("lastFirewareCheck", time(nullptr));

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
  Serial.println("Update Fireware End");
}

#endif
