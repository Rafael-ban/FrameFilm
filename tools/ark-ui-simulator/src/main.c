#include <SDL2/SDL.h>
#include <stdio.h>
#include <string.h>
#include "lvgl.h"
#include "app_boot.h"
#include "app_menu.h"
#include "app_settings.h"
#include "app_sleep.h"
#include "app_pass.h"
#include "app_interface.h"
#include "simulator.h"

static uint8_t menu_sel;

static void show_menu(void)
{
    simulator_show(g_app_menu_entry.ui_ops, &g_app_menu_entry);
    menu_sel = 0;
    g_app_menu_entry.ui_ops->on_msg(APP_UI_MSG_MENU_SEL, &menu_sel, 1);
}

static void show_boot(void)
{
    static const app_boot_tele_t tele[] = {
        {"PANEL", "OK"}, {"STORAGE", "OK"}, {"RADIO", "OK"}
    };
    app_boot_set_telemetry(tele, 3);
    simulator_show(app_boot_ops(), NULL);
}

static void show_sleep(void)
{
    app_sleep_set_info(1, 30);
    simulator_show(app_sleep_ops(), NULL);
}

static void activate_menu_selection(void)
{
    if(menu_sel == 1) simulator_show(g_app_pass_entry.ui_ops, &g_app_pass_entry);
    else if(menu_sel == 5) simulator_show(g_app_settings_entry.ui_ops, &g_app_settings_entry);
    else fprintf(stderr, "Entry %u is not connected to this first desktop preview.\n", menu_sel);
}

static void press_key(input_press_type_t key)
{
    const app_ui_ops_t *ops = simulator_current_ops();
    const app_entry_t *entry = simulator_current_entry();
    if(ops == app_sleep_ops() && key == INPUT_PRESS_SHORT) { show_boot(); return; }
    if(key == INPUT_PRESS_LONG) { show_sleep(); return; }
    if(key == INPUT_PRESS_DOUBLE) { show_menu(); return; }
    if(entry == &g_app_menu_entry) {
        if(key == INPUT_PRESS_UP) menu_sel = (menu_sel + 1) % APP_MENU_ENTRY_NUM;
        else if(key == INPUT_PRESS_DOWN) menu_sel = (menu_sel + APP_MENU_ENTRY_NUM - 1) % APP_MENU_ENTRY_NUM;
        else if(key == INPUT_PRESS_SHORT) { activate_menu_selection(); return; }
        ops->on_msg(APP_UI_MSG_MENU_SEL, &menu_sel, 1);
    }
    else if(ops && ops->on_key) ops->on_key(key);
}

static int save_frame(lv_display_t *display, const char *dir, const char *name)
{
    char path[1024];
    if(snprintf(path, sizeof(path), "%s/%s.bmp", dir, name) >= (int)sizeof(path)) return -1;
    for(int i = 0; i < 12; ++i) { lv_timer_handler(); simulator_process_requests(); SDL_Delay(10); }
    SDL_Renderer *renderer = lv_sdl_window_get_renderer(display);
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, 480, 720, 32, SDL_PIXELFORMAT_ARGB8888);
    if(!renderer || !surface) return -1;
    if(SDL_RenderReadPixels(renderer, NULL, SDL_PIXELFORMAT_ARGB8888,
                            surface->pixels, surface->pitch) != 0) { SDL_FreeSurface(surface); return -1; }
    int result = SDL_SaveBMP(surface, path);
    SDL_FreeSurface(surface);
    fprintf(stderr, "snapshot %s: %s\n", name, result == 0 ? path : SDL_GetError());
    return result;
}

int main(int argc, char **argv)
{
    lv_init();
    lv_display_t *display = lv_sdl_window_create(480, 720);
    if(!display) { fputs("LVGL SDL window failed\n", stderr); return 1; }
    lv_sdl_window_set_title(display, "FrameFilm Ark UI simulator");
    show_boot();

    if(argc == 3 && strcmp(argv[1], "--smoke") == 0) {
        int result = save_frame(display, argv[2], "boot");
        show_menu();
        result |= save_frame(display, argv[2], "menu");
        press_key(INPUT_PRESS_UP);
        press_key(INPUT_PRESS_SHORT);
        if(simulator_current_entry() != &g_app_pass_entry) result = -1;
        result |= save_frame(display, argv[2], "pass");
        press_key(INPUT_PRESS_DOUBLE);
        if(simulator_current_entry() != &g_app_menu_entry) result = -1;
        simulator_show(g_app_settings_entry.ui_ops, &g_app_settings_entry);
        simulator_process_requests();
        press_key(INPUT_PRESS_UP);
        press_key(INPUT_PRESS_SHORT);
        simulator_process_requests();
        result |= save_frame(display, argv[2], "settings");
        show_sleep();
        result |= save_frame(display, argv[2], "sleep");
        press_key(INPUT_PRESS_SHORT);
        if(simulator_current_ops() != app_boot_ops()) result = -1;
        fprintf(stderr, "smoke %s\n", result == 0 ? "PASS" : "FAIL");
        return result == 0 ? 0 : 1;
    }

    Uint32 enter_down_at = 0, enter_pending_at = 0;
    int old_up = 0, old_down = 0, old_enter = 0, old_shortcut = 0;
    int enter_long_sent = 0;
    int running = 1;
    while(running) {
        lv_timer_handler();
        simulator_process_requests();
        if(simulator_take_boot_done()) show_menu();
        const Uint8 *keys = SDL_GetKeyboardState(NULL);
        int up = keys[SDL_SCANCODE_UP], down = keys[SDL_SCANCODE_DOWN];
        int enter = keys[SDL_SCANCODE_RETURN] || keys[SDL_SCANCODE_KP_ENTER];
        Uint32 now = SDL_GetTicks();
        if(up && !old_up) press_key(INPUT_PRESS_UP);
        if(down && !old_down) press_key(INPUT_PRESS_DOWN);
        if(enter && !old_enter) { enter_down_at = now; enter_long_sent = 0; }
        if(enter && !enter_long_sent && now - enter_down_at >= 1000) {
            enter_long_sent = 1;
            enter_pending_at = 0;
            press_key(INPUT_PRESS_LONG);
        }
        if(!enter && old_enter) {
            if(enter_long_sent) enter_long_sent = 0;
            else if(enter_pending_at && now - enter_pending_at < 350) {
                enter_pending_at = 0; press_key(INPUT_PRESS_DOUBLE);
            }
            else enter_pending_at = now;
        }
        if(enter_pending_at && now - enter_pending_at >= 350) {
            enter_pending_at = 0; press_key(INPUT_PRESS_SHORT);
        }
        int shortcut = keys[SDL_SCANCODE_1] ? 1 : keys[SDL_SCANCODE_2] ? 2 :
                       keys[SDL_SCANCODE_3] ? 3 : keys[SDL_SCANCODE_4] ? 4 :
                       keys[SDL_SCANCODE_5] ? 5 : 0;
        if(shortcut && shortcut != old_shortcut) {
            if(shortcut == 1) show_boot();
            if(shortcut == 2) show_menu();
            if(shortcut == 3) simulator_show(g_app_settings_entry.ui_ops, &g_app_settings_entry);
            if(shortcut == 4) show_sleep();
            if(shortcut == 5) simulator_show(g_app_pass_entry.ui_ops, &g_app_pass_entry);
        }
        if(keys[SDL_SCANCODE_ESCAPE]) running = 0;
        old_up = up; old_down = down; old_enter = enter; old_shortcut = shortcut;
        SDL_Delay(10);
    }
    simulator_show(NULL, NULL);
    lv_deinit();
    return 0;
}
