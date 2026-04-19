/* pcb_test — factory PCB-test mode (legacy ENABLE_PCB_TEST block in
 * src/main.cpp setup(), inventory item 7). Enabled by CONFIG_EINKTODO_PCB_TEST
 * (sdkconfig.defaults.pcb-test overlay); called from app_main (step 11).
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Gathers chip/network/store self-test info, posts DISP_PCB_INFO to the
 * display task, then blocks the caller for 5 minutes (legacy
 * delay(5 * 60 * 1000)). */
void pcb_test_run(void);

#ifdef __cplusplus
}
#endif
