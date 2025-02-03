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
                    uint16_t x, y;
                    uint16_t nameW, nameH, authorW, authorH;
                    const char *name = "E-Ink Todo List";
                    const char *author = "by @Eson";

                    displayDriver.getStringBounds(name, &Font24, &x, &y, &nameW, &nameH);
                    displayDriver.getStringBounds(author, &Font20, &x, &y, &authorW, &authorH);

                    displayDriver.drawString((displayDriver.width - nameW) / 2, displayDriver.height / 2 - nameH - 80, name, &Font24, DISPLAY_COLOR_BLACK);
                    displayDriver.drawString((displayDriver.width - authorW) / 2, displayDriver.height / 2 + nameH / 2 - 80, author, &Font20, DISPLAY_COLOR_BLACK);

#ifdef GIT_VERSION
                    const char *version = GIT_VERSION;
                    uint16_t versionW, versionH;

                    displayDriver.getStringBounds(version, &Font12, &x, &y, &versionW, &versionH);

                    displayDriver.drawString(displayDriver.width - 10, displayDriver.height - versionH - 10, version, &Font12, DISPLAY_COLOR_BLACK, TEXT_ALIGN_RIGHT);
#endif
                  });
}

void showTextOnScreenCenter(const char *text, sFONT $font = Font24)
{
  display.display([text](BaseDisplayDriver &displayDriver)
                  {
                    uint16_t x, y;
                    uint16_t textW, textH;

                    displayDriver.getStringBounds(text, &Font24, &x, &y, &textW, &textH);

                    displayDriver.drawString(displayDriver.width / 2, (displayDriver.height - textH) / 2, text, &Font24, DISPLAY_COLOR_RED, TEXT_ALIGN_CENTER); });
}
