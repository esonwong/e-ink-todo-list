#ifndef UPDATE_FIRMWARE_H
#define UPDATE_FIRMWARE_H

#include <ESP8266httpUpdate.h>
#include <ESP8266HTTPClient.h>
#include <CertStoreBearSSL.h>
#include <LittleFS.h>

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
  showTextOnScreenCenter("Updated Fireware");
};

void updateFireWare(const char *url = FIREWARE_UPDATE_URL)
{

  Serial.println("Update file system!");

  BearSSL::CertStore certStore;

  LittleFS.begin();
  int numCerts = certStore.initCertStore(LittleFS, PSTR("/certs.idx"), PSTR("/certs.ar"));
  Serial.printf("Number of CA certs read: %d\n", numCerts);
  if (numCerts == 0)
  {
    updateFireWareClient.setInsecure();
  }

  updateFireWareClient.setCertStore(&certStore);
  String currentVersion;
  File currentVersionFile = LittleFS.open("/currentFSVersion", "r");
  if (!currentVersionFile)
  {
    Serial.println("Can't opening currentVersion file");
    currentVersion = "have no version";
  }
  else
  {
    currentVersion = currentVersionFile.readString();
  }

  Serial.print("Current FS Version: ");
  Serial.println(currentVersion);
  ESPhttpUpdate.onStart(onStartUpdateFireWare);
  ESPhttpUpdate.onProgress(onProgressUpdateFireWare);
  ESPhttpUpdate.onEnd(onEndUpdateFireWare);
  t_httpUpdate_return ret = ESPhttpUpdate.update(updateFireWareClient, url, currentVersion);
  updateFireWareClient.stop();
  LittleFS.end();

  switch (ret)
  {
  case HTTP_UPDATE_FAILED:
    Serial.printf("UPDATE FS FAILD Error (%d): %s\n", ESPhttpUpdate.getLastError(), ESPhttpUpdate.getLastErrorString().c_str());
    break;
  case HTTP_UPDATE_NO_UPDATES:
    Serial.println("UPDATE FS NO_UPDATES");
    break;
  case HTTP_UPDATE_OK:
    Serial.println("UPDATE FS SUCCESS");
    LittleFS.begin();
    File currentVersionFile = LittleFS.open("/currentFSVersion", "r");
    if (!currentVersionFile)
    {
      Serial.println("Can't opening currentVersion file");
    }
    else
    {
      Serial.print("Current FS Version: ");
      currentVersion = currentVersionFile.readString();
      Serial.println(currentVersion);
    }
    LittleFS.end();
    break;
  }
  Serial.println("Update file system done!");
}

#endif
