#include "service_ble_name.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_mac.h"
#include "nvs.h"
#include "sys_cfg.h"
#include "sys_log.h"

#define BLE_NAME_NVS_KEY "ble_name"
#define BLE_NAME_PREFIX SYS_DEVICE_NAME "-"
#define BLE_NAME_PREFIX_LEN (sizeof(BLE_NAME_PREFIX) - 1)
#define BLE_NAME_SUFFIX_MAX (BLE_DEVICE_NAME_MAX_BYTES - BLE_NAME_PREFIX_LEN)

static char m_configured_name[BLE_DEVICE_NAME_MAX_BYTES + 1];

static bool suffix_valid(const uint8_t *bytes, size_t len)
{
    if(bytes == NULL || len <= 1 || len > BLE_NAME_SUFFIX_MAX + 1 || bytes[len - 1] != 0)
        return false;

    /* Validate UTF-8 scalar values and reject C0/C1 control characters. */
    for(size_t i = 0; i + 1 < len;)
    {
        uint32_t cp;
        uint8_t first = bytes[i++];
        size_t more;
        if(first < 0x80) { cp = first; more = 0; }
        else if(first >= 0xC2 && first <= 0xDF) { cp = first & 0x1F; more = 1; }
        else if(first >= 0xE0 && first <= 0xEF) { cp = first & 0x0F; more = 2; }
        else if(first >= 0xF0 && first <= 0xF4) { cp = first & 0x07; more = 3; }
        else return false;

        if(i + more > len - 1) return false;
        for(size_t j = 0; j < more; j++)
        {
            if((bytes[i] & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (bytes[i++] & 0x3F);
        }
        if((more == 1 && cp < 0x80) || (more == 2 && cp < 0x800) ||
           (more == 3 && cp < 0x10000) || cp > 0x10FFFF ||
           (cp >= 0xD800 && cp <= 0xDFFF) || cp < 0x20 ||
           (cp >= 0x7F && cp <= 0x9F)) return false;
    }
    return true;
}

void service_ble_name_init(void)
{
    char suffix[BLE_NAME_SUFFIX_MAX + 1] = {0};
    nvs_handle_t handle;
    esp_err_t err = nvs_open(SYS_M_NVS_NAMESPACE, NVS_READONLY, &handle);
    if(err == ESP_OK)
    {
        size_t size = sizeof(suffix);
        err = nvs_get_str(handle, BLE_NAME_NVS_KEY, suffix, &size);
        nvs_close(handle);
    }
    if(err != ESP_OK || !suffix_valid((const uint8_t *)suffix,
                                      strnlen(suffix, sizeof(suffix)) + 1))
    {
        uint8_t mac[6] = {0};
        esp_read_mac(mac, ESP_MAC_BT);
        snprintf(suffix, sizeof(suffix), "%02X%02X%02X%02X%02X%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        if(err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)
            sys_logw("BLE_NAME", "read name failed: %s", esp_err_to_name(err));
    }
    snprintf(m_configured_name, sizeof(m_configured_name), "%s%s",
             BLE_NAME_PREFIX, suffix);
}

const char *service_ble_name_get(void)
{
    return m_configured_name;
}

uint8_t service_ble_name_set(const uint8_t *suffix, size_t payload_len)
{
    if(!suffix_valid(suffix, payload_len)) return 1;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(SYS_M_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if(err != ESP_OK) return 2;
    err = nvs_set_str(handle, BLE_NAME_NVS_KEY, (const char *)suffix);
    if(err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    if(err != ESP_OK) return 2;

    snprintf(m_configured_name, sizeof(m_configured_name), "%s%s",
             BLE_NAME_PREFIX, (const char *)suffix);
    return 0;
}
