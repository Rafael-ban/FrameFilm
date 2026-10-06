#include <string.h>

#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "hal_wifi.h"

static bool s_netif_ready;
static bool s_initialized;
static volatile bool s_connected;
static esp_event_handler_instance_t s_wifi_handler;
static esp_event_handler_instance_t s_ip_handler;

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)data;
    if(base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) s_connected = false;
    if(base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) s_connected = true;
}

esp_err_t hal_wifi_init(bool ram_storage)
{
    if(s_initialized) return ESP_OK;
    if(!s_netif_ready)
    {
        esp_err_t err = esp_netif_init();
        if(err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
        err = esp_event_loop_create_default();
        if(err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
        if(esp_netif_create_default_wifi_sta() == NULL) return ESP_ERR_NO_MEM;
        s_netif_ready = true;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_wifi_init(&cfg);
    if(err != ESP_OK) return err;
    err = esp_wifi_set_storage(ram_storage ? WIFI_STORAGE_RAM : WIFI_STORAGE_FLASH);
    if(err == ESP_OK) err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                                   wifi_event, NULL, &s_wifi_handler);
    if(err == ESP_OK) err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                                   wifi_event, NULL, &s_ip_handler);
    if(err == ESP_OK) err = esp_wifi_set_mode(WIFI_MODE_STA);
    if(err == ESP_OK) err = esp_wifi_start();
    if(err != ESP_OK)
    {
        if(s_ip_handler) esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, s_ip_handler);
        if(s_wifi_handler) esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, s_wifi_handler);
        s_ip_handler = NULL;
        s_wifi_handler = NULL;
        esp_wifi_deinit();
        return err;
    }
    s_initialized = true;
    s_connected = false;
    return ESP_OK;
}

void hal_wifi_deinit(void)
{
    if(!s_initialized) return;
    esp_wifi_disconnect();
    esp_wifi_stop();
    esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, s_ip_handler);
    esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, s_wifi_handler);
    s_ip_handler = NULL;
    s_wifi_handler = NULL;
    esp_wifi_deinit();
    s_connected = false;
    s_initialized = false;
}

esp_err_t hal_wifi_set_ram_storage(bool ram_storage)
{
    return s_initialized ? esp_wifi_set_storage(ram_storage ? WIFI_STORAGE_RAM : WIFI_STORAGE_FLASH)
                         : ESP_ERR_INVALID_STATE;
}

esp_err_t hal_wifi_get_sta_config(wifi_config_t *config)
{
    return s_initialized ? esp_wifi_get_config(WIFI_IF_STA, config) : ESP_ERR_INVALID_STATE;
}

esp_err_t hal_wifi_set_sta_config(const wifi_config_t *config)
{
    if(!s_initialized || !config) return ESP_ERR_INVALID_STATE;
    wifi_config_t copy = *config;
    return esp_wifi_set_config(WIFI_IF_STA, &copy);
}

esp_err_t hal_wifi_connect(const char *ssid, const char *password)
{
    if(!s_initialized || !ssid || !password) return ESP_ERR_INVALID_STATE;
    size_t ssid_len = strnlen(ssid, 33);
    size_t password_len = strnlen(password, 64);
    if(ssid_len == 0 || ssid_len > 32 || password_len > 63) return ESP_ERR_INVALID_ARG;
    wifi_config_t config = {0};
    memcpy(config.sta.ssid, ssid, ssid_len);
    memcpy(config.sta.password, password, password_len);
    esp_wifi_disconnect();
    s_connected = false;
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &config);
    return err == ESP_OK ? esp_wifi_connect() : err;
}

esp_err_t hal_wifi_reconnect(void)
{
    if(!s_initialized) return ESP_ERR_INVALID_STATE;
    esp_wifi_disconnect();
    s_connected = false;
    return esp_wifi_connect();
}

void hal_wifi_disconnect(void)
{
    if(!s_initialized) return;
    esp_wifi_disconnect();
    s_connected = false;
}

bool hal_wifi_initialized(void) { return s_initialized; }
bool hal_wifi_connected(void) { return s_connected; }
