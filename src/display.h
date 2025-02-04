#pragma once
#include "base_display_driver.h"

#ifdef E_INK_750
#include "display_750_driver.h"
DisplayDriver750 display = DisplayDriver750();
#endif

#ifdef E_INK_290
#include "display_290_driver.h"
DisplayDriver290 display = DisplayDriver290();
#endif

void initDisplay()
{
  Serial.println("Initializing display...");
  display.initialize();
  Serial.println("Display width: " + String(display.width));
  Serial.println("Display height: " + String(display.height));
  Serial.println("Display initialized");
}

void showLaunchScreen()
{

  display.display([](BaseDisplayDriver &displayDriver)
                  {
                    const char *name = "E-Ink Todo List";
                    const char *author = "by @Eson";

                    uint16_t y = displayDriver.drawString(displayDriver.width / 2, displayDriver.height / 5, name, &Font24, DISPLAY_COLOR_BLACK, TEXT_ALIGN_CENTER);
                    y = displayDriver.drawString(displayDriver.width / 2, y + 10, author, &Font20, DISPLAY_COLOR_BLACK, TEXT_ALIGN_CENTER);

#ifdef DEBUG
                    displayDriver.drawString(displayDriver.width / 2, y + 10, "DEBUG MODE", &Font20, DISPLAY_COLOR_RED, TEXT_ALIGN_CENTER);
#endif // DEBUG

#ifdef GIT_VERSION
                    const char *version = GIT_VERSION;
                    displayDriver.drawString(displayDriver.width - displayDriver.padding, displayDriver.height - displayDriver.padding - Font12.Height - 10, version, &Font12, DISPLAY_COLOR_BLACK, TEXT_ALIGN_RIGHT);
#endif // GIT_VERSION
                  });
}

void showTextOnScreenCenter(String text, sFONT *font = &Font24, DisplayColor color = DISPLAY_COLOR_BLACK)
{
  display.display([text, font, color](BaseDisplayDriver &displayDriver)
                  { displayDriver.drawString(displayDriver.width / 2, displayDriver.height / 2 - font->Height, text, font, color, TEXT_ALIGN_CENTER); });
}
