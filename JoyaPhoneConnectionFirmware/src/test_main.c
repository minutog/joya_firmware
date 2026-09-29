#include <zephyr/kernel.h>

#include "nbm5100.h"
#include "haptics.h"

int main(void)
{
    int nbm_err;
    int rdy_err;
    int haptics_err;

    nbm_err = nbm5100_init();
    if (nbm_err != 0) {
        return -1;
    }

    rdy_err = nbm_ready_init();
    if (rdy_err != 0) {
        return -1;
    }

    haptics_err = haptics_init();
    if (haptics_err != 0) {
        return -1;
    }

    haptics_set_available(true);

    k_msleep(1000);

    haptics_play_when_ready(HAPTICS_PATTERN_SETUP_MODE);

    k_sleep(K_FOREVER);

    return 0;
}