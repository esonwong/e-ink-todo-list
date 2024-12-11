
#pragma once

#include "base_display_driver.h"

class DisplayDriver : public BaseDisplayDriver
{
public:
    DisplayDriver();
    ~DisplayDriver();

    void initialize() override;
    void clear() override;
    void drawPixel(uint16_t x, uint16_t y, DisplayColor color) override;
    void drawRectangle(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, DisplayColor color) override;
    void display(const std::function<void(BaseDisplayDriver &)> drawFunction) override;
    void process() override;
    void testDisplay() override;

protected:
    void sendDisplayDataWithColor(const std::function<void(BaseDisplayDriver &)> drawFunction, DisplayColor color) override;
    void refresh() override;
    void sendPageData() override;

private:
    static const int DISPLAY_WIDTH = 296;
    static const int DISPLAY_HEIGHT = 128;
    static const int DISPLAY_PAGES = 4;
};