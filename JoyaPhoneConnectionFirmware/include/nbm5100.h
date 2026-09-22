#ifndef NBM5100_H
#define NBM5100_H

#include <stdbool.h>
#include <stdint.h>

#define NBM5100_I2C_MAX_RETRIES   3
#define NBM5100_I2C_RETRY_DELAY_MS 2

#define NBM5100_I2C_ADDR 0x2E

/* Register map */
#define NBM5100_REG_STATUS        0x00
#define NBM5100_REG_CHENGY_MSB    0x01
#define NBM5100_REG_CHENGY_2      0x02
#define NBM5100_REG_CHENGY_1      0x03
#define NBM5100_REG_CHENGY_LSB    0x04
#define NBM5100_REG_VCAP          0x05
#define NBM5100_REG_VCHEND        0x06
#define NBM5100_REG_PROFILE       0x07
#define NBM5100_REG_COMMAND       0x08
#define NBM5100_REG_SET1          0x09
#define NBM5100_REG_SET2          0x0A
#define NBM5100_REG_SET3          0x0B
#define NBM5100_REG_SET4          0x0C
#define NBM5100_REG_SET5          0x0D

/* Field masks */
#define NBM5100_SET1_VSET_MASK    GENMASK(3, 0)
#define NBM5100_SET1_VFIX_MASK    GENMASK(7, 4)

#define NBM5100_SET2_VMIN_MASK    GENMASK(2, 0)
#define NBM5100_SET2_ICH_MASK     GENMASK(7, 5)

/* Bit definitions */
#define NBM5100_SET2_VDHHIZ       BIT(4)
#define NBM5100_SET3_AUTOMODE     BIT(7)

#define NBM5100_CMD_EOD           BIT(0)
#define NBM5100_CMD_ECM           BIT(1)
#define NBM5100_CMD_ACT           BIT(2)

/* Configuration values */
#define NBM5100_VSET_3V4          0x0D
#define NBM5100_VFIX_3V84         0x07
#define NBM5100_VMIN_3V2          0x04
#define NBM5100_ICH_4MA           0x01

/* Specification values 
    * VSET/VDH: 3.4V
    * VFIX: 3.84V
    * VMIN: 3.2V
    * ICH: 4mA (possible: 6 mA)
    * VDHIZ: 0 (VDHIZ disabled)
    * OPTIMIZER: disabled
    * AUTOMODE: disabled
    * MODE: Continuous

*/

int nbm5100_init(void);

int nbm5100_read_reg(uint8_t reg, uint8_t *value);
int nbm5100_write_reg(uint8_t reg, uint8_t value);
int nbm_ready_init(void);
bool is_nbm_ready(void);
int nbm5100_set_active(bool active);


#endif /* NBM5100_H */