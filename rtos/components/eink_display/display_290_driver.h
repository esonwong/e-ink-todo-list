#pragma once

#include "base_display_driver.h"

/* Ported from lib/display-driver/display_290_driver.h (byte-identical; the 290
 * driver is a stub in the legacy tree — render/refresh are no-ops). */

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

    // The 290 stub provides no text rendering in the legacy tree; supply
    // no-op overrides so the abstract base can be instantiated.
    void drawChar(uint16_t x, uint16_t y, char c, sFONT *font, DisplayColor color) override;
    uint16_t drawString(uint16_t x, uint16_t y, const char *text, sFONT *font, DisplayColor color, TextAlign align = TEXT_ALIGN_LEFT) override;
    void getStringBounds(const char *text, sFONT *font, uint16_t *x, uint16_t *y, uint16_t *w, uint16_t *h) override;

    static const uint16_t width = 296;  // Width of the display.
    static const uint16_t height = 128; // Height of the display.
    static const int pages = 4;         // Number of pages in the display.

private:
    int currentSendingPage = 0;
    void sendDisplayDataWithColor(const std::function<void(BaseDisplayDriver &)> drawFunction, DisplayColor color) override;
    void refresh() override;
    void sendPageData() override;
};
