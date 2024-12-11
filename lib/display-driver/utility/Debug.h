#pragma once

#include <Arduino.h>

#define USE_DEBUG 1
#if USE_DEBUG
#define Debug(__info) Serial.print(__info)
#else
#define Debug(__info)
#endif
