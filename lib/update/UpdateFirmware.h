#ifndef UPDATE_FIRMWARE_H
#define UPDATE_FIRMWARE_H

#include <ESP8266httpUpdate.h>
#include <ESP8266HTTPClient.h>
#include <CertStoreBearSSL.h>
#include <LittleFS.h>
#include "store.h"
#include "fallbackNetwork.h"

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

#ifdef ENABLE_OTA
  Serial.println("OTA is enabled");
#else
  Serial.println("OTA is disabled");
  return;
#endif // ENABLE_OTA

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

  NetworkEndpointCandidate candidates[3];
  int candidateCount = buildEndpointCandidates(url, "/api/update/firmware", candidates, 3);
  prioritizeLastGoodEndpoint(candidates, candidateCount);

  ESPhttpUpdate.onStart(onStartUpdateFireWare);
  ESPhttpUpdate.onProgress(onProgressUpdateFireWare);
  ESPhttpUpdate.onEnd(onEndUpdateFireWare);

  for (int candidateIndex = 0; candidateIndex < candidateCount; candidateIndex++)
  {
    NetworkEndpointCandidate &candidate = candidates[candidateIndex];
    BearSSL::WiFiClientSecure attemptClient;
    configureSecureClientForCandidate(attemptClient, certStore, numCerts > 0, candidate);
    t_httpUpdate_return ret = ESPhttpUpdate.update(attemptClient, candidate.url);
    attemptClient.stop();

    switch (ret)
    {
    case HTTP_UPDATE_FAILED:
      Serial.printf("UPDATE Firmware FAILD for %s Error (%d): %s\n", candidate.id.c_str(), ESPhttpUpdate.getLastError(), ESPhttpUpdate.getLastErrorString().c_str());
      break;
    case HTTP_UPDATE_NO_UPDATES:
      rememberSuccessfulEndpoint(candidate);
      LittleFS.end();
      Serial.println("UPDATE Firmware NO_UPDATES");
      Serial.println("Update Firmware End");
      return;
    case HTTP_UPDATE_OK:
      rememberSuccessfulEndpoint(candidate);
      LittleFS.end();
      Serial.println("UPDATE Firmware SUCCESS");
      Serial.println("Update Firmware End");
      return;
    }
  }

  LittleFS.end();
  Serial.println("Update Firmware End");
}

#endif
