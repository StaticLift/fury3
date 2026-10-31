/* host_os.h on Windows. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include "host_os.h"

uint32_t os_ticks_ms(void) { return (uint32_t)timeGetTime(); }
uint64_t os_ticks_us(void) {
    static LARGE_INTEGER freq; LARGE_INTEGER now;
    if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);
    return (uint64_t)(now.QuadPart / freq.QuadPart) * 1000000u
         + (uint64_t)(now.QuadPart % freq.QuadPart) * 1000000u / (uint64_t)freq.QuadPart;
}
void     os_sleep_ms(uint32_t ms) { Sleep(ms); }
/* 1 ms scheduler granularity so the frame throttle paces precisely (the default
 * ~15.6 ms makes the game's dt jitter). */
void     os_timer_begin(void) { timeBeginPeriod(1); }

void* os_alloc_block(size_t size) {
    void* p = VirtualAlloc(NULL, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!p) fprintf(stderr, "[os] VirtualAlloc(0x%zX) failed, GetLastError=%lu\n", size, GetLastError());
    return p;
}

int os_file_info(const char* path, uint64_t* size, int* is_dir) {
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &fad)) return 0;
    if (size) *size = ((uint64_t)fad.nFileSizeHigh << 32) | fad.nFileSizeLow;
    if (is_dir) *is_dir = (fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    return 1;
}
void os_list_dir(const char* dir, os_dir_fn fn, void* ctx) {
    char pat[MAX_PATH + 4]; snprintf(pat, sizeof(pat), "%s/*", dir);
    WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.cFileName[0] == '.') continue;
        fn(fd.cFileName, (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0,
           ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow, ctx);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}
void os_resolve_case(char* path, size_t pathsz) { (void)path; (void)pathsz; }

typedef struct { int (*fn)(void*); void* arg; } thread_start_t;
static DWORD WINAPI thread_trampoline(LPVOID p) {
    thread_start_t s = *(thread_start_t*)p; HeapFree(GetProcessHeap(), 0, p);
    return (DWORD)s.fn(s.arg);
}
void os_thread(int (*fn)(void*), void* arg) {
    thread_start_t* s = (thread_start_t*)HeapAlloc(GetProcessHeap(), 0, sizeof(*s));
    s->fn = fn; s->arg = arg;
    HANDLE h = CreateThread(NULL, 0, thread_trampoline, s, 0, NULL);
    if (h) CloseHandle(h);
}

static os_crash_fn g_crash_report;
static LONG WINAPI veh(EXCEPTION_POINTERS* ep) {
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    /* Ignore non-fatal C++/debug/control exceptions the CRT raises and handles. */
    const char* what =
        code == EXCEPTION_ACCESS_VIOLATION    ? "access violation" :
        code == EXCEPTION_STACK_OVERFLOW      ? "stack overflow" :
        code == EXCEPTION_ILLEGAL_INSTRUCTION ? "illegal instruction" :
        code == EXCEPTION_INT_DIVIDE_BY_ZERO  ? "integer divide by zero" :
        code == EXCEPTION_PRIV_INSTRUCTION    ? "privileged instruction" :
        code == 0xC0000409                    ? "fast-fail" : NULL;
    if (!what) return EXCEPTION_CONTINUE_SEARCH;
    uintptr_t fault = ep->ExceptionRecord->NumberParameters >= 2
                    ? (uintptr_t)ep->ExceptionRecord->ExceptionInformation[1] : 0;
    if (g_crash_report) g_crash_report(what, (uintptr_t)ep->ExceptionRecord->ExceptionAddress, fault);
    return EXCEPTION_CONTINUE_SEARCH;
}
void os_install_crash_handler(os_crash_fn report) {
    g_crash_report = report;
    AddVectoredExceptionHandler(1, veh);
}

int os_make_dir(const char* path) {
    if (CreateDirectoryA(path, NULL)) return 1;
    return GetLastError() == ERROR_ALREADY_EXISTS;
}
