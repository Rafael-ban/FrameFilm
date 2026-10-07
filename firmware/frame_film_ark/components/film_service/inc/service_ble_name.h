#ifndef __SERVICE_BLE_NAME_H__
#define __SERVICE_BLE_NAME_H__

#include <stddef.h>
#include <stdint.h>

/* FRAMEFILMARK- (13 bytes) + suffix (1..16 bytes) + NUL. */
#define BLE_DEVICE_NAME_MAX_BYTES 29

/* NVS must already be initialized by service_param_init(). */
void service_ble_name_init(void);
const char *service_ble_name_get(void);
/* 0 = saved, 1 = invalid suffix, 2 = NVS failure. */
uint8_t service_ble_name_set(const uint8_t *suffix, size_t payload_len);

#endif /* __SERVICE_BLE_NAME_H__ */
