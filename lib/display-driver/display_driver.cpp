#include "display_driver.h"

#define EPD_SCK_PIN 14
#define EPD_MOSI_PIN 13
#define EPD_CS_PIN 15
#define EPD_RST_PIN 2
#define EPD_DC_PIN 4
#define EPD_BUSY_PIN 5 // 0:busy, 1: idle

/******************************************************************************
function :	send command
parameter:
     Reg : Command register
******************************************************************************/
static void EPD_7IN5B_V2_SendCommand(UBYTE Reg)
{
    digitalWrite(EPD_DC_PIN, 0);
    digitalWrite(EPD_CS_PIN, 0);
    DEV_SPI_WriteByte(Reg);
    digitalWrite(EPD_CS_PIN, 1);
}

/******************************************************************************
function :	send data
parameter:
    Data : Write data
******************************************************************************/
static void EPD_7IN5B_V2_SendData(UBYTE Data)
{
    digitalWrite(EPD_DC_PIN, 1);
    digitalWrite(EPD_CS_PIN, 0);
    DEV_SPI_WriteByte(Data);
    digitalWrite(EPD_CS_PIN, 1);
}

static void EPD_7IN5B_V2_SendData2(UBYTE *pData, UDOUBLE len)
{
    digitalWrite(EPD_DC_PIN, 1);
    digitalWrite(EPD_CS_PIN, 0);
    DEV_SPI_Write_nByte(pData, len);
    digitalWrite(EPD_CS_PIN, 1);
}

DisplayDriver::DisplayDriver()
{
    initialize();
}

DisplayDriver::~DisplayDriver()
{
}

void DisplayDriver::initialize()
{
    printf("e-Paper Init...\r\n");

    pinMode(EPD_BUSY_PIN, INPUT);
    pinMode(EPD_RST_PIN, OUTPUT);
    pinMode(EPD_DC_PIN, OUTPUT);

    pinMode(EPD_SCK_PIN, OUTPUT);
    pinMode(EPD_MOSI_PIN, OUTPUT);
    pinMode(EPD_CS_PIN, OUTPUT);

    digitalWrite(EPD_CS_PIN, HIGH);
    digitalWrite(EPD_SCK_PIN, LOW);

    EPD_7IN5B_V2_Init();
}

void DisplayDriver::clear()
{
    EPD_7IN5B_V2_Clear();
}

void DisplayDriver::drawPixel(uint16_t x, uint16_t y, DisplayColor color)
{
    if (x > width || y > height)
        return; // 超出屏幕范围，直接返回

    uint16_t pageBitLength = pageByteLength * 8;

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

void DisplayDriver::drawRectangle(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, DisplayColor color)
{
    for (uint16_t x = x1; x < x2; x++)
    {
        for (uint16_t y = y1; y < y2; y++)
        {
            drawPixel(x, y, color);
        }
    }
}

void DisplayDriver::refresh()
{
    printf("refresh\r\n");
    EPD_7IN5B_V2_SendCommand(0x12); // DISPLAY REFRESH
    DEV_Delay_ms(100);
    state = DISPLAY_DRIVER_IDLE;
}

void DisplayDriver::display(const std::function<void(DisplayDriver &)> drawFunction)
{
    printf("display\r\n");

    sendDisplayDataWithColor(drawFunction, DISPLAY_COLOR_BLACK);
    // sendDisplayDataWithColor(drawFunction, DISPLAY_COLOR_RED);

    refresh();
}

void DisplayDriver::sendDisplayDataWithColor(const std::function<void(DisplayDriver &)> drawFunction, DisplayColor color)
{
    printf("displayByColor\r\n");

    if (color == DISPLAY_COLOR_BLACK)
    {
        // Send black data
        EPD_7IN5B_V2_SendCommand(0x10);
        currentSendingColor = DISPLAY_COLOR_BLACK;
    }
    else
    {
        // Send Red data
        EPD_7IN5B_V2_SendCommand(0x13);
        currentSendingColor = DISPLAY_COLOR_RED;
    }

    for (currentSendingPage = 0; currentSendingPage < pages; currentSendingPage++)
    {
        printf("currentSendingPage: %d\r\n", currentSendingPage);
        currentPageData.assign(pageByteLength, 0xFF); // fill with white

        // draw on the page
        drawFunction(*this);

        // send page data
        sendPageData();

        // No need to manually free memory, vector will handle it
    }
}

void DisplayDriver::testDisplay()
{
    display([](DisplayDriver &displayDriver)
            {
                displayDriver.drawPixel(1, 1, DISPLAY_COLOR_BLACK);

                displayDriver.drawRectangle(10, 10, 30, 20, DISPLAY_COLOR_BLACK);

                displayDriver.drawRectangle(10, 30, 300, 50, DISPLAY_COLOR_BLACK);

                displayDriver.drawRectangle(10, 60, 790, 65, DISPLAY_COLOR_BLACK);

                displayDriver.drawRectangle(10, 75, 790, 80, DISPLAY_COLOR_BLACK);

                displayDriver.drawRectangle(10, 10, 60, 470, DISPLAY_COLOR_BLACK);

                displayDriver.drawRectangle(100, 100, 700, 380, DISPLAY_COLOR_RED);

                // displayDriver.drawRectangle(10, 10, 20, 790, DISPLAY_COLOR_BLACK);

                // // 一行 10x10 的黑色方块, 间隔 10
                // for (int i = 0; i < 80; i++)
                // {
                //   displayDriver.drawRectangle(10 + i * 20, 10, 20 + i * 20, 20, DISPLAY_COLOR_BLACK);
                // }

                // // 一列 20x20 的黑色方块, 间隔 30
                // for (int i = 0; i < 24; i++)
                // {
                //   displayDriver.drawRectangle(10, 30 + i * 30, 30, 50 + i * 30, DISPLAY_COLOR_BLACK);
                // }

                // displayDriver.drawRectangle(100, 100, 200, 200, DISPLAY_COLOR_BLACK);
            });
}

void DisplayDriver::sendPageData()
{
    printf("send %d page data\r\n", currentSendingPage);

    EPD_7IN5B_V2_SendData2(currentPageData.data(), pageByteLength);

    delay(1);
}

void DisplayDriver::process()
{
}