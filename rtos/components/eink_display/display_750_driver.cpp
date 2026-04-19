/* Ported from lib/display-driver/display_750_driver.cpp (R12, items 56-62, 64).
 * The init / refresh / sleep command sequences and page math are byte-identical
 * to the Arduino driver; only the Arduino call surface (SPI/digitalWrite/delay/
 * Serial/ESP.getFreeHeap) is replaced by hal_epd + ESP_LOG + esp_get_free_heap.
 * The Arduino `String` drawString overload is deleted. */
#include "display_750_driver.h"
#include "fonts.h"
#include "hal_epd.h"

#include <algorithm>

#include "esp_log.h"
#include "esp_system.h"

static const char *TAG = "epd750";

const uint16_t BaseDisplayDriver::width = 800;
const uint16_t BaseDisplayDriver::height = 480;

DisplayDriver750::DisplayDriver750()
{

  ESP_LOGI(TAG, "DisplayDriver constructor: 750");
  pageByteLength = (width * (height / pages)) / 8;
  // initialize();
}

DisplayDriver750::~DisplayDriver750()
{
  if (currentPageData.size() > 0)
  {
    freeMemory();
  }
  ESP_LOGI(TAG, "DisplayDriver destructor: 750");
  epd_spi_end();
}

void DisplayDriver750::rest()
{
  ESP_LOGI(TAG, "Reset display");
  epd_write(EPD_RST_PIN, 1);
  epd_delay_ms(200);
  epd_write(EPD_RST_PIN, 0);
  epd_delay_ms(2);
  epd_write(EPD_RST_PIN, 1);
  epd_delay_ms(200);
}

// /******************************************************************************
// function :	send command
// parameter:
//      command : Command register
// ******************************************************************************/
void DisplayDriver750::sendCommand(uint8_t command)
{
  epd_write(EPD_DC_PIN, 0);
  epd_write(EPD_CS_PIN, 0);
  epd_spi_send(&command, 1);
  epd_write(EPD_CS_PIN, 1);
  epd_write(EPD_DC_PIN, 1);
}

void DisplayDriver750::sendData(uint8_t data)
{
  epd_write(EPD_CS_PIN, 0);
  epd_spi_send(&data, 1);
  epd_write(EPD_CS_PIN, 1);
}

void DisplayDriver750::sendDataWithLen(uint8_t *pData, uint32_t len)
{
  epd_write(EPD_CS_PIN, 0);
  epd_spi_send(pData, len);
  epd_write(EPD_CS_PIN, 1);
}

void DisplayDriver750::initialize()
{

  ESP_LOGI(TAG, "Initializing display 750...");

  epd_pin_init(); // BUSY input, RST/DC outputs (legacy pinMode calls)

  // spi: MODE0, MSB first, ~legacy SPI_CLOCK_DIV4 base
  epd_spi_init();

  rest();

  sendCommand(0x01); // POWER SETTING
  sendData(0x07);
  sendData(0x17);
  sendData(0x3f);
  sendData(0x3f);

  sendCommand(0x04); // POWER ON
  epd_delay_ms(200);

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
  sendData(0x11);    // VCOM 设定
  // sendData(0x13); // 替代0x11，提升对比度
  sendData(0x07); // 数据间隔/边缘控制
}

uint16_t DisplayDriver750::drawString(uint16_t x, uint16_t y, const char *text, sFONT *font, DisplayColor color, TextAlign align)
{
  const char *textPtr = text;
  const char *lineStart = text;
  uint16_t currentY = y + padding; // 添加 padding

  while (true)
  {
    // 找到当前行的结束位置
    while (*textPtr != '\0' && *textPtr != '\n')
    {
      textPtr++;
    }

    // 计算当前行的长度
    size_t lineLength = textPtr - lineStart;
    uint16_t lineWidth = lineLength * font->Width;

    // 根据对齐方式计算起始X坐标
    uint16_t currentX = x;
    switch (align)
    {
    case TEXT_ALIGN_CENTER:
      currentX = x - (lineWidth / 2);
      break;
    case TEXT_ALIGN_RIGHT:
      currentX = x - lineWidth;
      break;
    case TEXT_ALIGN_LEFT:
    default:
      currentX += padding; // 添加 padding
      break;
    }

    // 绘制当前行的每个字符
    for (const char *p = lineStart; p < textPtr; p++)
    {
      drawChar(currentX, currentY, *p, font, color);
      currentX += font->Width;
    }

    // 如果到达字符串结尾，退出循环
    if (*textPtr == '\0')
    {
      break;
    }

    // 移动到下一行
    textPtr++;
    lineStart = textPtr;
    currentY += font->Height;
  }

  // 返回最后一行的 Y 坐标加上字体高度
  return currentY + font->Height;
}

void DisplayDriver750::drawChar(uint16_t x, uint16_t y, char c, sFONT *font, DisplayColor color)
{
  uint16_t Line, Column;                               // 行和列
  DisplayColor Color_Background = DISPLAY_COLOR_WHITE;  // 背景色
  DisplayColor Color_Foreground = color;               // 前景色

  uint32_t Char_Offset = (c - ' ') * font->Height * (font->Width / 8 + (font->Width % 8 ? 1 : 0));
  const uint8_t *ptr = &font->table[Char_Offset];

  for (Line = 0; Line < font->Height; Line++)
  {
    for (Column = 0; Column < font->Width; Column++)
    {
      uint8_t data = pgm_read_byte(ptr);
      if (DISPLAY_COLOR_WHITE == Color_Background)
      {
        if (data & (0x80 >> (Column % 8)))
          drawPixel(x + Column, y + Line, Color_Foreground);
      }
      else
      {
        if (data & (0x80 >> (Column % 8)))
          drawPixel(x + Column, y + Line, Color_Foreground);
        else
          drawPixel(x + Column, y + Line, Color_Background);
      }

      if (Column % 8 == 7)
        ptr++;
    }
    if (font->Width % 8 != 0)
      ptr++;
  }
}

void DisplayDriver750::clear()
{
  display([](BaseDisplayDriver &displayDriver)
          { displayDriver.drawRectangle(0, 0, displayDriver.width, displayDriver.height, DISPLAY_COLOR_WHITE); });
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
  ESP_LOGI(TAG, "Wait utill e-Paper busy release");
  epd_delay_ms(200); // !!!The delay here is necessary, 200uS at least!!!
  int busy = 0;
  while (true)
  {
    epd_delay_ms(100);
    busy = epd_read(EPD_BUSY_PIN);
    if (busy == 1)
      break;
  }
  ESP_LOGI(TAG, "e-Paper busy released");
}

void DisplayDriver750::refresh()
{
  ESP_LOGI(TAG, "Refresh display");
  sendCommand(0x12); // DISPLAY REFRESH
  waitUntilIdle();   // waiting for the electronic paper IC to release the idle signal
  epd_delay_ms(300); // wait for the display to refresh
  state = DISPLAY_DRIVER_IDLE;
}

void DisplayDriver750::display(const std::function<void(BaseDisplayDriver &)> drawFunction)
{

  initialize();
  state = DISPLAY_DRIVER_DRAWING;

  sendDisplayDataWithColor(drawFunction, DISPLAY_COLOR_BLACK);
  sendDisplayDataWithColor(drawFunction, DISPLAY_COLOR_RED);

  ESP_LOGI(TAG, "Free memory: %u", (unsigned)esp_get_free_heap_size());
  ESP_LOGI(TAG, "currentPageData size: %u", (unsigned)currentPageData.size());
  freeMemory();
  ESP_LOGI(TAG, "currentPageData size: %u", (unsigned)currentPageData.size());
  ESP_LOGI(TAG, "Free memory: %u", (unsigned)esp_get_free_heap_size());

  refresh();

  sleep();
  epd_spi_end();
}

void DisplayDriver750::sendDisplayDataWithColor(const std::function<void(BaseDisplayDriver &)> drawFunction, DisplayColor color)
{
  if (color == DISPLAY_COLOR_BLACK)
  {
    ESP_LOGI(TAG, "Send black data");
    // Send black data
    sendCommand(0x10);
    currentSendingColor = DISPLAY_COLOR_BLACK;
  }
  else
  {
    ESP_LOGI(TAG, "Send red data");
    // Send Red data
    sendCommand(0x13);
    currentSendingColor = DISPLAY_COLOR_RED;
  }

  for (currentSendingPage = 0; currentSendingPage < pages; currentSendingPage++)
  {
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

            displayDriver.drawRectangle(10, 10, 30, 20, DISPLAY_COLOR_BLACK);
            displayDriver.drawRectangle(10, 30, 300, 50, DISPLAY_COLOR_RED);
            displayDriver.drawChar(150, 150, 'A', &Font24, DISPLAY_COLOR_BLACK);
            displayDriver.drawChar(150 + Font24.Width, 150, 'B', &Font24, DISPLAY_COLOR_RED);
            displayDriver.drawString(10, 100, "Hello, World!", &Font24, DISPLAY_COLOR_BLACK);
            displayDriver.drawString(10, 200, "Hello, World!", &Font24, DISPLAY_COLOR_RED); });
}

void DisplayDriver750::sendPageData()
{
  sendDataWithLen(currentPageData.data(), pageByteLength);
  epd_delay_ms(1);
}

void DisplayDriver750::process()
{
}

void DisplayDriver750::sleep()
{

  ESP_LOGI(TAG, "Sleep display");

  sendCommand(0X02); // power off

  waitUntilIdle(); // waiting for the electronic paper IC to release the idle signal

  epd_delay_ms(500);

  sendCommand(0X07); // deep sleep
  sendData(0xA5);
}

void DisplayDriver750::getStringBounds(const char *text, sFONT *font, uint16_t *x, uint16_t *y, uint16_t *w, uint16_t *h)
{
  if (text == nullptr || font == nullptr)
    return;

  uint16_t maxWidth = 0;     // 最大行宽
  uint16_t currentWidth = 0; // 当前行宽
  uint16_t lines = 1;        // 行数

  const char *p = text;
  while (*p != '\0')
  {
    if (*p == '\n')
    {
      lines++;
      maxWidth = std::max(maxWidth, currentWidth);
      currentWidth = 0;
    }
    else
    {
      currentWidth += font->Width;
    }
    p++;
  }

  // 检查最后一行
  maxWidth = std::max(maxWidth, currentWidth);

  *w = maxWidth;             // 最大行宽
  *h = font->Height * lines; // 总高度 = 字体高度 × 行数

  // 对于居中对齐，需要调整x坐标
  if (*x != 0)
  {
    *x -= maxWidth / 2;
  }
}
