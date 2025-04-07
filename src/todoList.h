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

int updating = false;
String cachedFileName = "/todo.bitmap";

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

  const char *apiRoot = apiUrl.getValue();
  const char *apikey = apiKey.getValue();

  BearSSL::CertStore certStore;
  BearSSL::WiFiClientSecure client;

  // 添加WiFi连接状态日志
  Serial.println("WiFi连接状态:");
  Serial.printf("SSID: %s\n", WiFi.SSID().c_str());
  Serial.printf("IP地址: %s\n", WiFi.localIP().toString().c_str());
  Serial.printf("DNS服务器: %s\n", WiFi.dnsIP().toString().c_str());
  Serial.printf("网关: %s\n", WiFi.gatewayIP().toString().c_str());
  Serial.printf("信号强度(RSSI): %d dBm\n", WiFi.RSSI());

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
    Serial.println("Client set to insecure mode to bypass certificate validation");
  }
  else
  {
    client.setCertStore(&certStore);
  }

  // 增加TLS版本限制，尝试不同的TLS版本
  if (!client.setSSLVersion(BR_TLS12, BR_TLS12))
  {
    Serial.println("Failed to set TLS 1.2, falling back to default TLS version");
    client.setSSLVersion(BR_TLS10, BR_TLS12); // Allow fallback to TLS 1.0, 1.1, or 1.2
  }
  else
  {
    Serial.println("TLS version set to TLS 1.2 only");
  }

  client.setTimeout(30000);
  Serial.println("Client timeout set to 30000ms");

  String savedTodoLastModified = runningValue.todoLastModified;

  Serial.println("Last-Modified: " + savedTodoLastModified);

  String url = String(apiRoot) + "?width=" + String(display.width) + "&height=" + String(display.height);

  Serial.printf("GET %s\n", url.c_str());
  Serial.printf("If-Modified-Since: %s\n", savedTodoLastModified.c_str());
  Serial.printf("Authorization: Bearer %s\n", apikey);

  HTTPClient https;

  // 设置更详细的调试输出
  Serial.println("===== HTTP请求开始 =====");
  Serial.printf("连接到URL: %s\n", url.c_str());

  https.begin(client, url);

  Serial.println("请求头信息:");
  https.addHeader("If-Modified-Since", savedTodoLastModified);
  Serial.printf(" - If-Modified-Since: %s\n", savedTodoLastModified.c_str());

  https.addHeader("Authorization", "Bearer " + String(apikey));
  Serial.printf(" - Authorization: Bearer %s\n", apikey);

  https.addHeader("X-Device-Id", DeviceID);
  Serial.printf(" - X-Device-Id: %s\n", DeviceID.c_str());

#ifdef GIT_VERSION
  https.addHeader("X-Device-Firmware-Version", GIT_VERSION);
  Serial.printf(" - X-Device-Firmware-Version: %s\n", GIT_VERSION);
#endif

  const char *headerKeys[] = {"Content-Picture-Width", "Content-Picture-Height", "API-Version", "Last-Modified"};
  int headerKeysSize = sizeof(headerKeys) / sizeof(char *);
  https.collectHeaders(headerKeys, headerKeysSize);

  Serial.println("===== 开始发送GET请求 =====");
  unsigned long requestStartTime = millis();
  int httpCode = https.GET();
  unsigned long requestEndTime = millis();
  int contentLength = https.getSize();

  Serial.println("===== HTTP响应信息 =====");
  Serial.printf("请求耗时: %lu 毫秒\n", requestEndTime - requestStartTime);
  Serial.printf("HTTP状态码: %d\n", httpCode);
  Serial.printf("Content-Length: %d\n", contentLength);

  // 针对不同HTTP状态码提供更详细的诊断信息
  if (httpCode == -1)
  {
    Serial.println("\n===== HTTP连接错误详细诊断 =====");
    Serial.printf("HTTPS连接失败，错误: %s\n", https.errorToString(httpCode).c_str());

    // 获取SSL错误信息
    int sslError = client.getLastSSLError();
    Serial.printf("SSL错误代码: %d\n", sslError);

    // 尝试获取更详细的错误信息
    char error_buf[100];
    client.getLastSSLError(error_buf, sizeof(error_buf));
    Serial.printf("SSL详细错误: %s\n", error_buf);

    // SSL错误代码解释
    if (sslError > 0)
    {
      Serial.println("SSL错误说明:");
      switch (sslError)
      {
      case 1:
        Serial.println(" - X509_NOT_TRUSTED: 证书不受信任");
        break;
      case 2:
        Serial.println(" - X509_REQUEST_TIMEOUT: 证书验证超时");
        break;
      case 3:
        Serial.println(" - NOT_ENOUGH_MEMORY: 内存不足");
        break;
      case 4:
        Serial.println(" - ERR_RSA_DATA_LEN: RSA数据长度错误");
        break;
      default:
        Serial.println(" - 未知SSL错误");
      }
    }

    // 网络连接诊断
    Serial.println("\n网络连接诊断:");
    Serial.printf(" - WiFi连接状态: %s\n", WiFi.status() == WL_CONNECTED ? "已连接" : "未连接");
    Serial.printf(" - 信号强度(RSSI): %d dBm\n", WiFi.RSSI());
    Serial.printf(" - 目标URL: %s\n", url.c_str());
    Serial.printf(" - DNS解析: 尝试解析域名...\n");

    // 提取域名进行测试性DNS解析
    String domain = url;
    if (domain.startsWith("https://"))
    {
      domain = domain.substring(8);
    }
    else if (domain.startsWith("http://"))
    {
      domain = domain.substring(7);
    }
    int pathIndex = domain.indexOf('/');
    if (pathIndex > 0)
    {
      domain = domain.substring(0, pathIndex);
    }

    IPAddress ip;
    Serial.printf(" - 尝试解析域名: %s\n", domain.c_str());
    if (WiFi.hostByName(domain.c_str(), ip))
    {
      Serial.printf(" - DNS解析成功: %s\n", ip.toString().c_str());
    }
    else
    {
      Serial.println(" - DNS解析失败!");
    }

    // 尝试不同的连接方式（HTTP作为备用）
    Serial.println("\n尝试HTTP连接作为备用测试:");
    String httpUrl = url;
    if (httpUrl.startsWith("https://"))
    {
      httpUrl.replace("https://", "http://");
      Serial.printf("尝试HTTP URL: %s\n", httpUrl.c_str());

      WiFiClient plainClient;
      HTTPClient httpTest;
      httpTest.begin(plainClient, httpUrl);
      int httpTestCode = httpTest.GET();
      Serial.printf("HTTP测试状态码: %d\n", httpTestCode);
      httpTest.end();
    }

    https.end();
    LittleFS.end();
    return;
  }

  Serial.print("Free memory: ");
  Serial.println(ESP.getFreeHeap());

  if (httpCode == HTTP_CODE_NOT_MODIFIED)
  {
    Serial.println("Not Modified");
    https.end();
    LittleFS.end();
    return;
  }

  if (httpCode == 401)
  {
    Serial.printf("HTTPS GET failed, error: API Key authorization failed\n");
    https.end();
    LittleFS.end();
    show401();
    return;
  }

  if (httpCode == HTTP_CODE_NO_CONTENT)
  {
    Serial.println("No Content");
    https.end();
    LittleFS.end();
    showNoContent();
    return;
  }

  if (httpCode != HTTP_CODE_OK)
  {
    Serial.printf("HTTPS GET failed, error: %s\n", https.errorToString(httpCode).c_str());
    https.end();
    LittleFS.end();
    return;
  }

  if (contentLength <= 0)
  {
    Serial.println("Content-Length not set");
    https.end();
    LittleFS.end();
    return;
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

  String lastModified = https.header("Last-Modified");
  Serial.printf("Last-Modified: %s\n", lastModified.c_str());

  LittleFS.begin();
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
  LittleFS.end();

  Serial.println("Read all response body");
  https.end();

  strcpy(runningValue.todoLastModified, lastModified.c_str());
  delay(50);
  displayToScreen(cachedFileName, w, h, DISPLAY_COLOR_BLACK);
}
