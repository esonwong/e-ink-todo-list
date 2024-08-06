#ifndef UPDATE_FILES_H
#define UPDATE_FILES_H

#include <ESP8266httpUpdate.h>
#include <ESP8266HTTPClient.h>
#include <CertStoreBearSSL.h>
#include <LittleFS.h>
#include "network.h"
#include "store.h"

void updateFile(String name)
{

  Serial.print("Update file: ");
  Serial.println(name);

  String currentEtag = getPersistentValue(name + "_etag", "");
  String url = String(FILE_UPDATE_URL) + "/" + name;

  Serial.print("URL: ");
  Serial.println(url);

  Serial.print("currentEtag: ");
  Serial.println(currentEtag);

  int lastCheckTime = getPersistentValue(name + "_last_check_time", 0);
  int now = time(nullptr);
  int checkInterval = 60 * 60 * 24; // 10 day
  int diff = now - lastCheckTime;

  if (diff < checkInterval)
  {
    Serial.printf("Skip update %s, will check after %d hours\n", name.c_str(), (checkInterval - diff) / 3600);
    return;
  }

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
    Serial.println("No certs found. Need to restart to get certs. It will be more secure for your data.");
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
  https.addHeader("X-Last-Etag", currentEtag);

  const char *headerKeys[] = {"Etag"};
  int headerKeysSize = sizeof(headerKeys) / sizeof(char *);
  https.collectHeaders(headerKeys, headerKeysSize);

  int httpCode = https.GET();
  int contentLength = https.getSize();

  Serial.printf("HTTPS GET: %d\n", httpCode);
  Serial.printf("Content-Length: %d\n", contentLength);

  if (httpCode == HTTP_CODE_OK)
  {
    File downloadedFile = LittleFS.open(name, "w");
    if (downloadedFile)
    {
      WiFiClient *stream = https.getStreamPtr();
      while (https.connected())
      {
        if (stream->available())
        {
          downloadedFile.write(stream->read());
        }
      }
      downloadedFile.close();
      savePersistentValue(name + "_etag", https.header("Etag"));
      savePersistentValue(name + "_last_check_time", now);
      Serial.println(name + " Updated");
    } else {
      Serial.println("Failed to open file for writing");
    }
  } else {
    Serial.printf("HTTPS GET failed, error: %s\n", https.errorToString(httpCode).c_str());
  }
  https.end();
  LittleFS.end();
}

void updateFiles()
{
  updateFile("certs.ar");
}

#endif
