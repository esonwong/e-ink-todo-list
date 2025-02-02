#include "display_750_driver.h"
#include "utility/EPD_7in5b_V2.h"

#define EPD_SCK_PIN 14
#define EPD_MOSI_PIN 13
#define EPD_CS_PIN 15
#define EPD_RST_PIN 2
#define EPD_DC_PIN 4
#define EPD_BUSY_PIN 5 // 0:busy, 1: idle

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
static void EPD_7IN5B_V2_Reset(void)
{
    DEV_Digital_Write(EPD_RST_PIN, 1);
    DEV_Delay_ms(200);
    DEV_Digital_Write(EPD_RST_PIN, 0);
    DEV_Delay_ms(2);
    DEV_Digital_Write(EPD_RST_PIN, 1);
    DEV_Delay_ms(200);
}

DisplayDriver750::DisplayDriver750()
{

    Serial.println("DisplayDriver constructor: 750");
    pageByteLength = (width * (height / pages)) / 8;
    // initialize();
}

DisplayDriver750::~DisplayDriver750()
{
}

void DisplayDriver750::initialize()
{

    Serial.println("Initializing display 750...");

    pinMode(EPD_BUSY_PIN, INPUT);
    pinMode(EPD_RST_PIN, OUTPUT);
    pinMode(EPD_DC_PIN, OUTPUT);

    pinMode(EPD_SCK_PIN, OUTPUT);
    pinMode(EPD_MOSI_PIN, OUTPUT);
    pinMode(EPD_CS_PIN, OUTPUT);

    digitalWrite(EPD_CS_PIN, HIGH);
    digitalWrite(EPD_SCK_PIN, LOW);

    // EPD_7IN5B_V2_Init();

    EPD_7IN5B_V2_Reset();

    EPD_7IN5B_V2_SendCommand(0x01); // POWER SETTING
    EPD_7IN5B_V2_SendData(0x07);
    EPD_7IN5B_V2_SendData(0x07); // VGH=20V,VGL=-20V
    EPD_7IN5B_V2_SendData(0x3f); // VDH=15V
    EPD_7IN5B_V2_SendData(0x3f); // VDL=-15V

    EPD_7IN5B_V2_SendCommand(0x04); // POWER ON
    delay(100);
    EPD_7IN5B_V2_Wait_Until_Idle();

    EPD_7IN5B_V2_SendCommand(0X00); // PANNEL SETTING
    EPD_7IN5B_V2_SendData(0x0F);    // KW-3f   KWR-2F	BWROTP 0f	BWOTP 1f

    EPD_7IN5B_V2_SendCommand(0x61); // tres
    EPD_7IN5B_V2_SendData(0x03);    // source 800
    EPD_7IN5B_V2_SendData(0x20);
    EPD_7IN5B_V2_SendData(0x01); // gate 480
    EPD_7IN5B_V2_SendData(0xE0);

    EPD_7IN5B_V2_SendCommand(0X15);
    EPD_7IN5B_V2_SendData(0x00);

    EPD_7IN5B_V2_SendCommand(0X50); // VCOM AND DATA INTERVAL SETTING
    EPD_7IN5B_V2_SendData(0x11);
    EPD_7IN5B_V2_SendData(0x07);

    EPD_7IN5B_V2_SendCommand(0X60); // TCON SETTING
    EPD_7IN5B_V2_SendData(0x22);

    EPD_7IN5B_V2_SendCommand(0x65); // Resolution setting
    EPD_7IN5B_V2_SendData(0x00);
    EPD_7IN5B_V2_SendData(0x00); // 800*480
    EPD_7IN5B_V2_SendData(0x00);
    EPD_7IN5B_V2_SendData(0x00);
}

void DisplayDriver750::clear()
{
    EPD_7IN5B_V2_Clear();
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

void DisplayDriver750::refresh()
{
    printf("refresh\r\n");
    EPD_7IN5B_V2_SendCommand(0x12); // DISPLAY REFRESH
    // DEV_Delay_ms(100);
    state = DISPLAY_DRIVER_IDLE;
}

void DisplayDriver750::display(const std::function<void(BaseDisplayDriver &)> drawFunction)
{
    printf("display\r\n");

    sendDisplayDataWithColor(drawFunction, DISPLAY_COLOR_BLACK);
    sendDisplayDataWithColor(drawFunction, DISPLAY_COLOR_RED);

    refresh();
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
        printf("currentSendingPage: %d\r\n", currentSendingPage);

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
                displayDriver.drawPixel(1, 1, DISPLAY_COLOR_BLACK);

                displayDriver.drawRectangle(10, 10, 30, 20, DISPLAY_COLOR_BLACK);

                displayDriver.drawRectangle(10, 30, 300, 50, DISPLAY_COLOR_BLACK);

                displayDriver.drawRectangle(10, 60, 790, 65, DISPLAY_COLOR_BLACK);

                displayDriver.drawRectangle(10, 75, 790, 80, DISPLAY_COLOR_BLACK);

                displayDriver.drawRectangle(10, 10, 60, 470, DISPLAY_COLOR_BLACK);

                displayDriver.drawRectangle(100, 100, 700, 380, DISPLAY_COLOR_RED);

                displayDriver.drawRectangle(10, 10, 20, 790, DISPLAY_COLOR_RED);

                // 一行 10x10 的黑色方块, 间隔 10
                for (int i = 0; i < 80; i++)
                {
                  displayDriver.drawRectangle(10 + i * 20, 10, 20 + i * 20, 20, DISPLAY_COLOR_BLACK);
                }

                // 一列 20x20 的黑色方块, 间隔 30
                for (int i = 0; i < 24; i++)
                {
                  displayDriver.drawRectangle(10, 30 + i * 30, 30, 50 + i * 30, DISPLAY_COLOR_BLACK);
                }

                displayDriver.drawRectangle(100, 100, 200, 200, DISPLAY_COLOR_BLACK); });
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