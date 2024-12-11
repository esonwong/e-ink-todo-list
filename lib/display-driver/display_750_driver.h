#pragma once

#include "base_display_driver.h"
#include "EPD.h"

class DisplayDriver : public BaseDisplayDriver
/**
 * @class DisplayDriver
 * @brief A driver class for handling display operations on a WaveShare EPD 7.5 inch 3-color display.
 */
{
public:
    /**
     * @brief Constructor for DisplayDriver.
     */
    DisplayDriver();

    /**
     * @brief Destructor for DisplayDriver.
     */
    ~DisplayDriver();

    /**
     * @brief Initializes the display.
     */
    void initialize() override;

    /**
     * @brief Clears the display.
     */
    void clear() override;

    /**
     * @brief Draws a pixel on the display.
     * @details Draws a pixel on the display page buffer.
     *
     * @param x X-coordinate of the pixel.
     * @param y Y-coordinate of the pixel.
     * @param DisplayColor Color of the pixel.
     */
    void drawPixel(uint16_t x, uint16_t y, DisplayColor color) override;
    void drawRectangle(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, DisplayColor color) override;

    /**
     * @brief Displays content on the EPD. Sends the all pages data to the display.
     *
     * @param drawFunction A function that takes a reference to DisplayDriver and draws on it.
     */
    void display(const std::function<void(BaseDisplayDriver &)> drawFunction) override;

    /**
     * @brief Processes the display update.
     */
    void process() override;
    void testDisplay() override;
    int width = 800;  // Width of the display.
    int height = 480; // Height of the display.
    int pages = 8;    // Number of pages in the display buffer.

private:
    void sendDisplayDataWithColor(const std::function<void(BaseDisplayDriver &)> drawFunction, DisplayColor color) override;

    /**
     * @brief Refreshes the display.
     */
    void refresh() override;

    /**
     * @brief Sends the image page data to the display.
     *
     * @param pageIndex Index of the page to send.
     */
    void sendPageData() override;
};