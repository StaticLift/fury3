/* Fury3 — Dear ImGui menu overlay (SDL2 frontend).
 *
 * C API consumed by the SDL platform layer (recomp_runtime.c, FURY3_SDL build).
 * Implementation in menu_overlay.cpp (C++/ImGui) or menu_overlay_stub.c (no-op,
 * when the menu is compiled out). Mirrors the sibling recomps (lynx/apple2/snes).
 */
#ifndef FURY3_MENU_OVERLAY_H
#define FURY3_MENU_OVERLAY_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* Bindable flight actions. The platform layer maps held keys/pad to these and
 * drives the game (keyboard injection + synthetic joystick). */
enum {
    FURY3_ACT_PITCH_UP, FURY3_ACT_PITCH_DOWN,
    FURY3_ACT_ROLL_LEFT, FURY3_ACT_ROLL_RIGHT,
    FURY3_ACT_YAW_LEFT, FURY3_ACT_YAW_RIGHT,
    FURY3_ACT_THROTTLE_UP, FURY3_ACT_THROTTLE_DOWN,
    FURY3_ACT_FIRE_PRIMARY, FURY3_ACT_FIRE_SECONDARY,
    FURY3_ACT_AFTERBURNER, FURY3_ACT_SLIDE_LEFT, FURY3_ACT_SLIDE_RIGHT,
    FURY3_ACT_COUNT
};

/* Lifecycle. init() is called once after the SDL window+renderer exist; the void*
 * args are SDL_Window* and SDL_Renderer* (kept void* so the header stays C/SDL-free). */
void menu_overlay_init(void *sdl_window, void *sdl_renderer);
void menu_overlay_shutdown(void);

/* Per-frame: feed each SDL_Event (void* = const SDL_Event*); returns 1 if the menu
 * consumed it (the platform should not forward it to the game). */
int  menu_overlay_process_event(const void *sdl_event);
/* Build + draw the menu (call between SDL_RenderClear and SDL_RenderPresent). */
void menu_overlay_render(void);

/* True while the menu is open/active (platform feeds the game neutral input then). */
int  menu_overlay_is_active(void);
int  menu_overlay_quit_requested(void);

/* Video settings (read by the platform each frame). */
int  menu_overlay_get_scale(void);       /* integer window scale, 1..6 */
int  menu_overlay_get_filter(void);      /* 0 = nearest, 1 = linear   */
int  menu_overlay_get_vsync(void);
int  menu_overlay_get_fullscreen(void);
int  menu_overlay_get_menubar_height(void);

/* Audio settings. */
float menu_overlay_get_volume(void);     /* 0..1 */
int   menu_overlay_get_mute(void);

/* Controls: resolve a bound input for an action (FURY3_ACT_*). -1 = unbound. */
int  menu_overlay_key_for_action(int action);  /* SDL_Scancode */
int  menu_overlay_pad_for_action(int action);  /* SDL_GameControllerButton */
float menu_overlay_get_deadzone(void);   /* 0..1 */
int  menu_overlay_use_pad_axes(void);    /* 1 = analog stick drives pitch/roll */

/* One-shot requests latched by menu items (platform polls + clears). */
int  menu_overlay_take_new_game(void);
int  menu_overlay_take_save_state(void);
int  menu_overlay_take_load_state(void);

#ifdef __cplusplus
}
#endif
#endif /* FURY3_MENU_OVERLAY_H */
