#pragma once

#include "base_display_driver.h"
#include <Arduino.h>
#include <SPI.h>

class DisplayDriver750 : public BaseDisplayDriver
{
public:
    DisplayDriver750();
    ~DisplayDriver750();

    void rest();
    void spiWriteByte(uint8_t data);
    void spiWriteBytes(uint8_t *pData, uint32_t len);
    void waitUntilIdle();

    void sendCommand(uint8_t command);
    void sendData(uint8_t data);
    void sendDataWithLen(uint8_t *pData, uint32_t len);

    void initialize() override;
    void drawString(uint16_t x, uint16_t y, const char *text, sFONT *font, DisplayColor color);
    void drawChar(uint16_t x, uint16_t y, char c, sFONT *font, DisplayColor color);
    void clear() override;
    void drawPixel(uint16_t x, uint16_t y, DisplayColor color) override;
    void drawRectangle(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, DisplayColor color) override;
    void display(const std::function<void(BaseDisplayDriver &)> drawFunction) override;
    void process() override;
    void sleep();
    void testDisplay() override;
    void getStringBounds(const char *text, sFONT *font, uint16_t *x, uint16_t *y, uint16_t *w, uint16_t *h) override;
    static const uint16_t width = 800;  // Width of the display.
    static const uint16_t height = 480; // Height of the display.
    static const int pages = 8;         // Number of pages in the display.

private:
    SPISettings spiSettings = SPISettings(SPI_CLOCK_DIV4, MSBFIRST, SPI_MODE0);

protected:
    int currentSendingPage = 0;
    void sendDisplayDataWithColor(const std::function<void(BaseDisplayDriver &)> drawFunction, DisplayColor color) override;
    void refresh() override;
    void sendPageData() override;
};