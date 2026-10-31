/*
 * Fury3 Recompilation - hand-written import shims.
 *
 * These replace the auto-generated stubs for imports that the MSVC CRT startup
 * and early game init actually depend on (command line, environment, std
 * handles, the heap). Each runs in the recomp stack model: read args with
 * ARG()/ARGP(), return via RET(), and clean the stack with STDRET(argc).
 *
 * Names listed in gen_imports.py's MANUAL set resolve here (the generator emits
 * an extern decl and the bridge table points at these).
 */
#define RECOMP_GENERATED_CODE
#include "recomp_types.h"
#include "imports.h"
#include <string.h>
#include <math.h>
#include <stdlib.h>

/* ---- versions / locale ---- */
void imp_GetVersion(void)      { RET(0x00000004u); STDRET(0); }   /* Windows 4.0 */
void imp_GetACP(void)          { RET(1252); STDRET(0); }
void imp_GetOEMCP(void)        { RET(437);  STDRET(0); }
void imp_GetCPInfo(void) {       /* (CodePage, CPINFO* lpCPInfo) */
    uint32_t p = ARG(1);
    MEM32(p + 0) = 1;            /* MaxCharSize = 1 */
    MEM8(p + 4) = 0;            /* DefaultChar[0] */
    RET(1); STDRET(2);
}

/* ---- module / command line / environment ---- */
void imp_GetModuleHandleA(void) { RET(0x00400000u); STDRET(1); } /* hModule = image base */

void imp_GetCommandLineA(void) {
    static uint32_t va = 0;
    if (!va) va = shim_strdup("fury3.exe");
    RET(va); STDRET(0);
}
void imp_GetModuleFileNameA(void) { /* (hModule, lpFilename, nSize) */
    const char* path = "C:\\FURY3\\FURY3.EXE";
    uint32_t buf = ARG(1), n = ARG(2), len = (uint32_t)strlen(path);
    if (len + 1 > n) len = n ? n - 1 : 0;
    for (uint32_t i = 0; i < len; i++) MEM8(buf + i) = (uint8_t)path[i];
    MEM8(buf + len) = 0;
    RET(len); STDRET(3);
}
void imp_GetCurrentDirectoryA(void) { /* (nBufferLength, lpBuffer) */
    const char* d = "C:\\FURY3";
    uint32_t n = ARG(0), buf = ARG(1), len = (uint32_t)strlen(d);
    if (buf && len + 1 <= n) { for (uint32_t i=0;i<=len;i++) MEM8(buf+i)=(uint8_t)d[i]; }
    RET(len); STDRET(2);
}
void imp_GetEnvironmentStrings(void) {
    static uint32_t va = 0;
    if (!va) { va = shim_alloc(2); MEM8(va) = 0; MEM8(va + 1) = 0; }  /* "\0\0" */
    RET(va); STDRET(0);
}

/* ---- std handles / file output ---- */
void imp_GetStdHandle(void)  { RET(0xF0000000u | (ARG(0) & 0xFu)); STDRET(1); }
void imp_SetStdHandle(void)  { RET(1); STDRET(2); }
/* GetFileType + WriteFile are defined below (file-I/O aware). */

/* ---- startup info ---- */
void imp_GetStartupInfoA(void) { /* (LPSTARTUPINFOA) - 68 bytes */
    uint32_t p = ARG(0);
    for (int i = 0; i < 68; i += 4) MEM32(p + i) = 0;
    MEM32(p + 0) = 68;          /* cb */
    RET(0); STDRET(1);
}

/* ---- memory ---- */
/* VirtualAlloc returns page-aligned memory; the CRT heap relies on that. */
void imp_VirtualAlloc(void) {  /* (lpAddress, dwSize, flAllocationType, flProtect) */
    uint32_t addr = ARG(0), size = ARG(1);
    RET(addr ? addr : shim_alloc_aligned(size, 0x1000));
    STDRET(4);
}
void imp_VirtualFree(void)   { RET(1); STDRET(3); }
void imp_GlobalAlloc(void)   { /* (uFlags, dwBytes) */ RET(shim_alloc(ARG(1))); STDRET(2); }
void imp_GlobalLock(void)    { RET(ARG(0)); STDRET(1); }
void imp_GlobalFree(void)    { RET(0); STDRET(1); }
void imp_GlobalMemoryStatus(void) { /* (LPMEMORYSTATUS) - 32 bytes */
    uint32_t p = ARG(0);
    for (int i = 0; i < 32; i += 4) MEM32(p + i) = 0;
    MEM32(p + 0)  = 32;            /* dwLength */
    MEM32(p + 4)  = 50;            /* dwMemoryLoad % */
    MEM32(p + 8)  = 0x20000000u;   /* dwTotalPhys = 512 MB */
    MEM32(p + 12) = 0x10000000u;   /* dwAvailPhys = 256 MB */
    RET(0); STDRET(1);
}

/* ============================================================
 * Host-shimmed CRT functions (declared in the generated dispatch, defined here).
 * These replace lifted library code whose internals aren't worth debugging.
 * ============================================================ */

/* Shared null-safe formatter: reads a printf format + varargs from simulated
 * memory (args at sim VA `ap`), formats each conversion with the host CRT, and
 * writes the result into host buffer `dest`. A null %s prints "(null)" (the MSVC
 * behaviour) instead of dereferencing 0. Returns the length written. */
static int sim_format(uint32_t fmt, uint32_t ap, char* dest, int destsz) {
    char spec[64], tmp[1200], hoststr[4096];
    int out = 0;
    #define PUT(ch) do { if (out < destsz - 1) dest[out++] = (ch); } while (0)
    for (uint32_t f = fmt; MEM8(f); ) {
        char c = (char)MEM8(f++);
        if (c != '%') { PUT(c); continue; }
        int si = 0; spec[si++] = '%';
        int star_args = 0; uint32_t starv[2] = {0, 0};
        for (;;) {
            char d = (char)MEM8(f++);
            if (si < (int)sizeof(spec) - 2) spec[si++] = d;
            if (d == '*') { starv[star_args < 2 ? star_args : 1] = MEM32(ap); ap += 4; star_args++; continue; }
            if (d == '%' || d == 'c' || d == 's' || d == 'd' || d == 'i' || d == 'u' ||
                d == 'x' || d == 'X' || d == 'o' || d == 'p' || d == 'e' || d == 'E' ||
                d == 'f' || d == 'g' || d == 'G' || d == 0) break;
        }
        spec[si] = 0;
        char conv = spec[si - 1];
        for (int k = 0; spec[k]; k++) if (spec[k] == 'l' || spec[k] == 'h') {
            for (int j = k; spec[j]; j++) spec[j] = spec[j + 1];
            k--;
        }
        if (conv == '%') { PUT('%'); continue; }
        int n = 0;
        if (conv == 's') {
            uint32_t sva = MEM32(ap); ap += 4;
            uint32_t h = 0;
            if (sva) while (h < sizeof(hoststr) - 1 && MEM8(sva + h)) { hoststr[h] = (char)MEM8(sva + h); h++; }
            else { const char* nul = "(null)"; for (; nul[h]; h++) hoststr[h] = nul[h]; }
            hoststr[h] = 0;
            if (star_args == 2) n = snprintf(tmp, sizeof(tmp), spec, starv[0], starv[1], hoststr);
            else if (star_args == 1) n = snprintf(tmp, sizeof(tmp), spec, starv[0], hoststr);
            else n = snprintf(tmp, sizeof(tmp), spec, hoststr);
        } else if (conv == 'c') {
            uint32_t v = MEM32(ap); ap += 4;
            n = snprintf(tmp, sizeof(tmp), spec, (int)(v & 0xFF));
        } else if (conv == 'e' || conv == 'E' || conv == 'f' || conv == 'g' || conv == 'G') {
            union { uint64_t u; double d; } u; u.u = MEM32(ap) | ((uint64_t)MEM32(ap + 4) << 32); ap += 8;
            n = snprintf(tmp, sizeof(tmp), spec, u.d);
        } else { /* d i u x X o p */
            uint32_t v = MEM32(ap); ap += 4;
            if (star_args == 2) n = snprintf(tmp, sizeof(tmp), spec, starv[0], starv[1], v);
            else if (star_args == 1) n = snprintf(tmp, sizeof(tmp), spec, starv[0], v);
            else n = snprintf(tmp, sizeof(tmp), spec, v);
        }
        for (int k = 0; k < n && k < (int)sizeof(tmp); k++) PUT(tmp[k]);
    }
    dest[out] = 0;
    #undef PUT
    return out;
}

/* _sprintf(buf, fmt, ...) — sub_00450AE7. cdecl: [esp]=dummy ret, +4=buf, +8=fmt. */
void sub_00450AE7(void) {
    uint32_t buf = MEM32(g_esp + 4);
    static char hostbuf[8192];
    int n = sim_format(MEM32(g_esp + 8), g_esp + 12, hostbuf, sizeof(hostbuf));
    for (int i = 0; i < n; i++) MEM8(buf + i) = (uint8_t)hostbuf[i];
    MEM8(buf + n) = 0;
    RET(n);
    g_esp += 4;          /* pop dummy ret only (cdecl: caller cleans args) */
}

/* printf(fmt, ...) — sub_00452523. Debug output -> host stderr, null-safe. */
void sub_00452523(void) {
    static char hostbuf[8192];
    int n = sim_format(MEM32(g_esp + 4), g_esp + 8, hostbuf, sizeof(hostbuf));
    fputs(hostbuf, stderr);
    RET(n);
    g_esp += 4;
}

/* ============================================================
 * CRT stdio family, host-shimmed onto the real file layer.
 * The game does its file I/O through fopen/fread/fseek/... and the lifted CRT
 * _iob FILE table is not viable, so we replace the family wholesale. fopen
 * returns one of our file handles (>= 0xA0000); any other "stream" value (the
 * _iob stdout/stderr entries) is treated as a console stream -> host stderr.
 * All are cdecl: [esp]=dummy ret, args at +4.
 * ============================================================ */
void sub_0045250C(void) {   /* _fopen(name, mode) -> FILE* (our struct) or NULL */
    RET(host_fopen(MEM32(g_esp + 4), MEM32(g_esp + 8))); g_esp += 4;
}
/* ms_p5_mp_test_fdiv: the Pentium FDIV-bug test. The lifted FP test mis-detects the
 * bug, which sets _adjust_fdiv (0x5433B0) and routes every FP divide through the
 * (broken-in-lift) software helper sub_00451340 -> NaN across all FP math. The host
 * CPU has no FDIV bug, so report "clean" (0). This un-NaNs the 3D shade/lighting. */
void sub_00454818(void) { RET(0); g_esp += 4; }
/* In-flight cheat-code reader. Its kbhit (sub_446E90) returns garbage — the lifted
 * `cmp [0x541F14],1; sbb eax,eax` reads a stale carry (lifter doesn't publish CF
 * from cmp) — so the read loop spins/blocks and freezes flight. It only scans for
 * cheat codes (no rendering/gameplay), so no-op it. cdecl, no args. */
void sub_0043BFB0(void) { g_esp += 4; }
/* CRT x87 transcendental intrinsics — args arrive on the x87 stack (g_st[0]=st0),
 * result returns on st0. The lifted x87 dispatcher (__trandisp1) miscomputes a
 * table index and crashes, so compute with C math.h instead. cdecl, no int args. */
void sub_00452BCA(void) { g_st[0] = asin(g_st[0]); g_esp += 4; }   /* __CIasin */
void sub_00452BD4(void) { g_st[0] = acos(g_st[0]); g_esp += 4; }   /* __CIacos */
void sub_00452BE2(void) {                                          /* __CIatan2(y,x): st1=y, st0=x */
    double x = fp_pop(); double y = fp_pop(); fp_push(atan2(y, x)); g_esp += 4;
}
void sub_004508F0(void) {   /* _fclose(stream) */
    uint32_t h = host_stream_handle(MEM32(g_esp + 4));
    if (h) host_file_close(h);
    RET(0); g_esp += 4;
}
void sub_00450963(void) {   /* _fread(ptr, size, n, stream) -> items */
    uint32_t ptr = MEM32(g_esp + 4), sz = MEM32(g_esp + 8),
             n = MEM32(g_esp + 12), h = host_stream_handle(MEM32(g_esp + 16));
    uint32_t items = 0;
    if (h && sz) items = host_file_read(h, ptr, sz * n) / sz;
    RET(items); g_esp += 4;
}
void sub_0045277C(void) {   /* _fwrite(ptr, size, n, stream) -> items */
    uint32_t ptr = MEM32(g_esp + 4), sz = MEM32(g_esp + 8),
             n = MEM32(g_esp + 12), s = MEM32(g_esp + 16), h = host_stream_handle(s);
    if (h) { RET(sz ? host_file_write(h, ptr, sz * n) / sz : 0); }
    else { for (uint32_t i = 0; i < sz * n; i++) fputc(MEM8(ptr + i), stderr); RET(n); }
    g_esp += 4;
}
void sub_004525BA(void) {   /* _fseek(stream, offset, origin) */
    uint32_t h = host_stream_handle(MEM32(g_esp + 4));
    if (h) host_file_seek(h, (int32_t)MEM32(g_esp + 8), MEM32(g_esp + 12));
    RET(0); g_esp += 4;
}
void sub_004529F0(void) {   /* _ftell(stream) */
    uint32_t h = host_stream_handle(MEM32(g_esp + 4));
    RET(h ? host_file_seek(h, 0, 1 /*FILE_CURRENT*/) : 0); g_esp += 4;
}
/* FURY3_SCANF also traces the character-level stdio shims (_filbuf/ungetc/fgets). */
static int io_trace(void) {
    static int on = -1;
    if (on < 0) on = host_env("FURY3_SCANF") != NULL;
    return on;
}
void sub_0045235E(void) {   /* _filbuf(stream) -> next byte or EOF (getc fallthrough) */
    uint32_t s = MEM32(g_esp + 4), h = host_stream_handle(s);
    int c = h ? host_file_getc(h) : -1;
    host_stream_set_eof(s, c < 0);
    if (io_trace()) fprintf(stderr, "[filbuf] stream=0x%08X sh=0x%X caller=0x%08X -> %d\n", s, h, g_cur_func, c);
    RET((uint32_t)c); g_esp += 4;
}
/* ungetc(c, stream) -- sub_004522CA. The game hand-inlines getc and then calls
 * ungetc to push one char back (e.g. the ';' peek in the enemy/level parsers).
 * Our getc path goes through host_file_getc/_filbuf (real ReadFile, advancing the
 * OS file position) and never fills the MSVC FILE buffer, so the lifted ungetc --
 * which pushes into that unused buffer -- is incoherent and faults. Mirror the
 * host layer instead: seek the real stream back one byte. */
void sub_004522CA(void) {
    int ch = (int)MEM32(g_esp + 4);
    uint32_t h = host_stream_handle(MEM32(g_esp + 8));
    if (ch != -1 && h) { host_file_seek(h, -1, 1 /*SEEK_CUR*/); host_stream_set_eof(MEM32(g_esp + 8), 0); }
    if (io_trace()) fprintf(stderr, "[ungetc] ch=%d stream=0x%08X sh=0x%X caller=0x%08X\n", ch, MEM32(g_esp + 8), h, g_cur_func);
    RET(ch == -1 ? 0xFFFFFFFFu : (uint32_t)(ch & 0xFF));
    g_esp += 4;          /* pop dummy ret only (cdecl: caller cleans args) */
}
void sub_00452259(void) {   /* _fgets(buf, n, stream) -> buf or NULL */
    uint32_t buf = MEM32(g_esp + 4), n = MEM32(g_esp + 8), h = host_stream_handle(MEM32(g_esp + 12));
    if (!h || n == 0) { RET(0); g_esp += 4; return; }
    uint32_t i = 0; int c = -1;
    while (i + 1 < n) { c = host_file_getc(h); if (c < 0) break; MEM8(buf + i++) = (uint8_t)c; if (c == '\n') break; }
    MEM8(buf + i) = 0;
    if (io_trace()) {
        char sb[200]; uint32_t k = 0;
        for (; k < 199 && k < i; k++) { char ch = (char)MEM8(buf + k); sb[k] = (ch == '\r' || ch == '\n') ? '|' : ch; }
        sb[k] = 0;
        fprintf(stderr, "[fgets] stream=0x%08X sh=0x%X buf=0x%08X n=%u caller=0x%08X got=%u '%s'\n",
                MEM32(g_esp + 12), h, buf, n, g_cur_func, i, sb);
    }
    RET(i ? buf : 0); g_esp += 4;
}
void sub_0045249E(void) {   /* _fprintf(stream, fmt, ...) */
    static char hostbuf[8192];
    static uint32_t scratch = 0;
    uint32_t h = host_stream_handle(MEM32(g_esp + 4));
    int n = sim_format(MEM32(g_esp + 8), g_esp + 12, hostbuf, sizeof(hostbuf));
    if (h) {
        if (!scratch) scratch = shim_alloc(8192);
        for (int i = 0; i < n; i++) MEM8(scratch + i) = (uint8_t)hostbuf[i];
        host_file_write(h, scratch, (uint32_t)n);
    } else {
        fputs(hostbuf, stderr);
    }
    RET(n); g_esp += 4;
}
/* fscanf(stream, fmt, ...) -- sub_0045223E. The lifted MSVC scanf core reads via
 * a lowio path that bypasses our host FILE layer, so it never sees file bytes.
 * This host implementation reads through host_file_getc and covers the formats
 * the game uses to parse its text manifests (mission.ini, *.LVL): %d/%i/%u/%x/%o,
 * %s, %c, %f/%g/%e, plus literal and whitespace matching. cdecl varargs.
 * sscanf (sub_00452451) shares it: the lifted core gets a character back through the
 * shimmed ungetc, which only knows files, so on a string the delimiter after the first
 * number was lost and parsing stopped there. */
static int host_scan(uint32_t sh, uint32_t str, uint32_t fmt, uint32_t ap) {
    int assigned = 0, c;
    int fury3_dbg_scanf = host_env("FURY3_SCANF") != NULL;
    if (fury3_dbg_scanf) {
        char fb[64]; uint32_t fi = 0;
        for (; fi < 63 && MEM8(fmt + fi); fi++) fb[fi] = (char)MEM8(fmt + fi);
        fb[fi] = 0;
        fprintf(stderr, "[scanf] sh=0x%X fmt='%s'", sh, fb);
    }
    #define SC_GETC()  (str ? (MEM8(str) ? (int)MEM8(str++) : -1) : sh ? host_file_getc(sh) : -1)
    #define SC_UNGETC(ch) do { if ((ch) >= 0) { if (str) str--; else if (sh) host_file_seek(sh, -1, 1 /*SEEK_CUR*/); } } while (0)
    #define SC_ISWS(ch) ((ch)==' '||(ch)=='\t'||(ch)=='\n'||(ch)=='\r'||(ch)=='\f'||(ch)=='\v')
    for (uint32_t f = fmt; MEM8(f); ) {
        uint8_t fc = MEM8(f++);
        if (SC_ISWS(fc)) { do { c = SC_GETC(); } while (SC_ISWS(c)); SC_UNGETC(c); continue; }
        if (fc != '%') { c = SC_GETC(); if (c != fc) { SC_UNGETC(c); goto sc_done; } continue; }
        int suppress = 0, width = 0;
        if (MEM8(f) == '*') { suppress = 1; f++; }
        while (MEM8(f) >= '0' && MEM8(f) <= '9') { width = width * 10 + (MEM8(f) - '0'); f++; }
        while (MEM8(f)=='l'||MEM8(f)=='h'||MEM8(f)=='L') f++;
        uint8_t conv = MEM8(f++);
        if (conv == '%') { c = SC_GETC(); if (c != '%') { SC_UNGETC(c); goto sc_done; } continue; }
        uint32_t arg = suppress ? 0 : MEM32(ap); if (!suppress) ap += 4;
        if (conv != 'c') { do { c = SC_GETC(); } while (SC_ISWS(c)); } else c = SC_GETC();
        if (c < 0) goto sc_done;
        if (conv=='d'||conv=='i'||conv=='u'||conv=='x'||conv=='X'||conv=='o') {
            int neg = 0, any = 0, w = width ? width : 1<<30, base = (conv=='x'||conv=='X')?16:(conv=='o')?8:10;
            long val = 0;
            if (c=='-'){neg=1;c=SC_GETC();w--;} else if(c=='+'){c=SC_GETC();w--;}
            while (c>=0 && w>0) {
                int d;
                if (c>='0'&&c<='9') d=c-'0';
                else if (c>='a'&&c<='f') d=c-'a'+10;
                else if (c>='A'&&c<='F') d=c-'A'+10;
                else break;
                if (d>=base) break;
                val=val*base+d; any=1; c=SC_GETC(); w--;
            }
            SC_UNGETC(c);
            if (!any) goto sc_done;
            if (neg) val=-val;
            if (arg) MEM32(arg)=(uint32_t)val;
            if (!suppress) assigned++;
        } else if (conv=='s') {
            int n=0, w = width ? width : 1<<30;
            while (c>=0 && !SC_ISWS(c) && n<w) { if (arg) MEM8(arg+n)=(uint8_t)c; n++; c=SC_GETC(); }
            SC_UNGETC(c);
            if (arg) MEM8(arg+n)=0;
            if (n==0) goto sc_done;
            if (!suppress) assigned++;
            if (fury3_dbg_scanf && arg) {
                char sb[64]; int si=0; for (; si<63 && si<n; si++) sb[si]=(char)MEM8(arg+si);
                sb[si]=0;
                fprintf(stderr, "[scanf.s] dest=0x%08X len=%d val='%s'\n", arg, n, sb);
            }
        } else if (conv=='c') {
            int w = width ? width : 1;
            for (int n=0; n<w && c>=0; n++) { if (arg) MEM8(arg+n)=(uint8_t)c; if (n+1<w) c=SC_GETC(); }
            if (!suppress) assigned++;
        } else if (conv=='f'||conv=='g'||conv=='e'||conv=='E'||conv=='G') {
            char nb[80]; int n=0;
            if (c=='-'||c=='+'){ nb[n++]=(char)c; c=SC_GETC(); }
            while (c>=0 && n<78 && ((c>='0'&&c<='9')||c=='.'||c=='e'||c=='E'||c=='-'||c=='+')) { nb[n++]=(char)c; c=SC_GETC(); }
            SC_UNGETC(c);
            nb[n]=0; if (n==0) goto sc_done;
            double dv = atof(nb);
            if (arg) { float fv=(float)dv; uint32_t bits; memcpy(&bits,&fv,4); MEM32(arg)=bits; }
            if (!suppress) assigned++;
        } else { goto sc_done; }
    }
sc_done:
    if (fury3_dbg_scanf) fprintf(stderr, " -> assigned=%d\n", assigned);
    return assigned;
    #undef SC_GETC
    #undef SC_UNGETC
    #undef SC_ISWS
}
void sub_0045223E(void) {
    RET((uint32_t)host_scan(host_stream_handle(MEM32(g_esp + 4)), 0, MEM32(g_esp + 8), g_esp + 12));
    g_esp += 4;   /* pop dummy ret only (cdecl: caller cleans varargs) */
}
void sub_00452451(void) {   /* sscanf(str, fmt, ...) */
    RET((uint32_t)host_scan(0, MEM32(g_esp + 4), MEM32(g_esp + 8), g_esp + 12));
    g_esp += 4;
}
void sub_0045337F(void) { RET(0); g_esp += 4; }   /* _fflush(stream) */
void sub_0045C1D1(void) {   /* _stat(path, buf) */
    RET(host_stat(MEM32(g_esp + 4), MEM32(g_esp + 8))); g_esp += 4;
}

/* ---- file I/O (backed by the real game directory via runtime helpers) ---- */
void imp_CreateFileA(void) {   /* (name, access, share, sa, disp, flags, template) */
    RET(host_file_open(ARG(0), ARG(1), ARG(4))); STDRET(7);
}
void imp_ReadFile(void) {      /* (h, buf, n, *read, ovl) */
    uint32_t got = host_file_read(ARG(0), ARG(1), ARG(2));
    if (ARG(3)) MEM32(ARG(3)) = got;
    RET(1); STDRET(5);
}
void imp_WriteFile(void) {     /* (h, buf, n, *written, ovl) */
    uint32_t h = ARG(0), got;
    if (h >= 0x000A0000u && h < 0x000A0040u) got = host_file_write(h, ARG(1), ARG(2));
    else { uint32_t buf=ARG(1),n=ARG(2); for(uint32_t i=0;i<n;i++) fputc(MEM8(buf+i),stdout); got=n; }
    if (ARG(3)) MEM32(ARG(3)) = got;
    RET(1); STDRET(5);
}
void imp_SetFilePointer(void) { RET(host_file_seek(ARG(0), (int32_t)ARG(1), ARG(3))); STDRET(4); }
void imp_CloseHandle(void)    { host_file_close(ARG(0)); RET(1); STDRET(1); }
void imp_GetFileSize(void)    { RET(host_file_size(ARG(0))); STDRET(2); }
void imp_GetFileType(void) {   /* real files = FILE_TYPE_DISK(1); std handles = CHAR(2) */
    uint32_t h = ARG(0);
    RET((h >= 0x000A0000u && h < 0x000A0040u) ? 1 : 2); STDRET(1);
}

/* ---- INI config (real FURY3.INI via runtime helpers) ---- */
void imp_GetPrivateProfileStringA(void) { /* (sect,key,def,buf,size,file) */
    RET(host_ini_string(ARG(0), ARG(1), ARG(2), ARG(3), ARG(4), ARG(5))); STDRET(6);
}
void imp_GetPrivateProfileIntA(void) {    /* (sect,key,def,file) */
    RET(host_ini_int(ARG(0), ARG(1), ARG(2), ARG(3))); STDRET(4);
}
void imp_WritePrivateProfileStringA(void){ /* (sect,key,val,file) */
    RET(host_ini_write(ARG(0), ARG(1), ARG(2), ARG(3))); STDRET(4);
}

/* ---- display: real window + DIB blit ---- */
void imp_RegisterClassA(void) {   /* (WNDCLASSA*) -> capture the game's WndProc (+4) */
    uint32_t wp = MEM32(MEM32(g_esp + 4) + 4);
    host_set_game_wndproc(wp);
    RET(1); STDRET(1);
}
void imp_InvalidateRect(void) {   /* (hwnd, rect, erase) -> trigger a real WM_PAINT */
    host_invalidate(); RET(1); STDRET(3);
}
void imp_CreateWindowExA(void) {  /* (exStyle, cls, name, style, x,y,w,h, parent, menu, inst, param) */
    RET(host_create_window(MEM32(g_esp + 12))); STDRET(12);
}
void imp_GetDC(void)      { RET(0x00020001u); STDRET(1); }
void imp_ReleaseDC(void)  { RET(1); STDRET(2); }
void imp_CreateDIBSection(void) { /* (hdc, pbmi, usage, ppvBits, hSection, offset) */
    RET(host_create_dib(MEM32(g_esp + 8), MEM32(g_esp + 16))); STDRET(6);
}
void imp_SetDIBColorTable(void) { /* (hdc, start, n, prgbq) */
    if (host_env("FURY3_PALLOG")) fprintf(stderr, "[pal] SetDIBColorTable start=%u n=%u\n", MEM32(g_esp+8), MEM32(g_esp+12));
    host_set_dib_colors(MEM32(g_esp + 8), MEM32(g_esp + 12), MEM32(g_esp + 16));
    RET(MEM32(g_esp + 12)); STDRET(4);
}
void imp_CreatePalette(void) { host_set_logpalette(MEM32(g_esp + 4)); RET(0x00040001u); STDRET(1); }
/* ---- WINMM waveOut (sound effects) -> host_wave_* (SDL2 audio) ---- */
void imp_waveOutGetNumDevs(void)       { RET(host_wave_num_devs()); STDRET(0); }
void imp_waveOutGetDevCapsA(void)      { RET(host_wave_get_caps(ARG(1), ARG(2))); STDRET(3); }   /* (uDev, pwoc, cbwoc) */
void imp_waveOutOpen(void)             { RET(host_wave_open(ARG(0), ARG(2), ARG(5))); STDRET(6); } /* (phwo, uDev, pwfx, cb, inst, fdw) */
void imp_waveOutPrepareHeader(void)    { RET(host_wave_prepare(ARG(1))); STDRET(3); }            /* (hwo, pwh, cbwh) */
void imp_waveOutUnprepareHeader(void)  { RET(host_wave_unprepare(ARG(1))); STDRET(3); }
void imp_waveOutWrite(void)            { RET(host_wave_write(ARG(1))); STDRET(3); }
void imp_waveOutReset(void)            { RET(host_wave_reset()); STDRET(1); }
void imp_waveOutClose(void)            { RET(host_wave_close()); STDRET(1); }

void imp_CreateCompatibleDC(void) { RET(host_create_memdc()); STDRET(1); }
void imp_DeleteDC(void) { host_delete_dc(ARG(0)); RET(1); STDRET(1); }
void imp_SelectObject(void) { RET(host_select_object(ARG(0), ARG(1))); STDRET(2); }
/* (hdcDest, x, y, w, h, hdcSrc, ...) -- both present the source DC's DIB. */
void imp_StretchBlt(void) { host_blit_from_dc(ARG(5)); RET(1); STDRET(11); }
void imp_BitBlt(void)     { host_blit_from_dc(ARG(5)); RET(1); STDRET(9); }
void imp_PeekMessageA(void) {  /* (lpMsg, hwnd, min, max, removeFlag) -> have message? */
    uint32_t lpMsg = ARG(0), removeFlag = ARG(4);
    host_present_tick();
    /* Hand real input (WM_KEY/WM_CHAR/mouse) to the game's own loop -- it reads
     * keys from the PeekMessage result, not via its WndProc (which DefWindowProcs
     * them). This is what lets F2/Enter etc. actually reach the game. */
    if (host_peek_game_msg(lpMsg, removeFlag != 0)) { RET(1); STDRET(5); return; }
    if (host_should_quit() && lpMsg) {     /* deliver WM_QUIT so the game's loop exits cleanly */
        MEM32(lpMsg + 0) = 0; MEM32(lpMsg + 4) = 0x0012 /*WM_QUIT*/;
        MEM32(lpMsg + 8) = 0;  MEM32(lpMsg + 12) = 0;
        RET(1); STDRET(5); return;
    }
    if (!host_game_presenting())
    host_frame_throttle();     /* queue empty = one frame boundary; pace to ~60fps
                                * so timeGetTime advances and the game's dt != 0 */
    RET(0);                    /* no message -> game renders a frame */
    STDRET(5);
}
void imp_GetMessageA(void) {   /* (lpMsg, hwnd, min, max) */
    uint32_t lpMsg = ARG(0);
    host_present_tick();
    if (host_peek_game_msg(lpMsg, 1)) { RET(1); STDRET(4); return; }
    if (host_should_quit()) {
        if (lpMsg) { MEM32(lpMsg + 0) = 0; MEM32(lpMsg + 4) = 0x0012; MEM32(lpMsg + 8) = 0; MEM32(lpMsg + 12) = 0; }
        RET(0); STDRET(4); return;        /* GetMessage returns 0 on WM_QUIT */
    }
    if (lpMsg) { MEM32(lpMsg + 0) = 0; MEM32(lpMsg + 4) = 0; MEM32(lpMsg + 8) = 0; MEM32(lpMsg + 12) = 0; }
    RET(1); STDRET(4);         /* no quit: report an (empty) message, keep looping */
}

/* The front-end's menu keys (F2 = New Game, F1 = Help, ...) are an ACCELERATOR
 * table, not WndProc keystrokes. The game's message loop calls TranslateAccelerator
 * to convert a keydown into a WM_COMMAND. We point LoadAccelerators at the loaded
 * RT_ACCELERATOR resource and translate against it. */
void imp_LoadAcceleratorsA(void) {     /* (hInst, lpTableName) */
    RET(0x0054D7F8u); STDRET(2);       /* sim VA of RT_ACCELERATOR id 128 */
}
void imp_TranslateAcceleratorA(void) { /* (hWnd, hAccTable, lpMsg) */
    if (host_translate_accel(ARG(1), ARG(2))) { RET(1); } else { RET(0); }
    STDRET(3);
}
/* The front-end's modal dialogs have no real implementation here. We dismiss the
 * info dialogs (256-colour warning, Quick Help) as IDOK. The "Select Mission"
 * dialog (template 145) returns the chosen mission index via [0x45FCC0]; with no
 * real dialog we auto-pick mission 0 so a New Game proceeds to load the level. */
void imp_DispatchMessageA(void) {      /* (lpMsg) -> route to the game's WndProc */
    host_dispatch_msg(ARG(0));
    RET(0); STDRET(1);
}
void imp_DialogBoxParamA(void) {       /* (hInst, lpTemplate, hWndParent, lpDialogFunc, dwInitParam) */
    uint32_t tmpl = ARG(1);
    if (tmpl == 0x91) {                /* "Select Mission" -> choose the first mission */
        MEM32(0x0045FCC0) = 0;
        RET(0);
    } else if (tmpl == 0x8D) {         /* pilot/difficulty dialog: simulate clicking OK */
        MEM32(0x00460060) = 1;         /* dword_460060 = 1 (OK pressed) -- New Game proceeds */
        MEM32(0x0053E7BC) = 1;         /* dword_53E7BC = difficulty (WM_INITDIALOG default) */
        RET(0);                        /* the game reads [0x460060], not the return value */
    } else {
        RET(1);                        /* info dialogs: IDOK */
    }
    STDRET(5);
}

/* ---- heap: replace the fragile lifted MSVC small-block heap ---- */
void sub_00450B56(void) { RET(host_malloc(MEM32(g_esp + 4))); g_esp += 4; }            /* malloc */
void sub_00450CB1(void) { host_free(MEM32(g_esp + 4)); RET(0); g_esp += 4; }           /* free */
void sub_0045ADB9(void) { RET(host_calloc(MEM32(g_esp + 4), MEM32(g_esp + 8))); g_esp += 4; } /* calloc */
void sub_0045E49F(void) { RET(host_realloc(MEM32(g_esp + 4), MEM32(g_esp + 8))); g_esp += 4; } /* realloc */

/* ---- saved games: the common file dialogs, without the dialog ----
 * Save Game / Resume Saved Game ask comdlg32 for a "Fury3 Files (*.fff)" path in
 * <game>\data (OPENFILENAMEA: +0x1C lpstrFile, +0x20 nMaxFile, +0x2C
 * lpstrInitialDir, +0x38 nFileOffset, +0x3A nFileExtension). There is no UI to
 * pick one on a handheld, so the game gets one save slot, data\FURY3.FFF:
 * saving always writes it and restoring loads it (or, failing that, any other
 * .fff a player copied into data\). */
#include "host_os.h"
#define SAVE_SLOT "FURY3.FFF"
static void ofn_set_file(uint32_t ofn, const char* dir, const char* name) {
    char path[256];
    snprintf(path, sizeof(path), "%s\\%s", dir, name);
    uint32_t buf = MEM32(ofn + 0x1C), cap = MEM32(ofn + 0x20), n = (uint32_t)strlen(path);
    if (n + 1 > cap) n = cap - 1;
    for (uint32_t i = 0; i < n; i++) MEM8(buf + i) = (uint8_t)path[i];
    MEM8(buf + n) = 0;
    const char* dot = strrchr(name, '.');
    MEM16(ofn + 0x38) = (uint16_t)(strlen(dir) + 1);
    MEM16(ofn + 0x3A) = (uint16_t)(dot ? strlen(dir) + 1 + (dot - name) + 1 : 0);
}
static void ofn_dir(uint32_t ofn, char* dir, int dirsz) {
    uint32_t va = MEM32(ofn + 0x2C);
    int i = 0;
    if (va) for (; i < dirsz - 1 && MEM8(va + i); i++) dir[i] = (char)MEM8(va + i);
    dir[i] = 0;
    if (!i) snprintf(dir, dirsz, "C:\\FURY3\\data");
}
typedef struct { char found[64]; } save_scan;
static void save_scan_fn(const char* name, int is_dir, uint64_t size, void* ctx) {
    save_scan* s = (save_scan*)ctx;
    size_t n = strlen(name);
    (void)size;
    if (is_dir || n < 5 || n >= sizeof(s->found)) return;
    const char* ext = name + n - 4;
    if ((ext[0] == '.') && (ext[1] | 32) == 'f' && (ext[2] | 32) == 'f' && (ext[3] | 32) == 'f') {
        int slot = 1;
        for (int i = 0; SAVE_SLOT[i]; i++) if ((name[i] | 32) != (SAVE_SLOT[i] | 32)) { slot = 0; break; }
        if (slot || !s->found[0]) strcpy(s->found, name);
    }
}
void imp_GetSaveFileNameA(void) {   /* (LPOPENFILENAMEA) */
    uint32_t ofn = ARG(0);
    char dir[200];
    ofn_dir(ofn, dir, sizeof(dir));
    ofn_set_file(ofn, dir, SAVE_SLOT);
    fprintf(stderr, "[save] GetSaveFileName -> %s\\%s\n", dir, SAVE_SLOT);
    RET(1); STDRET(1);
}
void imp_GetOpenFileNameA(void) {   /* (LPOPENFILENAMEA) */
    uint32_t ofn = ARG(0);
    char dir[200], host_dir[512];
    save_scan s; s.found[0] = 0;
    ofn_dir(ofn, dir, sizeof(dir));
    host_translate_path(MEM32(ofn + 0x2C), host_dir, sizeof(host_dir));
    os_list_dir(host_dir, save_scan_fn, &s);
    fprintf(stderr, "[save] GetOpenFileName in %s -> %s\n", host_dir, s.found[0] ? s.found : "(no saved game)");
    if (!s.found[0]) { RET(0); STDRET(1); return; }
    ofn_set_file(ofn, dir, s.found);
    RET(1); STDRET(1);
}
void imp_CommDlgExtendedError(void) { RET(0); STDRET(0); }

/* ---- the AVI cinematics (story\*.avi) through MCI command strings ----
 * The movie player (0x450010..0x450326) opens "open avivideo", then
 * 'open "<file>" alias mov style child parent <hwnd>', asks "status mov window
 * handle" and "where mov source" to place the video window, sends
 * "play mov fullscreen notify" (or "window notify") and waits in its message
 * loop for MM_MCINOTIFY, whose handler closes the movie. The queries get plausible
 * answers; "play" runs the movie to its end (SDL builds; movie_sdl.inc) and then
 * reports completion. */
void host_mci_notify_later(void);
void host_mci_open(const char* guest_path);
void host_mci_play(void);
void imp_mciSendStringA(void) {   /* (lpszCommand, lpszReturnString, cchReturn, hwndCallback) */
    char cmd[300];
    uint32_t c = ARG(0), ret = ARG(1), cap = ARG(2), i = 0;
    for (; i < sizeof(cmd) - 1 && MEM8(c + i); i++) cmd[i] = (char)MEM8(c + i);
    cmd[i] = 0;
    const char* answer = "";
    if (!strncmp(cmd, "status", 6) && strstr(cmd, "window handle")) answer = "0";
    else if (!strncmp(cmd, "where", 5)) answer = "0 0 320 200";
    if (ret && cap) {
        uint32_t n = (uint32_t)strlen(answer);
        if (n + 1 > cap) n = cap - 1;
        for (uint32_t k = 0; k < n; k++) MEM8(ret + k) = (uint8_t)answer[k];
        MEM8(ret + n) = 0;
    }
    fprintf(stderr, "[mci] %s\n", cmd);
    if (!strncmp(cmd, "open \"", 6)) {
        char* q = strchr(cmd + 6, '"');
        if (q) { *q = 0; host_mci_open(cmd + 6); }
    } else if (!strncmp(cmd, "play", 4)) {
        host_mci_play();
        if (strstr(cmd, "notify")) host_mci_notify_later();
    }
    RET(0); STDRET(4);
}
void imp_mciGetErrorStringA(void) {   /* (fdwError, lpszErrorText, cchErrorText) */
    if (ARG(1) && ARG(2)) MEM8(ARG(1)) = 0;
    RET(1); STDRET(3);
}

/* ---- diagnostics ---- */
void imp_MessageBoxA(void) {   /* (hWnd, lpText, lpCaption, uType) */
    uint32_t text = ARG(1), cap = ARG(2);
    char t[512], c[128]; uint32_t i;
    for (i = 0; i < sizeof(t) - 1 && MEM8(text + i); i++) t[i] = (char)MEM8(text + i);
    t[i] = 0;
    for (i = 0; i < sizeof(c) - 1 && MEM8(cap + i); i++) c[i] = (char)MEM8(cap + i);
    c[i] = 0;
    fprintf(stderr, "\n=== MessageBox [%s] ===\n%s\n=====================\n", c, t);
    if (!strcmp(t, "Hose enemy crlf")) {
        uint32_t buf = MEM32(0x0053C6C8u), idx = ebx;
        fprintf(stderr, "[diag] buf=0x%08X ebx(idx)=0x%08X esi=0x%08X edi=0x%08X ecx=0x%08X\n",
                buf, idx, esi, edi, ecx);
        if (buf) {
            uint32_t addr = buf + idx + 0xF0u;
            fprintf(stderr, "[diag] field@[buf+idx+0xF0]=0x%08X bytes: ", addr);
            for (int k = 0; k < 24; k++) {
                uint8_t b = MEM8(addr + k);
                fprintf(stderr, "%02X ", b);
            }
            fprintf(stderr, "\n[diag] as text: '");
            for (int k = 0; k < 24; k++) {
                uint8_t b = MEM8(addr + k);
                fprintf(stderr, "%c", (b >= 32 && b < 127) ? b : '.');
            }
            fprintf(stderr, "'\n");
            fprintf(stderr, "[diag] full struct dump buf+idx (0x%08X) .. +0x230:\n", buf + idx);
            for (int row = 0; row < 0x230; row += 16) {
                fprintf(stderr, "  +%04X: ", row);
                for (int k = 0; k < 16; k++) fprintf(stderr, "%02X ", MEM8(buf + idx + row + k));
                fprintf(stderr, " '");
                for (int k = 0; k < 16; k++) {
                    uint8_t b = MEM8(buf + idx + row + k);
                    fprintf(stderr, "%c", (b >= 32 && b < 127) ? b : '.');
                }
                fprintf(stderr, "'\n");
            }
        }
    }
    RET(1 /*IDOK*/); STDRET(4);
}

/* ---- timing: must actually advance or the game's wait loops spin forever ---- */
void imp_timeGetTime(void)    { extern uint32_t host_hires_ms(void); RET(host_hires_ms()); STDRET(0); }
/* CreateThread: return a distinct fake handle (not 1) so SetThreadPriority can tell
 * a thread handle from the GetCurrentProcess pseudo-handle (1). We don't spawn the
 * thread (the global-register model isn't thread-safe yet). */
/* (sa, stack, start, param, flags, &tid). Lifted code is single-threaded: the runtime
 * emulates the one thread the game starts (its sound pump, see host_thread_created). */
void imp_CreateThread(void) { host_thread_created(ARG(2)); RET(0x00010000u); STDRET(6); }
/* SetThreadPriority: the game does SetThreadPriority(GetCurrentProcess(),1) and
 * aborts ("Cannot raise my priority!  Bool!") if it SUCCEEDS. Fail for the process
 * pseudo-handle (1), succeed for real thread handles. */
void imp_SetThreadPriority(void) { uint32_t h = MEM32(g_esp + 4); RET(h == 1u ? 0u : 1u); STDRET(2); }
/* ---- Joystick: forward to the host's real WINMM joystick (flight controls) ---- */
void imp_joyGetNumDevs(void) { extern uint32_t host_joy_getnumdevs(void); RET(host_joy_getnumdevs()); STDRET(0); }
void imp_joyGetPos(void) {     /* (uJoyID, LPJOYINFO) -> JOYINFO{wXpos,wYpos,wZpos,wButtons} */
    extern uint32_t host_joy_getpos(uint32_t,uint32_t*,uint32_t*,uint32_t*,uint32_t*);
    uint32_t id = ARG(0), p = ARG(1), x=0,y=0,z=0,b=0;
    uint32_t r = host_joy_getpos(id, &x,&y,&z,&b);
    if (r == 0 && p) { MEM32(p+0)=x; MEM32(p+4)=y; MEM32(p+8)=z; MEM32(p+12)=b; }
    RET(r); STDRET(2);
}
void imp_joyGetDevCapsA(void) {  /* (uJoyID, LPJOYCAPSA, cbjc) */
    extern uint32_t host_joy_getdevcaps(uint32_t,void*,uint32_t);
    uint32_t id = ARG(0), p = ARG(1), cb = ARG(2);
    unsigned char caps[512];
    if (cb > sizeof(caps)) cb = sizeof(caps);
    uint32_t r = host_joy_getdevcaps(id, caps, cb);
    if (r == 0 && p) for (uint32_t i = 0; i < cb; i++) MEM8(p+i) = caps[i];
    RET(r); STDRET(3);
}
void imp_timeBeginPeriod(void){ RET(0); STDRET(1); }
void imp_timeEndPeriod(void)  { RET(0); STDRET(1); }

/* ---- dynamic loading ---- */
void imp_LoadLibraryA(void)  {
    char n[128]; uint32_t v = ARG(0);
    uint32_t i = 0; for (; i < sizeof(n) - 1 && MEM8(v + i); i++) n[i] = (char)MEM8(v + i); n[i] = 0;
    RET(0); STDRET(1);
}
void imp_GetProcAddress(void){
    char n[128]; uint32_t v = ARG(1);
    if (v > 0xFFFF) { uint32_t i = 0; for (; i < sizeof(n)-1 && MEM8(v+i); i++) n[i]=(char)MEM8(v+i); n[i]=0; }
    else snprintf(n, sizeof(n), "#%u", v);
    RET(0); STDRET(2);
}

/* ---- process exit: actually terminate so we don't run into garbage ---- */
void imp_ExitProcess(void) {   /* (uExitCode) */
    uint32_t code = ARG(0);
    fprintf(stderr, "\n[ExitProcess] game called ExitProcess(%u) - terminating\n", code);
    recomp_dump_trace("ExitProcess");
    fflush(stdout); fflush(stderr);
    exit((int)code);
}
