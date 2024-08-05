#ifndef FIRMWARE_H
#define FIRMWARE_H

#include <ESP8266httpUpdate.h>
#include <ESP8266HTTPClient.h>
#include <CertStoreBearSSL.h>
#include <LittleFS.h>

void updateFS(const char *url = FS_UPDATE_URL)
{

  Serial.println("Update file system!");

  BearSSL::CertStore certStore;
  BearSSL::WiFiClientSecure client;

  LittleFS.begin();
  int numCerts = certStore.initCertStore(LittleFS, PSTR("/certs.idx"), PSTR("/certs.ar"));
  Serial.printf("Number of CA certs read: %d\n", numCerts);
  if (numCerts == 0)
  {
    Serial.println("No certs found. Need to run Upload Filesystem Image");
    LittleFS.end();
    return; // Can't connect to anything w/o certs!
  }

  client.setCertStore(&certStore);

  t_httpUpdate_return ret = ESPhttpUpdate.updateFS(client, url);

  LittleFS.end();

  switch (ret)
  {
  case HTTP_UPDATE_FAILED:
    Serial.printf("HTTP_UPDATE_FAILD Error (%d): %s\n", ESPhttpUpdate.getLastError(), ESPhttpUpdate.getLastErrorString().c_str());
    break;
  case HTTP_UPDATE_NO_UPDATES:
    Serial.println("HTTP_UPDATE_NO_UPDATES");
    break;
  case HTTP_UPDATE_OK:
    Serial.println("HTTP_UPDATE_OK");
    break;
  }
}

#endif
