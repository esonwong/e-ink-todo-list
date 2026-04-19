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

int updating = false;
String cachedFileName = "/todo.bitmap";
String cachedRedFileName = "/todo_red.bitmap";

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
  Serial.println(ESP.getFreeHeap());

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
      "X-Device-Config-Last-Modified"};
  int headerKeysSize = sizeof(headerKeys) / sizeof(char *);

  for (int candidateIndex = 0; candidateIndex < candidateCount; candidateIndex++)
  {
    NetworkEndpointCandidate &candidate = candidates[candidateIndex];
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
#ifdef GIT_VERSION
    https.addHeader("X-Device-Firmware-Version", GIT_VERSION);
#endif
    https.collectHeaders(headerKeys, headerKeysSize);

    int httpCode = https.GET();
    int contentLength = https.getSize();

    Serial.printf("HTTPS GET: %d\n", httpCode);
    Serial.printf("Content-Length: %d\n", contentLength);
    Serial.print("Free memory: ");
    Serial.println(ESP.getFreeHeap());

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

    bool isBWR = (bitmapLayers == 2 && blackBufferLength > 0 && redBufferLength > 0);

    if (isBWR)
    {
      File blackFile = LittleFS.open(cachedFileName, "w");
      File redFile = LittleFS.open(cachedRedFileName, "w");

      if (!blackFile || !redFile)
      {
        Serial.println("Cannot open BWR bitmap files for writing");
        if (blackFile)
        {
          blackFile.close();
        }
        if (redFile)
        {
          redFile.close();
        }
        https.end();
        LittleFS.end();
        return;
      }

      Serial.println("Start reading BWR response body");
      uint8_t buff[128];
      size_t buffSize = sizeof(buff);
      size_t readSize;
      size_t bytesWritten = 0;
      size_t totalSize = blackBufferLength + redBufferLength;

      Serial.printf("Download BWR Progress: 0%% , 0/%d bytes", totalSize);
      while (https.connected() && bytesWritten < totalSize)
      {
        size_t remaining = totalSize - bytesWritten;
        readSize = stream->readBytes(buff, std::min(buffSize, remaining));
        if (readSize == 0)
        {
          break;
        }

        if (bytesWritten < (size_t)blackBufferLength)
        {
          size_t blackRemaining = blackBufferLength - bytesWritten;
          size_t toBlack = std::min(readSize, blackRemaining);
          blackFile.write(buff, toBlack);

          if (readSize > toBlack)
          {
            redFile.write(buff + toBlack, readSize - toBlack);
          }
        }
        else
        {
          redFile.write(buff, readSize);
        }

        bytesWritten += readSize;
        Serial.print("\r");
        Serial.print("                                                                     ");
        Serial.printf("\rDownload BWR Progress: %d%% , %d/%d bytes", (bytesWritten * 100) / totalSize, bytesWritten, totalSize);
      }
      Serial.println();

      blackFile.close();
      redFile.close();
    }
    else
    {
      File file = LittleFS.open(cachedFileName, "w");
      if (!file)
      {
        Serial.printf("Can not open file %s for writing\n", cachedFileName.c_str());
        https.end();
        LittleFS.end();
        return;
      }

      Serial.println("Start reading response body");
      uint8_t buff[128];
      size_t buffSize = sizeof(buff);
      size_t readSize;
      size_t readSizeTotal = 0;
      Serial.printf("Download %s Progress: 0%% , 0/%d bytes", cachedFileName.c_str(), contentLength);
      while (https.connected() && (readSize = stream->readBytes(buff, std::min(buffSize, (contentLength - readSizeTotal)))) > 0)
      {
        file.write(buff, readSize);
        readSizeTotal += readSize;
        Serial.print("\r");
        Serial.print("                                                                     ");
        Serial.printf("\rDownload %s Progress: %d%% , %d/%d bytes", cachedFileName.c_str(), (readSizeTotal * 100) / contentLength, readSizeTotal, contentLength);
      }
      Serial.println();
      file.close();
    }

    LittleFS.end();
    Serial.println("Read all response body");
    https.end();
    rememberSuccessfulEndpoint(candidate);

    strcpy(runningValue.todoLastModified, lastModified.c_str());
    delay(50);

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
