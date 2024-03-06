#include <WiFiClientSecure.h>

#ifdef ESP32
#include <HTTPClient.h>
#endif

#ifdef ESP8266
#include <ESP8266HTTPClient.h>
#endif

#include <GxEPD2_BW.h>
#include "display.h"
#include "store.h"
#include "config.h"
#include "clock.h"
#include "network.h"

WiFiClientSecure client;
HTTPClient https;

int updating = false;

String urlencode(String str)
{
  String encodedString = "";
  char c;
  char code0;
  char code1;
  // char code2;
  for (int i = 0; i < str.length(); i++)
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

void downloadAndDrawTodo(String user = "eson", uint16_t color = GxEPD_BLACK)
{
  const char *apiRoot = apiUrl.getValue();
  const char *apikey = apiKey.getValue();

#ifdef ESP32
  client.setCACert(rootCACertificate);
#endif

#ifdef ESP8266
  X509List cert(rootCACertificate);
  client.setTrustAnchors(&cert);
  display.setRotation(1);
#endif

  String savedTodoLastModified = runningValue.todoLastModified;

  Serial.println("Last-Modified: " + savedTodoLastModified);

  Serial.printf("download and draw %s todo\n", user.c_str());

  String url = String(apiRoot) + "?width=" + String(display.width()) + "&height=" + String(display.height());

  Serial.printf("GET %s\n", url.c_str());
  Serial.printf("If-Modified-Since: %s\n", savedTodoLastModified.c_str());
  Serial.printf("Authorization: Bearer %s\n", apikey);

  https.begin(client, url);
  https.addHeader("If-Modified-Since", savedTodoLastModified);
  https.addHeader("Authorization", "Bearer " + String(apikey));

  const char *headerKeys[] = {"Content-Picture-Width", "Content-Picture-Height", "API-Version", "Last-Modified"};
  https.collectHeaders(headerKeys, 4);

  int httpCode = https.GET();
  int contentLength = https.getSize();

  Serial.printf("HTTPS GET: %d\n", httpCode);
  Serial.printf("Content-Length: %d\n", contentLength);
  if (httpCode == HTTP_CODE_NOT_MODIFIED)
  {
    Serial.println("Not Modified");
    return;
  }
  if (httpCode == HTTP_CODE_NO_CONTENT)
  {
    Serial.println("No Content");
    display.fillScreen(GxEPD_WHITE);
    display.setTextColor(GxEPD_BLACK);
    display.setCursor(0, 0);
    display.print("No Content");
    display.display();
    return;
  }
  if (httpCode != HTTP_CODE_OK)
  {
    Serial.printf("HTTPS GET failed, error: %s\n", https.errorToString(httpCode).c_str());
    return;
  }

  if (contentLength <= 0)
  {
    Serial.println("Content-Length not set");
    return;
  }

  Serial.printf("headers count %d\n", https.headers());
  for (int i = 0; i < https.headers(); i++)
  {
    String headerName = https.headerName(i);
    String headerValue = https.header(i);
    Serial.printf("header[%s]: %s\n", headerName.c_str(), headerValue.c_str());
  }

  uint8_t *buf = (uint8_t *)malloc(contentLength);

  WiFiClient *stream = https.getStreamPtr();
  int16_t w = https.header("Content-Picture-Width").toInt();
  int16_t h = https.header("Content-Picture-Height").toInt();

  String lastModified = https.header("Last-Modified");

  Serial.printf("Last-Modified: %s\n", lastModified.c_str());

  strcpy(runningValue.todoLastModified, lastModified.c_str());
  saveRunningValue(runningValue);

  delay(1000);

  Serial.println("Start reading response body");
  int readBytes = stream->readBytes(buf, contentLength);
  Serial.printf("Read %d bytes\n", readBytes);

  Serial.println("Read all response body");
  https.end();

  initDisplay();
  display.fillScreen(GxEPD_WHITE);

#ifdef E_INK_750
  do
  {
    display.drawBitmap(0, 0, buf, w, h, color);
    drawCurrentTime();
  } while (display.nextPage());
#else
  display.drawBitmap(0, 0, buf, w, h, color);
  display.display();
#endif

  free(buf);
  display.powerOff();

  delay(5000);

  Serial.println("draw bitmap done");
}
