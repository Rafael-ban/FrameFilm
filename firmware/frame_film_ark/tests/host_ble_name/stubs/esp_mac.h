#ifndef HOST_ESP_MAC_H
#define HOST_ESP_MAC_H
#include <stdint.h>
#define ESP_MAC_BT 1
void esp_read_mac(uint8_t mac[6], int type);
#endif
