#ifndef NBM5100_H
#define NBM5100_H

#include <stdbool.h>
#include <stdint.h>

#define NBM5100_I2C_ADDR 0x2E

int nbm5100_init(void);

int nbm5100_read_reg(uint8_t reg, uint8_t *value);
int nbm5100_write_reg(uint8_t reg, uint8_t value);
int nbm_ready_init(void);
bool is_nbm_ready(void);

#endif /* NBM5100_H */