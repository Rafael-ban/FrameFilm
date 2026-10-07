/* Host check for the app7 v1 -> v2 migration and language persistence. */
#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "app_boot_cfg.h"
#include "service_param.h"

static uint8_t stored[3];
static uint16_t stored_size;
static uint8_t stored_version;

int service_param_app_load(uint8_t id, void *buf, uint16_t size, uint8_t version)
{
    assert(id == SERVICE_PARAM_APP_ID_BOOT_CFG);
    if(size != stored_size || version != stored_version) return -1;
    memcpy(buf, stored, size);
    return 0;
}

int service_param_app_save(uint8_t id, const void *buf, uint16_t size, uint8_t version)
{
    assert(id == SERVICE_PARAM_APP_ID_BOOT_CFG);
    assert(size <= sizeof(stored));
    memcpy(stored, buf, size);
    stored_size = size;
    stored_version = version;
    return 0;
}

int service_param_app_current_get(void) { return APP_ID_IMAGE; }
int app_manager_is_registered(app_id_t id) { return id < APP_ID_MAX; }

int main(void)
{
    /* v1 2B settings must survive the language field addition. */
    stored[0] = APP_BOOT_PAGE_SKIP;
    stored[1] = APP_START_CLOCK;
    stored_size = 2;
    stored_version = 1;
    app_boot_cfg_init();
    assert(app_boot_cfg_get()->boot_page == APP_BOOT_PAGE_SKIP);
    assert(app_boot_cfg_get()->start_app == APP_START_CLOCK);
    assert(app_language_get() == APP_LANGUAGE_ZH_CN);

    app_language_set(APP_LANGUAGE_EN);
    assert(stored_size == 3 && stored_version == APP_BOOT_CFG_VER);
    assert(stored[0] == APP_BOOT_PAGE_SKIP && stored[1] == APP_START_CLOCK);
    assert(stored[2] == APP_LANGUAGE_EN);

    /* Loading the v2 blob is the same path used after a device reboot. */
    app_boot_cfg_init();
    assert(app_boot_cfg_get()->boot_page == APP_BOOT_PAGE_SKIP);
    assert(app_boot_cfg_get()->start_app == APP_START_CLOCK);
    assert(app_language_get() == APP_LANGUAGE_EN);

    app_language_set(APP_LANGUAGE_ZH_CN);
    app_boot_cfg_init();
    assert(app_language_get() == APP_LANGUAGE_ZH_CN);
    return 0;
}
