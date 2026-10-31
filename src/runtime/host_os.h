/*
 * Fury3 runtime - the host operating-system services the runtime needs, behind
 * one small interface (host_os_win32.c / host_os_posix.c; a Vita one later).
 * Everything above this layer is portable C plus SDL2 (or the Win32 GDI frontend).
 */
#ifndef FURY3_HOST_OS_H
#define FURY3_HOST_OS_H
#include <stdint.h>
#include <stddef.h>

uint32_t os_ticks_ms(void);                       /* monotonic milliseconds */
uint64_t os_ticks_us(void);                       /* monotonic microseconds (profiling) */
void     os_sleep_ms(uint32_t ms);
void     os_timer_begin(void);                    /* best sleep/timer precision */

/* The simulated address space: one zeroed read/write block, anywhere (guest
 * addresses are an offset into it -- g_mem_base). NULL on failure. */
void*    os_alloc_block(size_t size);

/* Files and directories. os_file_info returns 1 if the path exists. */
int      os_file_info(const char* path, uint64_t* size, int* is_dir);
typedef void (*os_dir_fn)(const char* name, int is_dir, uint64_t size, void* ctx);
void     os_list_dir(const char* dir, os_dir_fn fn, void* ctx);
/* Make `path` (in place) match the on-disk case of each existing component, as
 * Windows/FAT lookups would; a no-op on case-insensitive filesystems. */
void     os_resolve_case(char* path, size_t pathsz);
/* Create one directory (its parent must exist); 1 if it exists afterwards. */
int      os_make_dir(const char* path);

/* A detached background thread (diagnostics only: watchdog, profiler). */
void     os_thread(int (*fn)(void*), void* arg);

/* Report fatal faults (bad memory access etc.) before the process dies. */
typedef void (*os_crash_fn)(const char* what, uintptr_t pc, uintptr_t fault_addr);
void     os_install_crash_handler(os_crash_fn report);

#endif
