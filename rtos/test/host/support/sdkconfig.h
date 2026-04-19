/* Host-test stand-in for the IDF-generated sdkconfig.h.
 * The fallback config under test is injected by the Makefile via -D
 * (matching the device, where Kconfig values are baked in at compile time);
 * anything not injected falls back to the Kconfig defaults (""). */
#pragma once

#ifndef CONFIG_EINKTODO_FALLBACK_URL_1
#define CONFIG_EINKTODO_FALLBACK_URL_1 ""
#endif
#ifndef CONFIG_EINKTODO_FALLBACK_PIN_1
#define CONFIG_EINKTODO_FALLBACK_PIN_1 ""
#endif
#ifndef CONFIG_EINKTODO_FALLBACK_URL_2
#define CONFIG_EINKTODO_FALLBACK_URL_2 ""
#endif
#ifndef CONFIG_EINKTODO_FALLBACK_PIN_2
#define CONFIG_EINKTODO_FALLBACK_PIN_2 ""
#endif
