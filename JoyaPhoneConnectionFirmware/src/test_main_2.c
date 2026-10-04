#include <zephyr/kernel.h>

#include "nbm5100.h"
#include "drv2605.h"

int main(void)
{
    if (nbm5100_init() != 0) {
        goto done;
    }

    if (nbm5100_set_active(true) != 0) {
        goto done;
    }

    if (drv2605_init() != 0) {
        goto done;
    }

    k_msleep(3000);

    if (drv2605_set_rtp(50) != 0) {
        goto done;
    }

    k_msleep(700);

    if (drv2605_stop() != 0) {
        goto done;
    }

done:
    k_sleep(K_FOREVER);
    return 0;
}