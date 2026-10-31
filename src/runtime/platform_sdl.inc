/* SDL2 + Dear ImGui presentation/input frontend for Fury3 (FURY3_SDL build).
 *
 * #included into recomp_runtime.c so it shares the framebuffer/window statics
 * (g_dib_buf, g_pal, g_dib_w/h, g_quit, MEM8, RGBQUAD). Replaces the GDI present
 * (host_blit) and the real-Win32 input pump with: SDL window + streaming texture
 * of the game's 8bpp framebuffer, the imgui menu overlay on top, and SDL keyboard
 * /controller translated into the game's input. The game keeps its own input
 * model — we feed it synthetic Win32 key messages (host_peek_game_msg) and a
 * synthetic joystick (host_joy_getpos reads the SDL game controller).
 */
#define SDL_MAIN_HANDLED   /* we keep our own main(); don't let SDL remap it */
#include <SDL.h>
#include "menu_overlay.h"
#ifdef FURY3_GL
#include "gpu_gl.h"
#ifdef __vita__
#include <vitaGL.h>
#include <vitashark.h>
#endif
static int s_gl = 0;              /* the GPU renderer is drawing */
static SDL_Surface* s_ui = NULL;  /* prompts and messages, drawn by the software renderer */
static SDL_GLContext s_glctx;
static void gl_swap(void);
static void gl_window_size(int* w, int* h);
#endif

int  host_mods_available(void);
int  host_mods_enabled(void);
int  host_mods_changed(void);
uint32_t host_yesno_ms(void);
void host_mods_toggle(void);
void host_wings_toggle(void);
void host_wings_tick(void);
void host_level_pick(int step);

static SDL_Window     *s_win = NULL;
static SDL_Renderer   *s_ren = NULL;
static SDL_Texture    *s_tex = NULL;
static SDL_GameController *s_pad = NULL;
static uint32_t        s_rgba[1024 * 768];   /* RGBA scratch for the framebuffer */
static int             s_sdl_ready = 0;

/* Synthetic Win32 key message queue: SDL key events -> {WM_KEYDOWN/UP, VK} the
 * game's WndProc + PeekMessage path consume. */
typedef struct { uint32_t msg, vk, lp; } SynKey;
static SynKey s_synq[256]; static int s_synq_head = 0, s_synq_tail = 0;
/* The game's WM_KEYDOWN/UP handler keeps a key-state table indexed by the PC
 * (set 1) scancode in lParam bits 16..22 -- that is how flight controls are read
 * (fury3.ini stores them as scancodes) -- so every synthetic key carries one. */
static void synq_push_key(uint32_t msg, uint32_t vk, uint32_t sc) {
    int nt = (s_synq_tail + 1) & 255;
    if (nt == s_synq_head) return;            /* full -> drop */
    uint32_t lp = (sc & 0x7Fu) << 16 | 1u;    /* repeat count 1 */
    if (msg == 0x0101u) lp |= 0xC0000000u;    /* key-up: previous state + transition */
    s_synq[s_synq_tail].msg = msg; s_synq[s_synq_tail].vk = vk; s_synq[s_synq_tail].lp = lp;
    s_synq_tail = nt;
}
static int synq_get(uint32_t *msg, uint32_t *vk, uint32_t *lp, int remove) {
    if (s_synq_head == s_synq_tail) return 0;
    *msg = s_synq[s_synq_head].msg; *vk = s_synq[s_synq_head].vk; *lp = s_synq[s_synq_head].lp;
    if (remove) s_synq_head = (s_synq_head + 1) & 255;
    return 1;
}

/* SDL (USB HID) scancode -> PC set-1 scancode, for the keys a 1995 keyboard had. */
static uint32_t sdl_to_set1(SDL_Scancode s) {
    static const uint8_t letters[26] = { 0x1E,0x30,0x2E,0x20,0x12,0x21,0x22,0x23,0x17,0x24,0x25,0x26,0x32,
                                         0x31,0x18,0x19,0x10,0x13,0x1F,0x14,0x16,0x2F,0x11,0x2D,0x15,0x2C };
    static const uint8_t digits[10] = { 0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0A,0x0B };  /* 1..9,0 */
    static const uint8_t fkeys[12]  = { 0x3B,0x3C,0x3D,0x3E,0x3F,0x40,0x41,0x42,0x43,0x44,0x57,0x58 };
    if (s >= SDL_SCANCODE_A && s <= SDL_SCANCODE_Z)   return letters[s - SDL_SCANCODE_A];
    if (s >= SDL_SCANCODE_1 && s <= SDL_SCANCODE_0)   return digits[s - SDL_SCANCODE_1];
    if (s >= SDL_SCANCODE_F1 && s <= SDL_SCANCODE_F12) return fkeys[s - SDL_SCANCODE_F1];
    switch (s) {
        case SDL_SCANCODE_ESCAPE: return 0x01;   case SDL_SCANCODE_RETURN: return 0x1C;
        case SDL_SCANCODE_BACKSPACE: return 0x0E; case SDL_SCANCODE_TAB: return 0x0F;
        case SDL_SCANCODE_SPACE: return 0x39;    case SDL_SCANCODE_MINUS: return 0x0C;
        case SDL_SCANCODE_EQUALS: return 0x0D;   case SDL_SCANCODE_LEFTBRACKET: return 0x1A;
        case SDL_SCANCODE_RIGHTBRACKET: return 0x1B; case SDL_SCANCODE_SEMICOLON: return 0x27;
        case SDL_SCANCODE_APOSTROPHE: return 0x28; case SDL_SCANCODE_GRAVE: return 0x29;
        case SDL_SCANCODE_BACKSLASH: return 0x2B; case SDL_SCANCODE_COMMA: return 0x33;
        case SDL_SCANCODE_PERIOD: return 0x34;   case SDL_SCANCODE_SLASH: return 0x35;
        case SDL_SCANCODE_LCTRL: case SDL_SCANCODE_RCTRL: return 0x1D;
        case SDL_SCANCODE_LSHIFT: return 0x2A;   case SDL_SCANCODE_RSHIFT: return 0x36;
        case SDL_SCANCODE_LALT: case SDL_SCANCODE_RALT: return 0x38;
        case SDL_SCANCODE_HOME: case SDL_SCANCODE_KP_7: return 0x47;
        case SDL_SCANCODE_UP: case SDL_SCANCODE_KP_8: return 0x48;
        case SDL_SCANCODE_PAGEUP: case SDL_SCANCODE_KP_9: return 0x49;
        case SDL_SCANCODE_LEFT: case SDL_SCANCODE_KP_4: return 0x4B;
        case SDL_SCANCODE_KP_5: return 0x4C;
        case SDL_SCANCODE_RIGHT: case SDL_SCANCODE_KP_6: return 0x4D;
        case SDL_SCANCODE_END: case SDL_SCANCODE_KP_1: return 0x4F;
        case SDL_SCANCODE_DOWN: case SDL_SCANCODE_KP_2: return 0x50;
        case SDL_SCANCODE_PAGEDOWN: case SDL_SCANCODE_KP_3: return 0x51;
        case SDL_SCANCODE_INSERT: case SDL_SCANCODE_KP_0: return 0x52;
        case SDL_SCANCODE_DELETE: case SDL_SCANCODE_KP_PERIOD: return 0x53;
        case SDL_SCANCODE_KP_MINUS: return 0x4A; case SDL_SCANCODE_KP_PLUS: return 0x4E;
        default: return 0;
    }
}

/* VK for the synthetic keys the harness sends (it only knows VKs). */
static uint32_t vk_to_set1(uint32_t vk) {
    switch (vk) {
        case VK_RETURN: return 0x1C; case VK_ESCAPE: return 0x01; case VK_SPACE: return 0x39;
        case VK_F1: return 0x3B; case VK_F2: return 0x3C; case VK_F3: return 0x3D;
        default: return 0;
    }
}
static void synq_push(uint32_t msg, uint32_t vk) { synq_push_key(msg, vk, vk_to_set1(vk)); }

/* SDL keycode -> Win32 virtual-key (the codes Fury3's WndProc reads). */
static uint32_t sdl_to_vk(SDL_Keycode k) {
    switch (k) {
        case SDLK_UP: return VK_UP;       case SDLK_DOWN: return VK_DOWN;
        case SDLK_LEFT: return VK_LEFT;   case SDLK_RIGHT: return VK_RIGHT;
        case SDLK_LCTRL: case SDLK_RCTRL: return VK_CONTROL;
        case SDLK_LSHIFT: case SDLK_RSHIFT: return VK_SHIFT;
        case SDLK_LALT: case SDLK_RALT: return VK_MENU;
        case SDLK_SPACE: return VK_SPACE; case SDLK_RETURN: return VK_RETURN;
        case SDLK_ESCAPE: return VK_ESCAPE; case SDLK_TAB: return VK_TAB;
        case SDLK_BACKSPACE: return VK_BACK;
        case SDLK_COMMA: return VK_OEM_COMMA; case SDLK_PERIOD: return VK_OEM_PERIOD;
        case SDLK_F1: return VK_F1; case SDLK_F2: return VK_F2; case SDLK_F3: return VK_F3;
        case SDLK_F4: return VK_F4; case SDLK_F5: return VK_F5; case SDLK_F6: return VK_F6;
        default:
            if (k >= SDLK_a && k <= SDLK_z) return (uint32_t)('A' + (k - SDLK_a));
            if (k >= SDLK_0 && k <= SDLK_9) return (uint32_t)('0' + (k - SDLK_0));
            return 0;
    }
}

static void host_sdl_init(const char *title) {
    if (s_sdl_ready) return;     /* the game may create its window more than once */
    SDL_SetMainReady();
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");   /* the GPU path has no SDL window to be focused */
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK) != 0) {
        fprintf(stderr, "[sdl] SDL_Init failed: %s\n", SDL_GetError()); return;
    }
    int scale = 3;
    int mbar = 20;   /* approx menu-bar height reserve */
#ifdef FURY3_GL
    /* FURY3_SOFT=1 keeps the software renderer. On the PC the GPU path is opt-in. */
#ifdef __vita__
    /* vitaGL compiles shaders with the system's libshacccg, which players install
     * themselves, and cannot be shut down again: check before starting it. */
    if (!host_env("FURY3_SOFT") && (os_file_info("ur0:data/libshacccg.suprx", NULL, NULL) ||
                                    os_file_info("ur0:data/external/libshacccg.suprx", NULL, NULL))) {
        /* vitaGL takes all memory but this much; the shader compiler loads into what is left */
        /* Start the compiler on the C heap ourselves; vitaGL's own shark_init then finds
         * it running. Its translated shaders need the SceShaccCgExt patches (bit_cast). */
        { void gl_compiler_spy(void); gl_compiler_spy(); }
        { char b[48]; snprintf(b, sizeof(b), "shark_init: %d", shark_init(NULL)); gl_log(b); }
        gl_log("vglInit");
        vglInitExtended(0, 960, 544, 32 * 1024 * 1024, SCE_GXM_MULTISAMPLE_NONE);
        gl_log("vglInit done");
        vglWaitVblankStart(GL_FALSE);
        s_gl = gl_init();
    }
    gl_log(s_gl ? "GPU renderer on" : "software renderer");
#else
    if (host_env("FURY3_GL")) {
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
        s_win = SDL_CreateWindow("Fury3 (GPU)", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 960, 600, SDL_WINDOW_OPENGL);
        s_glctx = s_win ? SDL_GL_CreateContext(s_win) : NULL;
        if (s_glctx) { SDL_GL_SetSwapInterval(0); s_gl = gl_init(); }
    }
#endif
    if (s_gl) {
        s_ui = SDL_CreateRGBSurfaceWithFormat(0, 960, 544, 32, SDL_PIXELFORMAT_ARGB8888);
        s_ren = SDL_CreateSoftwareRenderer(s_ui);
        s_tex = SDL_CreateTexture(s_ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, g_dib_w, g_dib_h);
        host_gpu_enable();
        for (int i = 0; i < SDL_NumJoysticks(); i++)
            if (SDL_IsGameController(i)) { s_pad = SDL_GameControllerOpen(i); break; }
        s_sdl_ready = 1;
        fprintf(stderr, "[sdl] GPU renderer active\n");
        return;
    }
#endif
#ifdef __vita__
    s_win = SDL_CreateWindow("Fury3", 0, 0, 960, 544, 0);      /* the Vita screen */
#else
    s_win = SDL_CreateWindow(title[0] ? title : "Fury3 (recompiled)",
                             SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                             g_dib_w * scale, g_dib_h * scale + mbar,
                             SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
#endif
    if (!s_win) { fprintf(stderr, "[sdl] CreateWindow failed: %s\n", SDL_GetError()); return; }
    /* VSync makes a frame that misses a refresh wait for the next one (a 35 ms
     * frame shows at 50 ms), which costs a lot when the game runs below 60 fps.
     * FURY3_VSYNC=0/1 overrides; the Vita defaults to off, PC to on. */
#ifdef __vita__
    int vsync = 0;
#else
    int vsync = 1;
#endif
    if (host_env("FURY3_VSYNC")) vsync = atoi(host_env("FURY3_VSYNC")) != 0;
    s_ren = SDL_CreateRenderer(s_win, -1, SDL_RENDERER_ACCELERATED | (vsync ? SDL_RENDERER_PRESENTVSYNC : 0));
    if (!s_ren) s_ren = SDL_CreateRenderer(s_win, -1, 0);
    if (!s_ren) { fprintf(stderr, "[sdl] CreateRenderer failed: %s\n", SDL_GetError()); return; }
    s_tex = SDL_CreateTexture(s_ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                              g_dib_w, g_dib_h);
    SDL_SetTextureScaleMode(s_tex, SDL_ScaleModeLinear);
    menu_overlay_init(s_win, s_ren);
    if (host_env("FURY3_MENU_OPEN")) {     /* start with the menu open (for screenshots) */
        SDL_Event ev; memset(&ev, 0, sizeof(ev));
        ev.type = SDL_KEYDOWN; ev.key.keysym.sym = SDLK_F1;
        ev.key.keysym.scancode = SDL_SCANCODE_F1;
        menu_overlay_process_event(&ev);
    }
    for (int i = 0; i < SDL_NumJoysticks(); i++)
        if (SDL_IsGameController(i)) { s_pad = SDL_GameControllerOpen(i); break; }
    s_sdl_ready = 1;
    fprintf(stderr, "[sdl] window %dx%d (scale %d)%s\n", g_dib_w, g_dib_h, scale,
            s_pad ? ", game controller connected" : "");
}

/* Gamepad -> the game's own flight keys. Fury3 reads flight input from its key
 * table (by scancode), so a held pad binding becomes a held key: the same
 * scancodes fury3.ini's [Control] section uses by default. The left stick also
 * drives the four arrow keys digitally past the deadzone, and Start is Enter
 * (menus / skipping) -- or F2, New Game, on the title screen. Bindings come from the menu (menu_overlay_pad_for_action). */
typedef struct { int act; uint32_t vk, sc; } PadKey;
static const PadKey s_padkeys[] = {
    { FURY3_ACT_PITCH_UP,       VK_UP,     0x48 },   /* upKey=72        */
    { FURY3_ACT_PITCH_DOWN,     VK_DOWN,   0x50 },   /* downKey=80      */
    { FURY3_ACT_ROLL_LEFT,      VK_LEFT,   0x4B },   /* leftKey=75      */
    { FURY3_ACT_ROLL_RIGHT,     VK_RIGHT,  0x4D },   /* rightKey=77     */
    { FURY3_ACT_YAW_LEFT,       0x24,      0x47 },   /* rollLeftKey=71  (Home)  */
    { FURY3_ACT_YAW_RIGHT,      0x21,      0x49 },   /* rollRightKey=73 (PgUp)  */
    { FURY3_ACT_THROTTLE_UP,    'A',       0x1E },   /* throttleUpKey=30   */
    { FURY3_ACT_THROTTLE_DOWN,  'Z',       0x2C },   /* throttleDownKey=44 */
    { FURY3_ACT_FIRE_PRIMARY,   VK_SPACE,  0x39 },   /* fireKey=57      */
    { FURY3_ACT_FIRE_SECONDARY, 'F',       0x21 },   /* weaponKey=33    */
};
#define PADKEY_N (int)(sizeof(s_padkeys) / sizeof(s_padkeys[0]))

/* Weapon select: keys 1..7 pick weapon ids 1,2,3,0x12..0x15 into [0x514760],
 * each only if owned (nonzero [0x50DFA0 + id*8]) -- the key handler at 0x419980. A d-pad
 * left/right not bound to an action steps to the previous/next owned weapon by
 * tapping that number key. The game clears a key's latch on WM_KEYUP, so the
 * tap is held for a few frames rather than released in the same pump. */
static const struct { uint32_t id, sc; } s_weapons[] = {
    { 0x01, 0x02 }, { 0x02, 0x03 }, { 0x03, 0x04 }, { 0x12, 0x05 }, { 0x13, 0x06 }, { 0x14, 0x07 }, { 0x15, 0x08 },
};
#define WEAPON_N (int)(sizeof(s_weapons) / sizeof(s_weapons[0]))
static uint32_t s_tap_vk, s_tap_sc, s_tap_until, s_tap_ctrl;
static int s_paused_polls;   /* consecutive pad polls in flight with dt == 0 */
static void pad_tap_release(void) {
    if (!s_tap_sc) return;
    synq_push_key(0x0101u, s_tap_vk, s_tap_sc);
    if (s_tap_ctrl) synq_push_key(0x0101u, 0x11, 0x1D);
    s_tap_sc = 0;
}
static void pad_tap_key2(uint32_t vk, uint32_t sc, int ctrl) {
    pad_tap_release();
    if (ctrl) synq_push_key(0x0100u, 0x11, 0x1D);     /* accelerators like Ctrl+S need Ctrl held */
    synq_push_key(0x0100u, vk, sc);
    s_tap_vk = vk; s_tap_sc = sc; s_tap_ctrl = (uint32_t)ctrl; s_tap_until = host_millis() + 120;
}
static void pad_tap_key(uint32_t vk, uint32_t sc) { pad_tap_key2(vk, sc, 0); }
static void pad_weapon_step(int dir) {
    uint32_t cur = MEM32(0x00514760u);
    int ci = dir > 0 ? -1 : WEAPON_N;
    for (int i = 0; i < WEAPON_N; i++) if (s_weapons[i].id == cur) ci = i;
    for (int k = 1; k <= WEAPON_N; k++) {
        int j = ((ci + dir * k) % WEAPON_N + WEAPON_N) % WEAPON_N;
        if (MEM32(0x0050DFA0u + s_weapons[j].id * 8) != 0) {
            if (s_weapons[j].id != cur) pad_tap_key('0' + s_weapons[j].sc - 1, s_weapons[j].sc);
            return;
        }
    }
}
static int pad_button_bound(int button) {
    for (int a = 0; a < FURY3_ACT_COUNT; a++) if (menu_overlay_pad_for_action(a) == button) return 1;
    return 0;
}

/* Level select, on the title screen: a planet and one of its three missions. */
static const char* const s_planets[8] = { "TERRAN", "L24-D", "ARES", "NEW KROY", "SEBEK", "VESTRA", "TIAMAT", "FURY" };
static int s_lv_open, s_lv_planet, s_lv_mission;
static void level_key(int up, int right) {
    s_lv_planet = (s_lv_planet + up + 8) % 8;
    s_lv_mission = (s_lv_mission + right + 3) % 3;
}

static void host_sdl_pad_to_keys(void) {
    static uint8_t held[PADKEY_N + 1];
    static uint8_t tap_prev[8];
    { static uint8_t f6_prev;                 /* keyboard equivalent of Triangle */
      uint8_t f6 = SDL_GetKeyboardState(NULL)[SDL_SCANCODE_F6];
      if (f6 && !f6_prev && MEM32(0x0053E860u) == 0 && MEM32(0x00542464u) == 0) host_mods_toggle();
      f6_prev = f6; }
    { static uint8_t f7_prev;                 /* keyboard equivalent of d-pad down */
      uint8_t f7 = SDL_GetKeyboardState(NULL)[SDL_SCANCODE_F7];
      if (f7 && !f7_prev && MEM32(0x0053E860u) == 1 && MEM32(0x004EB9D0u) != 0) host_wings_toggle();
      f7_prev = f7; }
    host_wings_tick();
    { int title = MEM32(0x0053E860u) == 0 && MEM32(0x00542464u) == 0;
      const Uint8* k = SDL_GetKeyboardState(NULL);
      static uint8_t prev[6];
      const uint8_t now[6] = { k[SDL_SCANCODE_F8], k[SDL_SCANCODE_UP], k[SDL_SCANCODE_DOWN], k[SDL_SCANCODE_LEFT], k[SDL_SCANCODE_RIGHT], k[SDL_SCANCODE_F2] };
      if (!title) s_lv_open = 0;
      else {
          if (now[0] && !prev[0]) s_lv_open = !s_lv_open;
          if (s_lv_open) {
              if (now[1] && !prev[1]) level_key(-1, 0);
              if (now[2] && !prev[2]) level_key(1, 0);
              if (now[3] && !prev[3]) level_key(0, -1);
              if (now[4] && !prev[4]) level_key(0, 1);
              if (now[5] && !prev[5]) host_level_pick(s_lv_planet * 3 + s_lv_mission);
          }
      }
      memcpy(prev, now, sizeof(prev));
      /* FURY3_LEVELPICK=<step> (testing): the list open at that step, and chosen */
      if (title && host_env("FURY3_LEVELPICK")) {
          static int done;
          int step = atoi(host_env("FURY3_LEVELPICK"));
          if (!done) { done = 1; s_lv_open = 1; s_lv_planet = step / 3; s_lv_mission = step % 3; host_level_pick(step); }
      } }
    s_paused_polls = MEM32(0x0053E860u) == 1 && MEM32(0x004EB9D0u) == 0 ? s_paused_polls + 1 : 0;
    int paused = s_paused_polls >= 3;
    uint8_t want[PADKEY_N + 1]; memset(want, 0, sizeof(want));
    if (s_tap_sc && (int32_t)(host_millis() - s_tap_until) >= 0) pad_tap_release();
    /* FURY3_KEYSEQ=ms:sc:vk,... (hex sc/vk, decimal ms since start): scripted key
     * taps for unattended tests; sc|0x100 presses and sc|0x200 releases instead. */
    if (host_env("FURY3_KEYSEQ")) {
        static const char* p = NULL;
        static unsigned at, sc, vk;
        static int have = 0;
        if (!p) p = host_env("FURY3_KEYSEQ");
        if (!have && *p && sscanf(p, "%u:%x:%x", &at, &sc, &vk) == 3) {
            have = 1;
            while (*p && *p != ',') p++;
            if (*p == ',') p++;
        }
        if (have && host_millis() >= at) {
            fprintf(stderr, "[keyseq] @%u tap sc=%02X vk=%02X rmode=%d dt=%d\n", host_millis(), sc, vk, (int)MEM32(0x0053E860u), (int)MEM32(0x004EB9D0u));
            if (host_yesno_ms()) MEM32(0x00541F14u) = vk;                 /* answers the Retry prompt */
            if (sc & 0x100) synq_push_key(0x0100u, vk, sc & 0xFF);        /* 0x1xx: press only */
            else if (sc & 0x200) synq_push_key(0x0101u, vk, sc & 0xFF);   /* 0x2xx: release only */
            else pad_tap_key(vk, sc);
            have = 0;
        }
    }
    /* FURY3_TELEPORT=ms,x,y,z[,step];... (testing): puts the ship at each stop in turn, ms
     * after flight starts, and at that mission step, [0x48F3E8], an entry of the level's .NAV */
    if (host_env("FURY3_TELEPORT") && MEM32(0x0053E860u) == 1) {
        static uint32_t since; static const char* next;
        static uint32_t seen = 0xFFFFFFFFu;
        int ms, x, y, z, step = -1, used = 0;
        if (!since) { since = host_millis(); next = host_env("FURY3_TELEPORT"); }
        if (MEM32(0x0048F3E8u) != seen) {
            seen = MEM32(0x0048F3E8u);
            fprintf(stderr, "[step] %u @%u\n", seen, host_millis() - since);
        }
        if (next && *next && sscanf(next, "%d,%d,%d,%d%n", &ms, &x, &y, &z, &used) == 4 && host_millis() - since >= (uint32_t)ms) {
            const char* t = next + used;
            if (*t == ',') sscanf(t + 1, "%d", &step);
            static uint32_t angle[3]; static int have;      /* [0x4EBAFC] */
            if (!have) { have = 1; angle[2] = MEM32(0x004EBB04u); }      /* level, on that compass heading */
            for (int i = 0; i < 3; i++) MEM32(0x004EBAFCu + i * 4) = angle[i];
            MEM32(0x004EBAF0u) = (uint32_t)x; MEM32(0x004EBAF4u) = (uint32_t)y; MEM32(0x004EBAF8u) = (uint32_t)z;
            if (step >= 0) MEM32(0x0048F3E8u) = (uint32_t)step;
            fprintf(stderr, "[teleport] %d,%d,%d step %d\n", x, y, z, step);
            next = strchr(next, ';');
            if (next) next++;
        }
    }
    /* FURY3_ALLWEAPONS (testing): every weapon, topped up. */
    if (host_env("FURY3_ALLWEAPONS") && MEM32(0x0053E860u) == 1)
        for (int i = 0; i < WEAPON_N; i++) MEM32(0x0050DFA0u + s_weapons[i].id * 8) = 500;
    /* FURY3_BOSSDBG=ms (testing): that long after flight starts, at a boss step, logs the boss's
     * hit spheres (0x40DC40 through thunk 0x4010B9), clears its shields, then applies a hit
     * through the damage routine (thunk 0x4017DA) at each sphere. */
    if (host_env("FURY3_BOSSDBG") && MEM32(0x0053E860u) == 1) {
        static uint32_t since; static int done;
        uint32_t step = MEM32(0x0048F3E8u);
        if (!since) since = host_millis();
        if (!done && host_millis() - since >= (uint32_t)atoi(host_env("FURY3_BOSSDBG")) && MEM32(0x0048F46Cu + step * 0xA4u) == 5) {
            extern void sub_004010B9(void); extern void sub_004017DA(void);
            done = 1;
            uint32_t sv[3] = { g_eax, g_ecx, g_edx }, sp = g_esp, buf = sp - 0x4000;
            uint32_t rec = 0x0048F4C0u + step * 0xA4u, tab = MEM32(0x0053C6CCu);
            uint32_t obj = tab + MEM32(rec) * 0xB4u, n = MEM32(rec + 0x14);
            uint32_t idx = MEM32(obj + 0x18), def = MEM32(0x0053C6C8u) + idx * 0x124u;
            int cnt = (int)MEM32(def + 0x1B4);
            fprintf(stderr, "[bossdbg] boss at %d,%d,%d ship %d,%d,%d def=%u spheres=%d box=%d,%d,%d\n",
                    (int)MEM32(obj), (int)MEM32(obj + 4), (int)MEM32(obj + 8), (int)MEM32(0x004EBAF0u), (int)MEM32(0x004EBAF4u),
                    (int)MEM32(0x004EBAF8u), idx, cnt, (int)MEM32(def + 0x9C), (int)MEM32(def + 0xA0), (int)MEM32(def + 0xA4));
            for (uint32_t i = 0; i < n && i < 12; i++) MEM32(tab + MEM32(rec + 0x18 + i * 4) * 0xB4u + 0x1C) = 0;
            for (int i = 0; i < cnt && i < 8; i++) {
                g_esp = sp - 0x40;
                MEM32(g_esp) = 0; MEM32(g_esp + 4) = buf; MEM32(g_esp + 8) = obj; MEM32(g_esp + 12) = def;
                MEM32(g_esp + 16) = MEM32(def + 0x1B8 + i * 4);
                sub_004010B9();
                uint32_t p = g_eax;
                int x = (int)MEM32(p), y = (int)MEM32(p + 4), z = (int)MEM32(p + 8), hp = (int)MEM32(obj + 0x1C);
                MEM32(buf + 0x20) = (uint32_t)x; MEM32(buf + 0x24) = (uint32_t)y; MEM32(buf + 0x28) = (uint32_t)z;
                g_esp = sp - 0x40;
                MEM32(g_esp) = 0; MEM32(g_esp + 4) = buf + 0x20; MEM32(g_esp + 8) = 0x1000;
                sub_004017DA();
                int hit = (int)g_eax;
                extern void sub_00401843(void);
                g_esp = sp - 0x40;
                MEM32(g_esp) = 0; MEM32(g_esp + 4) = obj; MEM32(g_esp + 8) = def; MEM32(g_esp + 12) = buf + 0x20;
                sub_00401843();
                int t1 = (int)g_eax;
                g_esp = sp - 0x40;
                MEM32(g_esp) = 0; MEM32(g_esp + 4) = buf + 0x40; MEM32(g_esp + 8) = obj; MEM32(g_esp + 12) = def;
                MEM32(g_esp + 16) = MEM32(def + 0x1B8 + i * 4);
                sub_004010B9();
                uint32_t q = g_eax;
                fprintf(stderr, "[bossdbg]  again %d,%d,%d state=%d count=%d\n", (int)MEM32(q), (int)MEM32(q + 4), (int)MEM32(q + 8),
                        (int)MEM32(obj + 0x64), (int)MEM32(def + 0x1B4));
                g_eax = (uint32_t)t1;
                int inlist = 0;
                for (uint32_t k = 0; k < MEM32(0x00514F74u); k++) if (MEM32(0x005147A0u + k * 4) == obj) inlist = 1;
                fprintf(stderr, "[bossdbg] sphere %d vertex %u radius %d at %d,%d,%d: hit=%d test=%d count=%u inlist=%d flag=%u hp %d -> %d\n", i,
                        MEM32(def + 0x1B8 + i * 4), (int)MEM32(def + 0x1D8 + i * 4), x, y, z, hit, (int)g_eax, MEM32(0x00514F74u),
                        inlist, MEM32(obj + 0x84), hp, (int)MEM32(obj + 0x1C));
            }
            g_esp = sp; g_eax = sv[0]; g_ecx = sv[1]; g_edx = sv[2];
        }
    }
    /* FURY3_YESNO=ms (testing): opens the Retry prompt that long after flight starts. */
    if (host_env("FURY3_YESNO") && MEM32(0x0053E860u) == 1) {
        static uint32_t since; static int done;
        if (!since) since = host_millis();
        if (!done && host_millis() - since >= (uint32_t)atoi(host_env("FURY3_YESNO"))) {
            done = 1;
            extern void sub_0040189D(void);
            uint32_t ret = g_esp; g_esp -= 4;
            sub_0040189D();
            g_esp = ret;
        }
    }
    /* The Retry prompt: Cross or Start for yes, Circle for no. Presses in its first moments
     * are ignored, in case fire was being mashed when the ship went down. */
    if (s_pad && host_yesno_ms()) {
        static uint8_t was;
        uint8_t yes = SDL_GameControllerGetButton(s_pad, SDL_CONTROLLER_BUTTON_A) |
                      SDL_GameControllerGetButton(s_pad, SDL_CONTROLLER_BUTTON_START);
        uint8_t no = SDL_GameControllerGetButton(s_pad, SDL_CONTROLLER_BUTTON_B);
        if (host_yesno_ms() >= 400 && !was) {
            if (yes) MEM32(0x00541F14u) = 'Y';
            else if (no) MEM32(0x00541F14u) = 'N';
        }
        was = yes | no;
    }
    if (s_pad && !menu_overlay_is_active()) {
        int dz = (int)(menu_overlay_get_deadzone() * 32767.0f);
        if (dz < 8000) dz = 8000;
        int lx = SDL_GameControllerGetAxis(s_pad, SDL_CONTROLLER_AXIS_LEFTX);
        int ly = SDL_GameControllerGetAxis(s_pad, SDL_CONTROLLER_AXIS_LEFTY);
        int rx = SDL_GameControllerGetAxis(s_pad, SDL_CONTROLLER_AXIS_RIGHTX);
        int ry = SDL_GameControllerGetAxis(s_pad, SDL_CONTROLLER_AXIS_RIGHTY);
        for (int i = 0; i < PADKEY_N; i++) {
            int b = menu_overlay_pad_for_action(s_padkeys[i].act);
            if (b >= 0 && SDL_GameControllerGetButton(s_pad, (SDL_GameControllerButton)b)) want[i] = 1;
            if (paused && s_padkeys[i].act == FURY3_ACT_FIRE_SECONDARY) want[i] = 0;   /* Square saves instead */
            switch (s_padkeys[i].act) {
                case FURY3_ACT_PITCH_UP:      if (ly < -dz) want[i] = 1; break;
                case FURY3_ACT_PITCH_DOWN:    if (ly >  dz) want[i] = 1; break;
                case FURY3_ACT_ROLL_LEFT:     if (lx < -dz) want[i] = 1; break;
                case FURY3_ACT_ROLL_RIGHT:    if (lx >  dz) want[i] = 1; break;
                /* right stick: the game's roll keys across, throttle up/down */
                case FURY3_ACT_YAW_LEFT:      if (rx < -dz) want[i] = 1; break;
                case FURY3_ACT_YAW_RIGHT:     if (rx >  dz) want[i] = 1; break;
                case FURY3_ACT_THROTTLE_UP:   if (ry < -dz) want[i] = 1; break;
                case FURY3_ACT_THROTTLE_DOWN: if (ry >  dz) want[i] = 1; break;
            }
        }
        want[PADKEY_N] = SDL_GameControllerGetButton(s_pad, SDL_CONTROLLER_BUTTON_START);
        /* Unbound buttons tap game keys (the in-flight ones: the handler at 0x43C2E5):
         *   d-pad left/right  previous/next weapon
         *   Select            in flight V, camera (cockpit / chase / external, [0x53E71C]);
         *                     on the title screen Ctrl+R, Resume Saved Game
         *   d-pad up          C, cockpit display (plain / cockpit frame / no HUD, [0x53E770])
         *   d-pad down        in flight, open or close the wings of a modded ship
         *   Square            on the title screen, level select: the d-pad picks a planet
         *                     and mission, and Start begins there
         *   Triangle          on the title screen, mods on/off (only if pod.ini exists)
         *   Square            while paused, Ctrl+S, Save Game (otherwise the weapon key).
         *                     The pause flag is a local of the flight loop (0x425415), but
         *                     it forces dt [0x4EB9D0] to 0 every frame, which flight
         *                     never does for more than a moment. */
        static const int tap_btn[7] = { SDL_CONTROLLER_BUTTON_DPAD_LEFT, SDL_CONTROLLER_BUTTON_DPAD_RIGHT,
                                        SDL_CONTROLLER_BUTTON_BACK, SDL_CONTROLLER_BUTTON_DPAD_UP,
                                        SDL_CONTROLLER_BUTTON_X, SDL_CONTROLLER_BUTTON_Y,
                                        SDL_CONTROLLER_BUTTON_DPAD_DOWN };
        int flight = MEM32(0x0053E860u) == 1;
        int title = MEM32(0x0053E860u) == 0 && MEM32(0x00542464u) == 0;
        for (int d = 0; d < 7; d++) {
            uint8_t on = (d == 4 || !pad_button_bound(tap_btn[d])) &&
                         SDL_GameControllerGetButton(s_pad, (SDL_GameControllerButton)tap_btn[d]);
            /* in the list, a held d-pad button repeats */
            static uint32_t again[7];
            int repeat = 0;
            if (on && s_lv_open && title && d != 2 && d != 4 && d != 5) {
                if (!tap_prev[d]) again[d] = host_millis() + 320;
                else if ((int32_t)(host_millis() - again[d]) >= 0) { again[d] = host_millis() + 110; repeat = 1; }
            }
            if ((on && !tap_prev[d]) || repeat) {
                if (s_lv_open && title && d != 4) {
                    if (d < 2) level_key(0, d ? 1 : -1);
                    else if (d == 3) level_key(-1, 0);
                    else if (d == 6) level_key(1, 0);
                }
                else if (d == 4 && title) s_lv_open = !s_lv_open;
                else if (d < 2) pad_weapon_step(d ? 1 : -1);
                else if (d == 2 && flight) pad_tap_key('V', 0x2F);
                else if (d == 2 && title) pad_tap_key2('R', 0x13, 1);
                else if (d == 3 && flight) pad_tap_key('C', 0x2E);
                else if (d == 4 && paused) pad_tap_key2('S', 0x1F, 1);
                else if (d == 5 && title) host_mods_toggle();      /* Triangle: restarts on the Vita */
                else if (d == 6 && flight && !paused) host_wings_toggle();
            }
            tap_prev[d] = on;
        }
    }
    /* Start: F2 (New Game) on the title screen -- the title loop (sub_0044FD80)
     * zeroes [0x542464] and F2's handler starts a game only while it is 0; F3
     * (pause / resume) in flight; Enter everywhere else. It picks its key when
     * pressed and releases the same one. */
    static uint32_t start_vk = VK_RETURN, start_sc = 0x1C;
    if (want[PADKEY_N] && !held[PADKEY_N]) {
        int on_title = MEM32(0x0053E860u) == 0 && MEM32(0x00542464u) == 0;
        int in_flight = MEM32(0x0053E860u) == 1;
        if (on_title && s_lv_open) host_level_pick(s_lv_planet * 3 + s_lv_mission);
        start_vk = on_title ? VK_F2 : in_flight ? VK_F3 : VK_RETURN;
        start_sc = on_title ? 0x3C : in_flight ? 0x3D : 0x1C;
    }
    for (int i = 0; i <= PADKEY_N; i++) {
        if (want[i] == held[i]) continue;
        uint32_t vk = i < PADKEY_N ? s_padkeys[i].vk : start_vk;
        uint32_t sc = i < PADKEY_N ? s_padkeys[i].sc : start_sc;
        synq_push_key(want[i] ? 0x0100u : 0x0101u, vk, sc);
        held[i] = want[i];
    }
}

/* Drain SDL events: menu first, then translate game input. Called each frame from
 * the pump. */
static void host_sdl_pump(void) {
    if (!s_sdl_ready) return;
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (menu_overlay_process_event(&e)) continue;     /* menu consumed it */
        switch (e.type) {
            case SDL_QUIT: g_quit = 1; break;
            case SDL_CONTROLLERDEVICEADDED:
                if (!s_pad && SDL_IsGameController(e.cdevice.which))
                    s_pad = SDL_GameControllerOpen(e.cdevice.which);
                break;
            case SDL_KEYDOWN:
            case SDL_KEYUP:
                if (!menu_overlay_is_active() && !e.key.repeat) {
                    uint32_t vk = sdl_to_vk(e.key.keysym.sym);
                    uint32_t sc = sdl_to_set1(e.key.keysym.scancode);
                    if (e.type == SDL_KEYDOWN && host_yesno_ms()) {     /* no WM_CHAR here: answer directly */
                        if (e.key.keysym.sym == SDLK_y) MEM32(0x00541F14u) = 'Y';
                        else if (e.key.keysym.sym == SDLK_n) MEM32(0x00541F14u) = 'N';
                        else if (e.key.keysym.sym == SDLK_ESCAPE) MEM32(0x00541F14u) = 0x1B;
                    }
                    if (vk || sc) synq_push_key(e.type == SDL_KEYDOWN ? 0x0100u /*WM_KEYDOWN*/
                                                                      : 0x0101u /*WM_KEYUP*/, vk, sc);
                }
                break;
        }
    }
    host_sdl_pad_to_keys();
    /* File-menu one-shots. New Game = synthesize F2 (the game's New-Game key). */
    if (menu_overlay_take_new_game()) { synq_push(0x0100u, VK_F2); synq_push(0x0101u, VK_F2); }
    if (menu_overlay_take_save_state())
        fprintf(stderr, "[sdl] Save State requested -- not yet wired (recomp save states "
                        "need a re-entrant snapshot of sim memory + CPU/FPU state).\n");
    if (menu_overlay_take_load_state())
        fprintf(stderr, "[sdl] Load State requested -- not yet wired.\n");
}

/* ---- joyGet* on the SDL game controller (Linux / Vita have no WINMM) ----
 * Fury3 reads JOYINFO {wXpos, wYpos, wZpos, wButtons} with axes 0..65535:
 * X = roll, Y = pitch, Z = throttle. Left stick drives X/Y, right stick Y the
 * throttle, and A/B/X/Y are buttons 1/2/4/8 (fury3.ini's buttonFire=1,
 * buttonWeapon=2, buttonThrottleDown=4, buttonThrottleUp=8). The game only uses
 * the joystick when fury3.ini says joystickActive=1. */
#define JOYERR_UNPLUGGED 167
static uint32_t pad_axis(SDL_GameControllerAxis a) {
    return (uint32_t)((int)SDL_GameControllerGetAxis(s_pad, a) + 32768);
}
uint32_t host_joy_getnumdevs(void) { return s_pad ? 1u : 0u; }
uint32_t host_joy_getpos(uint32_t id, uint32_t *x, uint32_t *y, uint32_t *z, uint32_t *btn) {
    if (id != 0 || !s_pad) return JOYERR_UNPLUGGED;
    *x = pad_axis(SDL_CONTROLLER_AXIS_LEFTX);
    *y = pad_axis(SDL_CONTROLLER_AXIS_LEFTY);
    *z = pad_axis(SDL_CONTROLLER_AXIS_RIGHTY);
    *btn = (SDL_GameControllerGetButton(s_pad, SDL_CONTROLLER_BUTTON_A) ? 1u : 0u)
         | (SDL_GameControllerGetButton(s_pad, SDL_CONTROLLER_BUTTON_B) ? 2u : 0u)
         | (SDL_GameControllerGetButton(s_pad, SDL_CONTROLLER_BUTTON_X) ? 4u : 0u)
         | (SDL_GameControllerGetButton(s_pad, SDL_CONTROLLER_BUTTON_Y) ? 8u : 0u);
    return 0;
}
/* JOYCAPSA: wMid, wPid, szPname[32], then UINT ranges/counts from offset 36. */
uint32_t host_joy_getdevcaps(uint32_t id, void *caps, uint32_t cb) {
    if (id != 0 || !s_pad) return JOYERR_UNPLUGGED;
    uint8_t* c = (uint8_t*)caps; memset(c, 0, cb);
    if (cb < 112) return 0;
    uint16_t one = 1; memcpy(c + 0, &one, 2); memcpy(c + 2, &one, 2);
    snprintf((char*)c + 4, 32, "%s", "SDL game controller");
    uint32_t v[] = { 0, 65535, 0, 65535, 0, 65535, 4, 10, 1000 };   /* X/Y/Z min,max; buttons; period */
    memcpy(c + 36, v, sizeof(v));
    uint32_t axes = 3, maxb = 4; memcpy(c + 100, &axes, 4); memcpy(c + 104, &axes, 4); memcpy(c + 108, &maxb, 4);
    return 0;
}

/* Is the game window focused (watchdog diagnostics)? */
static int host_window_is_foreground(void) {
#if defined(FURY3_GL) && defined(__vita__)
    if (s_gl) return 1;                 /* no SDL window in GPU mode, and always in front */
#endif
    return s_win && (SDL_GetWindowFlags(s_win) & SDL_WINDOW_INPUT_FOCUS) != 0;
}

/* Apply video settings the menu may have changed. */
static void host_sdl_apply_settings(void) {
    static int last_scale = 0, last_filter = -1, last_fs = -1;
    int scale = menu_overlay_get_scale();
#ifdef __vita__
    last_scale = scale;                    /* fixed-size screen: never resize */
#endif
    if (scale != last_scale) { last_scale = scale;
        int mh = menu_overlay_get_menubar_height();
        SDL_SetWindowSize(s_win, g_dib_w * scale, g_dib_h * scale + (mh ? mh : 20)); }
    int filter = menu_overlay_get_filter();
    if (filter != last_filter) { last_filter = filter;
        SDL_SetTextureScaleMode(s_tex, filter ? SDL_ScaleModeLinear : SDL_ScaleModeNearest); }
    int fs = menu_overlay_get_fullscreen();
    if (fs != last_fs) { last_fs = fs;
        SDL_SetWindowFullscreen(s_win, fs ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0); }
}

/* Title-screen prompts ("START  NEW GAME   SELECT  CONTINUE"), drawn over the
 * game's picture: the game itself only offers these through its Windows menu. */
static const uint8_t s_font5x7[][5] = {   /* columns, bit 0 = top row; letters A-Z then the extras below */
    {0x7E,0x11,0x11,0x11,0x7E}, {0x7F,0x49,0x49,0x49,0x36}, {0x3E,0x41,0x41,0x41,0x22}, {0x7F,0x41,0x41,0x22,0x1C},
    {0x7F,0x49,0x49,0x49,0x41}, {0x7F,0x09,0x09,0x09,0x01}, {0x3E,0x41,0x49,0x49,0x7A}, {0x7F,0x08,0x08,0x08,0x7F},
    {0x00,0x41,0x7F,0x41,0x00}, {0x20,0x40,0x41,0x3F,0x01}, {0x7F,0x08,0x14,0x22,0x41}, {0x7F,0x40,0x40,0x40,0x40},
    {0x7F,0x02,0x0C,0x02,0x7F}, {0x7F,0x04,0x08,0x10,0x7F}, {0x3E,0x41,0x41,0x41,0x3E}, {0x7F,0x09,0x09,0x09,0x06},
    {0x3E,0x41,0x51,0x21,0x5E}, {0x7F,0x09,0x19,0x29,0x46}, {0x46,0x49,0x49,0x49,0x31}, {0x01,0x01,0x7F,0x01,0x01},
    {0x3F,0x40,0x40,0x40,0x3F}, {0x1F,0x20,0x40,0x20,0x1F}, {0x3F,0x40,0x38,0x40,0x3F}, {0x63,0x14,0x08,0x14,0x63},
    {0x07,0x08,0x70,0x08,0x07}, {0x61,0x51,0x49,0x45,0x43},
};
static const char s_font_extra_chars[] = "0123456789+:./-_(),!?'%";
static const uint8_t s_font_extra[][5] = {
    {0x3E,0x51,0x49,0x45,0x3E}, {0x00,0x42,0x7F,0x40,0x00}, {0x42,0x61,0x51,0x49,0x46}, {0x21,0x41,0x45,0x4B,0x31},
    {0x18,0x14,0x12,0x7F,0x10}, {0x27,0x45,0x45,0x45,0x39}, {0x3C,0x4A,0x49,0x49,0x30}, {0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36}, {0x06,0x49,0x49,0x29,0x1E}, {0x08,0x08,0x3E,0x08,0x08}, {0x00,0x36,0x36,0x00,0x00},
    {0x00,0x60,0x60,0x00,0x00}, {0x20,0x10,0x08,0x04,0x02}, {0x08,0x08,0x08,0x08,0x08}, {0x40,0x40,0x40,0x40,0x40},
    {0x00,0x1C,0x22,0x41,0x00}, {0x00,0x41,0x22,0x1C,0x00}, {0x00,0x50,0x30,0x00,0x00}, {0x00,0x00,0x5F,0x00,0x00},
    {0x02,0x01,0x51,0x09,0x06}, {0x00,0x05,0x03,0x00,0x00}, {0x23,0x13,0x08,0x64,0x62},
};
static const uint8_t* font_glyph(char c) {
    if (c >= 'a' && c <= 'z') c = (char)(c - 32);
    if (c >= 'A' && c <= 'Z') return s_font5x7[c - 'A'];
    const char* x = c ? strchr(s_font_extra_chars, c) : NULL;
    return x ? s_font_extra[x - s_font_extra_chars] : NULL;
}
/* Draws `s` at (x, y) with p-pixel dots; returns the advance. */
static int draw_text(int x, int y, int p, const char* s) {
    SDL_Rect r[64]; int n = 0, x0 = x;
    for (; *s; s++, x += 6 * p) {
        const uint8_t* g = font_glyph(*s);
        if (!g) continue;
        for (int cx = 0; cx < 5; cx++)
            for (int cy = 0; cy < 7; cy++)
                if (g[cx] >> cy & 1) {
                    r[n].x = x + cx * p; r[n].y = y + cy * p; r[n].w = p; r[n].h = p;
                    if (++n == 64) { SDL_RenderFillRects(s_ren, r, n); n = 0; }
                }
    }
    if (n) SDL_RenderFillRects(s_ren, r, n);
    return x - x0;
}
int host_save_exists(void);
static void level_select_draw(const SDL_Rect* dst, int p) {
    int row = 11 * p, w = 150 * p, h = row * 8 + 26 * p, px = dst->x + (dst->w - w) / 2, py = dst->y + dst->h * 10 / 200;
    SDL_SetRenderDrawBlendMode(s_ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(s_ren, 0, 0, 0, 215);
    SDL_Rect bg = { px, py, w, h };
    SDL_RenderFillRect(s_ren, &bg);
    for (int i = 0; i < 8; i++) {
        int y = py + 5 * p + i * row;
        if (i == s_lv_planet) {
            SDL_Rect hi = { px + 3 * p, y - 2 * p, w - 6 * p, row - p };
            SDL_SetRenderDrawColor(s_ren, 120, 70, 10, 230);
            SDL_RenderFillRect(s_ren, &hi);
        }
        SDL_SetRenderDrawColor(s_ren, 255, 255, 255, 235);
        draw_text(px + 8 * p, y, p, s_planets[i]);
        if (i == s_lv_planet) {
            char m[12];
            snprintf(m, sizeof(m), "MISSION %d", s_lv_mission + 1);
            SDL_SetRenderDrawColor(s_ren, 255, 205, 70, 255);
            draw_text(px + w - (int)strlen(m) * 6 * p - 8 * p, y, p, m);
        }
    }
    int y = py + h - 14 * p, x = px + 8 * p;
    SDL_SetRenderDrawColor(s_ren, 255, 205, 70, 255);
    x += draw_text(x, y, p, s_pad ? "START" : "F2") + 6 * p;
    SDL_SetRenderDrawColor(s_ren, 255, 255, 255, 235);
    x += draw_text(x, y, p, "BEGIN") + 12 * p;
    SDL_SetRenderDrawColor(s_ren, 255, 205, 70, 255);
    x += draw_text(x, y, p, s_pad ? "SQUARE" : "F8") + 6 * p;
    SDL_SetRenderDrawColor(s_ren, 255, 255, 255, 235);
    draw_text(x, y, p, "CLOSE");
    SDL_SetRenderDrawBlendMode(s_ren, SDL_BLENDMODE_NONE);
}

static void host_sdl_title_prompts(const SDL_Rect* dst) {
    if (MEM32(0x0053E860u) != 0 || MEM32(0x00542464u) != 0) return;   /* the title loop only */
    if (s_lv_open) { level_select_draw(dst, dst->w / 320 > 1 ? dst->w / 320 : 2); return; }
    static uint32_t checked; static int have_save;
    if (host_millis() - checked > 1000) { have_save = host_save_exists(); checked = host_millis(); }
    const char *k1 = s_pad ? "START" : "F2", *k2 = s_pad ? "SELECT" : "CTRL+R";
    int p = dst->w / 320 > 1 ? dst->w / 320 : 2, gap = 14 * p;
    int w = (int)(strlen(k1) + 1 + 8) * 6 * p + (have_save ? gap + (int)(strlen(k2) + 1 + 8) * 6 * p : 0);
    int x = dst->x + (dst->w - w) / 2, y = dst->y + dst->h * 148 / 200;
    SDL_SetRenderDrawBlendMode(s_ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(s_ren, 0, 0, 0, 150);
    SDL_Rect bar = { x - 4 * p, y - 3 * p, w + 8 * p, 13 * p };
    SDL_RenderFillRect(s_ren, &bar);
    SDL_SetRenderDrawColor(s_ren, 255, 205, 70, 255);
    x += draw_text(x, y, p, k1) + 6 * p;
    SDL_SetRenderDrawColor(s_ren, 255, 255, 255, 235);
    x += draw_text(x, y, p, "NEW GAME");
    if (have_save) {
        x += gap;
        SDL_SetRenderDrawColor(s_ren, 255, 205, 70, 255);
        x += draw_text(x, y, p, k2) + 6 * p;
        SDL_SetRenderDrawColor(s_ren, 255, 255, 255, 235);
        draw_text(x, y, p, "CONTINUE");
    }
    if (host_mods_available()) {
        const char* mk = s_pad ? "TRIANGLE" : "F6";
        const char* ml = host_mods_changed() ? "RESTART TO APPLY" : host_mods_enabled() ? "MODS ON" : "MODS OFF";
        int mw = (int)(strlen(mk) + 1 + strlen(ml)) * 6 * p, mx = dst->x + (dst->w - mw) / 2, my = y - 15 * p;
        SDL_Rect mb = { mx - 4 * p, my - 3 * p, mw + 8 * p, 13 * p };
        SDL_SetRenderDrawColor(s_ren, 0, 0, 0, 150);
        SDL_RenderFillRect(s_ren, &mb);
        SDL_SetRenderDrawColor(s_ren, 255, 205, 70, 255);
        mx += draw_text(mx, my, p, mk) + 6 * p;
        SDL_SetRenderDrawColor(s_ren, 255, 255, 255, 235);
        draw_text(mx, my, p, ml);
    }
    {
        const char* lk = s_pad ? "SQUARE" : "F8", *ll = "LEVEL SELECT";
        int lw = (int)(strlen(lk) + 1 + strlen(ll)) * 6 * p, lx = dst->x + (dst->w - lw) / 2, ly = y + 15 * p;
        SDL_Rect lb = { lx - 4 * p, ly - 3 * p, lw + 8 * p, 13 * p };
        SDL_SetRenderDrawColor(s_ren, 0, 0, 0, 150);
        SDL_RenderFillRect(s_ren, &lb);
        SDL_SetRenderDrawColor(s_ren, 255, 205, 70, 255);
        lx += draw_text(lx, ly, p, lk) + 6 * p;
        SDL_SetRenderDrawColor(s_ren, 255, 255, 255, 235);
        draw_text(lx, ly, p, ll);
    }
    SDL_SetRenderDrawBlendMode(s_ren, SDL_BLENDMODE_NONE);
}

/* While paused: how to save, then confirmation once the save file is written. */
extern uint32_t g_last_save_ms;
static void host_sdl_pause_prompt(const SDL_Rect* dst) {
    if (s_paused_polls < 3) return;
    int saved = g_last_save_ms && host_millis() - g_last_save_ms < 3000;
    const char* key = saved ? "" : s_pad ? "SQUARE" : "CTRL+S";
    const char* label = saved ? "GAME SAVED" : "SAVE GAME";
    int p = dst->w / 320 > 1 ? dst->w / 320 : 2;
    int w = (int)(strlen(key) + (saved ? 0 : 1) + strlen(label)) * 6 * p;
    int x = dst->x + (dst->w - w) / 2, y = dst->y + dst->h * 148 / 200;
    SDL_SetRenderDrawBlendMode(s_ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(s_ren, 0, 0, 0, 150);
    SDL_Rect bar = { x - 4 * p, y - 3 * p, w + 8 * p, 13 * p };
    SDL_RenderFillRect(s_ren, &bar);
    if (!saved) {
        SDL_SetRenderDrawColor(s_ren, 255, 205, 70, 255);
        x += draw_text(x, y, p, key) + 6 * p;
    }
    SDL_SetRenderDrawColor(s_ren, 255, 255, 255, 235);
    draw_text(x, y, p, label);
    SDL_SetRenderDrawBlendMode(s_ren, SDL_BLENDMODE_NONE);
}

/* Word-wraps `text` (lines split on '\n') to `maxc` characters, drawing from y;
 * returns the y below it. */
static int draw_wrapped(int x, int y, int p, const char* text, int maxc) {
    char line[128];
    while (*text) {
        int n = 0, cut = -1;
        while (text[n] && text[n] != '\n' && n < maxc) { if (text[n] == ' ') cut = n; n++; }
        if (text[n] && text[n] != '\n' && cut > 0) n = cut;
        if (n > (int)sizeof(line) - 1) n = (int)sizeof(line) - 1;
        memcpy(line, text, (size_t)n); line[n] = 0;
        draw_text(x, y, p, line);
        y += 10 * p;
        text += n;
        while (*text == ' ') text++;
        if (*text == '\n') { text++; if (!n) y += 4 * p; }
    }
    return y;
}

/* A full-screen message before the game has started (setting up from a disc
 * image, or a problem with the game files). percent >= 0 adds a progress bar;
 * wait keeps it up until a button or key is pressed or the app is closed. */
void host_message(const char* title, const char* text, int percent, int wait) {
    fprintf(stderr, "[message] %s: %s%s\n", title, text, wait ? " (waiting)" : "");
    host_sdl_init("Fury3");
    if (!s_sdl_ready) return;
    uint32_t shown = host_millis();
    for (;;) {
        SDL_Event e; int pressed = 0;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) return;
            if (e.type == SDL_CONTROLLERDEVICEADDED && !s_pad) s_pad = SDL_GameControllerOpen(e.cdevice.which);
            if (e.type == SDL_KEYDOWN || e.type == SDL_CONTROLLERBUTTONDOWN || e.type == SDL_JOYBUTTONDOWN) pressed = 1;
        }
        int ow, oh; SDL_GetRendererOutputSize(s_ren, &ow, &oh);
        int p = ow >= 900 ? 3 : 2, margin = 12 * p, maxc = (ow - 2 * margin) / (6 * p);
        SDL_SetRenderDrawBlendMode(s_ren, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(s_ren, 10, 12, 20, 255);
        SDL_RenderClear(s_ren);
        int y = margin;
        SDL_SetRenderDrawColor(s_ren, 255, 205, 70, 255);
        y = draw_wrapped(margin, y, p, title, maxc) + 6 * p;
        SDL_SetRenderDrawColor(s_ren, 235, 235, 235, 255);
        y = draw_wrapped(margin, y, p, text, maxc) + 6 * p;
        if (percent >= 0) {
            SDL_Rect bg = { margin, y, ow - 2 * margin, 6 * p }, fg = bg;
            fg.w = bg.w * (percent > 100 ? 100 : percent) / 100;
            SDL_SetRenderDrawColor(s_ren, 60, 64, 80, 255); SDL_RenderFillRect(s_ren, &bg);
            SDL_SetRenderDrawColor(s_ren, 255, 205, 70, 255); SDL_RenderFillRect(s_ren, &fg);
        }
        if (wait) {
            SDL_SetRenderDrawColor(s_ren, 150, 150, 160, 255);
            draw_wrapped(margin, oh - margin - 7 * p, p, "Press any button to exit.", maxc);
        }
        SDL_SetRenderDrawBlendMode(s_ren, SDL_BLENDMODE_NONE);
#ifdef FURY3_GL
        if (s_gl) {
            int gw, gh; gl_window_size(&gw, &gh);
            SDL_RenderFlush(s_ren);
            gl_begin(gw, gh);
            gl_rgba((const uint32_t*)s_ui->pixels, 960, 544, 0);
            gl_swap();
        } else
#endif
        SDL_RenderPresent(s_ren);
        if (!wait) return;
        if (pressed && host_millis() - shown > 600) return;
        SDL_Delay(16);
    }
}

/* Present the chosen game framebuffer (sim VA `buf`, 8bpp) + the menu overlay. */
#ifdef FURY3_GL
static void gl_swap(void) {
#ifdef __vita__
    vglSwapBuffers(GL_FALSE);
#else
    if (host_env("FURY3_SDL_SHOT")) {
        static int shot = 0;
        if (!shot && host_millis() > (uint32_t)atoi(host_env("FURY3_SDL_SHOT"))) { shot = 1; gl_save_bmp("build/frames/sdl_shot.bmp"); }
    }
    SDL_GL_SwapWindow(s_win);
#endif
}
static const uint8_t* guest_ptr(uint32_t va) { return (const uint8_t*)(uintptr_t)ADDR(va); }
static void gl_window_size(int* w, int* h) {
#ifdef __vita__
    *w = 960; *h = 544;
#else
    SDL_GetWindowSize(s_win, w, h);
#endif
}
static void host_gl_present(uint32_t buf) {
    uint8_t pal[768];
    const gl_cap_vert* v; const gl_cap_poly* p; const uint8_t* under; uint32_t np;
    for (int c = 0; c < 256; c++) { pal[c * 3] = g_pal[c].rgbRed; pal[c * 3 + 1] = g_pal[c].rgbGreen; pal[c * 3 + 2] = g_pal[c].rgbBlue; }
    int ow, oh; gl_window_size(&ow, &oh);
    int dw = ow, dh = ow * 200 / 320;
    if (dh > oh) { dh = oh; dw = oh * 320 / 200; }
    SDL_Rect dst = { (ow - dw) / 2, (oh - dh) / 2, dw, dh };
    host_gpu_frame(&v, &p, &np, &under);
    gl_begin(ow, oh);
    gl_tables(pal, guest_ptr(0x0047C770u), guest_ptr(0x0047D780u));
    gl_view(dst.x, dst.y, dst.w, dst.h);
    if (under) gl_layer(under, 320, 200, 0);
    /* The radar globe is drawn after the HUD panel it sits in, so the frame's last
     * polygons, if they all lie in that corner, go over the 2D layer. */
    uint32_t late = np;
    while (late > 0) {
        const gl_cap_poly* q = &p[late - 1]; int in = 1;
        for (uint32_t k = 0; k < q->count; k++) if (v[q->first + k].sx < (262 << 16) || v[q->first + k].sy > (48 << 16)) in = 0;
        if (!in) break;
        late--;
    }
    gl_depth_layers(v, p, np);
    if (late) gl_polys(v, p, late, guest_ptr);
    gl_layer(guest_ptr(buf), 320, 200, np != 0);
    if (late < np) {
        static uint8_t marks[64000];
        gl_polys(v, p + late, np - late, guest_ptr);
        host_gpu_radar_marks(guest_ptr(buf), marks);
        gl_layer(marks, 320, 200, 1);
    }
    if (MEM32(0x0053E860u) != 2 && (s_paused_polls >= 3 || (MEM32(0x0053E860u) == 0 && MEM32(0x00542464u) == 0))) {
        /* Filling and uploading the overlay costs the Vita whole frames, and it only changes
         * with the list, or the once-a-second checks behind the prompts. */
        static uint32_t ui_key = 0xFFFFFFFFu;
        uint32_t key = (uint32_t)s_lv_open | (uint32_t)s_lv_planet << 1 | (uint32_t)s_lv_mission << 4 | (uint32_t)(s_paused_polls >= 3) << 6 | (host_millis() / 1000) << 7;
        gl_view(0, 0, ow, oh);
        if (key != ui_key || !gl_rgba_holds((const uint32_t*)s_ui->pixels)) {
            SDL_Rect udst = { 0, 0, 960, 544 };
            int uw = 544 * 320 / 200;
            udst.x = (960 - uw) / 2; udst.w = uw;
            SDL_FillRect(s_ui, NULL, 0);
            host_sdl_title_prompts(&udst);
            host_sdl_pause_prompt(&udst);
            SDL_RenderFlush(s_ren);
            gl_view(0, 0, ow, oh);
            gl_rgba((const uint32_t*)s_ui->pixels, 960, 544, 1);
            ui_key = key;
        } else {
            gl_rgba(NULL, 960, 544, 1);
        }
    }
    gl_swap();
    host_gpu_clear();
}
#endif

static void host_sdl_present(uint32_t buf) {
    if (!s_sdl_ready) return;
#ifdef FURY3_GL
    if (s_gl) { host_gl_present(buf); return; }
#endif
    host_sdl_apply_settings();
    int n = g_dib_w * g_dib_h;
    if (n > (int)(sizeof(s_rgba)/4)) n = (int)(sizeof(s_rgba)/4);
    uint32_t lut[256];
    for (int c = 0; c < 256; c++)
        lut[c] = 0xFF000000u | ((uint32_t)g_pal[c].rgbRed << 16) | ((uint32_t)g_pal[c].rgbGreen << 8) | g_pal[c].rgbBlue;
    const uint8_t* src = (const uint8_t*)(uintptr_t)ADDR(buf);
    for (int i = 0; i < n; i++) s_rgba[i] = lut[src[i]];
    SDL_UpdateTexture(s_tex, NULL, s_rgba, g_dib_w * 4);
    SDL_SetRenderDrawColor(s_ren, 18, 18, 22, 255);
    SDL_RenderClear(s_ren);
    int ow, oh; SDL_GetRendererOutputSize(s_ren, &ow, &oh);
    int mh = menu_overlay_get_menubar_height();
    /* Keep the game's 320x200 (16:10) shape; letterbox the rest. */
    int aw = ow, ah = oh - mh, dw = aw, dh = aw * g_dib_h / g_dib_w;
    if (dh > ah) { dh = ah; dw = ah * g_dib_w / g_dib_h; }
    SDL_Rect dst = { (aw - dw) / 2, mh + (ah - dh) / 2, dw, dh };
    SDL_RenderCopy(s_ren, s_tex, NULL, &dst);
    host_sdl_title_prompts(&dst);
    host_sdl_pause_prompt(&dst);
    /* FURY3_MENU_AT=<ms>: pop the menu open at a chosen time (so we can screenshot
     * it over live gameplay, after the game has reached flight with the menu shut). */
    if (host_env("FURY3_MENU_AT")) {
        static int opened = 0;
        if (!opened && host_millis() > (uint32_t)atoi(host_env("FURY3_MENU_AT"))) {
            opened = 1; SDL_Event ev; memset(&ev, 0, sizeof(ev));
            ev.type = SDL_KEYDOWN; ev.key.keysym.sym = SDLK_F1; ev.key.keysym.scancode = SDL_SCANCODE_F1;
            menu_overlay_process_event(&ev);
        }
    }
    menu_overlay_render();
    /* FURY3_SDL_SHOT=<ms>: save one screenshot of the composited frame (game+menu). */
    if (host_env("FURY3_SDL_SHOT")) {
        static int shot = 0;
        if (!shot && host_millis() > (uint32_t)atoi(host_env("FURY3_SDL_SHOT"))) {
            shot = 1; int w, h; SDL_GetRendererOutputSize(s_ren, &w, &h);
            SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
            if (s && SDL_RenderReadPixels(s_ren, NULL, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch) == 0)
                SDL_SaveBMP(s, "build/frames/sdl_shot.bmp");
            if (s) SDL_FreeSurface(s);
            fprintf(stderr, "[sdl] saved screenshot\n");
        }
    }
    SDL_RenderPresent(s_ren);
}
