#pragma once
#include <cstdint>
#include <vector>
#include <functional>
#include "fonts.h"

// state of the display
enum DisplayState
{
    DISPLAY_DRIVER_IDLE,
    DISPLAY_DRIVER_DRAWING,
};

enum DisplayColor
{
    DISPLAY_COLOR_BLACK,
    DISPLAY_COLOR_RED,
    DISPLAY_COLOR_WHITE,
};

class BaseDisplayDriver
/**
 * @class BaseDisplayDriver
 * @brief A base driver class for handling display operations.
 */
{
public:
    /**
     * @brief Constructor for BaseDisplayDriver.
     */
    BaseDisplayDriver();

    /**
     * @brief Destructor for BaseDisplayDriver.
     */
    virtual ~BaseDisplayDriver();

    /**
     * @brief Initializes the display.
     */
    virtual void initialize() = 0;

    /**
     * @brief Clears the display.
     */
    virtual void clear() = 0;

    /**
     * @brief Draws a pixel on the display.
     * @details Draws a pixel on the display page buffer.
     *
     * @param x X-coordinate of the pixel.
     * @param y Y-coordinate of the pixel.
     * @param DisplayColor Color of the pixel.
     */
    virtual void drawPixel(uint16_t x, uint16_t y, DisplayColor color) = 0;
    virtual void drawRectangle(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, DisplayColor color) = 0;

    /**
     * @brief Displays content on the EPD. Sends the all pages data to the display.
     *
     * @param drawFunction A function that takes a reference to BaseDisplayDriver and draws on it.
     */
    virtual void display(const std::function<void(BaseDisplayDriver &)> drawFunction) = 0;

    /**
     * @brief Processes the display update.
     */
    virtual void process() = 0;
    virtual void testDisplay() = 0;

    virtual void drawChar(uint16_t x, uint16_t y, char c, sFONT *font, DisplayColor color) = 0;
    virtual void drawString(uint16_t x, uint16_t y, const char *text, sFONT *font, DisplayColor color) = 0;

    static const int width;  // Width of the display.
    static const int height; // Height of the display.
    static const int pages;  // Number of pages in the display buffer.

protected:
    std::vector<uint8_t> currentPageData;
    uint16_t pageByteLength;
    uint8_t currentSendingPage;
    DisplayColor currentSendingColor;

    DisplayState state; ///< Current state of the display.

    virtual void sendDisplayDataWithColor(const std::function<void(BaseDisplayDriver &)> drawFunction, DisplayColor color) = 0;

    /**
     * @brief Refreshes the display.
     */
    virtual void refresh() = 0;

    /**
     * @brief Sends the image page data to the display.
     *
     * @param pageIndex Index of the page to send.
     */
    virtual void sendPageData() = 0;
};