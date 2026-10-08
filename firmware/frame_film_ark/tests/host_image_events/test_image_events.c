/* Exercise real image-app callbacks with storage and display collaborators stubbed. */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../../components/film_app/apps/image/app_image.c"

struct host_param g_service_param;
static uint32_t count, current_id, displayed[8];
static uint8_t load_state;
static unsigned display_count;
uint32_t service_file_get_count(void) { return count; }
uint32_t service_file_get_current_id(void) { return current_id; }
uint8_t service_file_get_load_complete(void) { return load_state; }
void service_film_display(uint32_t id) { assert(display_count < 8); displayed[display_count++] = id; }
void app_state_save(void) { }
app_id_t app_manager_get_current(void) { return APP_ID_IMAGE; }

static void reset(uint32_t files, uint8_t state)
{
    count = files;
    current_id = files ? files - 1 : 0;
    load_state = state;
    display_count = 0;
    m_image = m_image_default;
}
static void list(int saving)
{
    app_event_t event = { .type = APP_EVT_SYS, .cmd = SYS_EVT_FILE_LIST,
                          .len = sizeof(uint32_t) };
    memcpy(event.payload, &count, sizeof(count));
    if(saving) { event.payload[sizeof(count)] = SYS_FILE_LIST_SAVE_PENDING; event.len++; }
    g_app_image_entry.on_event(&event);
}
static void saved(uint8_t auto_load)
{
    app_event_t event = { .type = APP_EVT_SYS, .cmd = SYS_EVT_FILE_SAVED, .len = 1 };
    event.payload[0] = auto_load;
    g_app_image_entry.on_event(&event);
}
int main(void)
{
    /* New-file and same-name overwrite: invalidated cache must queue only the target. */
    reset(2, FILE_LOAD_STATE_NONE); list(1); assert(display_count == 0);
    saved(1); assert(display_count == 1 && displayed[0] == 1);
    reset(2, FILE_LOAD_STATE_NONE); current_id = 0; list(1); saved(1);
    assert(display_count == 1 && displayed[0] == 0);
    /* Silent saves must remain silent, including the first file in an empty app. */
    reset(2, FILE_LOAD_STATE_NONE); list(1); saved(0); assert(display_count == 0);
    reset(0, FILE_LOAD_STATE_NONE); g_app_image_entry.on_enter();
    assert(display_count == 0); count = 1; current_id = 0; list(1); saved(0);
    assert(display_count == 0);
    /* Initial entry and ordinary delayed list, recovery and deletion retain their behavior. */
    reset(1, FILE_LOAD_STATE_NONE); g_app_image_entry.on_enter();
    assert(display_count == 1 && displayed[0] == 0);
    reset(0, FILE_LOAD_STATE_NONE); g_app_image_entry.on_enter();
    count = 1; list(0); assert(display_count == 1 && displayed[0] == 0);
    reset(1, FILE_LOAD_STATE_FAILED); list(0); assert(display_count == 1);
    reset(1, FILE_LOAD_STATE_NONE); m_image.file_id = 4; list(0);
    assert(display_count == 1 && displayed[0] == 0 && m_image.file_id == 0);
    reset(0, FILE_LOAD_STATE_NONE); list(0); assert(display_count == 0);
    reset(1, FILE_LOAD_STATE_DONE); list(0); assert(display_count == 0);
    puts("PASS: image events (new, overwrite, silent, first file, initial, recovery, delete)");
    return 0;
}
