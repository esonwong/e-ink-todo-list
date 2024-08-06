#ifndef FIRMWARE_H
#define FIRMWARE_H

#include <ESP8266httpUpdate.h>
#include <ESP8266HTTPClient.h>
#include <CertStoreBearSSL.h>
#include <LittleFS.h>

BearSSL::WiFiClientSecure updateFSClient;

void onStartUpdateFS()
{
  Serial.println("Update FS Start");
  showTextOnScreenCenter("Updating System Data");
};

void onProgressUpdateFS(int current, int total)
{
  Serial.printf("Progress: %d%%\n", (current * 100) / total);
};

void onEndUpdateFS()
{
  Serial.println("Update FS End");
  showTextOnScreenCenter("Updated System Data");
};

void updateFS(const char *url = FS_UPDATE_URL)
{

  Serial.println("Update file system!");

  BearSSL::CertStore certStore;

  LittleFS.begin();
  int numCerts = certStore.initCertStore(LittleFS, PSTR("/certs.idx"), PSTR("/certs.ar"));
  Serial.printf("Number of CA certs read: %d\n", numCerts);
  if (numCerts == 0)
  {
    updateFSClient.setInsecure();
  }

  updateFSClient.setCertStore(&certStore);
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
  ESPhttpUpdate.onStart(onStartUpdateFS);
  ESPhttpUpdate.onProgress(onProgressUpdateFS);
  ESPhttpUpdate.onEnd(onEndUpdateFS);
  t_httpUpdate_return ret = ESPhttpUpdate.updateFS(updateFSClient, url, currentVersion);
  updateFSClient.stop();
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
