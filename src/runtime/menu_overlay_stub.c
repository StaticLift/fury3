/* Fury3 — menu overlay compiled out (FURY3_MENU=OFF, e.g. the Vita build):
 * no UI, fixed defaults matching menu_overlay.cpp's. */
#include <SDL.h>
#include "menu_overlay.h"

void menu_overlay_init(void *sdl_window, void *sdl_renderer) { (void)sdl_window; (void)sdl_renderer; }
void menu_overlay_shutdown(void) {}
int  menu_overlay_process_event(const void *sdl_event) { (void)sdl_event; return 0; }
void menu_overlay_render(void) {}
int  menu_overlay_is_active(void) { return 0; }
int  menu_overlay_quit_requested(void) { return 0; }

int  menu_overlay_get_scale(void) { return 3; }
int  menu_overlay_get_filter(void) { return 1; }
int  menu_overlay_get_vsync(void) { return 1; }
int  menu_overlay_get_fullscreen(void) { return 0; }
int  menu_overlay_get_menubar_height(void) { return 0; }

float menu_overlay_get_volume(void) { return 1.0f; }
int   menu_overlay_get_mute(void) { return 0; }

int menu_overlay_key_for_action(int action) { (void)action; return -1; }
int menu_overlay_pad_for_action(int action) {
    switch (action) {
        /* pitch/turn are the left stick; d-pad left/right are weapon select, d-pad
         * up the cockpit display, Select the camera view; the game's roll keys are on the
         * right stick (the Vita has no stick clicks) */
        case FURY3_ACT_THROTTLE_UP:    return SDL_CONTROLLER_BUTTON_RIGHTSHOULDER;
        case FURY3_ACT_THROTTLE_DOWN:  return SDL_CONTROLLER_BUTTON_LEFTSHOULDER;
        case FURY3_ACT_FIRE_PRIMARY:   return SDL_CONTROLLER_BUTTON_A;
        case FURY3_ACT_FIRE_SECONDARY: return SDL_CONTROLLER_BUTTON_X;
        case FURY3_ACT_AFTERBURNER:    return SDL_CONTROLLER_BUTTON_B;
        default:                       return -1;
    }
}
float menu_overlay_get_deadzone(void) { return 0.25f; }
int   menu_overlay_use_pad_axes(void) { return 1; }

int menu_overlay_take_new_game(void) { return 0; }
int menu_overlay_take_save_state(void) { return 0; }
int menu_overlay_take_load_state(void) { return 0; }
