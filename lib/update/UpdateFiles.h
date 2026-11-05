#ifndef UPDATE_FILES_H
#define UPDATE_FILES_H

#include <ESP8266httpUpdate.h>
#include <ESP8266HTTPClient.h>
#include <CertStoreBearSSL.h>
#include <LittleFS.h>
#include "network.h"
#include "store.h"
#include "fallbackNetwork.h"

#ifndef FILE_DOWNLOAD_READ_TIMEOUT_MS
#define FILE_DOWNLOAD_READ_TIMEOUT_MS 30000
#endif
#ifndef FILE_DOWNLOAD_RETRY_SECONDS
#define FILE_DOWNLOAD_RETRY_SECONDS (60 * 60)
#endif

void updateFile(String name, int checkInterval = 60 * 60 * 24)
{
  String fallbackPath = String("/api/update/") + name;

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
    Serial.printf("Skip update %s, will check after %d hours\n", name.c_str(), (checkInterval - diff) / 3600);
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

  NetworkEndpointCandidate candidates[3];
  // The primary URL needs the file name too (lost in 7e475ac: it requested
  // FILE_UPDATE_URL itself, got 404 and taught every device to prefer the fallback).
  String primaryUrl = String(FILE_UPDATE_URL) + "/" + name;
  int candidateCount = buildEndpointCandidates(primaryUrl.c_str(), fallbackPath, candidates, 3);
  prioritizeLastGoodEndpoint(candidates, candidateCount);

  const char *headerKeys[] = {"etag"};
  int headerKeysSize = sizeof(headerKeys) / sizeof(char *);

  for (int candidateIndex = 0; candidateIndex < candidateCount; candidateIndex++)
  {
    NetworkEndpointCandidate &candidate = candidates[candidateIndex];
    ensureCertStoreMounted();
    BearSSL::WiFiClientSecure attemptClient;
    configureSecureClientForCandidate(attemptClient, certStore, numCerts > 0, candidate);

    Serial.printf("GET %s\n", candidate.url.c_str());

    HTTPClient https;
    if (!https.begin(attemptClient, candidate.url))
    {
      Serial.printf("HTTPS begin failed for %s\n", candidate.id.c_str());
      https.end();
      continue;
    }
#ifdef GIT_VERSION
    https.addHeader("X-Device-Firmware-Version", GIT_VERSION);
#endif
    https.addHeader("If-None-Match", currentEtag);
    https.collectHeaders(headerKeys, headerKeysSize);

    int httpCode = https.GET();
    int contentLength = https.getSize(); // -1 when the server didn't say

    Serial.printf("HTTPS GET: %d\n", httpCode);
    Serial.printf("Content-Length: %d\n", contentLength);

    if (httpCode == HTTP_CODE_NOT_MODIFIED)
    {
      rememberSuccessfulEndpoint(candidate);
      https.end();
      LittleFS.end();
      return;
    }

    if (httpCode == HTTP_CODE_OK)
    {
      if (!LittleFS.begin())
      {
        Serial.println("An Error has occurred while mounting LittleFS");
        https.end();
        return;
      }
      // Download into a temp file and only replace the real one (and store the
      // new ETag) once the whole body arrived. Up to v0.11.3 a cut-off
      // download was kept as if complete: with the 190 KB certs.ar that left
      // ~26 of 170 roots, the GTS root www.einktodo.com needs was missing, and
      // the matching ETag meant the server answered 304 forever after.
      String tmpName = name + ".tmp";
      File downloadedFile = LittleFS.open(tmpName, "w");
      if (downloadedFile)
      {
        WiFiClient *stream = https.getStreamPtr();
        uint8_t buff[128];
        size_t readSizeTotal = 0;
        bool writeOk = true;
        int progress = 0;
        Serial.printf("Download %s Progress: %d%%", name.c_str(), progress);
        while (contentLength <= 0 || readSizeTotal < (size_t)contentLength)
        {
          // Not readBytes(): it gives up after the stream timeout and the old
          // loop took that for the end of the body.
          size_t readSize = 0;
          unsigned long waitStart = millis();
          while (readSize == 0 && https.connected() && millis() - waitStart < FILE_DOWNLOAD_READ_TIMEOUT_MS)
          {
            size_t avail = stream->available();
            if (avail > 0)
            {
              size_t want = std::min(sizeof(buff), avail);
              if (contentLength > 0)
              {
                want = std::min(want, (size_t)contentLength - readSizeTotal);
              }
              readSize = stream->read(buff, want);
            }
            else
            {
              delay(1);
            }
          }
          if (readSize == 0)
          {
            break;
          }
          if (downloadedFile.write(buff, readSize) != readSize)
          {
            writeOk = false;
            break;
          }
          readSizeTotal += readSize;
          if (contentLength > 0)
          {
            int newProgress = (readSizeTotal * 100) / contentLength;
            if (newProgress != progress)
            {
              progress = newProgress;
              Serial.printf("\rDownload %s Progress: %d%%", name.c_str(), progress);
            }
          }
        }
        Serial.println();
        downloadedFile.close();

        // Without a Content-Length there is no way to tell a finished body from
        // a stalled one (and a chunked body would arrive with its framing), so
        // don't trust it.
        bool complete = writeOk && contentLength > 0 && readSizeTotal == (size_t)contentLength;
        if (!complete)
        {
          LittleFS.remove(tmpName);
          LittleFS.end();
          https.end();
          Serial.printf("Download %s incomplete: %u of %d bytes%s, keeping the old file\n", name.c_str(), readSizeTotal, contentLength, writeOk ? "" : " (write failed)");
          // Try the next endpoint now, and this one again in an hour instead
          // of after the full check interval.
          savePersistentValue(name + "_last_check_time", now - checkInterval + FILE_DOWNLOAD_RETRY_SECONDS);
          continue;
        }

        LittleFS.remove(name);
        LittleFS.rename(tmpName, name);
        LittleFS.end();
        savePersistentValue(name + "_etag", https.header("etag"));
        rememberSuccessfulEndpoint(candidate);
        Serial.printf("%s Updated (%u bytes)\n", name.c_str(), readSizeTotal);
        https.end();
        return;
      }

      Serial.printf("Can not open file %s for writing\n", name.c_str());
      LittleFS.end();
      https.end();
      return;
    }

    Serial.printf("HTTPS GET failed for %s, error: %s\n", candidate.id.c_str(), https.errorToString(httpCode).c_str());
    https.end();
  }

  LittleFS.end();
}

void updateFiles()
{
  updateFile("certs.ar");
}

#endif
