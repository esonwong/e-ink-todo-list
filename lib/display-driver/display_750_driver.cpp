#include "display_750_driver.h"
#include "fonts.h"

#define EPD_SCK_PIN 14
#define EPD_MOSI_PIN 13
#define EPD_CS_PIN 15
#define EPD_RST_PIN 2
#define EPD_DC_PIN 4
#define EPD_BUSY_PIN 5 // 0:busy, 1: idle

const uint16_t BaseDisplayDriver::width = 800;
const uint16_t BaseDisplayDriver::height = 480;

DisplayDriver750::DisplayDriver750()
{

  Serial.println("DisplayDriver constructor: 750");
  pageByteLength = (width * (height / pages)) / 8;
  // initialize();
}

DisplayDriver750::~DisplayDriver750()
{
  if (currentPageData.size() > 0)
  {
    freeMemory();
  }
  Serial.println("DisplayDriver destructor: 750");
  SPI.end();
}

void DisplayDriver750::rest()
{
  Serial.println("Reset display");
  digitalWrite(EPD_RST_PIN, HIGH);
  delay(200);
  digitalWrite(EPD_RST_PIN, LOW);
  delay(2);
  digitalWrite(EPD_RST_PIN, HIGH);
  delay(200);
}

void DisplayDriver750::spiWriteByte(uint8_t data)
{
  SPI.beginTransaction(spiSettings);
  // digitalWrite(EPD_CS_PIN, LOW);

  // for (int i = 0; i < 8; i++)
  // {
  //   if ((data & 0x80) == 0)
  //     digitalWrite(EPD_MOSI_PIN, LOW);
  //   else
  //     digitalWrite(EPD_MOSI_PIN, HIGH);

  //   data <<= 1;
  //   digitalWrite(EPD_SCK_PIN, HIGH);
  //   digitalWrite(EPD_SCK_PIN, LOW);
  // }

  SPI.transfer(data);
  // digitalWrite(EPD_CS_PIN, HIGH);
  SPI.endTransaction();
}

void DisplayDriver750::spiWriteBytes(uint8_t *pData, uint32_t len)
{
  for (int i = 0; i < len; i++)
    spiWriteByte(pData[i]);
}

// /******************************************************************************
// function :	send command
// parameter:
//      command : Command register
// ******************************************************************************/
void DisplayDriver750::sendCommand(uint8_t command)
{
  digitalWrite(EPD_DC_PIN, LOW);
  digitalWrite(EPD_CS_PIN, LOW);
  spiWriteByte(command);
  digitalWrite(EPD_CS_PIN, HIGH);

  // SPI.beginTransaction(spiSettings);
  // digitalWrite(EPD_DC_PIN, LOW); // 命令模式
  // digitalWrite(EPD_CS_PIN, LOW);
  // SPI.transfer(command);
  // digitalWrite(EPD_CS_PIN, HIGH);
  // digitalWrite(EPD_DC_PIN, HIGH);
  // SPI.endTransaction();
}

void DisplayDriver750::sendData(uint8_t data)
{
  digitalWrite(EPD_DC_PIN, HIGH);
  digitalWrite(EPD_CS_PIN, LOW);
  spiWriteByte(data);
  digitalWrite(EPD_CS_PIN, HIGH);

  // SPI.beginTransaction(spiSettings);
  // digitalWrite(EPD_CS_PIN, LOW);
  // digitalWrite(EPD_DC_PIN, HIGH); // 数据模式
  // SPI.transfer(data);
  // digitalWrite(EPD_CS_PIN, HIGH);
  // SPI.endTransaction();
}

void DisplayDriver750::sendDataWithLen(uint8_t *pData, uint32_t len)
{
  digitalWrite(EPD_DC_PIN, 1);
  digitalWrite(EPD_CS_PIN, 0);
  spiWriteBytes(pData, len);
  digitalWrite(EPD_CS_PIN, 1);
}

void DisplayDriver750::initialize()
{

  Serial.println("Initializing display 750...");

  pinMode(EPD_BUSY_PIN, INPUT); // BUSY PIN
  pinMode(EPD_RST_PIN, OUTPUT); // RST PIN
  pinMode(EPD_DC_PIN, OUTPUT);  // DC PIN

  // Configure SPI
  // pinMode(EPD_SCK_PIN, OUTPUT);
  // pinMode(EPD_MOSI_PIN, OUTPUT);
  // pinMode(EPD_CS_PIN, OUTPUT);
  // digitalWrite(EPD_CS_PIN, HIGH);
  // digitalWrite(EPD_SCK_PIN, LOW);

  // spi
  SPI.setDataMode(SPI_MODE0);
  SPI.setBitOrder(MSBFIRST);
  SPI.setClockDivider(SPI_CLOCK_DIV4);
  SPI.begin();

  rest();

  sendCommand(0x01); // POWER SETTING
  sendData(0x07);
  sendData(0x17);
  sendData(0x3f);
  sendData(0x3f);

  sendCommand(0x04); // POWER ON
  delay(100);

  waitUntilIdle(); // waiting for the electronic paper IC to release the idle signal

  sendCommand(0X00); // PANNEL SETTING
  // KW-3f
  // KWR-2F
  // BWROTP 0f
  // BWOTP 1f
  sendData(0x0F);

  sendCommand(0x61); // Resolution setting
  sendData(0x03);    // source 800
  sendData(0x20);
  sendData(0x01); // gate 480
  sendData(0xE0);

  sendCommand(0X15); // Dual SPI mode
  sendData(0x00);

  sendCommand(0X60); // TCON SETTING
  sendData(0x22);

  sendCommand(0X50); // VCOM AND DATA INTERVAL SETTING
  sendData(0x11);    //
  sendData(0x07);    // 数据间隔设置保持不变
}

void DisplayDriver750::drawString(uint16_t x, uint16_t y, const char *text, sFONT *font, DisplayColor color)
{
  uint16_t currentX = x;
  uint16_t currentY = y;

  while (*text != '\0')
  {
    drawChar(currentX, currentY, *text, font, color);
    currentX += font->Width;
    text++;
  }
}

void DisplayDriver750::drawChar(uint16_t x, uint16_t y, char c, sFONT *font, DisplayColor color)
{
  u_int16_t Line, Column;                              // 行和列
  DisplayColor Color_Background = DISPLAY_COLOR_WHITE; // 背景色
  DisplayColor Color_Foreground = color;               // 前景色

  uint32_t Char_Offset = (c - ' ') * font->Height * (font->Width / 8 + (font->Width % 8 ? 1 : 0));
  const unsigned char *ptr = &font->table[Char_Offset];

  // Serial.print("Char: ");
  // Serial.print(c);
  // Serial.print(" Char_Offset: ");
  // Serial.println(Char_Offset);

  for (Line = 0; Line < font->Height; Line++)
  {
    for (Column = 0; Column < font->Width; Column++)
    {

      // To determine whether the font background color and screen background color is consistent
      if (DISPLAY_COLOR_WHITE == Color_Background)
      {
        // this process is to speed up the scan
        if (*ptr & (0x80 >> (Column % 8)))
          drawPixel(x + Column, y + Line, Color_Foreground);
      }
      else
      {
        if (*ptr & (0x80 >> (Column % 8)))
        {
          drawPixel(x + Column, y + Line, Color_Foreground);
        }
        else
        {
          drawPixel(x + Column, y + Line, Color_Background);
        }
      }
      // One pixel is 8 bits
      if (Column % 8 == 7)
        ptr++;
    } // Write a line
    if (font->Width % 8 != 0)
      ptr++;
  } // Write all
}

void DisplayDriver750::clear()
{
  uint16_t Width, Height;
  Width = (width % 8 == 0) ? (width / 8) : (width / 8 + 1);
  Height = height;

  u_int8_t image[width / 8] = {0x00};

  uint16_t i;
  for (i = 0; i < Width; i++)
  {
    image[i] = 0xff;
  }
  sendCommand(0x10);
  for (i = 0; i < Height; i++)
  {
    sendDataWithLen(image, Width);
    delay(1);
  }

  for (i = 0; i < Width; i++)
  {
    image[i] = 0x00;
  }
  sendCommand(0x13);
  for (i = 0; i < Height; i++)
  {
    sendDataWithLen(image, Width);
    delay(1);
  }
  refresh();
}

void DisplayDriver750::drawPixel(uint16_t x, uint16_t y, DisplayColor color)
{
  if (color != currentSendingColor)
    return; // 不是当前发送的颜色，直接返回

  if (x > width || y > height)
    return; // 超出屏幕范围，直接返回

  uint16_t pageStartY = height * currentSendingPage / pages;
  uint16_t pageEndY = pageStartY + height / pages;

  if (y < pageStartY || y >= pageEndY)
    return; // 不在当前发送的页中，直接返回

  // Serial.print("drawPixel: ");
  // Serial.print(x);
  // Serial.print(", ");
  // Serial.print(y);
  // Serial.print(", ");
  // Serial.println(color);

  uint16_t pageY = y - pageStartY;
  uint16_t pageX = x;

  uint16_t pageBitIndex = pageY * width + pageX;
  uint16_t pageByteIndex = pageBitIndex / 8;
  uint8_t pageBitOffset = pageBitIndex % 8;

  // black bit value: 0
  // red bit value: 1
  if (currentSendingColor == DISPLAY_COLOR_BLACK)
  {
    // black
    currentPageData[pageByteIndex] &= ~(1 << (7 - pageBitOffset));
  }
  else
  {
    // red
    currentPageData[pageByteIndex] |= (1 << (7 - pageBitOffset));
  }
}

void DisplayDriver750::drawRectangle(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, DisplayColor color)
{
  for (uint16_t x = x1; x < x2; x++)
  {
    for (uint16_t y = y1; y < y2; y++)
    {
      drawPixel(x, y, color);
    }
  }
}

void DisplayDriver750::waitUntilIdle()
{
  Serial.println("Wait utill e-Paper busy release");
  int busy = 0;
  while (true)
  {
    delay(100);
    busy = digitalRead(EPD_BUSY_PIN);
    if (busy == 1)
      break;
  }
  Serial.println("e-Paper busy released");
}

void DisplayDriver750::refresh()
{
  Serial.println("Refresh display");
  sendCommand(0x12); // DISPLAY REFRESH
  delay(100);        // !!!The delay here is necessary, 200uS at least!!!
  waitUntilIdle();   // waiting for the electronic paper IC to release the idle signal

  state = DISPLAY_DRIVER_IDLE;
}

void DisplayDriver750::display(const std::function<void(BaseDisplayDriver &)> drawFunction)
{

  initialize();
  state = DISPLAY_DRIVER_DRAWING;

  sendDisplayDataWithColor(drawFunction, DISPLAY_COLOR_BLACK);
  sendDisplayDataWithColor(drawFunction, DISPLAY_COLOR_RED);

  Serial.print("Free memory: ");
  Serial.println(ESP.getFreeHeap());
  Serial.print("currentPageData size: ");
  Serial.println(currentPageData.size());
  freeMemory();
  Serial.print("currentPageData size: ");
  Serial.println(currentPageData.size());
  Serial.print("Free memory: ");
  Serial.println(ESP.getFreeHeap());

  refresh();

  sleep();
  SPI.end();
}

void DisplayDriver750::sendDisplayDataWithColor(const std::function<void(BaseDisplayDriver &)> drawFunction, DisplayColor color)
{
  if (color == DISPLAY_COLOR_BLACK)
  {
    Serial.println("Send black data");
    // Send black data
    sendCommand(0x10);
    currentSendingColor = DISPLAY_COLOR_BLACK;
  }
  else
  {
    Serial.println("Send red data");
    // Send Red data
    sendCommand(0x13);
    currentSendingColor = DISPLAY_COLOR_RED;
  }

  for (currentSendingPage = 0; currentSendingPage < pages; currentSendingPage++)
  {
    // printf("currentSendingPage: %d\r\n", currentSendingPage);

    switch (currentSendingColor)
    {
    case DISPLAY_COLOR_BLACK:
      currentPageData.assign(pageByteLength, 0xFF); // fill with white
      break;
    default:
      currentPageData.assign(pageByteLength, 0x00); // fill with black
      break;
    }

    // draw on the page
    drawFunction(*this);

    // send page data
    sendPageData();

    // No need to manually free memory, vector will handle it
  }
}

void DisplayDriver750::testDisplay()
{
  display([](BaseDisplayDriver &displayDriver)
          {
  
            // displayDriver.drawPixel(1, 1, DISPLAY_COLOR_BLACK);

            displayDriver.drawRectangle(10, 10, 30, 20, DISPLAY_COLOR_BLACK);
            displayDriver.drawRectangle(10, 30, 300, 50, DISPLAY_COLOR_RED);
            displayDriver.drawChar(150, 150, 'A', &Font24, DISPLAY_COLOR_BLACK);
            displayDriver.drawChar(150 + Font24.Width, 150, 'B', &Font24, DISPLAY_COLOR_RED);
            displayDriver.drawString(10, 100, "Hello, World!", &Font24, DISPLAY_COLOR_BLACK);
            displayDriver.drawString(10, 200, "Hello, World!", &Font24, DISPLAY_COLOR_RED); });
}

void DisplayDriver750::sendPageData()
{
  printf("send %d page data\r\n", currentSendingPage);

  sendDataWithLen(currentPageData.data(), pageByteLength);

  delay(1);
}

void DisplayDriver750::process()
{
}

void DisplayDriver750::sleep()
{

  Serial.println("Sleep display");

  sendCommand(0X02); // power off

  waitUntilIdle(); // waiting for the electronic paper IC to release the idle signal

  delay(500);

  sendCommand(0X07); // deep sleep
  sendData(0xA5);
}

void DisplayDriver750::getStringBounds(const char *text, sFONT *font, uint16_t *x, uint16_t *y, uint16_t *w, uint16_t *h)
{
  if (text == nullptr || font == nullptr)
    return;

  size_t length = strlen(text);
  *w = length * font->Width; // 字符串总宽度 = 字符数 × 单个字符宽度
  *h = font->Height;         // 高度就是字体高度
}