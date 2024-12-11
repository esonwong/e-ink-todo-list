
#include "base_display_driver.h"

BaseDisplayDriver::BaseDisplayDriver()
    : currentSendingPage(0),
      currentSendingColor(DISPLAY_COLOR_BLACK),
      state(DISPLAY_DRIVER_IDLE)
{
}

BaseDisplayDriver::~BaseDisplayDriver()
{
}