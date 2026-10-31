/*
 * menu_overlay.cpp - Dear ImGui menu overlay for the Fury3 recomp (SDL2 frontend).
 *
 * A toggle-able (F1 / Escape) main menu bar drawn over the running game via the
 * SDL2 + SDLRenderer2 ImGui backends. Adapted from sp00nznet's lynxrecomp
 * menu_overlay, retargeted to Fury3's flight-sim control scheme: thirteen
 * bindable actions (pitch / roll / yaw / throttle / fire / afterburner / slide),
 * each with a keyboard scancode and a gamepad button, plus an analog-stick
 * option for pitch/roll. Settings + bindings persist to fury3_config.ini.
 *
 * Implements the C API in menu_overlay.h verbatim. Note the Fury3 contract:
 *   - init() / process_event() take void* (cast to SDL_Window, SDL_Renderer, SDL_Event ptrs)
 *   - render() takes NO arguments, so the renderer is stashed at init time.
 */

#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_sdlrenderer2.h"
#include <SDL.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

extern "C" {
#include "menu_overlay.h"
}

#define FURY3_CONFIG_PATH "fury3_config.ini"
#define FURY3_REPO_URL    "https://github.com/sp00nznet"

/* Human-readable labels for the controls table, indexed by FURY3_ACT_*. */
static const char *kActLabels[FURY3_ACT_COUNT] = {
    "Pitch Up", "Pitch Down",
    "Roll Left", "Roll Right",
    "Yaw Left", "Yaw Right",
    "Throttle Up", "Throttle Down",
    "Fire Primary", "Fire Secondary",
    "Afterburner", "Slide Left", "Slide Right"
};

/* Stable config-file tokens (must not change once shipped), indexed by FURY3_ACT_*. */
static const char *kActTokens[FURY3_ACT_COUNT] = {
    "pitch_up", "pitch_down",
    "roll_left", "roll_right",
    "yaw_left", "yaw_right",
    "throttle_up", "throttle_down",
    "fire_primary", "fire_secondary",
    "afterburner", "slide_left", "slide_right"
};

struct MenuState {
    bool enabled        = false;
    bool menu_open      = false;   /* toggled by F1 / Escape */
    bool quit_requested = false;

    SDL_Renderer *renderer = nullptr;

    /* Video */
    int  scale      = 3;
    int  filter     = 1;   /* 0=nearest, 1=linear (Fury3 is a smooth 3D game) */
    int  vsync      = 1;
    int  fullscreen = 0;
    int  scanlines  = 0;
    int  show_fps   = 0;

    /* Audio (not wired yet - stored only) */
    float volume = 1.0f;
    bool  mute   = false;

    /* Controls */
    int   key[FURY3_ACT_COUNT];   /* SDL_Scancode, -1 = unbound */
    int   pad[FURY3_ACT_COUNT];   /* SDL_GameControllerButton, -1 = unbound */
    float deadzone   = 0.15f;
    int   use_pad_axes = 1;       /* analog stick drives pitch/roll */

    /* Windows */
    bool show_controls = false;
    bool show_about    = false;

    /* Rebind capture. rebind_act = -1 when idle; rebind_pad: 0=keyboard, 1=gamepad. */
    int  rebind_act = -1;
    int  rebind_pad = 0;

    int  menubar_h = 0;

    /* One-shot File-menu requests (consumed by the host via take_*). */
    bool new_game_req = false;
    bool save_req     = false;
    bool load_req     = false;
};

static MenuState g;

/* ---- Defaults ---- */

static void set_default_keys(MenuState &m) {
    m.key[FURY3_ACT_PITCH_UP]       = SDL_SCANCODE_UP;
    m.key[FURY3_ACT_PITCH_DOWN]     = SDL_SCANCODE_DOWN;
    m.key[FURY3_ACT_ROLL_LEFT]      = SDL_SCANCODE_LEFT;
    m.key[FURY3_ACT_ROLL_RIGHT]     = SDL_SCANCODE_RIGHT;
    m.key[FURY3_ACT_YAW_LEFT]       = SDL_SCANCODE_Z;
    m.key[FURY3_ACT_YAW_RIGHT]      = SDL_SCANCODE_X;
    m.key[FURY3_ACT_THROTTLE_UP]    = SDL_SCANCODE_A;
    m.key[FURY3_ACT_THROTTLE_DOWN]  = SDL_SCANCODE_Q;
    m.key[FURY3_ACT_FIRE_PRIMARY]   = SDL_SCANCODE_LCTRL;
    m.key[FURY3_ACT_FIRE_SECONDARY] = SDL_SCANCODE_SPACE;
    m.key[FURY3_ACT_AFTERBURNER]    = SDL_SCANCODE_LSHIFT;
    m.key[FURY3_ACT_SLIDE_LEFT]     = SDL_SCANCODE_COMMA;
    m.key[FURY3_ACT_SLIDE_RIGHT]    = SDL_SCANCODE_PERIOD;
}

static void set_default_pad(MenuState &m) {
    /* Left stick drives pitch/roll via axes (use_pad_axes); D-pad mirrors them
     * as digital fallbacks. Triggers/shoulders for throttle, face for fire. */
    m.pad[FURY3_ACT_PITCH_UP]       = SDL_CONTROLLER_BUTTON_DPAD_UP;
    m.pad[FURY3_ACT_PITCH_DOWN]     = SDL_CONTROLLER_BUTTON_DPAD_DOWN;
    m.pad[FURY3_ACT_ROLL_LEFT]      = SDL_CONTROLLER_BUTTON_DPAD_LEFT;
    m.pad[FURY3_ACT_ROLL_RIGHT]     = SDL_CONTROLLER_BUTTON_DPAD_RIGHT;
    m.pad[FURY3_ACT_YAW_LEFT]       = SDL_CONTROLLER_BUTTON_LEFTSTICK;
    m.pad[FURY3_ACT_YAW_RIGHT]      = SDL_CONTROLLER_BUTTON_RIGHTSTICK;
    m.pad[FURY3_ACT_THROTTLE_UP]    = SDL_CONTROLLER_BUTTON_RIGHTSHOULDER;
    m.pad[FURY3_ACT_THROTTLE_DOWN]  = SDL_CONTROLLER_BUTTON_LEFTSHOULDER;
    m.pad[FURY3_ACT_FIRE_PRIMARY]   = SDL_CONTROLLER_BUTTON_A;
    m.pad[FURY3_ACT_FIRE_SECONDARY] = SDL_CONTROLLER_BUTTON_X;
    m.pad[FURY3_ACT_AFTERBURNER]    = SDL_CONTROLLER_BUTTON_B;
    m.pad[FURY3_ACT_SLIDE_LEFT]     = SDL_CONTROLLER_BUTTON_BACK;
    m.pad[FURY3_ACT_SLIDE_RIGHT]    = SDL_CONTROLLER_BUTTON_START;
}

static void set_default_binds(MenuState &m) { set_default_keys(m); set_default_pad(m); }

static void reset_defaults(MenuState &m) {
    m.scale = 3; m.filter = 1; m.vsync = 1; m.fullscreen = 0;
    m.scanlines = 0; m.show_fps = 0;
    m.volume = 1.0f; m.mute = false;
    m.deadzone = 0.15f; m.use_pad_axes = 1;
    set_default_binds(m);
}

/* ---- Config persistence (simple key=value ini, mirrors lynx/apple2) ---- */

static void config_save(void) {
    FILE *f = fopen(FURY3_CONFIG_PATH, "w");
    if (!f) return;
    fprintf(f, "# fury3 recomp settings\n");
    fprintf(f, "scale=%d\n",      g.scale);
    fprintf(f, "filter=%d\n",     g.filter);
    fprintf(f, "vsync=%d\n",      g.vsync);
    fprintf(f, "fullscreen=%d\n", g.fullscreen);
    fprintf(f, "scanlines=%d\n",  g.scanlines);
    fprintf(f, "show_fps=%d\n",   g.show_fps);
    fprintf(f, "volume=%d\n",     (int)(g.volume * 100.0f + 0.5f));
    fprintf(f, "mute=%d\n",       g.mute ? 1 : 0);
    fprintf(f, "deadzone=%d\n",   (int)(g.deadzone * 100.0f + 0.5f));
    fprintf(f, "use_pad_axes=%d\n", g.use_pad_axes ? 1 : 0);
    for (int i = 0; i < FURY3_ACT_COUNT; i++) {
        fprintf(f, "key_%s=%d\n", kActTokens[i], g.key[i]);
        fprintf(f, "pad_%s=%d\n", kActTokens[i], g.pad[i]);
    }
    fclose(f);
}

static int act_index_by_token(const char *tok) {
    for (int i = 0; i < FURY3_ACT_COUNT; i++)
        if (strcmp(tok, kActTokens[i]) == 0) return i;
    return -1;
}

static void config_load(void) {
    FILE *f = fopen(FURY3_CONFIG_PATH, "r");
    if (!f) return;
    char line[160];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
        char keybuf[96]; int val;
        if (sscanf(line, "%95[^=]=%d", keybuf, &val) != 2) continue;
        if      (!strcmp(keybuf, "scale"))        g.scale = val;
        else if (!strcmp(keybuf, "filter"))       g.filter = val ? 1 : 0;
        else if (!strcmp(keybuf, "vsync"))        g.vsync = val ? 1 : 0;
        else if (!strcmp(keybuf, "fullscreen"))   g.fullscreen = val ? 1 : 0;
        else if (!strcmp(keybuf, "scanlines"))    g.scanlines = val ? 1 : 0;
        else if (!strcmp(keybuf, "show_fps"))     g.show_fps = val ? 1 : 0;
        else if (!strcmp(keybuf, "volume"))       g.volume = val / 100.0f;
        else if (!strcmp(keybuf, "mute"))         g.mute = val != 0;
        else if (!strcmp(keybuf, "deadzone"))     g.deadzone = val / 100.0f;
        else if (!strcmp(keybuf, "use_pad_axes")) g.use_pad_axes = val ? 1 : 0;
        else if (!strncmp(keybuf, "key_", 4)) { int a = act_index_by_token(keybuf + 4); if (a >= 0) g.key[a] = val; }
        else if (!strncmp(keybuf, "pad_", 4)) { int a = act_index_by_token(keybuf + 4); if (a >= 0) g.pad[a] = val; }
    }
    fclose(f);
    if (g.scale < 1) g.scale = 1;
    if (g.scale > 6) g.scale = 6;
    if (g.volume < 0.0f) g.volume = 0.0f;
    if (g.volume > 1.0f) g.volume = 1.0f;
    if (g.deadzone < 0.0f) g.deadzone = 0.0f;
    if (g.deadzone > 1.0f) g.deadzone = 1.0f;
}

/* ---- Theme ---- */

static void apply_theme(void) {
    ImGui::StyleColorsDark();
    ImGuiStyle &s = ImGui::GetStyle();
    s.WindowRounding = 4.0f;
    s.FrameRounding  = 3.0f;
    s.GrabRounding   = 3.0f;
    ImVec4 *c = s.Colors;
    c[ImGuiCol_MenuBarBg]     = ImVec4(0.10f, 0.12f, 0.16f, 1.00f);
    c[ImGuiCol_Header]        = ImVec4(0.20f, 0.45f, 0.85f, 0.65f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.25f, 0.55f, 0.95f, 0.80f);
}

/* ================= public API ================= */

extern "C" void menu_overlay_init(void *sdl_window, void *sdl_renderer) {
    set_default_binds(g);
    SDL_Window   *window   = (SDL_Window *)sdl_window;
    SDL_Renderer *renderer = (SDL_Renderer *)sdl_renderer;
    if (getenv("FURY3_HEADLESS")) { g.enabled = false; return; }
    if (!window || !renderer)     { g.enabled = false; return; }

    g.renderer = renderer;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = NULL;
    apply_theme();
    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);

    config_load();
    g.enabled = true;
    if (getenv("FURY3_SHOW_CONTROLS")) { g.menu_open = true; g.show_controls = true; }
}

extern "C" void menu_overlay_shutdown(void) {
    if (!g.enabled) return;
    config_save();
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    g.enabled = false;
}

extern "C" int menu_overlay_process_event(const void *sdl_event) {
    if (!g.enabled) return 0;
    const SDL_Event *ev = (const SDL_Event *)sdl_event;

    /* Rebind capture takes precedence so the menu UI doesn't swallow it first. */
    if (g.rebind_act >= 0) {
        if (ev->type == SDL_KEYDOWN) {
            if (ev->key.keysym.scancode == SDL_SCANCODE_ESCAPE) g.rebind_act = -1;
            else if (g.rebind_pad == 0) { g.key[g.rebind_act] = ev->key.keysym.scancode; g.rebind_act = -1; }
            return 1;
        }
        if (ev->type == SDL_CONTROLLERBUTTONDOWN && g.rebind_pad == 1) {
            g.pad[g.rebind_act] = ev->cbutton.button;
            g.rebind_act = -1;
            return 1;
        }
        /* While capturing, swallow everything else too. */
        return 1;
    }

    /* Hotkeys: F1 toggles the menu, Escape closes it when open. */
    if (ev->type == SDL_KEYDOWN && ev->key.repeat == 0) {
        SDL_Scancode sc = ev->key.keysym.scancode;
        if (sc == SDL_SCANCODE_F1) { g.menu_open = !g.menu_open; return 1; }
        if (sc == SDL_SCANCODE_ESCAPE && g.menu_open) { g.menu_open = false; return 1; }
    }

    ImGui_ImplSDL2_ProcessEvent(ev);

    if (!g.menu_open) return 0;
    ImGuiIO &io = ImGui::GetIO();
    return (io.WantCaptureKeyboard || io.WantCaptureMouse) ? 1 : 0;
}

/* ---- Controls window ---- */

static void bind_action_row(int act) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(kActLabels[act]);

    /* Keyboard cell */
    ImGui::TableNextColumn();
    bool kb_rebinding = (g.rebind_act == act && g.rebind_pad == 0);
    const char *kn = kb_rebinding ? "<press a key>"
                   : (g.key[act] >= 0 ? SDL_GetScancodeName((SDL_Scancode)g.key[act]) : "<unset>");
    if (!kn || !kn[0]) kn = "<unset>";
    char kl[80]; snprintf(kl, sizeof(kl), "%s##k%d", kn, act);
    if (ImGui::Button(kl, ImVec2(-1, 0))) { g.rebind_act = act; g.rebind_pad = 0; }

    /* Gamepad cell */
    ImGui::TableNextColumn();
    bool pad_rebinding = (g.rebind_act == act && g.rebind_pad == 1);
    const char *pn = pad_rebinding ? "<press a pad button>"
                   : (g.pad[act] >= 0 ? SDL_GameControllerGetStringForButton((SDL_GameControllerButton)g.pad[act]) : "<unset>");
    if (!pn || !pn[0]) pn = "<unset>";
    char pl[80]; snprintf(pl, sizeof(pl), "%s##p%d", pn, act);
    if (ImGui::Button(pl, ImVec2(-1, 0))) { g.rebind_act = act; g.rebind_pad = 1; }
}

static void draw_controls_window(void) {
    ImGui::SetNextWindowSize(ImVec2(480, 520), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Controls", &g.show_controls)) {
        int npads = 0;
        for (int i = 0; i < SDL_NumJoysticks(); i++) if (SDL_IsGameController(i)) npads++;
        ImGui::Text("Gamepads connected: %d", npads);
        ImGui::TextDisabled("Click a cell, then press a key / pad button. Esc cancels.");
        ImGui::Separator();

        ImGui::Checkbox("Use analog stick for pitch/roll", (bool *)&g.use_pad_axes);
        ImGui::SetNextItemWidth(180);
        ImGui::SliderFloat("Dead zone", &g.deadzone, 0.0f, 1.0f, "%.2f");
        ImGui::Separator();

        if (ImGui::BeginTable("binds", 3,
                ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchSame)) {
            ImGui::TableSetupColumn("Action");
            ImGui::TableSetupColumn("Keyboard");
            ImGui::TableSetupColumn("Gamepad");
            ImGui::TableHeadersRow();
            for (int i = 0; i < FURY3_ACT_COUNT; i++) bind_action_row(i);
            ImGui::EndTable();
        }
        ImGui::Spacing();
        if (ImGui::Button("Reset to defaults")) set_default_binds(g);
        ImGui::SameLine();
        if (ImGui::Button("Save")) config_save();
    }
    ImGui::End();
}

static void draw_about_window(void) {
    ImGui::SetNextWindowSize(ImVec2(400, 240), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("About", &g.show_about)) {
        ImGui::Text("Fury3 recomp");
        ImGui::TextDisabled("Static recompilation of Fury3 (Terminal Reality, 1995)");
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::BulletText("x86 -> native C, SDL2 + ImGui frontend");
        ImGui::BulletText("Flight controls: keyboard or gamepad");
        ImGui::Spacing();
        ImGui::TextUnformatted("Source repository:");
        ImGui::TextWrapped("%s", FURY3_REPO_URL);
        if (ImGui::Button("Open repository in browser")) SDL_OpenURL(FURY3_REPO_URL);
    }
    ImGui::End();
}

extern "C" void menu_overlay_render(void) {
    if (!g.enabled) return;

    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    if (g.menu_open) {
        if (ImGui::BeginMainMenuBar()) {
            g.menubar_h = (int)(ImGui::GetWindowSize().y + 0.5f);

            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("New Game")) g.new_game_req = true;
                ImGui::Separator();
                if (ImGui::MenuItem("Save State", "F5")) g.save_req = true;
                if (ImGui::MenuItem("Load State", "F8")) g.load_req = true;
                ImGui::Separator();
                if (ImGui::MenuItem("Save settings"))  config_save();
                if (ImGui::MenuItem("Load settings"))  config_load();
                if (ImGui::MenuItem("Reset settings")) reset_defaults(g);
                ImGui::Separator();
                if (ImGui::MenuItem("Quit")) g.quit_requested = true;
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Video")) {
                if (ImGui::BeginMenu("Window Scale")) {
                    for (int s = 1; s <= 6; s++) {
                        char lbl[8]; snprintf(lbl, sizeof(lbl), "%dx", s);
                        if (ImGui::MenuItem(lbl, NULL, g.scale == s)) g.scale = s;
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Filter")) {
                    if (ImGui::MenuItem("Nearest (sharp)",  NULL, g.filter == 0)) g.filter = 0;
                    if (ImGui::MenuItem("Linear (smooth)",  NULL, g.filter == 1)) g.filter = 1;
                    ImGui::EndMenu();
                }
                ImGui::MenuItem("V-Sync",     NULL, (bool *)&g.vsync);
                ImGui::MenuItem("Fullscreen", NULL, (bool *)&g.fullscreen);
                ImGui::MenuItem("Scanlines",  NULL, (bool *)&g.scanlines);
                ImGui::MenuItem("Show FPS",   NULL, (bool *)&g.show_fps);
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Audio")) {
                ImGui::SetNextItemWidth(140);
                ImGui::SliderFloat("Volume", &g.volume, 0.0f, 1.0f, "%.2f");
                ImGui::MenuItem("Mute", NULL, &g.mute);
                ImGui::Separator();
                ImGui::TextDisabled("audio coming soon");
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Controls")) {
                ImGui::MenuItem("Configure...", NULL, &g.show_controls);
                ImGui::Separator();
                ImGui::Checkbox("Use analog stick for pitch/roll", (bool *)&g.use_pad_axes);
                ImGui::SetNextItemWidth(160);
                ImGui::SliderFloat("Dead zone", &g.deadzone, 0.0f, 1.0f, "%.2f");
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Help")) {
                ImGui::MenuItem("About", NULL, &g.show_about);
                if (ImGui::MenuItem("Source repository")) SDL_OpenURL(FURY3_REPO_URL);
                ImGui::EndMenu();
            }

            ImGui::EndMainMenuBar();
        }

        if (g.show_controls) draw_controls_window();
        if (g.show_about)    draw_about_window();
    } else {
        g.menubar_h = 0;
    }

    if (g.show_fps) {
        ImGuiIO &io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 90, 24), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.35f);
        if (ImGui::Begin("##fps", NULL,
                ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("%.0f FPS", io.Framerate);
        }
        ImGui::End();
    }

    ImGui::Render();
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), g.renderer);
}

/* ---- Getters / one-shots ---- */

extern "C" int menu_overlay_is_active(void) {
    if (!g.enabled) return 0;
    return (g.menu_open || g.rebind_act >= 0) ? 1 : 0;
}
extern "C" int menu_overlay_quit_requested(void) { return g.quit_requested; }

extern "C" int menu_overlay_get_scale(void)      { return g.enabled ? g.scale : 3; }
extern "C" int menu_overlay_get_filter(void)     { return g.filter; }
extern "C" int menu_overlay_get_vsync(void)      { return g.vsync; }
extern "C" int menu_overlay_get_fullscreen(void) { return g.fullscreen; }
extern "C" int menu_overlay_get_menubar_height(void) { return g.enabled ? g.menubar_h : 0; }

extern "C" float menu_overlay_get_volume(void) { return g.mute ? 0.0f : g.volume; }
extern "C" int   menu_overlay_get_mute(void)   { return g.mute ? 1 : 0; }

extern "C" int menu_overlay_key_for_action(int action) {
    if (action < 0 || action >= FURY3_ACT_COUNT) return -1;
    return g.key[action];
}
extern "C" int menu_overlay_pad_for_action(int action) {
    if (action < 0 || action >= FURY3_ACT_COUNT) return -1;
    return g.pad[action];
}
extern "C" float menu_overlay_get_deadzone(void) { return g.deadzone; }
extern "C" int   menu_overlay_use_pad_axes(void) { return g.use_pad_axes ? 1 : 0; }

extern "C" int menu_overlay_take_new_game(void)   { int r = g.new_game_req; g.new_game_req = false; return r; }
extern "C" int menu_overlay_take_save_state(void) { int r = g.save_req;     g.save_req     = false; return r; }
extern "C" int menu_overlay_take_load_state(void) { int r = g.load_req;     g.load_req     = false; return r; }
