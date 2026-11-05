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

#ifndef FIRMWARE_CHECK_INTERVAL_SECONDS
#ifdef DEBUG_UPDATE_FIREWARE
#define FIRMWARE_CHECK_INTERVAL_SECONDS 60
#else
#define FIRMWARE_CHECK_INTERVAL_SECONDS (10 * 24 * 60 * 60)
#endif
#endif

// After a check where every endpoint failed (e.g. the download was cut off).
#ifndef FIRMWARE_RETRY_INTERVAL_SECONDS
#define FIRMWARE_RETRY_INTERVAL_SECONDS (6 * 60 * 60)
#endif

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

  // lastFirmwareCheck = last check that reached the server (updated or already
  // current); lastFirmwareFail = last check where every endpoint failed. Until
  // v0.11.1 the check time was saved *before* downloading, so one interrupted
  // download meant waiting the full interval before trying again.
  int lastFirmwareCheck = getPersistentValue("lastFirmwareCheck", 0);
  int lastFirmwareFail = getPersistentValue("lastFirmwareFail", 0);
  int now = time(nullptr);
  int diff = now - lastFirmwareCheck;
  int failDiff = now - lastFirmwareFail;

  if (diff < FIRMWARE_CHECK_INTERVAL_SECONDS)
  {
    Serial.printf("Skip update Firmware, will check after %d hours\n", (FIRMWARE_CHECK_INTERVAL_SECONDS - diff) / 3600);
    return;
  }

  if (failDiff < FIRMWARE_RETRY_INTERVAL_SECONDS)
  {
    Serial.printf("Skip update Firmware, last attempt failed, retry after %d minutes\n", (FIRMWARE_RETRY_INTERVAL_SECONDS - failDiff) / 60);
    return;
  }

  Serial.println("Update Firmware Start");

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
    ensureCertStoreMounted();
    BearSSL::WiFiClientSecure attemptClient;
    configureSecureClientForCandidate(attemptClient, certStore, numCerts > 0, candidate);
    t_httpUpdate_return ret = ESPhttpUpdate.update(attemptClient, candidate.url);
    if (ret == HTTP_UPDATE_FAILED)
    {
      logTlsFailure(attemptClient, candidate);
    }
    attemptClient.stop();

    switch (ret)
    {
    case HTTP_UPDATE_FAILED:
      Serial.printf("UPDATE Firmware FAILD for %s Error (%d): %s\n", candidate.id.c_str(), ESPhttpUpdate.getLastError(), ESPhttpUpdate.getLastErrorString().c_str());
      break;
    case HTTP_UPDATE_NO_UPDATES:
      savePersistentValue("lastFirmwareCheck", now);
      rememberSuccessfulEndpoint(candidate);
      LittleFS.end();
      Serial.println("UPDATE Firmware NO_UPDATES");
      Serial.println("Update Firmware End");
      return;
    case HTTP_UPDATE_OK:
      // Normally not reached: ESPhttpUpdate reboots into the new image, which
      // then checks again, gets NO_UPDATES and records lastFirmwareCheck.
      savePersistentValue("lastFirmwareCheck", now);
      rememberSuccessfulEndpoint(candidate);
      LittleFS.end();
      Serial.println("UPDATE Firmware SUCCESS");
      Serial.println("Update Firmware End");
      return;
    }
  }

  LittleFS.end();
  savePersistentValue("lastFirmwareFail", now);
  Serial.printf("Update Firmware failed on every endpoint, retry after %d minutes\n", FIRMWARE_RETRY_INTERVAL_SECONDS / 60);
  Serial.println("Update Firmware End");
}

#endif
