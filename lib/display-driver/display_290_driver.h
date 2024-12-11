#pragma once

#include "base_display_driver.h"

class DisplayDriver290 : public BaseDisplayDriver
{
public:
    DisplayDriver290();
    ~DisplayDriver290();

    void initialize() override;
    void clear() override;
    void drawPixel(uint16_t x, uint16_t y, DisplayColor color) override;
    void drawRectangle(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, DisplayColor color) override;
    void display(const std::function<void(BaseDisplayDriver &)> drawFunction) override;
    void process() override;
    void testDisplay() override;
    static const int width = 296;  // Width of the display.
    static const int height = 128; // Height of the display.
    static const int pages = 4;    // Number of pages in the display.

private:
    int currentSendingPage = 0;
    void sendDisplayDataWithColor(const std::function<void(BaseDisplayDriver &)> drawFunction, DisplayColor color) override;
    void refresh() override;
    void sendPageData() override;
};