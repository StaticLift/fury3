/* host_os.h on the PS Vita (VitaSDK). */
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/io/dirent.h>
#include <psp2/io/stat.h>
#include <stdio.h>
#include <string.h>
#include "host_os.h"

uint32_t os_ticks_ms(void) { return (uint32_t)(sceKernelGetProcessTimeWide() / 1000u); }
uint64_t os_ticks_us(void) { return (uint64_t)sceKernelGetProcessTimeWide(); }
void     os_sleep_ms(uint32_t ms) { sceKernelDelayThread(ms * 1000u); }
void     os_timer_begin(void) {}

/* User main memory comes in blocks of whole 4 KB pages; it is zeroed on allocation. */
void* os_alloc_block(size_t size) {
    size = (size + 0xFFFu) & ~(size_t)0xFFFu;
    SceUID uid = sceKernelAllocMemBlock("fury3_guest", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, size, NULL);
    if (uid < 0) { fprintf(stderr, "[os] sceKernelAllocMemBlock(0x%zX) failed: 0x%08X\n", size, (unsigned)uid); return NULL; }
    void* base = NULL;
    if (sceKernelGetMemBlockBase(uid, &base) < 0) return NULL;
    memset(base, 0, size);
    return base;
}

int os_file_info(const char* path, uint64_t* size, int* is_dir) {
    SceIoStat st;
    if (sceIoGetstat(path, &st) < 0) return 0;
    if (size) *size = (uint64_t)st.st_size;
    if (is_dir) *is_dir = SCE_S_ISDIR(st.st_mode);
    return 1;
}
void os_list_dir(const char* dir, os_dir_fn fn, void* ctx) {
    SceUID d = sceIoDopen(dir);
    if (d < 0) return;
    SceIoDirent e;
    while (sceIoDread(d, &e) > 0) {
        if (e.d_name[0] == '.') continue;
        fn(e.d_name, SCE_S_ISDIR(e.d_stat.st_mode), (uint64_t)e.d_stat.st_size, ctx);
    }
    sceIoDclose(d);
}
/* ux0: is FAT/exFAT: lookups are already case-insensitive. */
void os_resolve_case(char* path, size_t pathsz) { (void)path; (void)pathsz; }

typedef struct { int (*fn)(void*); void* arg; } thread_start_t;
static int thread_trampoline(SceSize args, void* argp) {
    thread_start_t s = *(thread_start_t*)argp;     /* argp is the kernel's copy */
    (void)args;
    s.fn(s.arg);
    return sceKernelExitDeleteThread(0);
}
void os_thread(int (*fn)(void*), void* arg) {
    thread_start_t s = { fn, arg };
    /* Diagnostics only: keep them on user core 1, away from the game's thread. */
    SceUID t = sceKernelCreateThread("fury3_diag", thread_trampoline, 0x10000100, 0x10000, 0,
                                     SCE_KERNEL_CPU_MASK_USER_1, NULL);
    if (t >= 0) sceKernelStartThread(t, sizeof(s), &s);
}

/* Vita faults are reported by the system's crash dump (ux0:data/*.psp2dmp). */
void os_install_crash_handler(os_crash_fn report) { (void)report; }

int os_make_dir(const char* path) {
    if (sceIoMkdir(path, 0777) >= 0) return 1;
    SceIoStat st;
    return sceIoGetstat(path, &st) >= 0 && SCE_S_ISDIR(st.st_mode);
}
