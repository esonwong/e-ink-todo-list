#include <algorithm>
#include "display.h"
#include "store.h"
#include "config.h"
#include "clock.h"
#include "network.h"
#include <FS.h>
#include <LittleFS.h>
#include <ESP8266HTTPClient.h>
#include <CertStoreBearSSL.h>
#include "fallbackNetwork.h"
#include "rle1.h"

// A bitmap download that ends short (or doesn't decode) is retried this many
// times right away, with a fresh connection, before giving up until the next
// poll. The server gets the X-Device-Download-Incomplete report on the retry.
#ifndef DOWNLOAD_IMMEDIATE_RETRIES
#define DOWNLOAD_IMMEDIATE_RETRIES 2
#endif
#ifndef DOWNLOAD_RETRY_DELAY_MS
#define DOWNLOAD_RETRY_DELAY_MS 1000
#endif
// Longest gap between body bytes before a download counts as stalled.
#ifndef DOWNLOAD_READ_TIMEOUT_MS
#define DOWNLOAD_READ_TIMEOUT_MS 60000
#endif

int updating = false;
String cachedFileName = "/todo.bitmap";
String cachedRedFileName = "/todo_red.bitmap";

// Raw button event to forward to the server on the next fetch (e.g.
// "double-click").  Server-side dispatcher decides what action it triggers,
// so behavior can change without an OTA.  Cleared after the request goes out.
String pendingDeviceEvent = "";
// Raw report of the last bitmap download that was cut off mid-body, sent on the
// next poll as `X-Device-Download-Incomplete: <received>/<expected> <streak>`.
// The server decides what that means; the firmware only reports the numbers.
String pendingDownloadReport = "";
uint16_t incompleteDownloadStreak = 0;

// 设备颜色能力，根据编译时的屏幕类型决定
#if defined(E_INK_750)
#define DEVICE_COLOR_SUPPORT "bwr"
#else
#define DEVICE_COLOR_SUPPORT "bw"
#endif

String urlencode(String str)
{
  String encodedString = "";
  char c;
  char code0;
  char code1;
  // char code2;
  for (unsigned int i = 0; i < str.length(); i++)
  {
    c = str.charAt(i);
    if (c == ' ')
    {
      encodedString += '+';
    }
    else if (isalnum(c))
    {
      encodedString += c;
    }
    else
    {
      code1 = (c & 0xf) + '0';
      if ((c & 0xf) > 9)
      {
        code1 = (c & 0xf) - 10 + 'A';
      }
      c = (c >> 4) & 0xf;
      code0 = c + '0';
      if (c > 9)
      {
        code0 = c - 10 + 'A';
      }
      // code2 = '\0';
      encodedString += '%';
      encodedString += code0;
      encodedString += code1;
      // encodedString+=code2;
    }
    yield();
  }
  return encodedString;
}

void showNoContent()
{
  showTextOnScreenCenter("No Content");
}

void show401()
{
  display.display([](BaseDisplayDriver &displayDriver)
                  { uint16_t y = displayDriver.drawString(displayDriver.width / 2, displayDriver.height / 5, "API Key authorization failed!", &Font24, DISPLAY_COLOR_RED, TEXT_ALIGN_CENTER); 
                    displayDriver.drawString(displayDriver.width / 2, y + 10, "Please long press the button to enter config mode!", &Font20, DISPLAY_COLOR_BLACK, TEXT_ALIGN_CENTER); });
}

void displayToScreen(String file = cachedFileName, uint16_t w = 0, uint16_t h = 0, DisplayColor color = DISPLAY_COLOR_BLACK)
{
  int start = millis();
  Serial.printf("Display to screen %s\n", file.c_str());

  // initDisplay();
  // display.fillScreen(GxEPD_WHITE);

  LittleFS.begin();
  File readFile = LittleFS.open(file, "r");
  if (!readFile)
  {
    Serial.printf("Can not open file %s for reading\n", file.c_str());
    return;
  }

  // do
  // {
  //   readFile.seek(0);
  //   uint16_t x = 0;
  //   uint16_t y = 0;
  //   uint8_t buf[128];
  //   while (readFile.available())
  //   {
  //     readFile.read(buf, sizeof(buf));
  //     // draw pixel from buf, 8 pixels in a byte, width is w, height is h
  //     for (unsigned int i = 0; i < sizeof(buf); i++)
  //     {
  //       for (int j = 0; j < 8; j++)
  //       {
  //         if (x >= w)
  //         {
  //           x = 0;
  //           y++;
  //         }
  //         if (y >= h)
  //         {
  //           break;
  //         }
  //         if (buf[i] & (0x80 >> j))
  //         {
  //           display.drawPixel(x, y, color);
  //         }
  //         x++;
  //       }
  //     }
  //   }
  //   // drawCurrentTime();
  // } while (display.nextPage());

  display.display([&readFile, w, h, color](BaseDisplayDriver &displayDriver)
                  {
                    readFile.seek(0);
                    uint16_t x = 0;
                    uint16_t y = 0;
                    uint8_t buf[128];
                    while (readFile.available())
                    {
                      readFile.read(buf, sizeof(buf));
                      // draw pixel from buf, 8 pixels in a byte, width is w, height is h
                      for (unsigned int i = 0; i < sizeof(buf); i++)
                      {
                        for (int j = 0; j < 8; j++)
                        {
                          if (x >= w)
                          {
                            x = 0;
                            y++;
                          }
                          if (y >= h)
                          {
                            break;
                          }
                          if (buf[i] & (0x80 >> j))
                          {
                            displayDriver.drawPixel(x, y, color);
                          }
                          x++;
                        }
                      }
                    } });

  readFile.close();
  LittleFS.end();
  Serial.printf("Display to screen %s done in %lu ms\n", file.c_str(), millis() - start);
}

/**
 * 显示 BWR（黑白红）三色位图到屏幕
 * v3 API 返回的 BWR 位图格式: bit=0 表示有墨色, bit=1 表示无墨色
 * - blackFile: 黑色图层位图
 * - redFile: 红色图层位图
 */
void displayBWRToScreen(String blackFile, String redFile, uint16_t w, uint16_t h)
{
  int start = millis();
  Serial.printf("Display BWR to screen black=%s red=%s\n", blackFile.c_str(), redFile.c_str());

  LittleFS.begin();
  File fBlack = LittleFS.open(blackFile, "r");
  File fRed = LittleFS.open(redFile, "r");

  if (!fBlack || !fRed)
  {
    Serial.println("Cannot open BWR bitmap files");
    if (fBlack)
      fBlack.close();
    if (fRed)
      fRed.close();
    LittleFS.end();
    return;
  }

  display.display([&fBlack, &fRed, w, h](BaseDisplayDriver &displayDriver)
                  {
                    // 读取并绘制黑色图层
                    // BWR 格式: bit=0 表示有墨色(黑色)，bit=1 表示无墨色(白色)
                    fBlack.seek(0);
                    {
                      uint16_t x = 0;
                      uint16_t y = 0;
                      uint8_t buf[128];
                      while (fBlack.available())
                      {
                        size_t bytesRead = fBlack.read(buf, sizeof(buf));
                        for (size_t i = 0; i < bytesRead; i++)
                        {
                          for (int j = 0; j < 8; j++)
                          {
                            if (x >= w)
                            {
                              x = 0;
                              y++;
                            }
                            if (y >= h)
                              break;
                            // BWR black: bit=0 means black ink ON
                            if (!(buf[i] & (0x80 >> j)))
                            {
                              displayDriver.drawPixel(x, y, DISPLAY_COLOR_BLACK);
                            }
                            x++;
                          }
                        }
                      }
                    }

                    // 读取并绘制红色图层
                    // BWR 格式: bit=0 表示有墨色(红色)，bit=1 表示无墨色
                    fRed.seek(0);
                    {
                      uint16_t x = 0;
                      uint16_t y = 0;
                      uint8_t buf[128];
                      while (fRed.available())
                      {
                        size_t bytesRead = fRed.read(buf, sizeof(buf));
                        for (size_t i = 0; i < bytesRead; i++)
                        {
                          for (int j = 0; j < 8; j++)
                          {
                            if (x >= w)
                            {
                              x = 0;
                              y++;
                            }
                            if (y >= h)
                              break;
                            // BWR red: bit=0 means red ink ON
                            if (!(buf[i] & (0x80 >> j)))
                            {
                              displayDriver.drawPixel(x, y, DISPLAY_COLOR_RED);
                            }
                            x++;
                          }
                        }
                      }
                    } });

  fBlack.close();
  fRed.close();
  LittleFS.end();
  Serial.printf("Display BWR to screen done in %lu ms\n", millis() - start);
}

// One bitmap plane being written to LittleFS while the body streams in.
struct PlaneWriter
{
  File file;
  size_t wireRemaining = 0; // body bytes still belonging to this plane
  size_t written = 0;       // decoded bytes that reached the file
};

// rle1 decoder output goes straight into the plane's file, in <= 256 B pieces.
static bool rle1Sink(const uint8_t *data, size_t len, void *ctx)
{
  PlaneWriter *p = (PlaneWriter *)ctx;
  size_t wrote = p->file.write(data, len);
  p->written += wrote;
  yield();
  return wrote == len;
}

// One decoder reused for every plane. Static so its output chunk lives in
// BSS instead of the 4 KB loop-task stack that already holds the TLS client.
static Rle1Decoder rle1Decoder(rle1Sink, nullptr, 0);

void downloadAndDrawTodo()
{

  runningValue.displayContentLastCheckTime = time(nullptr);

  // if (wifiManager.getConfigPortalActive())
  // {
  //   Serial.println("Config portal active");
  //   return;
  // }

  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("WiFi not connected");
    return;
  }

  Serial.print("Free memory: ");
  Serial.printf("%u, max block %u\n", ESP.getFreeHeap(), ESP.getMaxFreeBlockSize());

  const char *apikey = apiKey.getValue();

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

  client.setTimeout(60000);

  String savedTodoLastModified = runningValue.todoLastModified;
  Serial.println("Last-Modified: " + savedTodoLastModified);

  NetworkEndpointCandidate candidates[3];
  int candidateCount = buildEndpointCandidates(DEFAULT_API_URL, "/api/display/v3", candidates, 3);
  prioritizeLastGoodEndpoint(candidates, candidateCount);

  const char *headerKeys[] = {
      "Content-Picture-Width",
      "Content-Picture-Height",
      "API-Version",
      "Last-Modified",
      "X-Data-Modified",
      "X-Bitmap-Layers",
      "X-Black-Buffer-Length",
      "X-Red-Buffer-Length",
      "X-Bitmap-Encoding",
      "X-Device-Config-Last-Modified"};
  int headerKeysSize = sizeof(headerKeys) / sizeof(char *);

  int immediateRetriesLeft = DOWNLOAD_IMMEDIATE_RETRIES;

  for (int candidateIndex = 0; candidateIndex < candidateCount; candidateIndex++)
  {
    NetworkEndpointCandidate &candidate = candidates[candidateIndex];
    ensureCertStoreMounted();
    BearSSL::WiFiClientSecure attemptClient;
    configureSecureClientForCandidate(attemptClient, certStore, numCerts > 0, candidate);

    String url = candidate.url + "?width=" + String(display.width) + "&height=" + String(display.height);
    Serial.printf("GET %s\n", url.c_str());
    Serial.printf("If-Modified-Since: %s\n", savedTodoLastModified.c_str());
    Serial.printf("Authorization: Bearer %s\n", apikey);

    HTTPClient https;
    https.setTimeout(60000);
    if (!https.begin(attemptClient, url))
    {
      Serial.printf("HTTPS begin failed for %s\n", candidate.id.c_str());
      https.end();
      continue;
    }

    https.addHeader("If-Modified-Since", savedTodoLastModified);
    https.addHeader("X-If-Modified-Since", savedTodoLastModified);
    https.addHeader("Authorization", "Bearer " + String(apikey));
    https.addHeader("X-Device-Id", DeviceID);
    https.addHeader("X-Device-Colors", DEVICE_COLOR_SUPPORT);
    // We can stream-decode rle1 (see rle1.h); the server answers with
    // X-Bitmap-Encoding: rle1 when it took the offer, raw otherwise.
    https.addHeader("X-Device-Bitmap-Encoding", "rle1");
#ifdef GIT_VERSION
    https.addHeader("X-Device-Firmware-Version", GIT_VERSION);
#endif
    if (pendingDeviceEvent.length() > 0)
    {
      Serial.printf("X-Device-Event: %s\n", pendingDeviceEvent.c_str());
      https.addHeader("X-Device-Event", pendingDeviceEvent);
      pendingDeviceEvent = "";
    }
    bool sentDownloadReport = false;
    if (pendingDownloadReport.length() > 0)
    {
      Serial.printf("X-Device-Download-Incomplete: %s\n", pendingDownloadReport.c_str());
      https.addHeader("X-Device-Download-Incomplete", pendingDownloadReport);
      sentDownloadReport = true;
    }
    https.collectHeaders(headerKeys, headerKeysSize);

    int httpCode = https.GET();
    int contentLength = https.getSize();
    if (sentDownloadReport && httpCode > 0)
    {
      // The server got the request headers; don't report the same failure twice.
      pendingDownloadReport = "";
    }

    Serial.printf("HTTPS GET: %d\n", httpCode);
    Serial.printf("Content-Length: %d\n", contentLength);
    Serial.print("Free memory: ");
    Serial.printf("%u, max block %u\n", ESP.getFreeHeap(), ESP.getMaxFreeBlockSize());

    if (httpCode == HTTP_CODE_NOT_MODIFIED)
    {
      rememberSuccessfulEndpoint(candidate);
      Serial.println("Not Modified");
      https.end();
      LittleFS.end();
      return;
    }

    if (httpCode == 401)
    {
      rememberSuccessfulEndpoint(candidate);
      Serial.printf("HTTPS GET failed, error: API Key authorization failed\n");
      https.end();
      LittleFS.end();
      show401();
      return;
    }

    if (httpCode == HTTP_CODE_NO_CONTENT)
    {
      rememberSuccessfulEndpoint(candidate);
      Serial.println("No Content");
      https.end();
      LittleFS.end();
      showNoContent();
      return;
    }

    if (httpCode != HTTP_CODE_OK)
    {
      Serial.printf("HTTPS GET failed for %s, error: %s\n", candidate.id.c_str(), https.errorToString(httpCode).c_str());
      logTlsFailure(attemptClient, candidate);
      https.end();
      if (isReachableHttpCode(httpCode))
      {
        rememberSuccessfulEndpoint(candidate);
        LittleFS.end();
        return;
      }
      continue;
    }

    if (contentLength <= 0)
    {
      Serial.println("Content-Length not set");
      https.end();
      continue;
    }

    Serial.printf("headers count %d\n", https.headers());
    for (int i = 0; i < https.headers(); i++)
    {
      String headerName = https.headerName(i);
      String headerValue = https.header(i);
      Serial.printf("header[%s]: %s\n", headerName.c_str(), headerValue.c_str());
    }

    WiFiClient *stream = https.getStreamPtr();

    uint16_t w = https.header("Content-Picture-Width").toInt();
    uint16_t h = https.header("Content-Picture-Height").toInt();
    int bitmapLayers = https.header("X-Bitmap-Layers").toInt();
    int blackBufferLength = https.header("X-Black-Buffer-Length").toInt();
    int redBufferLength = https.header("X-Red-Buffer-Length").toInt();

    String lastModified = https.header("Last-Modified");
    if (lastModified.length() == 0)
    {
      lastModified = https.header("X-Data-Modified");
    }
    String apiVersion = https.header("API-Version");

    Serial.printf("Last-Modified: %s\n", lastModified.c_str());
    Serial.printf("API-Version: %s\n", apiVersion.c_str());
    Serial.printf("Bitmap-Layers: %d\n", bitmapLayers);
    Serial.printf("Black-Buffer-Length: %d\n", blackBufferLength);
    Serial.printf("Red-Buffer-Length: %d\n", redBufferLength);

    LittleFS.begin();

    // Transfer encoding negotiated with X-Device-Bitmap-Encoding. Servers that
    // predate it never send X-Bitmap-Encoding, so this stays raw.
    bool rle1 = https.header("X-Bitmap-Encoding").equalsIgnoreCase("rle1");
    bool isBWR = (bitmapLayers == 2 && blackBufferLength > 0 && redBufferLength > 0);
    // Body bytes per plane as sent (compressed lengths under rle1). A mono
    // response is one plane spanning the whole body.
    size_t blackWire = isBWR ? (size_t)blackBufferLength : (size_t)contentLength;
    size_t redWire = isBWR ? (size_t)redBufferLength : 0;
    size_t expectedSize = blackWire + redWire;
    // Every plane must come out to exactly this many bytes before we draw it.
    size_t planeBytes = (size_t)((w + 7) / 8) * h;
    Serial.printf("Bitmap encoding: %s, %d plane(s) of %u bytes\n", rle1 ? "rle1" : "raw", isBWR ? 2 : 1, planeBytes);

    PlaneWriter planes[2];
    int planeCount = isBWR ? 2 : 1;
    planes[0].file = LittleFS.open(cachedFileName, "w");
    planes[0].wireRemaining = blackWire;
    if (isBWR)
    {
      planes[1].file = LittleFS.open(cachedRedFileName, "w");
      planes[1].wireRemaining = redWire;
    }
    if (!planes[0].file || (isBWR && !planes[1].file))
    {
      Serial.println("Cannot open bitmap files for writing");
      for (int i = 0; i < planeCount; i++)
      {
        if (planes[i].file)
          planes[i].file.close();
      }
      https.end();
      LittleFS.end();
      return;
    }

    int cur = 0;
    if (rle1)
      rle1Decoder.reset(rle1Sink, &planes[0], planeBytes);

    Serial.println("Start reading response body");
    uint8_t buff[128];
    size_t bytesWritten = 0; // wire bytes received
    bool decodeOk = true;
    Serial.printf("Download Progress: 0%% , 0/%u bytes", expectedSize);
    while (https.connected() && bytesWritten < expectedSize && decodeOk)
    {
      size_t remaining = expectedSize - bytesWritten;
      // Not stream->readBytes(): that blocks for the full 60 s timeout when
      // the peer drops the connection mid-body, which is exactly the case the
      // immediate retry is for. Wait for data only while still connected.
      size_t readSize = 0;
      unsigned long waitStart = millis();
      while (readSize == 0 && https.connected() && millis() - waitStart < DOWNLOAD_READ_TIMEOUT_MS)
      {
        size_t avail = stream->available();
        if (avail > 0)
        {
          readSize = stream->read(buff, std::min(std::min(sizeof(buff), remaining), avail));
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

      size_t off = 0;
      while (off < readSize && cur < planeCount)
      {
        PlaneWriter &p = planes[cur];
        size_t n = std::min(readSize - off, p.wireRemaining);
        if (rle1)
        {
          if (!rle1Decoder.feed(buff + off, n))
          {
            decodeOk = false;
            break;
          }
        }
        else
        {
          p.written += p.file.write(buff + off, n);
        }
        p.wireRemaining -= n;
        off += n;
        if (p.wireRemaining == 0)
        {
          if (rle1 && !rle1Decoder.finish())
          {
            decodeOk = false;
            break;
          }
          cur++;
          if (rle1 && cur < planeCount)
            rle1Decoder.reset(rle1Sink, &planes[cur], planeBytes);
        }
      }

      bytesWritten += readSize;
      Serial.print("\r");
      Serial.print("                                                                     ");
      Serial.printf("\rDownload Progress: %u%% , %u/%u bytes", (bytesWritten * 100) / expectedSize, bytesWritten, expectedSize);
    }
    Serial.println();

    for (int i = 0; i < planeCount; i++)
    {
      planes[i].file.close();
    }
    LittleFS.end();
    size_t downloadedSize = bytesWritten;
    Serial.printf("Free memory after download: %u, max block %u\n", ESP.getFreeHeap(), ESP.getMaxFreeBlockSize());

    if (!decodeOk)
    {
      // Wire bytes arrived but don't decode: never draw it, never store its
      // Last-Modified. Not a cut-off download, so no report; just try again.
      Serial.printf("Corrupt rle1 stream in plane %d after %u/%u bytes, keep current screen\n", cur, downloadedSize, expectedSize);
      https.end();
      if (immediateRetriesLeft > 0)
      {
        immediateRetriesLeft--;
        Serial.printf("Retrying %s right away (%d left)\n", candidate.id.c_str(), immediateRetriesLeft);
        delay(DOWNLOAD_RETRY_DELAY_MS);
        candidateIndex--;
        continue;
      }
      return;
    }

    // A connection dropped mid-body leaves a truncated bitmap. Drawing it shows
    // only the top of the screen, and storing Last-Modified would make every
    // later poll a 304, so the half screen would stick until the content
    // changes. Skip both, report it, and retry right away before falling back
    // to the next poll.
    if (downloadedSize != expectedSize)
    {
      if (incompleteDownloadStreak < 65535)
      {
        incompleteDownloadStreak++;
      }
      pendingDownloadReport = String(downloadedSize) + "/" + String(expectedSize) + " " + String(incompleteDownloadStreak);
      Serial.printf("Incomplete download: %u/%u bytes (streak %u), keep current screen\n", downloadedSize, expectedSize, incompleteDownloadStreak);
      https.end();
      if (immediateRetriesLeft > 0)
      {
        immediateRetriesLeft--;
        Serial.printf("Retrying %s right away (%d left)\n", candidate.id.c_str(), immediateRetriesLeft);
        delay(DOWNLOAD_RETRY_DELAY_MS);
        candidateIndex--;
        continue;
      }
      return;
    }

    // Decoded size must match the advertised geometry, raw or rle1 (a short
    // write to a full filesystem would land here too).
    bool sizeOk = true;
    for (int i = 0; i < planeCount; i++)
    {
      if (planes[i].written != planeBytes)
      {
        Serial.printf("Plane %d is %u bytes, expected %u, keep current screen\n", i, planes[i].written, planeBytes);
        sizeOk = false;
      }
    }
    if (!sizeOk)
    {
      https.end();
      return;
    }

    Serial.println("Read all response body");
    incompleteDownloadStreak = 0;
    https.end();
    rememberSuccessfulEndpoint(candidate);

    strcpy(runningValue.todoLastModified, lastModified.c_str());
    delay(50);

    Serial.printf("Free memory before drawing: %u, max block %u\n", ESP.getFreeHeap(), ESP.getMaxFreeBlockSize());
    if (isBWR)
    {
      displayBWRToScreen(cachedFileName, cachedRedFileName, w, h);
    }
    else
    {
      displayToScreen(cachedFileName, w, h, DISPLAY_COLOR_BLACK);
    }
    return;
  }

  LittleFS.end();
}
