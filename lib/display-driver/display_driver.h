#pragma once

// #include "GUI_Paint.h"
#include "EPD.h"

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

class DisplayDriver
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

    int width = 800;  ///< Width of the display.
    int height = 480; ///< Height of the display.
    int pages = 8;    ///< Number of pages in the display buffer.

    /**
     * @brief Initializes the display.
     */
    void initialize();

    /**
     * @brief Clears the display.
     */
    void clear();

    /**
     * @brief Draws a pixel on the display.
     * @details Draws a pixel on the display page buffer.
     *
     * @param x X-coordinate of the pixel.
     * @param y Y-coordinate of the pixel.
     * @param DisplayColor Color of the pixel.
     */
    void drawPixel(uint16_t x, uint16_t y, DisplayColor color);
    void drawRectangle(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, DisplayColor color);

    /**
     * @brief Displays content on the EPD. Sends the all pages data to the display.
     *
     * @param drawFunction A function that takes a reference to DisplayDriver and draws on it.
     */
    void display(const std::function<void(DisplayDriver &)> drawFunction);

    /**
     * @brief Processes the display update.
     */
    void process();
    void testDisplay();

private:
    std::vector<uint8_t> currentPageData;
    uint16_t pageByteLength = 800 * 480 / 8 / pages; ///< Length of a page in bytes.
    uint8_t currentSendingPage;
    DisplayColor currentSendingColor;

    DisplayState state; ///< Current state of the display.

    void sendDisplayDataWithColor(const std::function<void(DisplayDriver &)> drawFunction, DisplayColor color);

    /**
     * @brief Refreshes the display.
     */
    void refresh();

    /**
     * @brief Sends the image page data to the display.
     *
     * @param pageIndex Index of the page to send.
     */
    void sendPageData();
};