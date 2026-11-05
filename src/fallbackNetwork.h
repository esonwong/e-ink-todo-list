#pragma once

#include <Arduino.h>
#include <CertStoreBearSSL.h>
#include <ESP8266HTTPClient.h>
#include "store.h"

#ifndef FALLBACK_BASE_URL_1
#define FALLBACK_BASE_URL_1 ""
#endif

#ifndef FALLBACK_BASE_URL_2
#define FALLBACK_BASE_URL_2 ""
#endif

#ifndef FALLBACK_TLS_FINGERPRINT_1
#define FALLBACK_TLS_FINGERPRINT_1 ""
#endif

#ifndef FALLBACK_TLS_FINGERPRINT_2
#define FALLBACK_TLS_FINGERPRINT_2 ""
#endif

struct NetworkEndpointCandidate
{
  String id;
  String url;
  const char *fingerprint;
  bool useFingerprint;
};

static const char *NETWORK_PRIMARY_ENDPOINT_ID = "primary";
// v2: up to v0.11.1 the primary endpoint always failed its TLS check (see
// ensureCertStoreMounted), so every device remembered the fallback. New key
// names make upgraded devices try the primary endpoint again.
static const char *NETWORK_LAST_GOOD_ENDPOINT_KEY = "network_last_good_endpoint_v2";
static const char *NETWORK_LAST_GOOD_USES_FINGERPRINT_KEY = "network_last_good_uses_fingerprint_v2";

inline bool isValidFallbackFingerprint(const char *fingerprint)
{
  return fingerprint != nullptr && strlen(fingerprint) > 0;
}

inline bool isValidFallbackBaseUrl(const char *baseUrl)
{
  return baseUrl != nullptr && strlen(baseUrl) > 0;
}

inline int appendFallbackCandidate(NetworkEndpointCandidate *candidates, int count, int maxCount, const char *baseUrl, const char *fingerprint, const String &path)
{
  if (count >= maxCount || !isValidFallbackBaseUrl(baseUrl) || !isValidFallbackFingerprint(fingerprint))
  {
    return count;
  }

  candidates[count].id = String(baseUrl);
  candidates[count].url = String(baseUrl) + path;
  candidates[count].fingerprint = fingerprint;
  candidates[count].useFingerprint = true;
  return count + 1;
}

inline int buildEndpointCandidates(const char *primaryUrl, const String &fallbackPath, NetworkEndpointCandidate *candidates, int maxCount)
{
  int count = 0;
  if (primaryUrl != nullptr && strlen(primaryUrl) > 0 && count < maxCount)
  {
    candidates[count].id = NETWORK_PRIMARY_ENDPOINT_ID;
    candidates[count].url = String(primaryUrl);
    candidates[count].fingerprint = nullptr;
    candidates[count].useFingerprint = false;
    count++;
  }

  count = appendFallbackCandidate(candidates, count, maxCount, FALLBACK_BASE_URL_1, FALLBACK_TLS_FINGERPRINT_1, fallbackPath);
  count = appendFallbackCandidate(candidates, count, maxCount, FALLBACK_BASE_URL_2, FALLBACK_TLS_FINGERPRINT_2, fallbackPath);
  return count;
}

inline void prioritizeLastGoodEndpoint(NetworkEndpointCandidate *candidates, int count)
{
  String lastGoodEndpoint = getPersistentValue<String>(NETWORK_LAST_GOOD_ENDPOINT_KEY, String(NETWORK_PRIMARY_ENDPOINT_ID));
  for (int i = 0; i < count; i++)
  {
    if (candidates[i].id == lastGoodEndpoint)
    {
      if (i > 0)
      {
        NetworkEndpointCandidate selected = candidates[i];
        for (int j = i; j > 0; j--)
        {
          candidates[j] = candidates[j - 1];
        }
        candidates[0] = selected;
      }
      return;
    }
  }
}

inline void rememberSuccessfulEndpoint(const NetworkEndpointCandidate &candidate)
{
  savePersistentValue(NETWORK_LAST_GOOD_ENDPOINT_KEY, candidate.id);
  savePersistentValue(NETWORK_LAST_GOOD_USES_FINGERPRINT_KEY, candidate.useFingerprint);
}

// BearSSL::CertStore reads /certs.idx and /certs.ar from LittleFS *during* the
// TLS handshake, but getPersistentValue()/savePersistentValue() unmount
// LittleFS when they return. Without a re-mount right before each attempt the
// trust-anchor lookup fails and every CA-verified connection is rejected with
// BearSSL error 62 ("Chain could not be linked to a trust anchor").
// LittleFS.begin() is a no-op when already mounted.
inline void ensureCertStoreMounted()
{
  LittleFS.begin();
}

inline void logTlsFailure(BearSSL::WiFiClientSecure &client, const NetworkEndpointCandidate &candidate)
{
  char sslErr[80];
  int sslCode = client.getLastSSLError(sslErr, sizeof(sslErr));
  if (sslCode != 0)
  {
    Serial.printf("TLS error for %s: %d (%s), free heap %u, max block %u\n", candidate.id.c_str(), sslCode, sslErr, ESP.getFreeHeap(), ESP.getMaxFreeBlockSize());
  }
}

inline void configureSecureClientForCandidate(BearSSL::WiFiClientSecure &client, BearSSL::CertStore &certStore, bool hasCertStore, const NetworkEndpointCandidate &candidate)
{
  client.setTimeout(60000);

  if (candidate.useFingerprint)
  {
    client.setFingerprint(candidate.fingerprint);
    return;
  }

  if (hasCertStore)
  {
    client.setCertStore(&certStore);
  }
  else
  {
    client.setInsecure();
  }
}

inline bool isReachableHttpCode(int httpCode)
{
  return httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_NOT_MODIFIED || httpCode == HTTP_CODE_NO_CONTENT || httpCode == 401;
}