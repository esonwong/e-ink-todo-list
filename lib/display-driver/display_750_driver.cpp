#include "display_750_driver.h"
#include "fonts.h"
#include <SPI.h>

/**
 * data
 **/
#define UBYTE uint8_t
#define UWORD uint16_t
#define UDOUBLE uint32_t

#define GPIO_PIN_SET 1
#define GPIO_PIN_RESET 0

#define EPD_SCK_PIN 14
#define EPD_MOSI_PIN 13
#define EPD_CS_PIN 15
#define EPD_RST_PIN 2
#define EPD_DC_PIN 4
#define EPD_BUSY_PIN 5 // 0:busy, 1: idle

#define DEV_Digital_Write(_pin, _value) digitalWrite(_pin, _value == 0 ? LOW : HIGH)

/******************************************************************************
function:
            SPI read and write
******************************************************************************/
void DEV_SPI_WriteByte(UBYTE data)
{
  // SPI.beginTransaction(spi_settings);
  digitalWrite(EPD_CS_PIN, GPIO_PIN_RESET);

  for (int i = 0; i < 8; i++)
  {
    if ((data & 0x80) == 0)
      digitalWrite(EPD_MOSI_PIN, GPIO_PIN_RESET);
    else
      digitalWrite(EPD_MOSI_PIN, GPIO_PIN_SET);

    data <<= 1;
    digitalWrite(EPD_SCK_PIN, GPIO_PIN_SET);
    digitalWrite(EPD_SCK_PIN, GPIO_PIN_RESET);
  }

  // SPI.transfer(data);
  digitalWrite(EPD_CS_PIN, GPIO_PIN_SET);
  // SPI.endTransaction();
}

// /******************************************************************************
// function :	send command
// parameter:
//      Reg : Command register
// ******************************************************************************/
static void EPD_7IN5B_V2_SendCommand(UBYTE Reg)
{
  digitalWrite(EPD_DC_PIN, 0);
  digitalWrite(EPD_CS_PIN, 0);
  DEV_SPI_WriteByte(Reg);
  digitalWrite(EPD_CS_PIN, 1);
}

// /******************************************************************************
// function :	send data
// parameter:
//     Data : Write data
// ******************************************************************************/
static void EPD_7IN5B_V2_SendData(UBYTE Data)
{
  digitalWrite(EPD_DC_PIN, 1);
  digitalWrite(EPD_CS_PIN, 0);
  DEV_SPI_WriteByte(Data);
  digitalWrite(EPD_CS_PIN, 1);
}

void DEV_SPI_Write_nByte(UBYTE *pData, UDOUBLE len)
{
  for (int i = 0; i < len; i++)
    DEV_SPI_WriteByte(pData[i]);
}

static void EPD_7IN5B_V2_SendData2(UBYTE *pData, UDOUBLE len)
{
  digitalWrite(EPD_DC_PIN, 1);
  digitalWrite(EPD_CS_PIN, 0);
  DEV_SPI_Write_nByte(pData, len);
  digitalWrite(EPD_CS_PIN, 1);
}

/******************************************************************************
function :	Wait until the busy_pin goes LOW
parameter:
******************************************************************************/
void EPD_7IN5B_V2_Wait_Until_Idle(void)
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

/******************************************************************************
function :	Software reset
parameter:
******************************************************************************/
// static void EPD_7IN5B_V2_Reset(void)
// {
//   DEV_Digital_Write(EPD_RST_PIN, 1);
//   delay(200);
//   DEV_Digital_Write(EPD_RST_PIN, 0);
//   delay(2);
//   DEV_Digital_Write(EPD_RST_PIN, 1);
//   delay(200);
// }

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

void DisplayDriver750::initialize()
{

  Serial.println("Initializing display 750...");

  pinMode(EPD_BUSY_PIN, INPUT);
  pinMode(EPD_RST_PIN, OUTPUT);
  pinMode(EPD_DC_PIN, OUTPUT);

  // Configure SPI
  // pinMode(EPD_SCK_PIN, OUTPUT);
  // pinMode(EPD_MOSI_PIN, OUTPUT);
  // pinMode(EPD_CS_PIN, OUTPUT);
  // digitalWrite(EPD_CS_PIN, HIGH);
  // digitalWrite(EPD_SCK_PIN, LOW);

  SPI.begin();

  // EPD_7IN5B_V2_Init();

  rest();

  EPD_7IN5B_V2_SendCommand(0x01); // POWER SETTING
  EPD_7IN5B_V2_SendData(0x07);
  EPD_7IN5B_V2_SendData(0x17);
  EPD_7IN5B_V2_SendData(0x3f);
  EPD_7IN5B_V2_SendData(0x3f);

  EPD_7IN5B_V2_SendCommand(0x04); // POWER ON
  delay(100);

  EPD_7IN5B_V2_Wait_Until_Idle(); // waiting for the electronic paper IC to release the idle signal

  EPD_7IN5B_V2_SendCommand(0X00); // PANNEL SETTING
  // KW-3f
  // KWR-2F
  // BWROTP 0f
  // BWOTP 1f
  EPD_7IN5B_V2_SendData(0x0F);

  EPD_7IN5B_V2_SendCommand(0x61); // Resolution setting
  EPD_7IN5B_V2_SendData(0x03);    // source 800
  EPD_7IN5B_V2_SendData(0x20);
  EPD_7IN5B_V2_SendData(0x01); // gate 480
  EPD_7IN5B_V2_SendData(0xE0);

  EPD_7IN5B_V2_SendCommand(0X15); // Dual SPI mode
  EPD_7IN5B_V2_SendData(0x00);

  EPD_7IN5B_V2_SendCommand(0X60); // TCON SETTING
  EPD_7IN5B_V2_SendData(0x22);

  EPD_7IN5B_V2_SendCommand(0X50); // VCOM AND DATA INTERVAL SETTING
  EPD_7IN5B_V2_SendData(0x11);    // 黑边框
  EPD_7IN5B_V2_SendData(0x07);    // 数据间隔设置保持不变
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
  UWORD Line, Column;                                  // 行和列
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
  UWORD Width, Height;
  Width = (width % 8 == 0) ? (width / 8) : (width / 8 + 1);
  Height = height;

  UBYTE image[width / 8] = {0x00};

  UWORD i;
  for (i = 0; i < Width; i++)
  {
    image[i] = 0xff;
  }
  EPD_7IN5B_V2_SendCommand(0x10);
  for (i = 0; i < Height; i++)
  {
    EPD_7IN5B_V2_SendData2(image, Width);
    delay(1);
  }

  for (i = 0; i < Width; i++)
  {
    image[i] = 0x00;
  }
  EPD_7IN5B_V2_SendCommand(0x13);
  for (i = 0; i < Height; i++)
  {
    EPD_7IN5B_V2_SendData2(image, Width);
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

void DisplayDriver750::refresh()
{
  Serial.println("Refresh display");
  EPD_7IN5B_V2_SendCommand(0x12); // DISPLAY REFRESH
  delay(100);                     // !!!The delay here is necessary, 200uS at least!!!
  EPD_7IN5B_V2_Wait_Until_Idle(); // waiting for the electronic paper IC to release the idle signal

  state = DISPLAY_DRIVER_IDLE;
}

void DisplayDriver750::display(const std::function<void(BaseDisplayDriver &)> drawFunction)
{

  initialize();
  state = DISPLAY_DRIVER_DRAWING;

  sendDisplayDataWithColor(drawFunction, DISPLAY_COLOR_BLACK);
  sendDisplayDataWithColor(drawFunction, DISPLAY_COLOR_RED);

  refresh();

  sleep();
}

void DisplayDriver750::sendDisplayDataWithColor(const std::function<void(BaseDisplayDriver &)> drawFunction, DisplayColor color)
{
  if (color == DISPLAY_COLOR_BLACK)
  {
    Serial.println("Send black data");
    // Send black data
    EPD_7IN5B_V2_SendCommand(0x10);
    currentSendingColor = DISPLAY_COLOR_BLACK;
  }
  else
  {
    Serial.println("Send red data");
    // Send Red data
    EPD_7IN5B_V2_SendCommand(0x13);
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

  EPD_7IN5B_V2_SendData2(currentPageData.data(), pageByteLength);

  delay(1);
}

void DisplayDriver750::process()
{
}

void DisplayDriver750::sleep()
{

  Serial.println("Sleep display");

  EPD_7IN5B_V2_SendCommand(0X02); // power off

  EPD_7IN5B_V2_Wait_Until_Idle(); // waiting for the electronic paper IC to release the idle signal

  delay(500);

  EPD_7IN5B_V2_SendCommand(0X07); // deep sleep
  EPD_7IN5B_V2_SendData(0xA5);
}

void DisplayDriver750::getStringBounds(const char *text, sFONT *font, uint16_t *x, uint16_t *y, uint16_t *w, uint16_t *h)
{
  if (text == nullptr || font == nullptr)
    return;

  size_t length = strlen(text);
  *w = length * font->Width; // 字符串总宽度 = 字符数 × 单个字符宽度
  *h = font->Height;         // 高度就是字体高度
}