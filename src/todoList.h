#include <algorithm>
#include <GxEPD2_BW.h>
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
  initDisplay();
  do
  {
    display.fillScreen(GxEPD_WHITE);
    display.setTextColor(GxEPD_BLACK);
    display.setCursor(0, 0);
    display.println("API Key authorization failed!");
    display.println("Please long press the button to enter config mode!");
  } while (display.nextPage());
}

void displayToScreen(String file = cachedFileName, uint16_t w = 0, uint16_t h = 0, uint16_t color = GxEPD_BLACK)
{
  initDisplay();
  display.fillScreen(GxEPD_WHITE);

  LittleFS.begin();
  File readFile = LittleFS.open(file, "r");
  if (!readFile)
  {
    Serial.println("Failed to open file for reading");
    return;
  }

  do
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
            display.drawPixel(x, y, color);
          }
          x++;
        }
      }
    }
    drawCurrentTime();
  } while (display.nextPage());
  readFile.close();
  LittleFS.end();
  display.powerOff();
  Serial.println("draw bitmap done");
}

void downloadAndDrawTodo()
{

  runningValue.lastCheck = time(nullptr);

  if (wifiManager.getConfigPortalActive())
  {
    Serial.println("Config portal active");
    return;
  }

  const char *apiRoot = apiUrl.getValue();
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

  String savedTodoLastModified = runningValue.todoLastModified;

  Serial.println("Last-Modified: " + savedTodoLastModified);

  String url = String(apiRoot) + "?width=" + String(display.width()) + "&height=" + String(display.height());

  Serial.printf("GET %s\n", url.c_str());
  Serial.printf("If-Modified-Since: %s\n", savedTodoLastModified.c_str());
  Serial.printf("Authorization: Bearer %s\n", apikey);

  HTTPClient https;
  https.begin(client, url);
  https.addHeader("If-Modified-Since", savedTodoLastModified);
  https.addHeader("Authorization", "Bearer " + String(apikey));
  https.addHeader("X-Device-Id", DeviceID);
#ifdef GIT_VERSION
  https.addHeader("X-Device-Firmware-Version", GIT_VERSION);
#endif

  const char *headerKeys[] = {"Content-Picture-Width", "Content-Picture-Height", "API-Version", "Last-Modified"};
  int headerKeysSize = sizeof(headerKeys) / sizeof(char *);
  https.collectHeaders(headerKeys, headerKeysSize);

  int httpCode = https.GET();
  int contentLength = https.getSize();

  Serial.printf("HTTPS GET: %d\n", httpCode);
  Serial.printf("Content-Length: %d\n", contentLength);
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

  File file = LittleFS.open(cachedFileName, "w");
  if (!file)
  {
    Serial.println("Failed to open file for writing");
    https.end();
    LittleFS.end();
    return;
  }

  Serial.println("Start reading response body");
  int bitsTotal = w * h;
  uint8_t buff[128];
  size_t buffSize = sizeof(buff);
  size_t readSize;
  size_t readSizeTotal = 0;
  while (https.connected() && (readSize = stream->readBytes(buff, std::min(buffSize, bitsTotal - readSizeTotal))) > 0)
  {
    file.write(buff, readSize);
    readSizeTotal += readSize;
  }
  file.close();
  LittleFS.end();

  Serial.println("Read all response body");
  https.end();

  strcpy(runningValue.todoLastModified, lastModified.c_str());

  displayToScreen(cachedFileName, w, h, GxEPD_BLACK);
}
