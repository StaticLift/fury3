/*
 * Fury3 Static Recompilation - runtime + entry point.
 *
 * Builds the simulated address space (loads the original PE's sections at their
 * original VAs), defines the global register model, implements indirect-call
 * dispatch, installs the import bridges, and (optionally) calls the recompiled
 * entry point.
 *
 * The host exe is linked at a high image base (see CMakeLists / build) so the
 * original game's VA range [0x00400000 ..] is free and we can map it fixed,
 * keeping g_mem_base == 0 (ADDR(va) == va) for both data and the simulated stack.
 */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>            /* joyGetPos (Win32 frontend) */
#endif
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "win32_compat.h"
#include "host_os.h"
#include "recomp_types.h"
#include "imports.h"

/* Without SDL the runtime presents through the Win32 GDI window frontend. */
#if !defined(FURY3_SDL) && !defined(_WIN32)
#error "non-Windows builds need the SDL2 frontend (-DFURY3_SDL)"
#endif

/* Where the runtime keeps its files: the game data (gamedata/), FURY3.EXE, logs.
 * On PC that is the working directory; the Vita has no working directory, so it
 * is a fixed folder on the memory card. */
#ifdef __vita__
#include <psp2/appmgr.h>
#define FURY3_HOME "ux0:data/fury3/"
/* Lifted functions call each other natively and some have large frames, so the
 * guest's call depth becomes host stack depth. The C heap is sized explicitly so
 * the simulated address space (os_alloc_block) still fits in user memory. */
int sceUserMainThreadStackSize = 4 * 1024 * 1024;
int _newlib_heap_size_user = 96 * 1024 * 1024;
/* The system C library's own heap, separate from newlib's: the shader compiler module
 * allocates from it. The default is far too small to compile with. */
unsigned int sceLibcHeapSize = 16 * 1024 * 1024;
#else
#define FURY3_HOME ""
#endif

/* ---- global register model ---- */
uint32_t g_eax = 0, g_ecx = 0, g_edx = 0, g_esp = 0;
uint32_t g_ebx = 0, g_esi = 0, g_edi = 0, g_ebp = 0;
uint16_t g_seg_cs = 0, g_seg_ds = 0, g_seg_es = 0;
uint16_t g_seg_fs = 0, g_seg_gs = 0, g_seg_ss = 0;
ptrdiff_t g_mem_base = 0;
uint32_t g_fs_base = 0;  /* simulated TIB base (set in setup) */
uint32_t g_gs_base = 0;

/* Global x87 FPU stack (shared across all recompiled functions). */
int      g_fp_top = 0;
uint16_t g_fpu_cw = 0x037F;

uint32_t g_icall_trace[ICALL_TRACE_SIZE] = {0};
uint32_t g_icall_from[ICALL_TRACE_SIZE] = {0};
uint32_t g_icall_trace_idx = 0;
uint32_t g_icall_count = 0;
uint32_t g_cur_func = 0;   /* VA of the function currently executing */

/* getenv() is slow on Windows (it locks and scans the whole environment block,
 * case-insensitively), and the FURY3_* switches are tested on hot paths -- every
 * function entry, file read, blit and pump. With them checked through getenv the
 * game spent ~85% of its frame time in the CRT. Read each name once. Callers
 * pass string literals, so the cache keys on the pointer. */
const char* host_env(const char* name) {
    static struct { const char* key; const char* val; } cache[64];
    static int n = 0;
    for (int i = 0; i < n; i++) if (cache[i].key == name) return cache[i].val;
    const char* v = getenv(name);
    if (n < 64) { cache[n].key = name; cache[n].val = v; n++; }
    return v;
}

/* Function-entry tracer (active with -DRECOMP_TRACE). */
#ifdef RECOMP_TRACE
uint32_t g_enter_trace[RECOMP_ENTER_SIZE] = {0};
uint32_t g_enter_idx = 0;
void recomp_trace_enter(uint32_t va) {
    g_enter_trace[g_enter_idx % RECOMP_ENTER_SIZE] = va;
    g_enter_idx++;
    /* Periodic loop probe: dump the ring every 200M entries to reveal a tight loop
     * the message pump never returns from (debug only). */
    if (host_env("FURY3_LOOPDUMP") && (g_enter_idx % 20000000u) == 0)
        recomp_dump_trace("periodic loop probe (20M entries)");
    /* Loop watchdog: an absurd number of entries without progress -> dump + bail. */
    if (g_enter_idx == 4000000000u) {
        recomp_dump_trace("loop watchdog (4B entries)");
        fflush(stderr);
        _Exit(7);
    }
}
#endif
void recomp_dump_trace(const char* why) {
#ifdef RECOMP_TRACE
    fprintf(stderr, "\n=== function-entry trace (last 48 before: %s) ===\n", why);
    uint32_t n = g_enter_idx < 64 ? g_enter_idx : 64;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t idx = (g_enter_idx - n + i) % RECOMP_ENTER_SIZE;
        fprintf(stderr, "  #%-3u 0x%08X\n", g_enter_idx - n + i, g_enter_trace[idx]);
    }
    fprintf(stderr, "  (total function entries: %u)\n", g_enter_idx);
#else
    (void)why;
#endif
}

/* ---- memory layout ----
 * One compact block, guest VAs 0x00100000..FURY3_SPACE_END, allocated wherever
 * the host likes (g_mem_base = host address - FURY3_SPACE_BASE). The Vita can't
 * map memory at chosen addresses and has a tight budget, so every platform uses
 * this same layout. The game's heap peaks around 8 MB through mission load and
 * flight; 64 MB leaves room for long sessions. */
#define FURY3_SPACE_BASE 0x00100000u
#define FURY3_TIB_BASE   0x00100000u  /* simulated TIB/TEB (1 page) */
#define FURY3_TIB_SIZE   0x00001000u
#define FURY3_STACK_BASE 0x00200000u
#define FURY3_STACK_SIZE 0x00100000u  /* 1 MB */
#define FURY3_IMAGE_BASE 0x00400000u  /* the PE's own base; the image must end below the heap */
#define FURY3_HEAP_BASE  0x00600000u  /* simulated heap (shim allocations) */
#define FURY3_HEAP_SIZE  0x04000000u  /* 64 MB */
#define FURY3_SPACE_END  (FURY3_HEAP_BASE + FURY3_HEAP_SIZE)

static void* g_image_view = NULL;
static void* g_stack_view = NULL;
static void* g_tib_view = NULL;
static void* g_heap_view = NULL;
static uint32_t g_heap_next = FURY3_HEAP_BASE;

/* Bump-allocate `n` bytes from the simulated heap, `align`-aligned; returns a VA. */
uint32_t shim_alloc_aligned(uint32_t n, uint32_t align) {
    uint32_t mask = align - 1u;
    uint32_t va = (g_heap_next + mask) & ~mask;
    g_heap_next = va + ((n + 15u) & ~15u);
    if (g_heap_next > FURY3_HEAP_BASE + FURY3_HEAP_SIZE) {
        fprintf(stderr, "[shim_alloc] OUT OF SIMULATED HEAP\n");
        return 0;
    }
    return va;
}
uint32_t shim_alloc(uint32_t n) { return shim_alloc_aligned(n, 16); }

/* ---- host malloc/free/realloc/calloc: a simple free-list allocator that
 * replaces the lifted (and fragile) MSVC small-block heap. Each block carries a
 * 16-byte header [size, magic, free-next, pad]; data is 16-byte aligned. ---- */
#define HDR_MAGIC 0x46555259u  /* "FURY" */
static uint32_t g_free_head = 0;

uint32_t host_malloc(uint32_t n) {
    n = (n + 15u) & ~15u; if (!n) n = 16;
    uint32_t prev = 0, cur = g_free_head;
    while (cur) {                                   /* first-fit over the free list */
        uint32_t hdr = cur - 16;
        if (MEM32(hdr) >= n) {
            if (prev) MEM32(prev - 16 + 8) = MEM32(hdr + 8); else g_free_head = MEM32(hdr + 8);
            return cur;
        }
        prev = cur; cur = MEM32(hdr + 8);
    }
    uint32_t hdr = shim_alloc(16 + n);
    if (!hdr) return 0;
    MEM32(hdr) = n; MEM32(hdr + 4) = HDR_MAGIC; MEM32(hdr + 8) = 0;
    return hdr + 16;
}
void host_free(uint32_t ptr) {
    if (!ptr) return;
    uint32_t hdr = ptr - 16;
    if (MEM32(hdr + 4) != HDR_MAGIC) return;        /* not ours -> ignore (bad free) */
    MEM32(hdr + 8) = g_free_head; g_free_head = ptr;
}
uint32_t host_realloc(uint32_t ptr, uint32_t n) {
    if (!ptr) return host_malloc(n);
    if (!n) { host_free(ptr); return 0; }
    uint32_t hdr = ptr - 16;
    if (MEM32(hdr + 4) == HDR_MAGIC && MEM32(hdr) >= ((n + 15u) & ~15u)) return ptr;
    uint32_t old = (MEM32(hdr + 4) == HDR_MAGIC) ? MEM32(hdr) : 0;
    uint32_t np = host_malloc(n);
    uint32_t cp = old < n ? old : n;
    for (uint32_t i = 0; i < cp; i++) MEM8(np + i) = MEM8(ptr + i);
    host_free(ptr);
    return np;
}
uint32_t host_calloc(uint32_t num, uint32_t sz) {
    uint32_t n = num * sz, p = host_malloc(n);
    for (uint32_t i = 0; i < n; i++) MEM8(p + i) = 0;
    return p;
}

/* Milliseconds since runtime start (for timeGetTime/GetTickCount shims). */
static uint32_t g_start_tick = 0;
uint32_t host_millis(void) { return os_ticks_ms() - g_start_tick; }
/* The game's timer (imp_timeGetTime) needs 1 ms resolution: its frames are paced
 * to ~6 ms and dt = (timeGetTime delta * 1179.648)/18, so a coarse clock (Win32
 * GetTickCount is ~15 ms) rounds dt to 0 and freezes time-gated loops.
 * os_ticks_ms is 1 ms everywhere (timeBeginPeriod(1) on Windows). */
uint32_t host_hires_ms(void) {
    static uint32_t base = 0; static int init = 0;
    uint32_t t = os_ticks_ms();
    if (!init) { base = t; init = 1; }
    return t - base;   /* ms since first call, 1ms resolution, small range like host_millis */
}

/* ---- Joystick (flight controls) ----
 * Fury3 polls joyGetPos for analog stick (X=roll, Y=pitch, Z=throttle) + buttons.
 * The Win32 frontend forwards to the real WINMM joystick; the SDL frontend reads
 * the SDL game controller instead (platform_sdl.inc), which is what Linux and the
 * Vita have. */
#ifndef FURY3_SDL
uint32_t host_joy_getnumdevs(void) {
    /* joyGetNumDevs() reports supported PORTS (usually 16) regardless of what's
     * plugged in; Fury3 treats a non-zero count as "a joystick is present" and
     * tries to use it. Report the number of CONNECTED sticks (probe joyGetPos) so
     * that with none plugged in we return 0 and the game falls back to keyboard. */
    UINT ports = joyGetNumDevs(); JOYINFO ji; UINT connected = 0;
    for (UINT i = 0; i < ports; i++) if (joyGetPos(i, &ji) == JOYERR_NOERROR) connected++;
    return connected;
}
uint32_t host_joy_getpos(uint32_t id, uint32_t *x, uint32_t *y, uint32_t *z, uint32_t *btn) {
    JOYINFO ji; MMRESULT r = joyGetPos(id, &ji);
    if (r == JOYERR_NOERROR) { *x = ji.wXpos; *y = ji.wYpos; *z = ji.wZpos; *btn = ji.wButtons; }
    return (uint32_t)r;
}
uint32_t host_joy_getdevcaps(uint32_t id, void *caps, uint32_t cb) {
    return (uint32_t)joyGetDevCapsA(id, (LPJOYCAPSA)caps, cb);
}
#endif

/* ============================================================
 * Display: a real Win32 window + an 8-bit DIB the game renders into.
 *
 * The game CreateDIBSection's a 320x200 8bpp surface (we hand it a real pixel
 * buffer in simulated memory via ppvBits), renders into it, sets a 256-colour
 * palette, and StretchBlt's it to its window DC each frame. We map all that onto
 * one real window and StretchDIBits the buffer to it. Fake 32-bit handles stand
 * in for the GDI/USER objects: one window/window-DC, but a distinct handle per DIB
 * section and per memory DC, since the game re-creates its frame DIB and selects
 * it into a memory DC that it BitBlts from.
 * ============================================================ */
static void sim_read_str(uint32_t va, char* out, int outsz);  /* defined below */

#define H_HWND  0x00010001u
#define H_HDC   0x00020001u
#define H_DIB   0x00030001u          /* stock 1x1 bitmap of a fresh memory DC */
#define H_PAL   0x00040001u
#define H_DIB_BASE   0x00030100u     /* DIB sections: H_DIB_BASE + slot */
#define H_MEMDC_BASE 0x00050100u     /* memory DCs:   H_MEMDC_BASE + index */
#define FURY3_MAX_DIB_HANDLES 64
#define FURY3_MAX_MEMDC       16

static HWND g_hwnd = NULL;
static HDC  g_hdc  = NULL;
static uint32_t g_dib_buf = 0;          /* sim VA of the 8bpp pixel buffer */
static uint32_t g_dibs[8]; static int g_ndibs = 0;  /* all DIB surfaces (multi-buffered) */
static int g_dib_w = 320, g_dib_h = 200;
static struct { uint32_t buf; int w, h; } g_dib_tab[FURY3_MAX_DIB_HANDLES];
static uint32_t g_dib_next = 0;
static uint32_t g_memdc_sel[FURY3_MAX_MEMDC]; static uint8_t g_memdc_live[FURY3_MAX_MEMDC];
static uint32_t g_last_game_blit = 0;   /* host ms of the last BitBlt from a memory DC */
static volatile uint32_t g_game_frames = 0;    /* game presents (BitBlt from memory DC) */
static volatile uint32_t g_throttle_sleep_ms = 0;  /* total ms slept in host_frame_throttle */
static RGBQUAD g_pal[256];
static int g_quit = 0;
static uint32_t g_game_wndproc = 0;     /* the game's registered WndProc (sim VA) */

/* Call a lifted function by VA from host code (push dummy ret + dispatch). */
static void recomp_call_va(uint32_t va) {
    recomp_func_t fn = recomp_lookup_manual(va);
    if (!fn) fn = recomp_lookup(va);
    if (!fn) fn = recomp_lookup_import(va);
    if (fn) { PUSH32(g_esp, 0xDEAD0000u); fn(); }
}
/* Dispatch a window message to the game's WndProc (stdcall hwnd,msg,wParam,lParam).
 * The game's WM_PAINT handler is what BitBlt's the DIB to the window. Save/restore
 * the callee-saved regs + esp so re-entering lifted code here can't corrupt the
 * game's state across its PeekMessage call. */
/* The whole x87 state: the stack window position, its 8 slots and their int64
 * shadows, TOP and the control word. */
typedef struct { double* win; int64_t* win64; double v[8]; int64_t s[8]; int top; uint16_t cw; } FpuState;
static void fpu_save(FpuState* f) {
    f->win = g_st; f->win64 = g_st_i64;
    memcpy(f->v, g_st, sizeof(f->v)); memcpy(f->s, g_st_i64, sizeof(f->s));
    f->top = g_fp_top; f->cw = g_fpu_cw;
}
static void fpu_restore(const FpuState* f) {
    g_st = f->win; g_st_i64 = f->win64;
    memcpy(g_st, f->v, sizeof(f->v)); memcpy(g_st_i64, f->s, sizeof(f->s));
    g_fp_top = f->top; g_fpu_cw = f->cw;
}

static uint32_t call_game_wndproc(uint32_t msg, uint32_t wp, uint32_t lp) {
    if (!g_game_wndproc) return 0;
    /* Full save/restore: this re-enters lifted code from the message pump while
     * the game is mid-computation, so the WndProc must not perturb ANY game state
     * (integer regs + the global x87 stack). */
    uint32_t sa=g_eax,sc=g_ecx,sd=g_edx,sb=g_ebx,si=g_esi,sdi=g_edi,sp=g_esp;
    FpuState fs; fpu_save(&fs);
    PUSH32(g_esp, lp); PUSH32(g_esp, wp); PUSH32(g_esp, msg); PUSH32(g_esp, H_HWND);
    recomp_call_va(g_game_wndproc);
    uint32_t result = g_eax;
    g_eax=sa;g_ecx=sc;g_edx=sd;g_ebx=sb;g_esi=si;g_edi=sdi;g_esp=sp;
    fpu_restore(&fs);
    return result;
}

#ifndef FURY3_SDL
static LRESULT CALLBACK fury3_wndproc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_CLOSE || m == WM_DESTROY) { g_quit = 1; PostQuitMessage(0); return 0; }
    if (m == WM_KEYDOWN && w == VK_ESCAPE) { g_quit = 1; }
    /* Bridge into the game's lifted WndProc (its WM_PAINT blits the DIB, its key
     * handlers set the input flag). Full CPU+FPU state is saved/restored. */
    if (g_game_wndproc) { call_game_wndproc(m, (uint32_t)w, (uint32_t)l); }
    return DefWindowProcA(h, m, w, l);
}
static int host_window_is_foreground(void) { return GetForegroundWindow() == g_hwnd; }
#endif
void host_set_game_wndproc(uint32_t va) { g_game_wndproc = va; }
static int translate_path_str(const char* guest, char* out, int outsz);

/* DispatchMessageA: route a game MSG to the game's WndProc. Some game loops poll
 * an input flag ([0x541F14]) that only the WndProc sets on WM_KEYDOWN, so the
 * dispatch must actually reach it -- a no-op stub would hang those loops. */
/* Ctrl/Shift/Alt held, from the key messages the game dispatches (accelerator
 * flags FCONTROL 0x08 / FSHIFT 0x04 / FALT 0x10). */
static uint16_t g_key_mods;
void host_dispatch_msg(uint32_t lpMsg) {
    if (!lpMsg || !g_game_wndproc) return;
    uint32_t m = MEM32(lpMsg + 4), w = MEM32(lpMsg + 8), l = MEM32(lpMsg + 12);
    if (m == WM_KEYDOWN || m == WM_KEYUP || m == WM_SYSKEYDOWN || m == 0x0105u /* WM_SYSKEYUP */) {
        uint16_t bit = w == 0x11 ? 0x08 : w == 0x10 ? 0x04 : w == 0x12 ? 0x10 : 0;
        if (m == WM_KEYDOWN || m == WM_SYSKEYDOWN) g_key_mods |= bit; else g_key_mods &= (uint16_t)~bit;
    }
    if (m == WM_CLOSE || m == WM_DESTROY) { g_quit = 1; return; }
    if (m == WM_KEYDOWN && w == VK_ESCAPE) g_quit = 1;
    call_game_wndproc(m, w, l);
}

/* Translate an accelerator key into a WM_COMMAND sent to the game's WndProc.
 * The game's front-end keys (F2 = New Game -> command 32784, etc.) are an
 * ACCELERATOR table, not WndProc keystrokes: its message loop calls
 * TranslateAcceleratorA, which converts the keydown into WM_COMMAND. accelVA
 * points at the loaded RT_ACCELERATOR table (8-byte ACCEL entries:
 * WORD fFlags, WORD key, WORD id, WORD pad; fFlags bit0=FVIRTKEY, bit7=last). */
int host_translate_accel(uint32_t accelVA, uint32_t lpMsg) {
    if (!g_game_wndproc || !accelVA || !lpMsg) return 0;
    uint32_t msg = MEM32(lpMsg + 4), vk = MEM32(lpMsg + 8);
    static uint32_t dumped;
    if (dumped != accelVA && host_env("FURY3_ACCELDUMP")) {
        dumped = accelVA;
        for (int i = 0; i < 64; i++) {
            uint32_t e = accelVA + i * 8;
            fprintf(stderr, "[acceltab] flags=%02X key=%04X id=%u\n", MEM16(e), MEM16(e + 2), MEM16(e + 4));
            if (MEM16(e) & 0x80) break;
        }
    }
    if (msg != WM_KEYDOWN && msg != WM_SYSKEYDOWN) return 0;
    /* Ctrl+S (save) must not fire on a plain S. */
    uint16_t mods = g_key_mods | (msg == WM_SYSKEYDOWN ? 0x10 : 0);
    for (int i = 0; i < 64; i++) {
        uint32_t e = accelVA + i * 8;
        uint16_t fFlags = MEM16(e + 0), key = MEM16(e + 2), id = MEM16(e + 4);
        if ((fFlags & 0x01) && key == (uint16_t)vk && (fFlags & 0x1C) == mods) {
            call_game_wndproc(WM_COMMAND, (uint32_t)id | (1u << 16) /*accelerator*/, 0);
            fprintf(stderr, "[accel] vk=0x%X -> WM_COMMAND id=%u\n", vk, id);
            return 1;
        }
        if (fFlags & 0x80) break;     /* last entry */
    }
    return 0;
}

#ifdef FURY3_SDL
#include "platform_sdl.inc"
#else
void host_message(const char* title, const char* text, int percent, int wait) {
    (void)wait;
    fprintf(stderr, "[message] %s: %s", title, text);
    if (percent >= 0) fprintf(stderr, " (%d%%)", percent);
    fprintf(stderr, "\n");
}
#endif

/* ============================================================
 * waveOut on SDL2 queued audio (sound effects).
 *
 * The game mixes its own 8-bit PCM into 8 WAVEHDR buffers. It opens the device
 * with CALLBACK_WINDOW but never handles MM_WOM_DONE: each frame it walks its
 * headers and refills any whose dwFlags has WHDR_DONE (sub_0044A000). So all we
 * need is to queue written buffers to SDL and set WHDR_DONE once SDL has played
 * past them -- done on the main thread at frame boundaries (host_wave_poll).
 * Without SDL there is no device: waveOutGetNumDevs reports 0 and the game runs
 * silent, as before.
 * ============================================================ */
#define WHDR_DONE      0x01u
#define WHDR_PREPARED  0x02u
#define WHDR_INQUEUE   0x10u
#define WAVEHDR_DATA   0x00
#define WAVEHDR_LEN    0x04
#define WAVEHDR_FLAGS  0x10
#define FURY3_WAVE_HANDLE 0x00060001u
#define WAVE_MAX_PENDING 64
void host_wave_poll_done(void);
static struct { uint32_t hdr; uint64_t end; } g_wave_q[WAVE_MAX_PENDING];
static int g_wave_qn = 0;
static uint64_t g_wave_written = 0;          /* bytes handed to the device so far */
static int g_wave_bits = 8;
static uint32_t g_wave_ch = 1, g_wave_rate = 11025;
#ifdef FURY3_SDL
static SDL_AudioDeviceID g_wave_dev = 0;
/* (Re)opens the device in the format the game asked for; empty queue, so all of
 * g_wave_written counts as played. */
static int wave_device_open(void) {
    SDL_AudioSpec want, have; SDL_zero(want);
    want.freq = (int)g_wave_rate; want.channels = (Uint8)g_wave_ch; want.samples = 512;
    want.format = g_wave_bits == 8 ? AUDIO_U8 : AUDIO_S16LSB;
    g_wave_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!g_wave_dev) { fprintf(stderr, "[audio] SDL_OpenAudioDevice failed: %s\n", SDL_GetError()); return 0; }
    SDL_PauseAudioDevice(g_wave_dev, 0);
    return 1;
}
static void wave_device_close(void) { if (g_wave_dev) { SDL_CloseAudioDevice(g_wave_dev); g_wave_dev = 0; } }
static int g_wave_ever_opened(void) { return SDL_WasInit(SDL_INIT_AUDIO) || SDL_InitSubSystem(SDL_INIT_AUDIO) == 0; }
static int host_wave_available(void) {
    static int ok = -1;
    if (ok < 0) {
        ok = (SDL_WasInit(SDL_INIT_AUDIO) || SDL_InitSubSystem(SDL_INIT_AUDIO) == 0)
             && SDL_GetNumAudioDevices(0) != 0;
        if (!ok) fprintf(stderr, "[audio] no SDL audio device: %s\n", SDL_GetError());
    }
    return ok;
}
#endif

uint32_t host_wave_num_devs(void) {
#ifdef FURY3_SDL
    return host_wave_available() ? 1 : 0;
#else
    return 0;
#endif
}

/* WAVEOUTCAPSA (52 bytes): wMid, wPid, vDriverVersion, szPname[32], dwFormats,
 * wChannels, wReserved1, dwSupport. Claim every standard PCM format (the game
 * picks its rate/channels from dwFormats); SDL converts whatever it opens. */
uint32_t host_wave_get_caps(uint32_t pwoc, uint32_t cb) {
    if (!pwoc || host_wave_num_devs() == 0) return 2;      /* MMSYSERR_BADDEVICEID */
    for (uint32_t i = 0; i < cb; i++) MEM8(pwoc + i) = 0;
    const char* name = "SDL2 audio";
    if (cb >= 52) {
        MEM16(pwoc + 0) = 1; MEM16(pwoc + 2) = 1; MEM32(pwoc + 4) = 0x0100;
        for (int i = 0; name[i] && i < 31; i++) MEM8(pwoc + 8 + i) = (uint8_t)name[i];
        MEM32(pwoc + 40) = 0x00000FFFu;                      /* WAVE_FORMAT_1M08..4S16 */
        MEM16(pwoc + 44) = 2;
        MEM32(pwoc + 48) = 0x0004u | 0x0008u;                /* WAVECAPS_VOLUME | LRVOLUME */
    }
    return 0;
}

uint32_t host_wave_open(uint32_t phwo, uint32_t pwfx, uint32_t fdw) {
#ifdef FURY3_SDL
    if (!pwfx || !host_wave_available()) return 2;
    uint16_t tag = MEM16(pwfx + 0), ch = MEM16(pwfx + 2), bits = MEM16(pwfx + 14);
    uint32_t rate = MEM32(pwfx + 4);
    if (tag != 1 || (bits != 8 && bits != 16) || ch < 1 || ch > 2) return 32;  /* WAVERR_BADFORMAT */
    if (fdw & 0x1u) return 0;                                /* WAVE_FORMAT_QUERY */
    if (g_wave_dev) SDL_CloseAudioDevice(g_wave_dev);
    g_wave_bits = bits; g_wave_ch = ch; g_wave_rate = rate; g_wave_qn = 0; g_wave_written = 0;
    if (!wave_device_open()) return 1;
    if (phwo) MEM32(phwo) = FURY3_WAVE_HANDLE;
    fprintf(stderr, "[audio] waveOutOpen %u Hz, %u-bit, %u ch\n", rate, bits, ch);
    return 0;
#else
    (void)phwo; (void)pwfx; (void)fdw; return 2;
#endif
}

uint32_t host_wave_prepare(uint32_t pwh) {
    if (pwh) MEM32(pwh + WAVEHDR_FLAGS) |= WHDR_PREPARED;
    return 0;
}
uint32_t host_wave_unprepare(uint32_t pwh) {
    if (!pwh) return 0;
    if (MEM32(pwh + WAVEHDR_FLAGS) & WHDR_INQUEUE) return 33;   /* WAVERR_STILLPLAYING */
    MEM32(pwh + WAVEHDR_FLAGS) &= ~WHDR_PREPARED;
    return 0;
}

uint32_t host_wave_write(uint32_t pwh) {
    if (!pwh) return 11;                                       /* MMSYSERR_INVALPARAM */
    uint32_t data = MEM32(pwh + WAVEHDR_DATA), len = MEM32(pwh + WAVEHDR_LEN);
    uint32_t fl = MEM32(pwh + WAVEHDR_FLAGS);
    if (!(fl & WHDR_PREPARED)) return 34;                      /* WAVERR_UNPREPARED */
#ifdef FURY3_SDL
    if (g_wave_dev && data && len && g_wave_qn < WAVE_MAX_PENDING) {
        static uint8_t buf[1 << 16];
        float vol = menu_overlay_get_volume();
        uint32_t n = len < sizeof(buf) ? len : (uint32_t)sizeof(buf);
        if (g_wave_bits == 8) {
            for (uint32_t i = 0; i < n; i++)
                buf[i] = (uint8_t)(128 + (int)(((int)MEM8(data + i) - 128) * vol));
        } else {
            for (uint32_t i = 0; i + 1 < n; i += 2) {
                int16_t s = (int16_t)(MEM8(data + i) | (MEM8(data + i + 1) << 8));
                s = (int16_t)(s * vol);
                buf[i] = (uint8_t)s; buf[i + 1] = (uint8_t)((uint16_t)s >> 8);
            }
        }
        SDL_QueueAudio(g_wave_dev, buf, n);
        g_wave_written += n;
        /* FURY3_AUDIOCAP=<file.wav>: also record everything played (for checking by ear). */
        { static FILE* cap = NULL; static int tried = 0; static uint32_t capbytes = 0;
          if (!tried) { tried = 1; const char* p = host_env("FURY3_AUDIOCAP"); if (p) cap = fopen(p, "wb");
              if (cap) { uint8_t z[44] = {0}; fwrite(z, 1, 44, cap); } }
          if (cap) {
              fwrite(buf, 1, n, cap); capbytes += n;
              uint32_t ch = g_wave_ch, rate = g_wave_rate, bps = (uint32_t)g_wave_bits, ba = ch * bps / 8;
              long here = ftell(cap); fseek(cap, 0, SEEK_SET);
              uint8_t h[44]; memcpy(h, "RIFF", 4); *(uint32_t*)(h + 4) = 36 + capbytes; memcpy(h + 8, "WAVEfmt ", 8);
              *(uint32_t*)(h + 16) = 16; *(uint16_t*)(h + 20) = 1; *(uint16_t*)(h + 22) = (uint16_t)ch;
              *(uint32_t*)(h + 24) = rate; *(uint32_t*)(h + 28) = rate * ba; *(uint16_t*)(h + 32) = (uint16_t)ba;
              *(uint16_t*)(h + 34) = (uint16_t)bps; memcpy(h + 36, "data", 4); *(uint32_t*)(h + 40) = capbytes;
              fwrite(h, 1, 44, cap); fseek(cap, here, SEEK_SET); fflush(cap);
          } }
        g_wave_q[g_wave_qn].hdr = pwh; g_wave_q[g_wave_qn].end = g_wave_written; g_wave_qn++;
        { static uint32_t nw = 0, loud = 0, peakmax = 0; nw++;
          if (host_env("FURY3_FLY") && g_wave_bits == 8) {
              uint32_t pk = 0; for (uint32_t i = 0; i < n; i++) { int d = (int)MEM8(data + i) - 128; if (d < 0) d = -d; if ((uint32_t)d > pk) pk = (uint32_t)d; }
              if (pk > 4) loud++; if (pk > peakmax) peakmax = pk;
              if (nw <= 6 || (nw % 250) == 0) {
                  fprintf(stderr, "[audio] write #%u @%ums len=%u queued=%u pending=%d non-silent=%u/%u peak=%u\n", nw, host_millis(),
                          n, (unsigned)SDL_GetQueuedAudioSize(g_wave_dev), g_wave_qn, loud, nw, peakmax);
                  peakmax = 0; } } }
        MEM32(pwh + WAVEHDR_FLAGS) = (fl & ~WHDR_DONE) | WHDR_INQUEUE;
        return 0;
    }
#else
    (void)data; (void)len;
#endif
    MEM32(pwh + WAVEHDR_FLAGS) = (fl & ~WHDR_INQUEUE) | WHDR_DONE;   /* nowhere to play it */
    return 0;
}

#ifdef FURY3_SDL
#include "movie_sdl.inc"
#endif
/* A movie the game opened and then started (imp_mciSendStringA). */
static char g_movie_path[600];
void host_mci_open(const char* guest_path) { translate_path_str(guest_path, g_movie_path, sizeof(g_movie_path)); }
void host_mci_play(void) {
#ifdef FURY3_SDL
    host_movie_play(g_movie_path);
#endif
}

/* The game's sound thread, sub_00409B90 (started through thunk 0x401B81):
 *     do { sub_0044A000(); Sleep(50); } while (![0x53E7C8]);
 * sub_0044A000 refills every WHDR_DONE buffer. While the thread's handle
 * ([0x53C10C]) is non-zero the game leaves refilling to it -- its MM_WOM_DONE
 * path (0x40B0B8) is skipped. Lifted code is single-threaded, so CreateThread
 * records the thread and the runtime runs one loop iteration every 50 ms at a
 * frame boundary, with the interrupted guest state saved around it. */
static int g_sound_thread = 0;
void host_thread_created(uint32_t start) {
    if (start == 0x00401B81u || start == 0x00409B90u) g_sound_thread = 1;
    else fprintf(stderr, "[thread] CreateThread(start=0x%08X) is not emulated\n", start);
}
static void sound_thread_tick(void) {
    static uint32_t last = 0; static int busy = 0;
    if (!g_sound_thread || busy || MEM32(0x0053E7C8)) return;
    uint32_t now = host_millis();
    if (now - last < 50) return;
    last = now; busy = 1;
    uint32_t sa = g_eax, sc = g_ecx, sd = g_edx, sb = g_ebx, si = g_esi, sdi = g_edi, sp = g_esp, sbp = g_ebp;
    uint32_t sf = g_cur_func, fk = g_flag_k, fa = g_flag_a, fb = g_flag_b, fcf = g_flag_cf;
    FpuState fs; fpu_save(&fs);
    recomp_call_va(0x0044A000u);
    g_eax = sa; g_ecx = sc; g_edx = sd; g_ebx = sb; g_esi = si; g_edi = sdi; g_esp = sp; g_ebp = sbp;
    g_cur_func = sf; g_flag_k = fk; g_flag_a = fa; g_flag_b = fb; g_flag_cf = fcf;
    fpu_restore(&fs);
    busy = 0;
}

/* Mark buffers SDL has finished playing as WHDR_DONE, then give the emulated
 * sound thread its turn. Main thread only. */
void host_wave_poll(void) {
    host_wave_poll_done();
    sound_thread_tick();
}
void host_wave_poll_done(void) {
#ifdef FURY3_SDL
    if (!g_wave_dev || !g_wave_qn) return;
    uint64_t played = g_wave_written - SDL_GetQueuedAudioSize(g_wave_dev);
    int k = 0;
    while (k < g_wave_qn && g_wave_q[k].end <= played) {
        uint32_t h = g_wave_q[k].hdr;
        MEM32(h + WAVEHDR_FLAGS) = (MEM32(h + WAVEHDR_FLAGS) & ~WHDR_INQUEUE) | WHDR_DONE;
        k++;
    }
    if (k) { memmove(g_wave_q, g_wave_q + k, (size_t)(g_wave_qn - k) * sizeof(g_wave_q[0])); g_wave_qn -= k; }
#endif
}

uint32_t host_wave_reset(void) {
#ifdef FURY3_SDL
    if (g_wave_dev) SDL_ClearQueuedAudio(g_wave_dev);
#endif
    for (int k = 0; k < g_wave_qn; k++) {
        uint32_t h = g_wave_q[k].hdr;
        MEM32(h + WAVEHDR_FLAGS) = (MEM32(h + WAVEHDR_FLAGS) & ~WHDR_INQUEUE) | WHDR_DONE;
    }
    g_wave_qn = 0;
    return 0;
}

uint32_t host_wave_close(void) {
    host_wave_reset();
#ifdef FURY3_SDL
    if (g_wave_dev) { SDL_CloseAudioDevice(g_wave_dev); g_wave_dev = 0; }
#endif
    return 0;
}

uint32_t host_create_window(uint32_t title_va) {
    char title[128]; sim_read_str(title_va, title, sizeof(title));
#ifdef FURY3_SDL
    /* SDL2 + Dear ImGui frontend: SDL owns the window/present; the game's HWND is a
     * benign non-null fake (its GDI calls are shimmed; we present via SDL). */
    host_sdl_init(title);
    g_hwnd = (HWND)(uintptr_t)0x1;
    return H_HWND;
#else
    if (!title[0]) snprintf(title, sizeof(title), "Fury3 (recompiled)");
    WNDCLASSA wc; memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = fury3_wndproc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
    wc.lpszClassName = "Fury3Wnd";
    RegisterClassA(&wc);
    RECT r = { 0, 0, g_dib_w * 2, g_dib_h * 2 };
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    g_hwnd = CreateWindowExA(0, "Fury3Wnd", title, WS_OVERLAPPEDWINDOW,
                             CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top,
                             NULL, NULL, wc.hInstance, NULL);
    ShowWindow(g_hwnd, SW_SHOW);
    g_hdc = GetDC(g_hwnd);
    printf("[gfx] window created (%dx%d)\n", g_dib_w, g_dib_h);
    return H_HWND;
#endif
}

/* CreateDIBSection: parse the BITMAPINFOHEADER, allocate a sim pixel buffer, and
 * hand its VA back through ppvBits so the game renders into real memory. */
uint32_t host_create_dib(uint32_t pbmi_va, uint32_t ppvbits_va) {
    int w = (int32_t)MEM32(pbmi_va + 4);
    int h = (int32_t)MEM32(pbmi_va + 8);
    uint16_t bpp = MEM16(pbmi_va + 14);
    if (w) g_dib_w = w;
    if (h) g_dib_h = h < 0 ? -h : h;
    uint32_t stride = ((uint32_t)g_dib_w * (bpp ? bpp : 8) + 31) / 32 * 4;
    g_dib_buf = shim_alloc(stride * g_dib_h + 64);
    if (g_ndibs < 8) g_dibs[g_ndibs++] = g_dib_buf;
    if (ppvbits_va) MEM32(ppvbits_va) = g_dib_buf;
    uint32_t slot = g_dib_next++ % FURY3_MAX_DIB_HANDLES;
    g_dib_tab[slot].buf = g_dib_buf; g_dib_tab[slot].w = g_dib_w; g_dib_tab[slot].h = g_dib_h;
    printf("[gfx] CreateDIBSection %dx%d %dbpp -> buf 0x%08X (handle 0x%08X)\n",
           g_dib_w, g_dib_h, bpp, g_dib_buf, H_DIB_BASE + slot);
    return H_DIB_BASE + slot;
}

/* Memory DCs. The game keeps its current frame DIB selected into a memory DC and
 * presents with BitBlt/StretchBlt(windowDC, ..., memDC, ...) from WM_PAINT, so the
 * screen must show whatever DIB the *source* DC holds -- not a guess. */
uint32_t host_create_memdc(void) {
    for (uint32_t i = 0; i < FURY3_MAX_MEMDC; i++)
        if (!g_memdc_live[i]) { g_memdc_live[i] = 1; g_memdc_sel[i] = 0; return H_MEMDC_BASE + i; }
    fprintf(stderr, "[dc] out of memory-DC slots (%d live) from 0x%08X\n", FURY3_MAX_MEMDC, g_cur_func);
    return 0;
}
void host_delete_dc(uint32_t hdc) {
    uint32_t i = hdc - H_MEMDC_BASE;
    if (i < FURY3_MAX_MEMDC) { g_memdc_live[i] = 0; g_memdc_sel[i] = 0; }
}
static int dib_slot_of(uint32_t h) {
    uint32_t i = h - H_DIB_BASE;
    return (i < FURY3_MAX_DIB_HANDLES && g_dib_tab[i].buf) ? (int)i : -1;
}
/* SelectObject(hdc, obj) -> previously selected object of that kind. Only bitmap
 * selection into our memory DCs is modelled; everything else keeps the old
 * "success" behaviour. A fresh memory DC holds the 1x1 stock bitmap (H_DIB). */
uint32_t host_select_object(uint32_t hdc, uint32_t obj) {
    uint32_t i = hdc - H_MEMDC_BASE;
    int is_bitmap = dib_slot_of(obj) >= 0 || obj == H_DIB;
    if (i >= FURY3_MAX_MEMDC || !is_bitmap) return 1;
    uint32_t prev = g_memdc_sel[i] ? g_memdc_sel[i] : H_DIB;
    g_memdc_sel[i] = obj;
    return prev;
}
void host_gpu_palette(uint8_t rgb[768]) {
    for (int i = 0; i < 256; i++) { rgb[i * 3] = g_pal[i].rgbRed; rgb[i * 3 + 1] = g_pal[i].rgbGreen; rgb[i * 3 + 2] = g_pal[i].rgbBlue; }
}
/* BitBlt/StretchBlt: present the DIB selected into the source DC. */
void host_blit_from_dc(uint32_t hdc_src) {
    uint32_t i = hdc_src - H_MEMDC_BASE;
    int s = (i < FURY3_MAX_MEMDC) ? dib_slot_of(g_memdc_sel[i]) : -1;
    if (s >= 0) {
        g_dib_buf = g_dib_tab[s].buf; g_dib_w = g_dib_tab[s].w; g_dib_h = g_dib_tab[s].h;
        g_last_game_blit = host_millis();
        g_game_frames++;
        /* FURY3_SNAP=<dir>: save the frame the game presents every 5s, tagged with
         * render mode (0 menu, 2 briefing/intros/fly-in, 1 flight). */
        static const char* snap = (const char*)-1;
        static uint32_t last_snap = 0;
        if (snap == (const char*)-1) snap = host_env("FURY3_SNAP");
        if (snap && host_millis() - last_snap >= 5000) {
            void host_dump_bmp(const char*, uint32_t);
            char path[260];
            last_snap = host_millis();
            snprintf(path, sizeof(path), "%s/snap_%06u_r%d.bmp", snap, host_millis(), (int)MEM32(0x0053E860));
            host_dump_bmp(path, g_dib_buf);
            { void host_gpu_dump(const char*); char w[280]; snprintf(w, sizeof(w), "%s/wire_%06u.bmp", snap, last_snap); host_gpu_dump(w);
              void host_gpu_paint(const char*); snprintf(w, sizeof(w), "%s/paint_%06u.bmp", snap, last_snap); host_gpu_paint(w); }
        }
    }
    host_blit();
    { void host_gpu_end_frame(void); host_gpu_end_frame(); }
}

/* Load an Adobe Color Table (256 RGB triplets) into the palette as a fallback,
 * so a rendered frame is visible even while the game's own palette is mid-fade. */
void host_load_act(const char* path) {
    FILE* f = fopen(path, "rb"); if (!f) return;
    uint8_t rgb[768];
    if (fread(rgb, 1, 768, f) == 768)
        for (int i = 0; i < 256; i++) {
            g_pal[i].rgbRed = rgb[i*3]; g_pal[i].rgbGreen = rgb[i*3+1];
            g_pal[i].rgbBlue = rgb[i*3+2]; g_pal[i].rgbReserved = 0;
        }
    fclose(f);
}
static int pal_nonblack(void) {
    for (int i = 0; i < 256; i++) if (g_pal[i].rgbRed | g_pal[i].rgbGreen | g_pal[i].rgbBlue) return 1;
    return 0;
}

/* Capture the palette from a LOGPALETTE (CreatePalette): WORD ver, WORD count,
 * then PALETTEENTRY[] {R,G,B,flags}. This is the path Fury3 uses for its colours. */
void host_set_logpalette(uint32_t logpal) {
    if (!logpal) return;
    uint32_t n = MEM16(logpal + 2); if (n > 256) n = 256;
    RGBQUAD tmp[256]; uint32_t any = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t e = logpal + 4 + i * 4;
        tmp[i].rgbRed = MEM8(e+0); tmp[i].rgbGreen = MEM8(e+1);
        tmp[i].rgbBlue = MEM8(e+2); tmp[i].rgbReserved = 0;
        any |= tmp[i].rgbRed | tmp[i].rgbGreen | tmp[i].rgbBlue;
    }
    if (!any) return;                /* don't clobber a good palette with a black fade frame */
    for (uint32_t i = 0; i < n; i++) g_pal[i] = tmp[i];
}

void host_set_dib_colors(uint32_t start, uint32_t n, uint32_t prgb_va) {
    if (start + n > 256) n = 256 - start;
    RGBQUAD tmp[256]; uint32_t any = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t e = prgb_va + i * 4;            /* RGBQUAD: B,G,R,reserved */
        tmp[i].rgbBlue = MEM8(e+0); tmp[i].rgbGreen = MEM8(e+1);
        tmp[i].rgbRed = MEM8(e+2); tmp[i].rgbReserved = 0;
        any |= tmp[i].rgbRed | tmp[i].rgbGreen | tmp[i].rgbBlue;
    }
    if (!any) return;
    for (uint32_t i = 0; i < n; i++) g_pal[start + i] = tmp[i];
}

/* Dump the current 8-bit DIB (through the palette) to a 24-bit BMP, for inspection. */
void host_dump_bmp(const char* path, uint32_t buf) {
    if (!buf) return;
    int w = g_dib_w, h = g_dib_h, row = w * 3;
    uint32_t pix = (uint32_t)row * h, total = 54 + pix;
    FILE* f = fopen(path, "wb"); if (!f) return;
    uint8_t hdr[54] = {0};
    hdr[0]='B'; hdr[1]='M';
    *(uint32_t*)(hdr+2)=total; *(uint32_t*)(hdr+10)=54;
    *(uint32_t*)(hdr+14)=40; *(int32_t*)(hdr+18)=w; *(int32_t*)(hdr+22)=h;
    *(uint16_t*)(hdr+26)=1; *(uint16_t*)(hdr+28)=24; *(uint32_t*)(hdr+34)=pix;
    fwrite(hdr,1,54,f);
    for (int y = h - 1; y >= 0; y--)               /* BMP is bottom-up */
        for (int x = 0; x < w; x++) {
            uint8_t idx = MEM8(buf + (uint32_t)y * w + x);
            fputc(g_pal[idx].rgbBlue, f); fputc(g_pal[idx].rgbGreen, f); fputc(g_pal[idx].rgbRed, f);
        }
    fclose(f);
    fprintf(stderr, "[gfx] dumped %s (%dx%d)\n", path, w, h);
}

/* Voxel-aware dump: Fury3's span rasterizer is column-major -- dword_47BB70[x] is
 * the base of screen column x and pixel(x,y) = MEM8(col[x] + y). Transpose into a
 * row-major image and save (through the palette). table = VA of the 320-entry
 * column-pointer table (0x47BB70). */
void host_dump_voxel(const char* path, uint32_t table) {
    if (!table) return;
    int w = g_dib_w, h = g_dib_h, row = w * 3;
    uint32_t pix = (uint32_t)row * h, total = 54 + pix, nz = 0;
    FILE* f = fopen(path, "wb"); if (!f) return;
    uint8_t hdr[54] = {0};
    hdr[0]='B'; hdr[1]='M';
    *(uint32_t*)(hdr+2)=total; *(uint32_t*)(hdr+10)=54;
    *(uint32_t*)(hdr+14)=40; *(int32_t*)(hdr+18)=w; *(int32_t*)(hdr+22)=h;
    *(uint16_t*)(hdr+26)=1; *(uint16_t*)(hdr+28)=24; *(uint32_t*)(hdr+34)=pix;
    fwrite(hdr,1,54,f);
    for (int y = h - 1; y >= 0; y--) {             /* BMP bottom-up; dword_47BB70[y]=row base */
        uint32_t rowp = MEM32(table + (uint32_t)y * 4);
        for (int x = 0; x < w; x++) {
            uint8_t idx = rowp ? MEM8(rowp + (uint32_t)x) : 0;
            if (idx) nz++;
            fputc(g_pal[idx].rgbBlue, f); fputc(g_pal[idx].rgbGreen, f); fputc(g_pal[idx].rgbRed, f);
        }
    }
    fclose(f);
    fprintf(stderr, "[gfx] voxel-dumped %s nz=%u\n", path, nz);
}

/* Blit the 8-bit DIB to the window, scaled to the client area. */
void host_blit(void) {
    host_wave_poll();          /* flight loops present without pumping messages */
    if (!g_hwnd || !g_dib_buf) return;
#ifdef FURY3_SDL
    host_frame_throttle();
    host_sdl_present(g_dib_buf);
#else
    host_frame_throttle();   /* the true per-frame present: every render loop blits,
                              * even those that never pump messages (enemy-intro,
                              * fly-in, flight) — keeps the game's timeGetTime-derived
                              * dt non-zero so time-gated while(1) loops progress. */
    { static uint32_t bc=0; if(host_env("FURY3_FLY") && (bc++%200)==0) fprintf(stderr,"[blit] #%u @%ums dt=%d\n",bc,host_millis(),(int)MEM32(0x004EB9D0)); }
    struct { BITMAPINFOHEADER h; RGBQUAD pal[256]; } bi;
    memset(&bi, 0, sizeof(bi));
    bi.h.biSize = sizeof(BITMAPINFOHEADER);
    bi.h.biWidth = g_dib_w;
    bi.h.biHeight = -g_dib_h;                    /* top-down */
    bi.h.biPlanes = 1;
    bi.h.biBitCount = 8;
    bi.h.biCompression = BI_RGB;
    bi.h.biClrUsed = 256;
    memcpy(bi.pal, g_pal, sizeof(g_pal));
    RECT rc; GetClientRect(g_hwnd, &rc);
    HDC dc = GetDC(g_hwnd);
    SetStretchBltMode(dc, COLORONCOLOR);
    StretchDIBits(dc, 0, 0, rc.right, rc.bottom, 0, 0, g_dib_w, g_dib_h,
                  (void*)(uintptr_t)ADDR(g_dib_buf), (BITMAPINFO*)&bi, DIB_RGB_COLORS, SRCCOPY);
    ReleaseDC(g_hwnd, dc);
#endif
}

/* InvalidateRect. Windows answers it with a WM_PAINT once the queue is empty, and
 * the game's WM_PAINT handler is what BitBlts its frame DIB. In SDL mode there is
 * no real window to do that, so remember the request; host_peek_game_msg delivers
 * the WM_PAINT when no input is waiting (as the real window path does through
 * host_drain_noninput). */
static int g_paint_pending = 0;
void host_invalidate(void) {
#ifdef FURY3_SDL
    g_paint_pending = 1;
#else
    if (g_hwnd) InvalidateRect(g_hwnd, NULL, FALSE);
#endif
}

/* Frame-rate cap. The game derives its per-frame time delta dword_4EB9D0 from
 * timeGetTime() (1ms resolution): dt = (now - last)*1179.648/18. Our frames run
 * far faster than 1ms, so two reads land in the same ms -> dt rounds to 0 and
 * every time-based progression (briefing crawl, flight physics, animation)
 * freezes. Pace the frame loop to ~60fps so the ms clock actually advances each
 * frame; dt then scales with real frame time, so durations come out correct
 * regardless of the cap. Called once per frame from the PeekMessage shim. */
void host_frame_throttle(void) {
    static uint32_t last = 0;
    uint32_t now = host_millis();
    if (last) {
        uint32_t dt = now - last;
        if (dt < 6) { uint32_t t0 = now; os_sleep_ms(6 - dt); now = host_millis(); g_throttle_sleep_ms += now - t0; }   /* ~160fps cap: keeps dt>0
                                                               * but lets per-frame-gated
                                                               * level loading run faster */
    }
    last = now;
}

/* Pump the window's messages, and present the game's DIB. Returns 1 to quit. */
/* Window upkeep + presentation, with NO input-message consumption: the game
 * reads keys from its own PeekMessage loop, so we must leave WM_KEY/WM_CHAR/
 * mouse messages in the queue for the game and only handle the rest ourselves. */
#ifndef FURY3_SDL
static int is_input_msg(UINT m) {
    return (m >= WM_KEYFIRST && m <= WM_KEYLAST) ||      /* 0x100..0x109 */
           (m >= WM_MOUSEFIRST && m <= WM_MOUSELAST);     /* 0x200..0x20A */
}
#endif

/* While the game presents for itself (BitBlt from its memory DC), that present is
 * the frame boundary: host_blit throttles there, and the other per-pump throttles
 * must not add sleeps of their own (several per frame cost ~1/3 of wall time). */
static int getenv_cached_dumps(void) {
    static int on = -1;
    if (on < 0) on = host_env("FURY3_DUMP") || host_env("FURY3_SHOTS");
    return on;
}
int host_game_presenting(void) {
    return g_last_game_blit && host_millis() - g_last_game_blit < 500;
}

void host_present_tick(void) {
#ifdef FURY3_SDL
    host_sdl_pump();           /* drain SDL events -> menu + synthetic game input */
#endif
    host_wave_poll();
    if (!host_game_presenting())
    host_frame_throttle();     /* present = reliable per-frame boundary for ALL render
                                * loops (briefing, enemy-intro, flight) — many of them
                                * never hit the PeekMessage shim, so without this their
                                * frames run sub-millisecond and the game's dt
                                * (timeGetTime delta / 18) rounds to 0, freezing every
                                * time-gated `while(1)` (e.g. sub_446F50's v18). */
    { static int once=0; if(!once && g_hwnd){once=1; recomp_call_va(0x00425860u); fprintf(stderr,"[camp] forced sub_00425860 (hardcoded campaign init)\n");} }
    { static int dumped=0; if(!dumped && host_millis()>50000){ dumped=1; recomp_dump_trace("steady-state @50s"); } }
    { static uint32_t lp=0; if(host_env("FURY3_DT") && (lp++%120)==0)
        fprintf(stderr,"[dt] ms=%u dt=%d cur_func=0x%08X newgame[542464]=%d keyflag[541F14]=%d\n",host_millis(),(int)MEM32(0x004EB9D0),g_cur_func,(int)MEM32(0x00542464),(int)MEM32(0x00541F14)); }
    /* No real keyboard focus in headless testing: synthesize F2 (New Game, per
     * the Quick-Help dialog) / Enter periodically. Posted to the real queue so
     * host_peek_game_msg() hands them to the game's own input loop. */
    /* Only for unattended runs (FURY3_AUTOSTART, or FURY3_CAMPAIGN's test starts):
     * a player starts the game themselves. */
    static uint32_t last_key = 0;
    if (g_hwnd && (host_env("FURY3_AUTOSTART") || host_env("FURY3_CAMPAIGN")) && host_millis() - last_key > 700) {
        /* State-driven: keep firing F2 (New Game) until the new-game flag
         * [0x542464] is set by the menu handler (sub_44EE2C) -- fixed-time
         * injection raced with the now-throttled frame timing. Once New Game
         * has started, nudge the briefing/ready screens ([0x541F14]) with Enter. */
        static int new_game_started = 0;        /* latch: never re-fire F2 once New Game
                                                 * began (the flag is transient) */
        /* sub_0044EE2C (F2's handler) treats 0 as "not yet requested" and sets this
         * to exactly 1 when it fires (cmp ...,0 / mov ecx,1 / mov [0x542464],ecx).
         * The cell is uninitialized game memory beforehand, not necessarily 0 (seen
         * as -1 at startup), so checking "!= 0" false-positives on that garbage and
         * skips the F2 injection entirely, leaving the game stuck at the menu while
         * Enter gets spammed at it forever. Match the real sentinel exactly. */
        if (MEM32(0x00542464) == 1) new_game_started = 1;
        /* Only inject at the menu (rmode [0x53E860]==0). Once a mission is loading
         * (rmode==2: briefing/enemy-intro/fly-in) it auto-advances on its own timers,
         * and a stray injected Enter reaches the menu handler (sub_44E540) and can
         * open a modal pump -> intermittent freeze. In flight (rmode==1) a continuous
         * Enter feeds the cheat-code reader. So inject F2/Enter only in the menu. */
        int rmode = (int)MEM32(0x0053E860);
        uint32_t vk = (rmode != 0) ? 0
                  : (!new_game_started && host_millis() > 2500) ? VK_F2
                  : (new_game_started ? VK_RETURN : 0);
        if (vk) {
            last_key = host_millis();
#ifdef FURY3_SDL
            /* No real Win32 queue in SDL mode (headless auto-start): push the
             * synthetic New-Game F2 / Enter straight into the game's input queue. */
            synq_push(0x0100u, (uint32_t)vk); synq_push(0x0101u, (uint32_t)vk);
#else
            LPARAM sc = (LPARAM)(MapVirtualKeyA((UINT)vk, 0) << 16);
            PostMessageA(g_hwnd, WM_KEYDOWN, vk, sc | 1);
            PostMessageA(g_hwnd, WM_KEYUP,   vk, sc | 0xC0000001);
#endif
            { static int logged_ng = 0; if (vk == VK_F2 && !logged_ng) { logged_ng = 1; fprintf(stderr, "[input] injecting F2 (New Game) until flag set\n"); }
              if (vk == VK_RETURN) { static int once=0; if(!once){once=1; fprintf(stderr,"[input] New Game started; nudging with Enter\n"); } } }
        }
    }
    /* While the game presents for itself (BitBlt from its memory DC, see
     * host_blit_from_dc) that is authoritative. Otherwise fall back to presenting
     * whichever surface has the most content; also consider the game's 320x200
     * work buffer at [0x4EBC48]. */
    int game_presenting = g_last_game_blit != 0;   /* its title screen redraws only once a second */
    if (game_presenting && !getenv_cached_dumps()) return;
    static uint32_t pumps = 0;
    if ((pumps++ % 4) != 0) return;
    uint32_t best = g_dib_buf, bestnz = 0;
    uint32_t cands[16]; int nc = 0;
    for (int i = 0; i < g_ndibs; i++) cands[nc++] = g_dibs[i];
    static uint32_t recent[4] = {0,0,0,0};       /* [0x4EBC48] is reallocated per frame */
    uint32_t wb = MEM32(0x004EBC48);
    if (wb && wb != recent[0]) { recent[3]=recent[2]; recent[2]=recent[1]; recent[1]=recent[0]; recent[0]=wb; }
    for (int r = 0; r < 4; r++) if (recent[r]) cands[nc++] = recent[r];
    uint32_t rt = MEM32(0x0047BB70);             /* span-rasterizer row-0 ptr = 3D render target */
    if (rt >= FURY3_HEAP_BASE && rt < FURY3_HEAP_BASE + FURY3_HEAP_SIZE) cands[nc++] = rt;
    for (int c = 0; c < nc; c++) {
        uint32_t nz = 0, b = cands[c];
        for (int i = 0; i < g_dib_w * g_dib_h; i += 16) if (MEM8(b + i)) nz++;
        if (nz > bestnz) { bestnz = nz; best = b; }
    }
    if (best) { if (!game_presenting) { g_dib_buf = best; host_blit(); }
        if (host_env("FURY3_DUMP")) {        /* capture each new peak-content frame + periodic */
            extern void host_dump_voxel(const char*,uint32_t);
            static uint32_t peak = 0, fn = 0;
            if (bestnz > peak + 200 || (fn % 60) == 0) {
                if (bestnz > peak) peak = bestnz;
                char path[80];
                snprintf(path, sizeof(path), "build/frames/f%06u_nz%05u_%08X.bmp", host_millis(), bestnz, best);
                host_dump_bmp(path, best);
                snprintf(path, sizeof(path), "build/frames/vx%06u.bmp", host_millis());   /* 3D voxel render target */
                host_dump_voxel(path, 0x0047BB70u);
            }
            /* In flight (rmode==1) dump EVERY candidate buffer (not just brightest) so
             * a dark canyon isn't hidden behind the static title art. */
            if (MEM32(0x0053E860) == 1 && (fn % 30) == 0) {
                for (int c = 0; c < nc; c++) {
                    uint32_t nz = 0; for (int i = 0; i < g_dib_w*g_dib_h; i += 16) if (MEM8(cands[c]+i)) nz++;
                    char path[96];
                    snprintf(path, sizeof(path), "build/frames/fly%06u_c%d_nz%05u_%08X.bmp", host_millis(), c, nz, cands[c]);
                    host_dump_bmp(path, cands[c]);
                }
            }
            fn++;
        }
        /* FURY3_SHOTS: dump every candidate buffer ~every 3s in ANY state, tagged
         * with render-mode + ms, so a representative frame per state (menu, briefing,
         * enemy-intro, fly-in, flight) can be picked for the README. */
        if (host_env("FURY3_SHOTS")) {
            extern void host_dump_voxel(const char*,uint32_t);
            static uint32_t last_shot = 0;
            if (host_millis() - last_shot >= 3000) {
                last_shot = host_millis();
                int rmode = (int)MEM32(0x0053E860);
                char path[112];
                /* The 3D rasterizer (briefing/enemy-intro/fly-in/canyon) renders to the
                 * row-major target 0x47BB70 -- capture it the voxel-aware way. */
                snprintf(path, sizeof(path), "build/frames/shot_%06ums_r%d_VOXEL.bmp", host_millis(), rmode);
                host_dump_voxel(path, 0x0047BB70u);
                for (int c = 0; c < nc; c++) {
                    uint32_t nz = 0; for (int i = 0; i < g_dib_w*g_dib_h; i += 16) if (MEM8(cands[c]+i)) nz++;
                    if (nz < 200) continue;            /* skip near-empty buffers */
                    snprintf(path, sizeof(path), "build/frames/shot_%06ums_r%d_c%d_nz%05u_%08X.bmp",
                             host_millis(), rmode, c, nz, cands[c]);
                    host_dump_bmp(path, cands[c]);
                }
            }
        }
    }
}

/* Dispatch all non-input window messages (paint/close/move/timer) to the real
 * WndProc so the window stays live, leaving input messages queued for the game. */
#ifdef FURY3_SDL
static void host_drain_noninput(void) {}
#else
static void host_drain_noninput(void) {
    MSG m;
    while (g_hwnd && PeekMessageA(&m, NULL, 0, 0, PM_NOREMOVE)) {
        if (is_input_msg(m.message)) break;
        if (!PeekMessageA(&m, NULL, 0, 0, PM_REMOVE)) break;
        TranslateMessage(&m);
        DispatchMessageA(&m);
    }
}
#endif

/* A movie started with "play ... notify" (imp_mciSendStringA) reports itself
 * finished through the game's own message loop, like a repaint. */
static int g_mci_notify_pending;
void host_mci_notify_later(void) { g_mci_notify_pending = 1; }

/* Deliver the next queued INPUT message to the game's MSG struct (sim memory),
 * remapping the host HWND to the game's fake handle. Returns 1 if delivered. */
int host_peek_game_msg(uint32_t lpMsg, int remove) {
    if (g_mci_notify_pending) {
        g_mci_notify_pending = 0;
        call_game_wndproc(0x03B9u /* MM_MCINOTIFY */, 0x0001u /* MCI_NOTIFY_SUCCESSFUL */, 1);
    }
#ifdef FURY3_SDL
    /* Deliver synthetic Win32 key messages built from SDL input by host_sdl_pump. */
    { uint32_t msg, vk, lp;
      if (!synq_get(&msg, &vk, &lp, remove)) {
          if (g_paint_pending) { g_paint_pending = 0; call_game_wndproc(WM_PAINT, 0, 0); }
          return 0;
      }
      if (msg == 0x0100u && vk == VK_ESCAPE) g_quit = 1;
      if (remove && host_env("FURY3_FLY") && (msg == 0x0100u || msg == 0x0101u))
          fprintf(stderr, "[key] %s vk=0x%02X sc=0x%02X @%ums rmode=%d\n", msg == 0x0100u ? "down" : "up",
                  vk, (lp >> 16) & 0x7F, host_millis(), (int)MEM32(0x0053E860));
      if (lpMsg) {
          MEM32(lpMsg + 0) = H_HWND; MEM32(lpMsg + 4) = msg; MEM32(lpMsg + 8) = vk;
          MEM32(lpMsg + 12) = lp; MEM32(lpMsg + 16) = host_millis(); MEM32(lpMsg + 20) = 0; MEM32(lpMsg + 24) = 0;
      }
      return 1; }
#else
    host_drain_noninput();
    MSG m;
    if (!g_hwnd || !PeekMessageA(&m, NULL, 0, 0, PM_NOREMOVE)) return 0;
    if (!is_input_msg(m.message)) return 0;          /* (drained above; be safe) */
    /* Real keyboard/mouse input is delivered to the game in ALL states, including
     * flight — that's how flight controls work interactively. (The in-flight
     * cheat-code reader sub_43BFB0 that used to hang on key input is now a no-op
     * HOST_SHIM, so keys are safe to deliver while flying.) */
    if (remove) { PeekMessageA(&m, NULL, 0, 0, PM_REMOVE); TranslateMessage(&m); }
    if (remove && m.message == WM_KEYDOWN && host_env("FURY3_FLY"))
        fprintf(stderr, "[key] vk=0x%02X delivered @%ums rmode=%d\n", (unsigned)m.wParam, host_millis(), (int)MEM32(0x0053E860));
    if (m.message == WM_KEYDOWN && m.wParam == VK_ESCAPE) g_quit = 1;
    if (lpMsg) {
        MEM32(lpMsg + 0)  = (m.hwnd == g_hwnd) ? 0x10001u : 0u;  /* game's window handle */
        MEM32(lpMsg + 4)  = m.message;
        MEM32(lpMsg + 8)  = (uint32_t)m.wParam;
        MEM32(lpMsg + 12) = (uint32_t)m.lParam;
        MEM32(lpMsg + 16) = m.time;
        MEM32(lpMsg + 20) = (uint32_t)m.pt.x;
        MEM32(lpMsg + 24) = (uint32_t)m.pt.y;
    }
    return 1;
#endif
}

int host_pump(void) {
    host_present_tick();
    host_drain_noninput();
    return g_quit;
}
int host_should_quit(void) { return g_quit; }

/* ============================================================
 * Host file + INI I/O (backed by the real game directory).
 *
 * The game uses paths like ".\system\fury3.ini", "C:\FURY3\data\...", or bare
 * "mapback.raw". We translate them all to live under FURY3_GAME_ROOT so reads/
 * writes hit real files. Implemented here (where windows.h is available);
 * shims_impl.c calls these.
 * ============================================================ */
/* The game folder (the CD's FURY3 folder: FURY3.EXE, SYSTEM\*.POD, ...): on the
 * Vita <home>FURY3, found and checked by game_setup(); for PC development runs,
 * ./FURY3 if present, else ./gamedata. */
static char g_game_root[512] = FURY3_HOME "gamedata";
#define FURY3_GAME_ROOT g_game_root

/* Time spent in host file operations, by kind (reported by the FURY3_FLY
 * watchdog) -- on the Vita every filesystem call costs real time. */
enum { IO_OPEN, IO_READ, IO_SEEK, IO_GETC, IO_LEN, IO_KINDS };
static volatile uint64_t g_io_us[IO_KINDS];
static volatile uint32_t g_io_n[IO_KINDS];

/* Read a NUL-terminated string out of simulated memory into a host buffer. */
static void sim_read_str(uint32_t va, char* out, int outsz) {
    int i = 0;
    if (!va) { out[0] = 0; return; }
    for (; i < outsz - 1; i++) { uint8_t c = MEM8(va + i); if (!c) break; out[i] = (char)c; }
    out[i] = 0;
}

/* ---- basename index: the game opens many assets by bare name (e.g. "laser.bin"
 * really MODELS/LASER.BIN). We scan the game tree once and resolve a bare name to
 * its real path when the literal path doesn't exist. ---- */
#define FURY3_MAX_INDEX 8192
static char     g_idx_name[FURY3_MAX_INDEX][28];   /* lowercase basename */
static char     g_idx_path[FURY3_MAX_INDEX][260];  /* full host path, on-disk case */
static uint32_t g_idx_size[FURY3_MAX_INDEX];
static int      g_idx_count = 0;
/* The index is the filesystem view after startup: every game file with its size,
 * found by full path (case-insensitive, like Windows/FAT) or by bare name, through
 * hash tables -- no filesystem calls. Only once the game has written a file does a
 * lookup that misses the index fall back to asking the filesystem. */
#define IDX_HASH 16384
static int16_t  g_idx_by_path[IDX_HASH], g_idx_by_name[IDX_HASH];  /* index + 1; 0 = empty */
static int      g_files_written = 0;

static uint32_t lower_hash(const char* s) {
    uint32_t h = 2166136261u;
    while (*s) h = (h ^ (uint8_t)tolower((unsigned char)*s++)) * 16777619u;
    return h;
}
static int eq_nocase(const char* a, const char* b) {
    for (; *a && *b; a++, b++) if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
    return *a == *b;
}
static void idx_insert(int16_t* table, uint32_t h, int k) {
    for (uint32_t p = 0; p < IDX_HASH; p++) {
        int16_t* e = &table[(h + p) & (IDX_HASH - 1)];
        if (!*e) { *e = (int16_t)(k + 1); return; }
    }
}
static int idx_find_path(const char* path) {
    uint32_t h = lower_hash(path);
    for (uint32_t p = 0; p < IDX_HASH; p++) {
        int16_t e = g_idx_by_path[(h + p) & (IDX_HASH - 1)];
        if (!e) return -1;
        if (eq_nocase(g_idx_path[e - 1], path)) return e - 1;
    }
    return -1;
}
static int idx_find_name(const char* lowname) {
    uint32_t h = lower_hash(lowname);
    for (uint32_t p = 0; p < IDX_HASH; p++) {
        int16_t e = g_idx_by_name[(h + p) & (IDX_HASH - 1)];
        if (!e) return -1;
        if (!strcmp(g_idx_name[e - 1], lowname)) return e - 1;     /* first file with that name wins */
    }
    return -1;
}

static void index_dir(const char* dir);
static void index_entry(const char* name, int is_dir, uint64_t size, void* ctx) {
    const char* dir = (const char*)ctx;
    char full[300]; snprintf(full, sizeof(full), "%s/%s", dir, name);
    if (is_dir) { index_dir(full); return; }
    if (g_idx_count >= FURY3_MAX_INDEX) return;
    int k = g_idx_count++;
    int i = 0; for (; name[i] && i < 27; i++) g_idx_name[k][i] = (char)tolower((unsigned char)name[i]);
    g_idx_name[k][i] = 0;
    snprintf(g_idx_path[k], 260, "%s", full);
    g_idx_size[k] = (uint32_t)size;
    idx_insert(g_idx_by_path, lower_hash(full), k);
    if (idx_find_name(g_idx_name[k]) < 0) idx_insert(g_idx_by_name, lower_hash(g_idx_name[k]), k);
}
static void index_dir(const char* dir) { os_list_dir(dir, index_entry, (void*)dir); }
void host_index_build(void) {
    g_idx_count = 0;
    memset(g_idx_by_path, 0, sizeof(g_idx_by_path)); memset(g_idx_by_name, 0, sizeof(g_idx_by_name));
    index_dir(FURY3_GAME_ROOT);
}

/* Resolve `host` in place: the indexed file at that path (any case), else the
 * indexed file with that bare name, else -- once files have been written, or for
 * directories -- the real filesystem. */
static void index_resolve_uncached(char* host, int hostsz, int* exists, uint64_t* size, int* is_dir) {
    *exists = 1; *is_dir = 0;
    int k = idx_find_path(host);
    if (k < 0) {
        const char* b = host; for (const char* p = host; *p; p++) if (*p == '/' || *p == '\\') b = p + 1;
        char low[64]; int i = 0; for (; b[i] && i < 63; i++) low[i] = (char)tolower((unsigned char)b[i]); low[i] = 0;
        k = idx_find_name(low);
    }
    if (k >= 0) { snprintf(host, hostsz, "%s", g_idx_path[k]); *size = g_idx_size[k]; return; }
    os_resolve_case(host, (size_t)hostsz);
    size_t rl = strlen(FURY3_GAME_ROOT);
    if (g_files_written || strncmp(host, FURY3_GAME_ROOT, rl) != 0 || host[rl] != '/' || strchr(host, '.') == NULL)
        if (os_file_info(host, size, is_dir)) return;    /* a file written since startup, or a directory */
    *exists = 0;
}

/* Path-resolution cache. The game opens the same few hundred names over and over
 * (each asset load re-opens its .POD; each texture probes for a .ACT palette, many
 * of which the disc doesn't have), and every resolution costs filesystem lookups
 * plus an index scan -- milliseconds each on the Vita's memory card, which showed
 * up as 1 fps loading and stalls on every explosion. Results, including "missing",
 * are remembered; any write-open clears the cache since it may create a file. */
#define PATH_CACHE 2048
static struct { char key[200]; char host[260]; uint32_t size; uint8_t used, exists, is_dir, logged; } g_pcache[PATH_CACHE];
static uint32_t path_hash(const char* s) { uint32_t h = 2166136261u; while (*s) h = (h ^ (uint8_t)*s++) * 16777619u; return h; }
static void path_cache_clear(void) { for (int i = 0; i < PATH_CACHE; i++) g_pcache[i].used = 0; }
static int index_resolve_slot(char* host, int hostsz) {      /* returns the cache slot, -1 if uncacheable */
    uint64_t size = 0; int e = 0, dir = 0;
    if (strlen(host) >= sizeof(g_pcache[0].key)) { index_resolve_uncached(host, hostsz, &e, &size, &dir); return -1; }
    uint32_t h = path_hash(host);
    for (int k = 0; k < 16; k++) {
        int s = (int)((h + (uint32_t)k) & (PATH_CACHE - 1));
        if (g_pcache[s].used && !strcmp(g_pcache[s].key, host)) { snprintf(host, hostsz, "%s", g_pcache[s].host); return s; }
        if (!g_pcache[s].used) {
            snprintf(g_pcache[s].key, sizeof(g_pcache[s].key), "%s", host);
            index_resolve_uncached(host, hostsz, &e, &size, &dir);
            snprintf(g_pcache[s].host, sizeof(g_pcache[s].host), "%s", host);
            g_pcache[s].exists = (uint8_t)e; g_pcache[s].is_dir = (uint8_t)dir;
            g_pcache[s].size = (uint32_t)size; g_pcache[s].logged = 0; g_pcache[s].used = 1;
            return s;
        }
    }
    index_resolve_uncached(host, hostsz, &e, &size, &dir);       /* probe window full */
    return -1;
}
static void index_resolve(char* host, int hostsz) { index_resolve_slot(host, hostsz); }

/* Translate a game path to a host path under the game root; returns its
 * path-cache slot (-1 if not cached). */
static int translate_path_slot(uint32_t va, char* out, int outsz);
void host_translate_path(uint32_t va, char* out, int outsz) { translate_path_slot(va, out, outsz); }
/* The save slot imp_GetSaveFileNameA writes (shims_impl.c). */
int host_save_exists(void) {
    char p[512]; uint64_t sz; int dir;
    snprintf(p, sizeof(p), "%s/data/FURY3.FFF", FURY3_GAME_ROOT);
    os_resolve_case(p, sizeof(p));
    return os_file_info(p, &sz, &dir) && !dir;
}
/* Mods: the game mounts the archives pod.ini lists, once, at startup, and loads the
 * ship then, so a change needs a fresh start. <game>/mods_off.txt holds "1" to hide
 * pod.ini, and the game then uses its stock archives. */
static int g_mods_have, g_mods_off, g_mods_changed;
static void mods_path(char* out, size_t n, const char* name) {
    snprintf(out, n, "%s/%s", FURY3_GAME_ROOT, name);
    os_resolve_case(out, n);
}
static void host_mods_load(void) {
    char p[600]; uint64_t sz; int dir;
    mods_path(p, sizeof(p), "pod.ini");
    g_mods_have = os_file_info(p, &sz, &dir) && !dir;
    mods_path(p, sizeof(p), "mods_off.txt");
    FILE* f = fopen(p, "rb");
    if (f) { g_mods_off = fgetc(f) == '1'; fclose(f); }
    if (g_mods_have) fprintf(stderr, "[mods] pod.ini found, mods %s\n", g_mods_off ? "off" : "on");
}
int host_mods_available(void) { return g_mods_have; }
int host_mods_enabled(void) { return !g_mods_off; }
int host_mods_changed(void) { return g_mods_changed; }
void host_mods_toggle(void) {
    char p[600];
    if (!g_mods_have) return;
    g_mods_off = !g_mods_off;
    mods_path(p, sizeof(p), "mods_off.txt");
    FILE* f = fopen(p, "wb");
    if (f) { fputc(g_mods_off ? '1' : '0', f); fclose(f); }
#ifdef __vita__
    sceAppMgrLoadExec("app0:eboot.bin", NULL, NULL);
#else
    g_mods_changed = !g_mods_changed;
#endif
}

/* Moving wings. A ship model may carry a second pose after its end marker, where the
 * game never looks: 'WING', nverts, nverts * i32 x,y,z (tools/mod/glb2fury.py --wings).
 * The game keeps the model as read from the file, reads vertices and face planes from
 * it each time it draws, and loads the ship once [0x4EB628] without ever freeing it, so
 * rewriting them in place is all it takes. The stock ship has no second pose, which
 * leaves the button doing nothing with mods off. */
#define WINGS_FULL 1024
#define WINGS_MS   600
static struct {
    uint32_t  model;        /* guest address the rest describes */
    uint8_t*  m;
    int32_t*  closed;       /* copy: the model's own vertices get overwritten */
    const int32_t* open;
    uint32_t* faces;        /* offsets of the faces that move */
    int32_t*  glow;         /* palette indices of the engine discs: centre, then six rim */
    int nv, nf, pos, want, nglow;
    uint32_t last, glow_at, glow_seed;
} g_wings;

/* Walks a face list from p to its end marker, returning the marker's offset, or 0 on a chunk
 * it doesn't know. With faces, records those that move. The chunk after 0x17 may be the
 * LOD marker, in either of its states (gpu_capture.c); *lod gets where the cheap list is. */
static uint32_t wings_walk(uint8_t* m, uint32_t p, const int32_t* closed, const int32_t* open, uint32_t* faces, uint32_t* nf, uint32_t* lod) {
    for (;;) {
        uint32_t tag = *(const uint32_t*)(m + p);
        if (tag == 0) return p;
        if (tag == 0x17) { p += 12; continue; }
        if (tag == 0x0D) { p += 24; continue; }
        if (tag == 0x1F) {
            uint32_t n = *(const uint32_t*)(m + p + 8);
            g_wings.glow = (int32_t*)(m + p + 12); g_wings.nglow = (int)n;
            p += 12 + n * 4;
            continue;
        }
        if (tag == 6) { p += 24 + *(const uint32_t*)(m + p + 4) * 4; continue; }
        if (tag == 0x0B || tag == 0x1B) {
            if (lod) *lod = p + *(const uint32_t*)(m + p + 16);
            p += 24;
            continue;
        }
        if (tag != 0x0E && tag != 0x18) return 0;
        uint32_t n = *(const uint32_t*)(m + p + 4), moved = 0;
        if (!faces) { (*nf)++; p += 24 + n * 12; continue; }
        for (uint32_t i = 0; i < n; i++) {
            uint32_t v = *(const uint32_t*)(m + p + 24 + i * 12) * 3;
            moved |= (uint32_t)(closed[v] != open[v]) | (uint32_t)(closed[v + 1] != open[v + 1]) | (uint32_t)(closed[v + 2] != open[v + 2]);
        }
        if (moved && n >= 3) faces[(*nf)++] = p;
        p += 24 + n * 12;
    }
}

static void wings_parse(uint32_t model) {
    uint8_t* m = (uint8_t*)(uintptr_t)ADDR(model);
    const uint32_t* h = (const uint32_t*)m;
    uint32_t nv = h[4], nf = 0, lod = 0, first = 20 + nv * 12;
    g_wings.model = model; g_wings.nv = 0; g_wings.pos = 0; g_wings.nglow = 0;
    if (h[0] != 0x14) return;
    uint32_t p = wings_walk(m, first, NULL, NULL, NULL, &nf, &lod);
    if (!p || memcmp(m + p + 4, "WING", 4) || *(const uint32_t*)(m + p + 8) != nv) return;
    if (lod && !wings_walk(m, lod, NULL, NULL, NULL, &nf, NULL)) return;
    int32_t* closed = (int32_t*)realloc(g_wings.closed, nv * 12);
    uint32_t* faces = (uint32_t*)realloc(g_wings.faces, (nf + 1) * 4);
    if (closed) g_wings.closed = closed;
    if (faces) g_wings.faces = faces;
    if (!closed || !faces) return;
    memcpy(closed, m + 20, nv * 12);
    const int32_t* open = (const int32_t*)(m + p + 12);
    nf = 0;
    wings_walk(m, first, closed, open, faces, &nf, NULL);
    if (lod) wings_walk(m, lod, closed, open, faces, &nf, NULL);
    g_wings.m = m; g_wings.open = open; g_wings.nf = (int)nf; g_wings.nv = (int)nv;
    fprintf(stderr, "[wings] ship has a second pose: %u vertices, %u moving faces\n", nv, nf);
}

static void wings_apply(void) {
    int32_t* v = (int32_t*)(g_wings.m + 20);
    int pos = g_wings.pos;
    for (int i = 0; i < g_wings.nv * 3; i++)
        v[i] = g_wings.closed[i] + (g_wings.open[i] - g_wings.closed[i]) * pos / WINGS_FULL;
    for (int f = 0; f < g_wings.nf; f++) {      /* +8 normal, +14 d, +18 vertex list */
        int32_t* r = (int32_t*)(g_wings.m + g_wings.faces[f]);
        const int32_t* a = v + (uint32_t)r[6] * 3, *b = v + (uint32_t)r[9] * 3, *c = v + (uint32_t)r[12] * 3;
        float ux = (float)(b[0] - a[0]), uy = (float)(b[1] - a[1]), uz = (float)(b[2] - a[2]);
        float wx = (float)(c[0] - a[0]), wy = (float)(c[1] - a[1]), wz = (float)(c[2] - a[2]);
        float nx = uy * wz - uz * wy, ny = uz * wx - ux * wz, nz = ux * wy - uy * wx;
        float len = sqrtf(nx * nx + ny * ny + nz * nz);
        if (len < 1.0f) continue;
        nx /= len; ny /= len; nz /= len;
        r[2] = (int32_t)(nx * 65536.0f); r[3] = (int32_t)(ny * 65536.0f); r[4] = (int32_t)(nz * 65536.0f);
        r[5] = (int32_t)((nx * (float)a[0] + ny * (float)a[1] + nz * (float)a[2]) * 65536.0f);
    }
}

void host_wings_toggle(void) { g_wings.want = !g_wings.want; }

/* The engine discs run up the palette's heat ramp, 0x80 (near black) through red and orange
 * to 0x9B (yellow), the same on every planet, with throttle [0x53E734]: 0x4000 to 0xFFFF. */
static void wings_glow(uint32_t now) {
    if (now - g_wings.glow_at < 50) return;
    g_wings.glow_at = now;
    int t = ((int)MEM32(0x0053E734u) - 0x4000) * 256 / (0xFFFF - 0x4000);
    if (t < 0) t = 0; else if (t > 256) t = 256;
    for (int i = 0; i + 7 <= g_wings.nglow; i += 7) {
        g_wings.glow_seed = g_wings.glow_seed * 1103515245u + 12345u;
        int flick = (int)(g_wings.glow_seed >> 16 & 3) - 1;
        int centre = 0x8C + t * 15 / 256 + flick, rim = 0x84 + t * 12 / 256 + flick;
        if (centre > 0x9B) centre = 0x9B;
        g_wings.glow[i] = centre;
        for (int k = 1; k < 7; k++) g_wings.glow[i + k] = rim;
    }
}

void host_wings_tick(void) {
    static const char* every; static int env_read; static uint32_t next;
    uint32_t model = MEM32(0x004EB628u), now = host_millis();
    if (MEM32(0x0053E860u) != 1 || !model) return;
    if (model != g_wings.model) wings_parse(model);
    if (!g_wings.nv) return;
    if (!env_read) { every = host_env("FURY3_WINGS"); env_read = 1; }   /* test: toggle every N ms */
    if (every && (int32_t)(now - next) >= 0) { next = now + (uint32_t)atoi(every); host_wings_toggle(); }
    wings_glow(now);
    int target = g_wings.want ? WINGS_FULL : 0;
    if (MEM32(0x004EB9D0u) == 0 || g_wings.pos == target) { g_wings.last = now; return; }
    int step = (int)((now - g_wings.last) * WINGS_FULL / WINGS_MS);
    if (step <= 0) return;
    g_wings.last = now;
    g_wings.pos = target ? (g_wings.pos + step > target ? target : g_wings.pos + step)
                         : (g_wings.pos - step < 0 ? 0 : g_wings.pos - step);
    wings_apply();
}

static int translate_path_slot(uint32_t va, char* out, int outsz) {
    char p[512]; sim_read_str(va, p, sizeof(p));
    return translate_path_str(p, out, outsz);
}
static int translate_path_str(const char* guest, char* out, int outsz) {
    char p[512]; snprintf(p, sizeof(p), "%s", guest);
    for (char* q = p; *q; q++) if (*q == '\\') *q = '/';
    const char* rel = p;
    /* strip a drive letter ("c:/...") */
    if (rel[0] && rel[1] == ':') rel += 2;
    /* strip everything up to and incl. a "fury3/" segment, else a leading "./" or "/" */
    char low[512]; int i; for (i = 0; rel[i] && i < 511; i++) low[i] = (char)tolower((unsigned char)rel[i]); low[i] = 0;
    char* f = strstr(low, "fury3/");
    if (f) rel += (f - low) + 6;
    else { if (rel[0] == '.' && rel[1] == '/') rel += 2; else if (rel[0] == '/') rel += 1; }
    snprintf(out, outsz, "%s/%s", FURY3_GAME_ROOT, rel);
    return index_resolve_slot(out, outsz);   /* fall back to basename match if exact path missing */
}

/* When a saved game was last written (0 = never this run), for the on-screen note. */
uint32_t g_last_save_ms = 0;

/* File handle table: sim handle = index + base. Backed by C stdio (binary mode,
 * so no newline translation -- the game's text parsing sees the real CRLFs, as it
 * did through CreateFile/ReadFile). */
#define FURY3_HANDLE_BASE 0x000A0000u
#define FURY3_MAX_FILES   64
static volatile uint32_t g_real_opens = 0, g_game_opens = 0; /* host fopen calls vs game open requests */

/* Read-only files are read through a 64 KB window with our own position: the
 * game's POD access is "seek, read a few bytes, seek, read" (~12k seeks and ~13k
 * reads per mission load), and stdio may drop its buffer on every seek, making
 * each one a real filesystem call -- slow on the Vita. Seeks only move `pos`;
 * reads are served from the window and refill it only when they leave it.
 * Writable files go straight to stdio. */
#define HF_BUF 65536u
typedef struct {
    FILE*    f;
    int      ro;
    char     path[260];
    uint8_t* buf;           /* ro: window over [buf_off, buf_off + buf_len) */
    uint32_t buf_off, buf_len;
    uint32_t pos, size;     /* ro: logical position and file size */
} HostFile;
static HostFile g_hf[FURY3_MAX_FILES];

/* Idle read-only files kept open (with their windows) after the game closes them:
 * its POD reader re-opens fury3.pod / startup.pod for every asset. */
#define IDLE_FILES 8
static HostFile g_idle[IDLE_FILES];
static void hf_release(HostFile* h) { if (h->f) fclose(h->f); free(h->buf); memset(h, 0, sizeof(*h)); }
static int idle_take(const char* path, HostFile* out) {
    for (int i = 0; i < IDLE_FILES; i++)
        if (g_idle[i].f && !strcmp(g_idle[i].path, path)) {
            *out = g_idle[i]; memset(&g_idle[i], 0, sizeof(g_idle[i]));
            out->pos = 0;
            return 1;
        }
    return 0;
}
static void idle_put(HostFile* h) {
    for (int i = 0; i < IDLE_FILES; i++)
        if (!g_idle[i].f) { g_idle[i] = *h; memset(h, 0, sizeof(*h)); return; }
    hf_release(h);
}
static void idle_close_path(const char* path) {          /* before a write-open of the same file */
    for (int i = 0; i < IDLE_FILES; i++)
        if (g_idle[i].f && !strcmp(g_idle[i].path, path)) hf_release(&g_idle[i]);
}
/* Make the window cover `pos` (refilling from the file); 0 at end of file. */
static uint32_t hf_fill(HostFile* h) {
    if (h->pos >= h->buf_off && h->pos < h->buf_off + h->buf_len) return 1;
    if (h->pos >= h->size) return 0;
    if (!h->buf && !(h->buf = (uint8_t*)malloc(HF_BUF))) return 0;
    if (fseek(h->f, (long)h->pos, SEEK_SET) != 0) return 0;
    h->buf_off = h->pos;
    h->buf_len = (uint32_t)fread(h->buf, 1, HF_BUF, h->f);
    return h->buf_len != 0;
}

/* access/disp are CreateFileA's: GENERIC_READ 0x80000000, GENERIC_WRITE
 * 0x40000000; CREATE_NEW 1, CREATE_ALWAYS 2, OPEN_EXISTING 3, OPEN_ALWAYS 4. */
static uint32_t host_file_open_impl(uint32_t name_va, uint32_t access, uint32_t disp) {
    char host[600]; int slot = translate_path_slot(name_va, host, sizeof(host));
    int wr = (access & 0x40000000u) != 0, rd = (access & 0x80000000u) != 0 || !wr;
    int exists = slot >= 0 ? g_pcache[slot].exists : os_file_info(host, NULL, NULL);
    HostFile h; memset(&h, 0, sizeof(h));
    g_game_opens++;
    if (wr) {
        idle_close_path(host); path_cache_clear(); g_files_written = 1;
        size_t hl = strlen(host);
        if (hl > 4 && eq_nocase(host + hl - 4, ".fff")) g_last_save_ms = host_millis() | 1;
    }
    const char* create = rd && wr ? "w+b" : "wb";
    if (disp == 2)                                  h.f = fopen(host, create);          /* CREATE_ALWAYS */
    else if (disp == 1)                             h.f = exists ? NULL : fopen(host, create);   /* CREATE_NEW */
    else if (disp == 4 && !exists && wr)            h.f = fopen(host, create);          /* OPEN_ALWAYS, new */
    else if (exists && !wr) {                                                           /* read existing */
        if (!idle_take(host, &h)) {
            h.f = fopen(host, "rb"); g_real_opens++;
            if (h.f) {
                h.ro = 1; snprintf(h.path, sizeof(h.path), "%s", host);
                fseek(h.f, 0, SEEK_END); h.size = (uint32_t)ftell(h.f);
            }
        }
    } else if (exists) { h.f = fopen(host, "r+b"); g_real_opens++; }                  /* update existing */
    if (host_env("FURY3_IOLOG") && h.f) fprintf(stderr, "[ioLog] opened: %s\n", host);
    if (!h.f) {
        if (slot < 0 || !g_pcache[slot].logged) fprintf(stderr, "[io] open FAILED: %s\n", host);
        if (slot >= 0) g_pcache[slot].logged = 1;
        return 0xFFFFFFFFu;
    }
    for (int i = 0; i < FURY3_MAX_FILES; i++) if (!g_hf[i].f) {
        g_hf[i] = h;
        return FURY3_HANDLE_BASE + i;
    }
    hf_release(&h); return 0xFFFFFFFFu;
}
static HostFile* handle_of(uint32_t sh) {
    uint32_t i = sh - FURY3_HANDLE_BASE;
    return (i < FURY3_MAX_FILES && g_hf[i].f) ? &g_hf[i] : NULL;
}
static uint32_t host_file_read_impl(uint32_t sh, uint32_t buf_va, uint32_t n) {
    HostFile* h = handle_of(sh); if (!h) return 0;
    uint32_t got = 0;
    if (!h->ro) got = (uint32_t)fread((void*)(uintptr_t)ADDR(buf_va), 1, n, h->f);
    else while (got < n && h->pos < h->size) {
        if (n - got >= HF_BUF) {                       /* large read: straight into guest memory */
            if (fseek(h->f, (long)h->pos, SEEK_SET) != 0) break;
            uint32_t r = (uint32_t)fread((void*)(uintptr_t)ADDR(buf_va + got), 1, n - got, h->f);
            h->pos += r; got += r;
            break;
        }
        if (!hf_fill(h)) break;
        uint32_t avail = h->buf_off + h->buf_len - h->pos, take = n - got < avail ? n - got : avail;
        memcpy((void*)(uintptr_t)ADDR(buf_va + got), h->buf + (h->pos - h->buf_off), take);
        h->pos += take; got += take;
    }
    if (host_env("FURY3_IOLOG"))
        fprintf(stderr, "[rd] sh=0x%X requested=%u got=%u func=0x%08X ret=0x%08X\n", sh, n, got, g_cur_func, MEM32(g_esp));
    return got;
}
uint32_t host_file_write(uint32_t sh, uint32_t buf_va, uint32_t n) {
    HostFile* h = handle_of(sh); if (!h || h->ro) return 0;
    return (uint32_t)fwrite((void*)(uintptr_t)ADDR(buf_va), 1, n, h->f);
}
/* method is SetFilePointer's FILE_BEGIN/CURRENT/END (0/1/2) == SEEK_SET/CUR/END. */
static uint32_t host_file_seek_impl(uint32_t sh, int32_t dist, uint32_t method) {
    HostFile* h = handle_of(sh); if (!h) return 0xFFFFFFFFu;
    uint32_t r;
    if (h->ro) {
        int64_t base = method == 1 ? (int64_t)h->pos : method == 2 ? (int64_t)h->size : 0;
        int64_t np = base + dist;
        if (np < 0) r = 0xFFFFFFFFu; else { h->pos = (uint32_t)np; r = h->pos; }
    } else r = fseek(h->f, dist, (int)method) == 0 ? (uint32_t)ftell(h->f) : 0xFFFFFFFFu;
    if (host_env("FURY3_IOLOG"))
        fprintf(stderr, "[sk] sh=0x%X dist=%d method=%u -> pos=%u\n", sh, dist, method, r);
    return r;
}
uint32_t host_file_size(uint32_t sh) {
    HostFile* h = handle_of(sh); if (!h) return 0xFFFFFFFFu;
    if (h->ro) return h->size;
    long here = ftell(h->f); fseek(h->f, 0, SEEK_END);
    long size = ftell(h->f); fseek(h->f, here, SEEK_SET);
    return (uint32_t)size;
}
void host_file_close(uint32_t sh) {
    HostFile* h = handle_of(sh); if (!h) return;
    if (h->ro) idle_put(h);
    else { hf_release(h); path_cache_clear(); }         /* a written file's size may have changed */
}
static int host_file_getc_impl(uint32_t sh) {
    HostFile* h = handle_of(sh); if (!h) return -1;
    if (!h->ro) return fgetc(h->f);
    if (!hf_fill(h)) return -1;
    return h->buf[h->pos++ - h->buf_off];
}
/* Timed entry points (see g_io_us). */
uint32_t host_file_open(uint32_t name_va, uint32_t access, uint32_t disp) {
    uint64_t t0 = os_ticks_us(); uint32_t r = host_file_open_impl(name_va, access, disp);
    g_io_us[IO_OPEN] += os_ticks_us() - t0; g_io_n[IO_OPEN]++; return r;
}
uint32_t host_file_read(uint32_t sh, uint32_t buf_va, uint32_t n) {
    uint64_t t0 = os_ticks_us(); uint32_t r = host_file_read_impl(sh, buf_va, n);
    g_io_us[IO_READ] += os_ticks_us() - t0; g_io_n[IO_READ]++; return r;
}
uint32_t host_file_seek(uint32_t sh, int32_t dist, uint32_t method) {
    uint64_t t0 = os_ticks_us(); uint32_t r = host_file_seek_impl(sh, dist, method);
    g_io_us[IO_SEEK] += os_ticks_us() - t0; g_io_n[IO_SEEK]++; return r;
}
int host_file_getc(uint32_t sh) {
    uint64_t t0 = os_ticks_us(); int r = host_file_getc_impl(sh);
    g_io_us[IO_GETC] += os_ticks_us() - t0; g_io_n[IO_GETC]++; return r;
}
int host_is_file(uint32_t sh) { return sh >= FURY3_HANDLE_BASE && sh < FURY3_HANDLE_BASE + FURY3_MAX_FILES; }

/* fopen returns a pointer to a real (mapped) MSVC-style FILE struct, because the
 * game dereferences it (the getc() macro reads _cnt/_ptr, code reads _file). We
 * keep _cnt=0 so getc always falls through to _filbuf, and stash our file-slot
 * index in _file (+16). Stream->handle conversion reads _file back. */
#define FILE_CNT  4    /* int _cnt   */
#define FILE_FLAG 12   /* int _flag  */
#define FILE_FILE 16   /* int _file  */
#define FILE_STRUCT_SIZE 32

uint32_t host_fopen(uint32_t name_va, uint32_t mode_va) {
    if (host_env("FURY3_OPENLOG")) { char nm[200]; sim_read_str(name_va, nm, sizeof(nm)); fprintf(stderr, "[fopen] %s (caller=0x%08X)\n", nm, g_cur_func); }
    if (g_mods_off) { char nm[32]; sim_read_str(name_va, nm, sizeof(nm)); if (eq_nocase(nm, "pod.ini")) return 0; }
    char m[16]; sim_read_str(mode_va, m, sizeof(m));
    uint32_t access = 0x80000000u, disp = 3 /*OPEN_EXISTING*/;
    for (char* p = m; *p; p++) {
        if (*p == 'w') { access = 0x40000000u; disp = 2 /*CREATE_ALWAYS*/; }
        else if (*p == 'a') { access = 0x40000000u; disp = 4 /*OPEN_ALWAYS*/; }
        else if (*p == '+') { access = 0xC0000000u; }
    }
    uint32_t h = host_file_open(name_va, access, disp);
    if (h == 0xFFFFFFFFu) return 0;                 /* fopen -> NULL on failure */
    uint32_t i = h - FURY3_HANDLE_BASE;
    uint32_t f = shim_alloc(FILE_STRUCT_SIZE);
    for (int o = 0; o < FILE_STRUCT_SIZE; o += 4) MEM32(f + o) = 0;
    MEM32(f + FILE_CNT) = 0;                         /* getc -> _filbuf */
    MEM32(f + FILE_FILE) = i;                        /* our file-slot index */
    MEM32(f + FILE_FLAG) = (access == 0x80000000u) ? 1 : 2;
    return f;
}
/* Map a FILE* stream (our struct) to its sim file handle; 0 for console/_iob. */
uint32_t host_stream_handle(uint32_t stream_va) {
    if (stream_va < FURY3_HEAP_BASE) return 0;      /* _iob stdout/stderr/stdin */
    uint32_t i = MEM32(stream_va + FILE_FILE);
    return (i < FURY3_MAX_FILES) ? FURY3_HANDLE_BASE + i : 0;
}
/* Set/clear the EOF flag on a stream after a read. */
void host_stream_set_eof(uint32_t stream_va, int eof) {
    if (stream_va >= FURY3_HEAP_BASE) {
        uint32_t fl = MEM32(stream_va + FILE_FLAG);
        MEM32(stream_va + FILE_FLAG) = eof ? (fl | 0x10) : (fl & ~0x10u);
    }
}

/* stat(): fill an MSVC _stat struct (st_mode@+4, st_size@+16) for a game path.
 * Returns 0 if the file exists, -1 otherwise. */
uint32_t host_stat(uint32_t name_va, uint32_t buf_va) {
    char host[600]; int slot = translate_path_slot(name_va, host, sizeof(host));
    uint64_t size = 0; int dir = 0;
    int ok = slot >= 0 ? g_pcache[slot].exists : os_file_info(host, &size, &dir);
    if (slot >= 0) { size = g_pcache[slot].size; dir = g_pcache[slot].is_dir; }
    if (!ok) {
        if (slot < 0 || !g_pcache[slot].logged) fprintf(stderr, "[io] stat FAILED: %s\n", host);
        if (slot >= 0) g_pcache[slot].logged = 1;
        return 0xFFFFFFFFu;
    }
    for (int o = 0; o < 32; o += 4) MEM32(buf_va + o) = 0;
    MEM16(buf_va + 4) = (uint16_t)(dir ? 0x4000 : 0x8000) | 0x1B6;  /* st_mode */
    MEM32(buf_va + 16) = (uint32_t)size;                            /* st_size */
    return 0;
}

/* Length of a game file by path (the game's own POD length lookup isn't wired up,
 * so we answer from the extracted loose files). Returns size, or -1 if missing. */
uint32_t host_filelength(uint32_t name_va) {
    uint64_t t0 = os_ticks_us();
    char host[600]; int slot = translate_path_slot(name_va, host, sizeof(host));
    uint64_t size = 0;
    int ok = slot >= 0 ? g_pcache[slot].exists : os_file_info(host, &size, NULL);
    if (slot >= 0) size = g_pcache[slot].size;
    g_io_us[IO_LEN] += os_ticks_us() - t0; g_io_n[IO_LEN]++;
    return ok ? (uint32_t)size : 0xFFFFFFFFu;
}

/* ---- INI files: a portable GetPrivateProfileString/Int/WritePrivateProfileString.
 * Section and key names match case-insensitively, whitespace around names and
 * values is ignored, ';' starts a comment line -- the Win32 behaviour the game's
 * fury3.ini relies on. ---- */
#define INI_MAX_LINES 512
static int ini_load(const char* path, char lines[][256]) {
    FILE* f = fopen(path, "rb"); int n = 0;
    if (!f) return 0;
    while (n < INI_MAX_LINES && fgets(lines[n], 256, f)) {
        size_t l = strlen(lines[n]);
        while (l && (lines[n][l - 1] == '\n' || lines[n][l - 1] == '\r')) lines[n][--l] = 0;
        n++;
    }
    fclose(f);
    return n;
}
static void ini_trim(char* s) {
    size_t a = 0, b = strlen(s);
    while (s[a] == ' ' || s[a] == '\t') a++;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t')) b--;
    memmove(s, s + a, b - a); s[b - a] = 0;
}
static int ini_eq(const char* a, const char* b) {
    for (; *a && *b; a++, b++) if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
    return *a == *b;
}
/* Classify a line: 1 = "[section]" (name out), 2 = "key=value" (key/val out), 0 = other. */
static int ini_parse(const char* line, char* name, char* val) {
    char t[256]; snprintf(t, sizeof(t), "%s", line); ini_trim(t);
    if (t[0] == '[') {
        char* e = strchr(t, ']'); if (!e) return 0;
        *e = 0; snprintf(name, 128, "%s", t + 1); ini_trim(name); return 1;
    }
    if (!t[0] || t[0] == ';') return 0;
    char* eq = strchr(t, '='); if (!eq) return 0;
    *eq = 0; snprintf(name, 128, "%s", t); ini_trim(name);
    snprintf(val, 256, "%s", eq + 1); ini_trim(val);
    return 2;
}
/* Value of [sect] key, or NULL. With key == NULL, the section's key names as a
 * NUL-separated list (the Win32 "enumerate keys" form) in `out`. */
static int ini_get(const char* path, const char* sect, const char* key, char* out, int outsz) {
    static char lines[INI_MAX_LINES][256];
    int n = ini_load(path, lines), in = 0, len = 0;
    for (int i = 0; i < n; i++) {
        char name[128], val[256]; int t = ini_parse(lines[i], name, val);
        if (t == 1) { in = ini_eq(name, sect); continue; }
        if (t != 2 || !in) continue;
        if (!key) {
            int l = (int)strlen(name);
            if (len + l + 2 > outsz) break;
            memcpy(out + len, name, (size_t)l + 1); len += l + 1;
        } else if (ini_eq(name, key)) { snprintf(out, (size_t)outsz, "%s", val); return 1; }
    }
    if (!key) { out[len] = 0; return len > 0; }
    return 0;
}

uint32_t host_ini_string(uint32_t sect, uint32_t key, uint32_t def,
                         uint32_t buf, uint32_t bufsz, uint32_t file) {
    char s[128], k[128], d[256], host[600], tmp[1024];
    sim_read_str(sect, s, sizeof(s)); sim_read_str(key, k, sizeof(k));
    sim_read_str(def, d, sizeof(d));  host_translate_path(file, host, sizeof(host));
    uint32_t n;
    if (!key) {                                         /* key-name list, NUL-separated */
        ini_get(host, s, NULL, tmp, sizeof(tmp));
        n = 0; while (tmp[n] && n < sizeof(tmp) - 1) n += (uint32_t)strlen(tmp + n) + 1;
    } else {
        if (!ini_get(host, s, k, tmp, sizeof(tmp))) snprintf(tmp, sizeof(tmp), "%s", d);
        n = (uint32_t)strlen(tmp);
    }
    n = n < bufsz ? n : (bufsz ? bufsz - 1 : 0);
    for (uint32_t i = 0; i < n; i++) MEM8(buf + i) = (uint8_t)tmp[i];
    if (bufsz) MEM8(buf + n) = 0;
    return n;
}
uint32_t host_ini_int(uint32_t sect, uint32_t key, uint32_t def, uint32_t file) {
    char s[128], k[128], host[600], tmp[256];
    sim_read_str(sect, s, sizeof(s)); sim_read_str(key, k, sizeof(k));
    host_translate_path(file, host, sizeof(host));
    if (!ini_get(host, s, k, tmp, sizeof(tmp))) return def;
    return (uint32_t)strtol(tmp, NULL, 10);
}
/* key == 0 deletes the section, val == 0 deletes the key; otherwise set/insert. */
uint32_t host_ini_write(uint32_t sect, uint32_t key, uint32_t val, uint32_t file) {
    static char lines[INI_MAX_LINES][256];
    char s[128], k[128], v[256], host[600];
    sim_read_str(sect, s, sizeof(s)); sim_read_str(key, k, sizeof(k));
    sim_read_str(val, v, sizeof(v));  host_translate_path(file, host, sizeof(host));
    int n = ini_load(host, lines), in = 0, sect_at = -1, done = 0;
    FILE* f = fopen(host, "wb");
    if (!f) return 0;
    for (int i = 0; i < n; i++) {
        char name[128], cur[256]; int t = ini_parse(lines[i], name, cur);
        if (t == 1) {
            if (in && !done && key && val) { fprintf(f, "%s=%s\r\n", k, v); done = 1; }
            in = ini_eq(name, s);
            if (in) sect_at = i;
            if (in && !key) continue;                    /* deleting the whole section */
        } else if (in && !key) continue;
        else if (in && t == 2 && key && ini_eq(name, k)) {
            if (val && !done) fprintf(f, "%s=%s\r\n", k, v);
            done = 1;
            continue;
        }
        fprintf(f, "%s\r\n", lines[i]);
    }
    if (key && val && !done) {
        if (!in || sect_at < 0) fprintf(f, "[%s]\r\n", s);
        fprintf(f, "%s=%s\r\n", k, v);
    }
    fclose(f);
    return 1;
}

/* Copy a host C string into simulated memory; returns its VA. */
uint32_t shim_strdup(const char* s) {
    uint32_t n = (uint32_t)strlen(s) + 1;
    uint32_t va = shim_alloc(n);
    if (va) memcpy((void*)(uintptr_t)ADDR(va), s, n);
    return va;
}

/* The recompiled entry point (CRT startup -> WinMain). From PE: EP RVA 0x53081. */
extern void sub_00453081(void);

/* ============================================================
 * Dispatch
 * ============================================================ */
recomp_func_t recomp_lookup(uint32_t va) {
    int lo = 0, hi = (int)recomp_dispatch_count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        uint32_t m = recomp_dispatch_table[mid].address;
        if (m == va) return recomp_dispatch_table[mid].func;
        else if (m < va) lo = mid + 1;
        else hi = mid - 1;
    }
    return NULL;
}
/* Bring-up instrumentation: log when the game's WinMain is dispatched. */
extern void sub_0040BA90(void);
void native_raster_init(uint32_t space_lo, uint32_t space_hi);
recomp_func_t native_raster_lookup(uint32_t va);
static void dump_str(const char* tag, uint32_t va) {
    fprintf(stderr, "  %s = 0x%08X -> \"", tag, va);
    for (int i = 0; i < 48 && va; i++) {
        uint8_t c = MEM8(va + i);
        if (!c) break;
        fputc(c >= 32 && c < 127 ? c : '.', stderr);
    }
    fprintf(stderr, "\"\n");
}
static void trace_winmain(void) {
    fprintf(stderr, "\n[BRINGUP] >>> game WinMain (0x0040BA90) entered <<<\n");
    /* At dispatch, g_esp points at the dummy ret; WinMain args follow. */
    for (int i = 1; i <= 5; i++)
        fprintf(stderr, "  arg%d [esp+0x%X] = 0x%08X\n", i, i*4, MEM32(g_esp + i*4));
    dump_str("arg3(lpCmdLine)", MEM32(g_esp + 0xC));
    sub_0040BA90();
}
/* The title screen's level select (platform_sdl.inc), or FURY3_CAMPAIGN=<step> for
 * testing: start New Game at that step of the 24-step
 * campaign (3 missions per planet: 0 Terran, 3 Atmos, 6 Red, 9 City, 12 Egypt,
 * 15 Asteroid mine, 18 Water, 21 Borg) -- what loading a saved game restores.
 * New Game zeroes the step [0x5000A8] and tail-jumps (thunk 0x401690) to the
 * campaign runner sub_00426690, which reads it; set it on that first entry. */
extern void sub_00426690(void);
static int g_level_pick = -1;
void host_level_pick(int step) { g_level_pick = step; }
static void campaign_runner_hook(void) {
    /* Every New Game resets the step to 0 before entering the runner (the bring-up
     * harness's Enter nudge can start it twice), so redirect each such start. */
    const char* e = host_env("FURY3_CAMPAIGN");
    int want = e ? atoi(e) : g_level_pick;
    if (want >= 0 && MEM32(0x005000A8) == 0 && MEM32(0x00542464) == 1) {
        MEM32(0x005000A8) = (uint32_t)want;
        g_level_pick = -1;
        fprintf(stderr, "[camp] starting campaign at step %u @%ums\n", MEM32(0x005000A8), host_millis());
    }
    sub_00426690();
}

/* FURY3_OPENLOG also names files opened through the game's POD reader
 * sub_0041AF00(dir, name, mode) (thunk 0x401B95), which never reaches host_fopen. */
extern void sub_0041AF00(void);
static void pod_open_logger(void) {
    char dir[64], nm[96];
    sim_read_str(MEM32(g_esp + 4), dir, sizeof(dir));
    sim_read_str(MEM32(g_esp + 8), nm, sizeof(nm));
    fprintf(stderr, "[podopen] %s\\%s @%ums\n", dir, nm, host_millis());
    sub_0041AF00();
}

/* Retry Mission (Y/N), sub_00421360, reached through the thunk at 0x40189D. It waits for a
 * character in [0x541F14], which the game's window procedure sets from WM_CHAR, and gives up
 * after a while, so the input code answers it directly while this is set. */
extern void sub_00421360(void);
static uint32_t g_yesno_since;
void sub_0040189D(void) {
    g_yesno_since = host_millis() | 1;
    sub_00421360();
    g_yesno_since = 0;
    fprintf(stderr, "[retry] answered %d\n", (int)g_eax);
}
uint32_t host_yesno_ms(void) { return g_yesno_since ? (host_millis() - g_yesno_since) | 1 : 0; }

recomp_func_t recomp_lookup_manual(uint32_t va) {
    recomp_func_t nf = native_raster_lookup(va);
    if (nf) return nf;
    if (va == 0x0040BA90u) return trace_winmain;
    if (va == 0x0041AF00u && host_env("FURY3_OPENLOG")) return pod_open_logger;
    if (va == 0x00426690u) return campaign_runner_hook;
    return NULL;
}

recomp_func_t recomp_lookup_import(uint32_t va) {
    for (uint32_t i = 0; i < fury3_import_bridge_count; i++)
        if (fury3_import_bridges[i].address == va)
            return fury3_import_bridges[i].func;
    return NULL;
}

/* Indirect calls/tails resolve the same few targets over and over (the span
 * rasterizer calls its per-span callback through [0x4EBD28] thousands of times
 * a frame; import calls fell through to a linear scan). Direct-mapped cache of
 * successful lookups; misses are not cached so their diagnostics still fire. */
#define RESOLVE_CACHE 1024
recomp_func_t recomp_resolve(uint32_t va) {
    static uint32_t c_va[RESOLVE_CACHE];
    static recomp_func_t c_fn[RESOLVE_CACHE];
    uint32_t s = (va ^ (va >> 10)) & (RESOLVE_CACHE - 1);
    if (c_fn[s] && c_va[s] == va) return c_fn[s];
    recomp_func_t fn = recomp_lookup_manual(va);
    if (!fn) fn = recomp_lookup(va);
    if (!fn) fn = recomp_lookup_import(va);
    if (fn) { c_va[s] = va; c_fn[s] = fn; }
    return fn;
}

/* ============================================================
 * VEH crash handler
 * ============================================================ */
static void dump_state(void) {
    fprintf(stderr, "  EAX=%08X ECX=%08X EDX=%08X EBX=%08X\n", g_eax, g_ecx, g_edx, g_ebx);
    fprintf(stderr, "  ESP=%08X ESI=%08X EDI=%08X\n", g_esp, g_esi, g_edi);
    fprintf(stderr, "  last indirect-call targets:\n");
    for (int i = 0; i < ICALL_TRACE_SIZE; i++) {
        uint32_t idx = (g_icall_trace_idx - ICALL_TRACE_SIZE + i) & (ICALL_TRACE_SIZE - 1);
        if (g_icall_trace[idx]) fprintf(stderr, "    0x%08X\n", g_icall_trace[idx]);
    }
}
static void crash_report(const char* what, uintptr_t pc, uintptr_t fault_addr) {
    fprintf(stderr, "\n=== FATAL %s at pc=%p, address %p (g_cur_func=0x%08X) ===\n",
            what, (void*)pc, (void*)fault_addr, g_cur_func);
    if (fault_addr >= (uintptr_t)g_mem_base)
        fprintf(stderr, "  (guest address 0x%08X)\n", (uint32_t)(fault_addr - (uintptr_t)g_mem_base));
    dump_state();
    recomp_dump_trace(what);
    fflush(stderr); fflush(stdout);
}

/* ============================================================
 * PE image loader (parses section headers at runtime)
 * ============================================================ */
#pragma pack(push,1)
typedef struct { char name[8]; uint32_t vsize, vaddr, rsize, roff; uint32_t a,b; uint16_t c,d; uint32_t chr; } SecHdr;
#pragma pack(pop)

/* Checks whether [addr, addr+size) is entirely unclaimed. Used to find a host
 * offset where the whole simulated address space (image + stack + TIB + heap)
 * can be mapped together, for when the fixed base (g_mem_base == 0) collides
 * with something the OS/CRT already mapped there (e.g. an NLS codepage file
 * loaded during argv conversion, which can land exactly on FURY3_IMAGE_BASE). */
/* Allocate the whole simulated address space as one block (see the layout note
 * at the top) and point g_mem_base at it. */
static int map_guest_space(void) {
    void* block = os_alloc_block(FURY3_SPACE_END - FURY3_SPACE_BASE);
    if (!block) return 0;
    g_mem_base = (ptrdiff_t)((uintptr_t)block - FURY3_SPACE_BASE);
    g_tib_view   = (void*)ADDR(FURY3_TIB_BASE);
    g_stack_view = (void*)ADDR(FURY3_STACK_BASE);
    g_image_view = (void*)ADDR(FURY3_IMAGE_BASE);
    g_heap_view  = (void*)ADDR(FURY3_HEAP_BASE);
    native_raster_init(FURY3_SPACE_BASE, FURY3_SPACE_END);
    return 1;
}

static int load_image(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "FATAL: cannot open %s\n", path); return 0; }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t* buf = (uint8_t*)malloc(sz);
    if (fread(buf, 1, sz, f) != (size_t)sz) { fclose(f); free(buf); return 0; }
    fclose(f);

    uint32_t e_lfanew = *(uint32_t*)(buf + 0x3C);
    uint8_t* nt = buf + e_lfanew;                 /* "PE\0\0" */
    uint16_t nsec = *(uint16_t*)(nt + 6);
    uint16_t opt_sz = *(uint16_t*)(nt + 20);
    SecHdr* sec = (SecHdr*)(nt + 24 + opt_sz);

    /* compute image span */
    uint32_t max_end = 0;
    for (int i = 0; i < nsec; i++) {
        uint32_t end = sec[i].vaddr + (sec[i].vsize ? sec[i].vsize : sec[i].rsize);
        if (end > max_end) max_end = end;
    }
    uint32_t span = (max_end + 0xFFF) & ~0xFFFu;  /* page round */

    if (FURY3_IMAGE_BASE + span > FURY3_HEAP_BASE) {
        fprintf(stderr, "FATAL: image span 0x%X overlaps the simulated heap at 0x%08X\n", span, FURY3_HEAP_BASE);
        free(buf); return 0;
    }
    if (!map_guest_space()) {
        fprintf(stderr, "FATAL: could not allocate the simulated address space (0x%X bytes)\n",
                FURY3_SPACE_END - FURY3_SPACE_BASE);
        free(buf); return 0;
    }

    /* zero the image (handles .bss) then copy each section's raw bytes to its VA */
    memset(g_image_view, 0, span);
    for (int i = 0; i < nsec; i++) {
        if (sec[i].rsize == 0) continue;
        uint32_t va = FURY3_IMAGE_BASE + sec[i].vaddr;
        uint32_t n = sec[i].rsize < sec[i].vsize || sec[i].vsize == 0 ? sec[i].rsize : sec[i].vsize;
        memcpy((void*)ADDR(va), buf + sec[i].roff, n);
    }
    free(buf);
    printf("[*] image: %d sections, span 0x%X mapped at 0x%08X (host offset 0x%tX)\n", nsec, span, FURY3_IMAGE_BASE, g_mem_base);
    return 1;
}

/* ============================================================
 * Init: stack, IAT self-patch, bridge install
 * ============================================================ */
static int setup(const char* exe) {
    if (!load_image(exe)) return 0;

    g_esp = FURY3_STACK_BASE + FURY3_STACK_SIZE - 64;

    /* Simulated TIB so fs:[...] accesses (SEH setup in CRT startup) work.
     * fs:[0]=SEH chain head (-1 = end), fs:[4]=stack base (top), fs:[8]=stack
     * limit (bottom), fs:[0x18]=self (linear TEB address). */
    g_fs_base = FURY3_TIB_BASE;
    MEM32(g_fs_base + 0x00) = 0xFFFFFFFFu;                      /* ExceptionList */
    MEM32(g_fs_base + 0x04) = FURY3_STACK_BASE + FURY3_STACK_SIZE; /* StackBase   */
    MEM32(g_fs_base + 0x08) = FURY3_STACK_BASE;                 /* StackLimit    */
    MEM32(g_fs_base + 0x18) = g_fs_base;                        /* Self          */

    /* Simulated heap for shim allocations (GlobalAlloc/VirtualAlloc/CRT). */
    g_heap_next = FURY3_HEAP_BASE;

    /* Self-patch IAT slots so both call paths resolve through the bridge table:
     *   direct : RECOMP_ICALL(slot_va)         -> lookup_import(slot_va)
     *   thunk  : RECOMP_ITAIL(MEM32(slot_va))  -> lookup_import(slot_va)   */
    for (uint32_t i = 0; i < fury3_iat_slot_count; i++)
        MEM32(fury3_iat_slots[i]) = fury3_iat_slots[i];

    printf("[*] stack: 0x%08X..0x%08X (ESP=0x%08X)\n",
           FURY3_STACK_BASE, FURY3_STACK_BASE + FURY3_STACK_SIZE, g_esp);
    printf("[*] dispatch table: %u functions\n", recomp_dispatch_count);
    printf("[*] import bridges: %u (%u IAT slots patched)\n",
           fury3_import_bridge_count, fury3_iat_slot_count);
    return 1;
}

/* Watchdog (enabled via FURY3_FLY): sample the runtime's progress to a file every
 * 500ms, with its own FILE so the main thread's stderr use can't hold it up. Logs
 * the current function plus a few flight-state words (render mode, frame dt,
 * ship Z) so menu/briefing/fly-in/flight are tellable apart from the trace alone. */
static int watchdog_thread(void* p) {
    (void)p;
    FILE* wf = fopen(FURY3_HOME "build/watchdog.log", "wb");
    uint32_t last_frames = 0, last_sleep = 0;
    for (;;) {
        os_sleep_ms(500);
        char line[512];
        uint32_t fr = g_game_frames, sl = g_throttle_sleep_ms;
        int n = snprintf(line, sizeof(line), "%ums cur=0x%08X rmode[53E860]=%d dt[4EB9D0]=%d ship_z[4EBBD0]=%d fps=%u throttle_ms=%u paused[53B8B0]=%d active[53B8B4]=%d fg=%d heap_kb=%u opens=%u/%u\n",
                         host_millis(), g_cur_func, (int)MEM32(0x0053E860), (int)MEM32(0x004EB9D0), (int)MEM32(0x004EBBD0),
                         (fr - last_frames) * 2, sl - last_sleep, (int)MEM32(0x0053B8B0), (int)MEM32(0x0053B8B4),
                         host_window_is_foreground(), (g_heap_next - FURY3_HEAP_BASE) / 1024, g_real_opens, g_game_opens);
        last_frames = fr; last_sleep = sl;
        /* the game's key-state table (by scancode): list keys currently held */
        { char keys[64]; int kn = 0; keys[0] = 0;
          for (int sc = 1; sc < 0x80 && kn < 56; sc++)
              if (MEM8(0x004EB940u + sc)) kn += snprintf(keys + kn, sizeof(keys) - kn, " %02X", sc);
          if (kn && n > 0 && n < (int)sizeof(line) - 1) {
              n -= 1;                                     /* overwrite the trailing newline */
              n += snprintf(line + n, sizeof(line) - (size_t)n, " keys_held:%s\n", keys);
              if (n >= (int)sizeof(line)) n = (int)sizeof(line) - 1;
          } }
        /* cumulative host file I/O: calls / milliseconds per kind */
        if (n > 0 && n < (int)sizeof(line) - 1) {
            static const char* kind[IO_KINDS] = { "open", "read", "seek", "getc", "len" };
            n -= 1;
            for (int k = 0; k < IO_KINDS && n < (int)sizeof(line) - 24; k++)
                n += snprintf(line + n, sizeof(line) - (size_t)n, " %s=%u/%ums", kind[k],
                              g_io_n[k], (unsigned)(g_io_us[k] / 1000u));
            if (n < (int)sizeof(line) - 1) { line[n++] = '\n'; line[n] = 0; }
        }
        if (wf) { fwrite(line, 1, (size_t)n, wf); fflush(wf); }
        /* at a boss step: the target and the objects shielding it (0x44202E), and the
         * number of objects that can take damage (0x40D690) */
        { uint32_t step = MEM32(0x0048F3E8u), rec = 0x0048F4C0u + step * 0xA4u;
          if (wf && MEM32(0x0053E860u) == 1 && step < 64 && MEM32(0x0048F46Cu + step * 0xA4u) == 5) {
              uint32_t tab = MEM32(0x0053C6CCu), target = MEM32(rec), cnt = MEM32(rec + 0x14);
              uint32_t o = tab + target * 0xB4u;
              fprintf(wf, "[boss] step=%u target=%u hp=%d/%d state=%d listed=%u damageable=%u shields:", step, target,
                      (int)MEM32(o + 0x1C), (int)MEM32(o + 0x24), (int)MEM32(o + 0x64), MEM32(o + 0x84), MEM32(0x00514F74u));
              for (uint32_t i = 0; i < cnt && i < 12; i++) {
                  uint32_t s = MEM32(rec + 0x18 + i * 4), q = tab + s * 0xB4u;
                  fprintf(wf, " %u:%d/%d", s, (int)MEM32(q + 0x1C), MEM32(q + 0x84));
              }
              fprintf(wf, "\n"); fflush(wf);
          } }
    }
    return 0;
}

/* Sampling profiler (enabled via FURY3_PROF): histogram g_cur_func every ~1ms and
 * write the hottest functions to prof.log every 10s (cumulative).
 * FURY3_PROF=2 instead samples the main thread's real instruction pointer and
 * names it by module / PDB symbol, so time in host code (GDI, shims, the CRT)
 * is visible rather than charged to whichever lifted function called it. */
#define PROF_SLOTS 4096
static uint32_t g_prof_va[PROF_SLOTS], g_prof_n[PROF_SLOTS];
static void prof_count(uint32_t va) {
    uint32_t h = (va * 2654435761u) >> 20;
    for (uint32_t k = 0; k < PROF_SLOTS; k++) {
        uint32_t s = (h + k) & (PROF_SLOTS - 1);
        if (g_prof_va[s] == va || g_prof_n[s] == 0) { g_prof_va[s] = va; g_prof_n[s]++; return; }
    }
}
#ifdef _WIN32
#include <dbghelp.h>
static HANDLE g_main_thread;
static void prof_native_dump(uint32_t total) {
    /* Aggregate raw EIPs by symbol (our exe) or module (everything else). */
    static int sym_ok = -1;
    if (sym_ok < 0) { SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
                      sym_ok = SymInitialize(GetCurrentProcess(), NULL, TRUE) ? 1 : 0; }
    static char names[PROF_SLOTS][96]; static uint32_t counts[PROF_SLOTS]; uint32_t nn = 0;
    for (uint32_t s = 0; s < PROF_SLOTS; s++) {
        if (!g_prof_n[s]) continue;
        char nm[96] = "?";
        char buf[sizeof(SYMBOL_INFO) + 96]; SYMBOL_INFO* si = (SYMBOL_INFO*)buf;
        si->SizeOfStruct = sizeof(SYMBOL_INFO); si->MaxNameLen = 80; DWORD64 disp = 0;
        HMODULE mod = NULL; char mpath[MAX_PATH] = "";
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCSTR)(uintptr_t)g_prof_va[s], &mod))
            GetModuleFileNameA(mod, mpath, sizeof(mpath));
        const char* base = mpath; for (const char* q = mpath; *q; q++) if (*q == '\\') base = q + 1;
        if (mod == GetModuleHandleA(NULL) && sym_ok && SymFromAddr(GetCurrentProcess(), g_prof_va[s], &disp, si))
            snprintf(nm, sizeof(nm), "%s", si->Name);
        else if (mod) snprintf(nm, sizeof(nm), "[%s]+0x%X", base, (unsigned)((g_prof_va[s] - (uint32_t)(uintptr_t)mod) & ~0x3Fu));
        else snprintf(nm, sizeof(nm), "[unknown]");
        uint32_t j = 0; for (; j < nn; j++) if (!strcmp(names[j], nm)) break;
        if (j == nn) { if (nn >= PROF_SLOTS) continue; snprintf(names[nn], 96, "%s", nm); counts[nn++] = 0; }
        counts[j] += g_prof_n[s];
    }
    FILE* f = fopen(FURY3_HOME "prof.log", "a"); if (!f) return;
    fprintf(f, "=== native %ums, %u samples ===\n", host_millis(), total);
    for (int top = 0; top < 40 && top < (int)nn; top++) {
        uint32_t best = top;
        for (uint32_t j = top + 1; j < nn; j++) if (counts[j] > counts[best]) best = j;
        char tn[96]; uint32_t tc = counts[top];
        memcpy(tn, names[top], 96); memcpy(names[top], names[best], 96); memcpy(names[best], tn, 96);
        counts[top] = counts[best]; counts[best] = tc;
        fprintf(f, "  %6u  %5.1f%%  %s\n", counts[top], 100.0 * counts[top] / total, names[top]);
    }
    fclose(f);
}
#endif
static int prof_thread(void* p) {
    (void)p;
#ifdef _WIN32
    if (atoi(host_env("FURY3_PROF")) == 2) {
        uint32_t total = 0, last_dump = host_millis();
        for (;;) {
            Sleep(1);
            CONTEXT ctx; ctx.ContextFlags = CONTEXT_CONTROL;
            if (SuspendThread(g_main_thread) == (DWORD)-1) continue;
            if (GetThreadContext(g_main_thread, &ctx)) { prof_count((uint32_t)ctx.Eip); total++; }
            ResumeThread(g_main_thread);
            if (host_millis() - last_dump >= 10000) { last_dump = host_millis(); prof_native_dump(total); }
        }
    }
#endif
    uint32_t total = 0, last_dump = host_millis();
    for (;;) {
        os_sleep_ms(1);
        uint32_t va = g_cur_func, h = (va * 2654435761u) >> 20;
        for (uint32_t k = 0; k < PROF_SLOTS; k++) {
            uint32_t s = (h + k) & (PROF_SLOTS - 1);
            if (g_prof_va[s] == va || g_prof_n[s] == 0) { g_prof_va[s] = va; g_prof_n[s]++; break; }
        }
        total++;
        if (host_millis() - last_dump >= 10000) {
            last_dump = host_millis();
            FILE* f = fopen(FURY3_HOME "prof.log", "a");
            if (!f) continue;
            fprintf(f, "=== %ums, %u samples ===\n", host_millis(), total);
            uint32_t idx[PROF_SLOTS], n = 0;
            for (uint32_t s = 0; s < PROF_SLOTS; s++) if (g_prof_n[s]) idx[n++] = s;
            for (int top = 0; top < 25 && top < (int)n; top++) {
                uint32_t best = top;
                for (uint32_t j = top + 1; j < n; j++) if (g_prof_n[idx[j]] > g_prof_n[idx[best]]) best = j;
                uint32_t t = idx[top]; idx[top] = idx[best]; idx[best] = t;
                fprintf(f, "  0x%08X  %6u  %5.1f%%\n", g_prof_va[idx[top]], g_prof_n[idx[top]],
                        100.0 * g_prof_n[idx[top]] / total);
            }
            fclose(f);
        }
    }
    return 0;
}

/* Rewrites one of the game's on-screen strings in the loaded image (`to` must not
 * be longer): the pause text names a keyboard key the Vita doesn't have. */
static void patch_text(const char* from, const char* to) {
    size_t n = strlen(from) + 1;
    for (uint32_t va = FURY3_IMAGE_BASE; va + n < FURY3_HEAP_BASE; va++) {
        if (MEM8(va) != (uint8_t)from[0] || memcmp((const void*)ADDR(va), from, n) != 0) continue;
        for (size_t i = 0; i <= strlen(to); i++) MEM8(va + i) = (uint8_t)to[i];
        return;
    }
}

#ifdef __vita__
/* No console or command line on the Vita: log to a file next to the game data,
 * and take FURY3_* debug switches from fury3_env.txt (KEY=VALUE per line). */
#include <psp2/power.h>
static void vita_startup(void) {
    /* The Vita boots apps at 333 MHz; homebrew may use the full 444 MHz (and the
     * matching bus/GPU clocks). The game is CPU-bound, so this is ~1/3 more speed. */
    scePowerSetArmClockFrequency(444);
    scePowerSetBusClockFrequency(222);
    scePowerSetGpuClockFrequency(222);
    scePowerSetGpuXbarClockFrequency(166);
    /* Unbuffered: closing the app from the home screen kills it without flushing
     * stdio, which left the logs empty. Steady-state logging is sparse. */
    freopen(FURY3_HOME "fury3_log.txt", "w", stdout);
    freopen(FURY3_HOME "fury3_err.txt", "w", stderr);
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    FILE* f = fopen(FURY3_HOME "fury3_env.txt", "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char* eq = strchr(line, '=');
        if (!eq || line[0] == '#') continue;
        *eq = 0;
        char* v = eq + 1; v[strcspn(v, "\r\n")] = 0;
        setenv(line, v, 1);
    }
    fclose(f);
}
#endif

int main(int argc, char** argv) {
#ifdef __vita__
    vita_startup();
    static char vita_exe[512];
    static char* vita_argv[] = { "fury3", vita_exe, "--run", NULL };
    int game_setup(const char* home, char* root, size_t rootsz, char* exe, size_t exesz);
    if (!game_setup(FURY3_HOME, g_game_root, sizeof(g_game_root), vita_exe, sizeof(vita_exe))) return 1;
    argc = 3; argv = vita_argv;
#else
    { uint64_t sz; int dir; if (os_file_info("FURY3/SYSTEM/FURY3.POD", &sz, &dir)) snprintf(g_game_root, sizeof(g_game_root), "FURY3"); }
    if (host_env("FURY3_SETUP_HOME")) {     /* test the Vita's setup path: FURY3_SETUP_HOME=<dir ending in /> */
        static char exe[512];
        int game_setup(const char* home, char* root, size_t rootsz, char* exe, size_t exesz);
        if (!game_setup(host_env("FURY3_SETUP_HOME"), g_game_root, sizeof(g_game_root), exe, sizeof(exe))) return 1;
        static char* setup_argv[] = { "fury3", exe, "--run", NULL };
        argc = 3; argv = setup_argv;
    }
#endif
    fprintf(stderr, "[setup] game folder: %s\n", g_game_root);
    host_mods_load();
    printf("=== Fury3 Static Recompilation - runtime ===\n");
    if (argc < 2) {
        printf("usage: %s <path-to-FURY3.EXE> [--run]\n", argv[0]);
        return 2;
    }
    os_timer_begin();
    g_start_tick = os_ticks_ms();
    host_index_build();
    { char act[600]; snprintf(act, sizeof(act), "%s/ART/VGA.ACT", FURY3_GAME_ROOT);
      host_load_act(act); }   /* fallback palette so frames are visible during fade-in */
    os_install_crash_handler(crash_report);
    if (!setup(argv[1])) return 1;
#ifdef __vita__
    patch_text("Paused--F3 to resume", "Paused--press START");
    patch_text("Paused--F3 to resume. Game is saved.", "Game saved--press START");
    patch_text("Retry Mission (Y/N)", "Retry? X yes, O no");
#else
    if (host_env("FURY3_PADTEXT")) {
        patch_text("Paused--F3 to resume", "Paused--press START");
        patch_text("Paused--F3 to resume. Game is saved.", "Game saved--press START");
        patch_text("Retry Mission (Y/N)", "Retry? X yes, O no");
    }
#endif
    printf("[*] runtime initialized.\n");

    if (host_env("FURY3_FLY")) os_thread(watchdog_thread, NULL);
    if (host_env("FURY3_PROF")) {
#ifdef _WIN32
        DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &g_main_thread,
                        THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, 0);
#endif
        os_thread(prof_thread, NULL);
    }
    int run = (argc > 2 && strcmp(argv[2], "--run") == 0);
    if (run) {
        printf("[*] calling entry point sub_00453081 ...\n\n");
        PUSH32(g_esp, 0xDEAD0000u);  /* dummy return address */
        sub_00453081();
        printf("\n[*] entry returned. EAX=0x%08X\n", g_eax);
    } else {
        printf("[*] ready. Re-run with --run to call the entry point.\n");
    }
    return 0;
}
