
#include "display_290_driver.h"

DisplayDriver::DisplayDriver()
{
    width = DISPLAY_WIDTH;
    height = DISPLAY_HEIGHT;
    pages = DISPLAY_PAGES;
    pageByteLength = width * height / 8 / pages;
    currentPageData.resize(pageByteLength);
}

DisplayDriver::~DisplayDriver()
{
}

void DisplayDriver::initialize()
{
    // 初始化 2.90 寸屏幕
}

void DisplayDriver::clear()
{
}

void DisplayDriver::drawPixel(uint16_t x, uint16_t y, DisplayColor color)
{
}

void DisplayDriver::drawRectangle(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, DisplayColor color)
{
}

void DisplayDriver::display(const std::function<void(BaseDisplayDriver &)> drawFunction)
{
    state = DISPLAY_DRIVER_DRAWING;
    sendDisplayDataWithColor(drawFunction, DISPLAY_COLOR_BLACK);
}

void DisplayDriver::process()
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

void DisplayDriver::testDisplay()
{
    // 显示测试图案
    display([](BaseDisplayDriver &driver)
            { driver.drawRectangle(10, 10, 286, 118, DISPLAY_COLOR_BLACK); });
}

void DisplayDriver::sendDisplayDataWithColor(const std::function<void(BaseDisplayDriver &)> drawFunction, DisplayColor color)
{
    currentSendingColor = color;
    currentSendingPage = 0;
    drawFunction(*this);
}

void DisplayDriver::refresh()
{
}

void DisplayDriver::sendPageData()
{
}