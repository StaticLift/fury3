/*
 * Fury3 Recompilation - import bridge helpers.
 *
 * Shims run inside the recomp register/stack model. When the lifted code calls
 * an import, RECOMP_ICALL has already pushed a dummy return address, so the
 * stack from ESP looks like: [ret][arg0][arg1]... Each arg is one 32-bit slot.
 *
 *   ARG(n)     read argument n (0-based)
 *   ARGP(n,T)  read argument n as a host pointer of type T* (maps VA->host)
 *   RET(v)     set the return value (EAX)
 *   STDRET(k)  stdcall epilogue: pop the dummy ret addr + k argument slots
 */
#ifndef FURY3_IMPORTS_H
#define FURY3_IMPORTS_H

#include "recomp_types.h"

#define ARG(n)      MEM32(g_esp + 4 + (n)*4)
#define ARGP(n, T)  ((T*)(void*)(uintptr_t)ADDR(ARG(n)))
#define RET(v)      do { g_eax = (uint32_t)(v); } while (0)
#define STDRET(k)   do { g_esp += 4 + (uint32_t)(k)*4; } while (0)

/* Log an unimplemented import once, by name. */
#define IMPORT_STUB(nm) do { \
    static int _w = 0; \
    if (!_w) { fprintf(stderr, "[import-stub] %s\n", (nm)); _w = 1; } \
} while (0)

/* Simulated-memory allocators (recomp_runtime.c). Return VAs. */
uint32_t shim_alloc(uint32_t n);
uint32_t shim_alloc_aligned(uint32_t n, uint32_t align);
uint32_t shim_strdup(const char* s);
uint32_t host_millis(void);
const char* host_env(const char* name);   /* cached getenv for FURY3_* switches */

/* Host file + INI I/O (recomp_runtime.c), backed by the real game directory. */
void     host_translate_path(uint32_t va, char* out, int outsz);
uint32_t host_file_open(uint32_t name_va, uint32_t access, uint32_t disp);
uint32_t host_file_read(uint32_t sh, uint32_t buf_va, uint32_t n);
uint32_t host_file_write(uint32_t sh, uint32_t buf_va, uint32_t n);
uint32_t host_file_seek(uint32_t sh, int32_t dist, uint32_t method);
uint32_t host_file_size(uint32_t sh);
void     host_file_close(uint32_t sh);
int      host_file_getc(uint32_t sh);
int      host_is_file(uint32_t sh);
uint32_t host_fopen(uint32_t name_va, uint32_t mode_va);
uint32_t host_stream_handle(uint32_t stream_va);
void     host_stream_set_eof(uint32_t stream_va, int eof);
uint32_t host_ini_string(uint32_t sect, uint32_t key, uint32_t def, uint32_t buf, uint32_t bufsz, uint32_t file);
uint32_t host_ini_int(uint32_t sect, uint32_t key, uint32_t def, uint32_t file);
uint32_t host_ini_write(uint32_t sect, uint32_t key, uint32_t val, uint32_t file);
uint32_t host_stat(uint32_t name_va, uint32_t buf_va);
void     host_index_build(void);
uint32_t host_filelength(uint32_t name_va);
uint32_t host_malloc(uint32_t n);
void     host_free(uint32_t ptr);
uint32_t host_realloc(uint32_t ptr, uint32_t n);
uint32_t host_calloc(uint32_t num, uint32_t sz);

/* Display (recomp_runtime.c). */
uint32_t host_create_window(uint32_t title_va);
uint32_t host_create_dib(uint32_t pbmi_va, uint32_t ppvbits_va);
void     host_set_dib_colors(uint32_t start, uint32_t n, uint32_t prgb_va);
void     host_blit(void);
uint32_t host_create_memdc(void);
void     host_delete_dc(uint32_t hdc);
uint32_t host_select_object(uint32_t hdc, uint32_t obj);
void     host_blit_from_dc(uint32_t hdc_src);
int      host_game_presenting(void);

/* waveOut on SDL2 audio (recomp_runtime.c); no device without FURY3_SDL. */
uint32_t host_wave_num_devs(void);
uint32_t host_wave_get_caps(uint32_t pwoc, uint32_t cb);
uint32_t host_wave_open(uint32_t phwo, uint32_t pwfx, uint32_t fdw);
uint32_t host_wave_prepare(uint32_t pwh);
uint32_t host_wave_unprepare(uint32_t pwh);
uint32_t host_wave_write(uint32_t pwh);
uint32_t host_wave_reset(void);
uint32_t host_wave_close(void);
void     host_wave_poll(void);
void     host_thread_created(uint32_t start);   /* CreateThread: emulate known guest threads */
int      host_pump(void);
int      host_should_quit(void);
void     host_present_tick(void);
void     host_frame_throttle(void);
int      host_peek_game_msg(uint32_t lpMsg, int remove);
int      host_translate_accel(uint32_t accelVA, uint32_t lpMsg);
void     host_dispatch_msg(uint32_t lpMsg);
void     host_set_logpalette(uint32_t logpal);
void     host_set_game_wndproc(uint32_t va);
void     host_invalidate(void);

/* Generated bridge tables (imports_gen.c). */
extern const recomp_dispatch_entry_t fury3_import_bridges[];
extern const uint32_t fury3_import_bridge_count;
extern const uint32_t fury3_iat_slots[];
extern const uint32_t fury3_iat_slot_count;

#endif /* FURY3_IMPORTS_H */
