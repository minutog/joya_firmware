#ifndef OTA_ACCESS_H
#define OTA_ACCESS_H

#include <stdbool.h>

/* SMP is registered by MCUmgr before main(); close it before bt_enable(). */
int ota_access_init(void);
int ota_access_enable(void);
void ota_access_disable(void);
void ota_access_on_disconnect(void);
bool ota_access_is_update_active(void);

#endif
