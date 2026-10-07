#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "service_ble_name.h"
#include "nvs.h"
#include "esp_mac.h"

static char saved[17];
static char staged[17];
static bool has_saved;
static bool fail_commit;
static int current_mode;

void esp_read_mac(uint8_t mac[6], int type)
{
    const uint8_t address[6] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB};
    assert(type == ESP_MAC_BT);
    memcpy(mac, address, sizeof(address));
}

esp_err_t nvs_open(const char *namespace_name, int mode, nvs_handle_t *handle)
{
    assert(strcmp(namespace_name, "framefilm") == 0);
    assert(mode == NVS_READONLY || mode == NVS_READWRITE);
    current_mode = mode;
    *handle = 1;
    return ESP_OK;
}

esp_err_t nvs_get_str(nvs_handle_t handle, const char *key, char *value, size_t *length)
{
    assert(handle == 1 && current_mode == NVS_READONLY);
    assert(strcmp(key, "ble_name") == 0);
    if(!has_saved) return ESP_ERR_NVS_NOT_FOUND;
    size_t needed = strlen(saved) + 1;
    assert(*length >= needed);
    memcpy(value, saved, needed);
    *length = needed;
    return ESP_OK;
}

esp_err_t nvs_set_str(nvs_handle_t handle, const char *key, const char *value)
{
    assert(handle == 1 && current_mode == NVS_READWRITE);
    assert(strcmp(key, "ble_name") == 0);
    assert(strlen(value) < sizeof(staged));
    memcpy(staged, value, strlen(value) + 1);
    return ESP_OK;
}

esp_err_t nvs_commit(nvs_handle_t handle)
{
    assert(handle == 1 && current_mode == NVS_READWRITE);
    if(fail_commit) return 2;
    memcpy(saved, staged, strlen(staged) + 1);
    has_saved = true;
    return ESP_OK;
}

void nvs_close(nvs_handle_t handle) { assert(handle == 1); }
const char *esp_err_to_name(esp_err_t error) { (void)error; return "mock error"; }

static void expect_invalid(const uint8_t *bytes, size_t length)
{
    char before[30];
    memcpy(before, service_ble_name_get(), strlen(service_ble_name_get()) + 1);
    assert(service_ble_name_set(bytes, length) == 1);
    assert(strcmp(service_ble_name_get(), before) == 0);
}

int main(void)
{
    service_ble_name_init();
    assert(strcmp(service_ble_name_get(), "FRAMEFILMARK-0123456789AB") == 0);

    const uint8_t max_suffix[] = "1234567890123456";
    assert(service_ble_name_set(max_suffix, sizeof(max_suffix)) == 0);
    assert(strcmp(service_ble_name_get(), "FRAMEFILMARK-1234567890123456") == 0);
    service_ble_name_init();
    assert(strcmp(service_ble_name_get(), "FRAMEFILMARK-1234567890123456") == 0);

    const uint8_t chinese[] = "中文设备";
    assert(service_ble_name_set(chinese, sizeof(chinese)) == 0);
    assert(strcmp(service_ble_name_get(), "FRAMEFILMARK-中文设备") == 0);
    service_ble_name_init();
    assert(strcmp(service_ble_name_get(), "FRAMEFILMARK-中文设备") == 0);

    const uint8_t empty[] = {0};
    const uint8_t embedded_nul[] = {'A', 0, 'B', 0};
    const uint8_t bad_utf8[] = {0xC3, 0x28, 0};
    const uint8_t c1_control[] = {0xC2, 0x80, 0};
    const uint8_t too_long[] = "12345678901234567";
    expect_invalid(empty, sizeof(empty));
    expect_invalid(embedded_nul, sizeof(embedded_nul));
    expect_invalid(bad_utf8, sizeof(bad_utf8));
    expect_invalid(c1_control, sizeof(c1_control));
    expect_invalid(too_long, sizeof(too_long));

    fail_commit = true;
    const uint8_t unsaved[] = "FailedSave";
    assert(service_ble_name_set(unsaved, sizeof(unsaved)) == 2);
    assert(strcmp(service_ble_name_get(), "FRAMEFILMARK-中文设备") == 0);
    service_ble_name_init();
    assert(strcmp(service_ble_name_get(), "FRAMEFILMARK-中文设备") == 0);

    puts("service_ble_name host test passed");
    return 0;
}
