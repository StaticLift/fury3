/* host_os.h on POSIX (Linux). */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "host_os.h"


uint32_t os_ticks_ms(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u);
}
uint64_t os_ticks_us(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}
void os_sleep_ms(uint32_t ms) {
    struct timespec ts = { (time_t)(ms / 1000u), (long)(ms % 1000u) * 1000000L };
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR) {}
}
void os_timer_begin(void) {}

void* os_alloc_block(size_t size) {
    void* p = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) { fprintf(stderr, "[os] mmap(0x%zX) failed: %s\n", size, strerror(errno)); return NULL; }
    return p;
}

int os_file_info(const char* path, uint64_t* size, int* is_dir) {
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    if (size) *size = (uint64_t)st.st_size;
    if (is_dir) *is_dir = S_ISDIR(st.st_mode);
    return 1;
}
void os_list_dir(const char* dir, os_dir_fn fn, void* ctx) {
    DIR* d = opendir(dir);
    if (!d) return;
    struct dirent* e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        char full[1024]; int is_dir = 0; uint64_t size = 0;
        snprintf(full, sizeof(full), "%s/%s", dir, e->d_name);
        os_file_info(full, &size, &is_dir);
        fn(e->d_name, is_dir, size, ctx);
    }
    closedir(d);
}

/* Walk the path one component at a time; where a component doesn't exist as
 * spelled, substitute a case-insensitive match from its directory. Stops at the
 * first component with no match (the rest is kept as given, e.g. a file about to
 * be created). */
void os_resolve_case(char* path, size_t pathsz) {
    if (os_file_info(path, NULL, NULL)) return;
    char out[1024] = ""; size_t outlen = 0;
    const char* p = path;
    int absolute = (*p == '/');
    if (absolute) { out[0] = '/'; out[1] = 0; outlen = 1; p++; }
    while (*p) {
        const char* slash = strchr(p, '/');
        size_t n = slash ? (size_t)(slash - p) : strlen(p);
        char comp[256]; if (n >= sizeof(comp)) return;
        memcpy(comp, p, n); comp[n] = 0;
        char trial[1024];
        snprintf(trial, sizeof(trial), "%s%s%s", out, (outlen && out[outlen - 1] != '/') ? "/" : "", comp);
        if (!os_file_info(trial, NULL, NULL)) {
            DIR* d = opendir(outlen ? out : ".");
            int found = 0;
            if (d) {
                struct dirent* e;
                while ((e = readdir(d)) != NULL)
                    if (!strcasecmp(e->d_name, comp)) { snprintf(comp, sizeof(comp), "%s", e->d_name); found = 1; break; }
                closedir(d);
            }
            snprintf(trial, sizeof(trial), "%s%s%s", out, (outlen && out[outlen - 1] != '/') ? "/" : "", comp);
            if (!found) {                         /* keep the remainder verbatim */
                snprintf(out, sizeof(out), "%s%s", trial, slash ? slash : "");
                snprintf(path, pathsz, "%s", out);
                return;
            }
        }
        snprintf(out, sizeof(out), "%s", trial); outlen = strlen(out);
        if (!slash) break;
        p = slash + 1;
    }
    snprintf(path, pathsz, "%s", out);
}

typedef struct { int (*fn)(void*); void* arg; } thread_start_t;
static void* thread_trampoline(void* p) {
    thread_start_t s = *(thread_start_t*)p; free(p);
    s.fn(s.arg);
    return NULL;
}
void os_thread(int (*fn)(void*), void* arg) {
    thread_start_t* s = (thread_start_t*)malloc(sizeof(*s));
    s->fn = fn; s->arg = arg;
    pthread_t t;
    if (pthread_create(&t, NULL, thread_trampoline, s) == 0) pthread_detach(t);
    else free(s);
}

static os_crash_fn g_crash_report;
static void on_signal(int sig, siginfo_t* si, void* uc) {
    (void)uc;
    const char* what = sig == SIGSEGV ? "segmentation fault" : sig == SIGBUS ? "bus error" :
                       sig == SIGILL  ? "illegal instruction" : sig == SIGFPE ? "arithmetic fault" : "signal";
    if (g_crash_report) g_crash_report(what, 0, (uintptr_t)si->si_addr);
    signal(sig, SIG_DFL);
    raise(sig);
}
void os_install_crash_handler(os_crash_fn report) {
    g_crash_report = report;
    struct sigaction sa; memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = on_signal; sa.sa_flags = SA_SIGINFO | SA_RESETHAND;
    sigaction(SIGSEGV, &sa, NULL); sigaction(SIGBUS, &sa, NULL);
    sigaction(SIGILL, &sa, NULL);  sigaction(SIGFPE, &sa, NULL);
}

int os_make_dir(const char* path) { return mkdir(path, 0755) == 0 || errno == EEXIST; }
