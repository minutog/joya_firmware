#include "nbm5100.h"

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <errno.h>
#include "app_state.h"


static const struct device *nbm_i2c = DEVICE_DT_GET(DT_NODELABEL(i2c0));
static const struct gpio_dt_spec nbm_ready = GPIO_DT_SPEC_GET(DT_ALIAS(nbm_ready), gpios);

static struct gpio_callback nbm_ready_cb_data;
static volatile bool nbm_ready_flag = false;

bool is_nbm_ready(void) {
    return nbm_ready_flag;
}

static int nbm5100_configure_mode(void) {
    // Implement the configuration of the NBM5100 mode here
    // This is a placeholder for the actual implementation
    return 0;
}

static void nbm_ready_isr(const struct device *port, struct gpio_callback *cb, uint32_t pins) {
    ARG_UNUSED(port);
    ARG_UNUSED(cb);
    ARG_UNUSED(pins);

    int level = gpio_pin_get_dt(&nbm_ready);
    nbm_ready_flag = (level > 0);

    add_event(EV_NBM_READY);
}


int nbm5100_init(void) {
    int err;
    if (!device_is_ready(nbm_i2c)) {
        // Error treatment
        return -ENODEV;
    }

    err = nbm5100_configure_mode();
    if (err) {
        return err;
    }

    return 0;
}

int nbm_ready_init(void) {
    int ret;

    if (!gpio_is_ready_dt(&nbm_ready)) {
        return -ENODEV;
    }

    ret = gpio_pin_configure_dt(&nbm_ready, GPIO_INPUT);
    if (ret) {
        return ret;
    }

    ret = gpio_pin_interrupt_configure_dt(&nbm_ready, GPIO_INT_EDGE_BOTH);
    if (ret) {
        return ret;
    }

    gpio_init_callback(&nbm_ready_cb_data, nbm_ready_isr, BIT(nbm_ready.pin));

    ret = gpio_add_callback(nbm_ready.port, &nbm_ready_cb_data);

    return ret;
}


int nbm5100_read_reg(uint8_t reg, uint8_t *value) {
    if (value == NULL) {
        return -EINVAL;
    }

    int err = i2c_write_read(nbm_i2c, NBM5100_I2C_ADDR, &reg, sizeof(reg), value, sizeof(*value));

    if (err) {
        // Error treatment
        return err;
    }

    return 0;
}


int nbm5100_write_reg(uint8_t reg, uint8_t value)
{
    uint8_t buf[2] = {reg, value};

    int err = i2c_write(nbm_i2c, buf, sizeof(buf), NBM5100_I2C_ADDR);

    if (err) {
        // Error treatment
        return err;
    }

    return 0;
}