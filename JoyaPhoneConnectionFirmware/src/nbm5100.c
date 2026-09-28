#include "nbm5100.h"

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <errno.h>


static const struct device *nbm_i2c = DEVICE_DT_GET(DT_NODELABEL(i2c0));
static const struct gpio_dt_spec nbm_ready = GPIO_DT_SPEC_GET(DT_ALIAS(nbm_ready), gpios);

static struct gpio_callback nbm_ready_cb_data;
static atomic_t nbm_ready_flag = ATOMIC_INIT(0);



bool is_nbm_ready(void) {
    return atomic_get(&nbm_ready_flag) != 0;
}

static int nbm5100_configure_mode(void)
{
    int ret;

    // set1: vfix3, vfix2, vfix1, vfix0, vset3, vset2, vset1, vset0
    uint8_t set1 =
        FIELD_PREP(NBM5100_SET1_VFIX_MASK, NBM5100_VFIX_3V84) |
        FIELD_PREP(NBM5100_SET1_VSET_MASK, NBM5100_VSET_3V4);

    // set2: ich2, ich1, ich0, vdhhiz, -, vmin2, vmin1, vmin0 
    uint8_t set2 =
        FIELD_PREP(NBM5100_SET2_ICH_MASK, NBM5100_ICH_4MA) |
        NBM5100_SET2_VDHHIZ |
        FIELD_PREP(NBM5100_SET2_VMIN_MASK, NBM5100_VMIN_3V2);

    /* Disable optimizer: profile = 0 */
    ret = nbm5100_write_reg(NBM5100_REG_PROFILE, 0x00);
    if (ret < 0) {
        return ret;
    }

    /* VFIX = 3.84 V, VSET = 3.4 V */
    ret = nbm5100_write_reg(NBM5100_REG_SET1, set1);
    if (ret < 0) {
        return ret;
    }

    /* ICH = 4 mA, VDHHIZ = 1, VMIN = 3.2 V */
    ret = nbm5100_write_reg(NBM5100_REG_SET2, set2);
    if (ret < 0) {
        return ret;
    }

    /* Auto mode disabled */
    ret = nbm5100_write_reg(NBM5100_REG_SET3, 0x00);
    if (ret < 0) {
        return ret;
    }

    /*
     * VCAPMAX = 4.95 V (valor binario 0)
     * Capacitor balancing disabled
     */
    // set4: bal_mode1, bal_mode0, enbal, vcapmax, -,-,-,-
    ret = nbm5100_write_reg(NBM5100_REG_SET4, 0x00);
    if (ret < 0) {
        return ret;
    }

    /*
     * Continuous mode:
     * ACT = 0
     * ECM = 1
     * EOD = 0
     */
    ret = nbm5100_write_reg(NBM5100_REG_COMMAND, NBM5100_CMD_ECM);
    if (ret < 0) {
        return ret;
    }

    return 0;
}

static void nbm_ready_isr(const struct device *port, struct gpio_callback *cb, uint32_t pins) {
    ARG_UNUSED(port);
    ARG_UNUSED(cb);
    ARG_UNUSED(pins);

    int level = gpio_pin_get_dt(&nbm_ready);
    if (level < 0) {
        return;
    }

    atomic_set(&nbm_ready_flag, level > 0 ? 1 : 0);
}


int nbm5100_init(void) {
    int err;
    if (!device_is_ready(nbm_i2c)) {
        // Error treatment
        return -ENODEV;
    }

    k_msleep(20); // datasheet requirement

    /* Recover the shared bus before the first NBM configuration transfer. */
    err = i2c_recover_bus(nbm_i2c);
    if (err < 0 && err != -ENOSYS) {
        return err;
    }

    err = nbm5100_configure_mode();
    if (err) {
        return err;
    }

    return 0;
}

int nbm_ready_init(void)
{
    int ret;
    unsigned int key;

    if (!gpio_is_ready_dt(&nbm_ready)) {
        return -ENODEV;
    }

    ret = gpio_pin_configure_dt(&nbm_ready, GPIO_INPUT);
    if (ret) {
        return ret;
    }

    gpio_init_callback(&nbm_ready_cb_data, nbm_ready_isr, BIT(nbm_ready.pin));

    ret = gpio_add_callback(nbm_ready.port, &nbm_ready_cb_data);
    if (ret) {
        return ret;
    }

    ret = gpio_pin_interrupt_configure_dt(&nbm_ready, GPIO_INT_EDGE_BOTH);
    if (ret) {
        (void)gpio_remove_callback(nbm_ready.port, &nbm_ready_cb_data);
        return ret;
    }

    /*
     * Read the level after enabling the interrupt so an edge cannot be lost
     * between the initial sample and interrupt setup. Keep local interrupts
     * locked until the sampled value is stored: an edge occurring in this
     * small window remains pending and the ISR refreshes the value afterward.
     */
    key = irq_lock();
    ret = gpio_pin_get_dt(&nbm_ready);
    if (ret >= 0) {
        atomic_set(&nbm_ready_flag, ret > 0 ? 1 : 0);
    }
    irq_unlock(key);

    if (ret < 0) {
        (void)gpio_pin_interrupt_configure_dt(&nbm_ready, GPIO_INT_DISABLE);
        (void)gpio_remove_callback(nbm_ready.port, &nbm_ready_cb_data);
        return ret;
    }

    return 0;
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
    int err;

    for (int attempt = 0; attempt < NBM5100_I2C_MAX_RETRIES; attempt++) {

        err = i2c_write(nbm_i2c, buf, sizeof(buf), NBM5100_I2C_ADDR);

        if (err == 0) {
            return 0;
        }

        k_msleep(NBM5100_I2C_RETRY_DELAY_MS);
    }

    return err;
}

int nbm5100_set_active(bool active)
{
    uint8_t command = NBM5100_CMD_ECM;

    if (active) {
        command |= NBM5100_CMD_ACT;
    }

    return nbm5100_write_reg(NBM5100_REG_COMMAND, command);
}
