#include "display_290_driver.h"
#include <Arduino.h>

DisplayDriver290::DisplayDriver290()
{
    Serial.println("Initializing display 290...");
    pageByteLength = width * height / 8 / pages;
}

DisplayDriver290::~DisplayDriver290()
{
}

void DisplayDriver290::initialize()
{
    // 初始化 2.90 寸屏幕
}

void DisplayDriver290::clear()
{
}

void DisplayDriver290::drawPixel(uint16_t x, uint16_t y, DisplayColor color)
{
}

void DisplayDriver290::drawRectangle(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, DisplayColor color)
{
}

void DisplayDriver290::display(const std::function<void(BaseDisplayDriver &)> drawFunction)
{
    state = DISPLAY_DRIVER_DRAWING;
    sendDisplayDataWithColor(drawFunction, DISPLAY_COLOR_BLACK);
}

void DisplayDriver290::process()
{
    if (state == DISPLAY_DRIVER_DRAWING)
    {
        if (currentSendingPage < pages)
        {
            sendPageData();
            currentSendingPage++;
        }
        else
        {
            refresh();
            state = DISPLAY_DRIVER_IDLE;
        }
    }
}

void DisplayDriver290::testDisplay()
{
    // 显示测试图案
    display([](BaseDisplayDriver &driver)
            { driver.drawRectangle(10, 10, 286, 118, DISPLAY_COLOR_BLACK); });
}

void DisplayDriver290::sendDisplayDataWithColor(const std::function<void(BaseDisplayDriver &)> drawFunction, DisplayColor color)
{
    currentSendingColor = color;
    currentSendingPage = 0;
    drawFunction(*this);
}

void DisplayDriver290::refresh()
{
}

void DisplayDriver290::sendPageData()
{
}