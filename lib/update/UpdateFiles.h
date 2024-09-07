#ifndef UPDATE_FILES_H
#define UPDATE_FILES_H

#include <ESP8266httpUpdate.h>
#include <ESP8266HTTPClient.h>
#include <CertStoreBearSSL.h>
#include <LittleFS.h>
#include "network.h"
#include "store.h"

void updateFile(String name, int checkInterval = 60 * 60 * 24)
{

  String url = String(FILE_UPDATE_URL) + "/" + name;

  int lastCheckTime = getPersistentValue(name + "_last_check_time", 0);
  int now = time(nullptr);
  int diff = now - lastCheckTime;

  // Serial.print("Update file: ");
  // Serial.println(name);
  // Serial.print("Last check time: ");
  // Serial.println(lastCheckTime);
  // Serial.print("Now: ");
  // Serial.println(now);
  // Serial.print("Diff: ");
  // Serial.println(diff);
  // Serial.print("Check interval: ");
  // Serial.println(checkInterval);

  if (diff < checkInterval)
  {
    // Serial.printf("Skip update %s, will check after %d hours\n", name.c_str(), (checkInterval - diff) / 3600);
    return;
  }

  savePersistentValue(name + "_last_check_time", now);

  String currentEtag = getPersistentValue(name + "_etag", "");
  Serial.print("currentEtag: ");
  Serial.println(currentEtag);

  BearSSL::CertStore certStore;
  BearSSL::WiFiClientSecure client;

  if (!LittleFS.begin())
  {
    Serial.println("An Error has occurred while mounting LittleFS");
    return;
  }
  int numCerts = certStore.initCertStore(LittleFS, PSTR("/certs.idx"), PSTR("/certs.ar"));
  Serial.printf("Number of CA certs read: %d\n", numCerts);
  if (numCerts == 0)
  {
    LittleFS.end();
    client.setInsecure();
  }
  else
  {
    client.setCertStore(&certStore);
  }

  Serial.printf("GET %s\n", url.c_str());

  HTTPClient https;
  https.begin(client, url);
#ifdef GIT_VERSION
  https.addHeader("X-Device-Firmware-Version", GIT_VERSION);
#endif
  https.addHeader("If-None-Match", currentEtag);

  const char *headerKeys[] = {"etag"};
  int headerKeysSize = sizeof(headerKeys) / sizeof(char *);
  https.collectHeaders(headerKeys, headerKeysSize);

  int httpCode = https.GET();
  size_t contentLength = https.getSize();

  Serial.printf("HTTPS GET: %d\n", httpCode);
  Serial.printf("Content-Length: %d\n", contentLength);

  if (httpCode == HTTP_CODE_OK)
  {
    if (!LittleFS.begin())
    {
      Serial.println("An Error has occurred while mounting LittleFS");
      return;
    }
    File downloadedFile = LittleFS.open(name, "w+");
    if (downloadedFile)
    {
      WiFiClient *stream = https.getStreamPtr();
      uint8_t buff[128];
      size_t buffSize = sizeof(buff);
      size_t readSize;
      size_t readSizeTotal = 0;
      int progress = 0;
      while (https.connected() && (readSize = stream->readBytes(buff, std::min(buffSize, (contentLength - readSizeTotal)))) > 0)
      {
        downloadedFile.write(buff, readSize);
        readSizeTotal += readSize;
        int newProgress = (readSizeTotal * 100) / contentLength;
        if (newProgress != progress)
        {
          progress = newProgress;
          Serial.printf("Download %s Progress: %d%%\n", name.c_str(), progress);
        }
      }
      downloadedFile.close();
      LittleFS.end();
      savePersistentValue(name + "_etag", https.header("etag"));
      Serial.println(name + " Updated");
    }
    else
    {
      Serial.println("Failed to open file for writing");
      LittleFS.end();
    }
  }
  else
  {
    Serial.printf("HTTPS GET failed, error: %s\n", https.errorToString(httpCode).c_str());
    LittleFS.end();
  }
  https.end();
}

void updateFiles()
{
  updateFile("certs.ar");
}

#endif
