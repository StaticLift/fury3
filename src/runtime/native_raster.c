/*
 * Native replacements for the game's hottest span routines.
 *
 * 0x403C83 and 0x40509E are the perspective-correct, depth-shaded texture
 * mappers the polygon filler calls through [0x4EBD28] once per scanline. Both
 * are hand-written x86 with one register-only calling convention:
 *   in:  esi, edi -> the two edge vertices (+8 X 16.16, +0x10 Z, +0x18 U, +0x20 V),
 *        ebx = screen row
 *   out: pixels in the row, scratch globals 0x53B004..0x53B060, eax/ebx/ecx/edx
 *        and flags as the last instruction left them; esi/edi/ebp preserved.
 * The line runs in 16-pixel steps with a true perspective divide at each end
 * and affine interpolation between, then a remainder of 1..15 pixels. Texel =
 * texture[(v>>8 & 0xFF) << 8 | (u>>8 & 0xFF)] (0x40509E wraps at 64x64), shaded
 * through the table at 0x47D780 indexed by (depth-light << 8 | texel).
 *
 * These reproduce every guest-visible effect of the originals bit for bit
 * (including the integer-divide behaviour of the lifted idiv and the stack
 * slots the routine pushes). FURY3_VERIFY_RAST=<n> runs n calls of each
 * through both the lifted translation and this code on identical snapshots of
 * the whole guest space and reports any difference; FURY3_LIFTED_RAST=1 turns
 * the native versions off.
 */
#define RECOMP_GENERATED_CODE
#include "recomp_types.h"
#undef eax
#undef ecx
#undef edx
#undef ebx
#undef esp
#undef esi
#undef edi
#undef ebp
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void sub_00403C83(void);
extern void sub_0040509E(void);
extern uint32_t g_cur_func;
const char* host_env(const char* name);

#define GP(va)     ((uint8_t*)((uintptr_t)(uint32_t)(va) + (uintptr_t)g_mem_base))
#define R32(va)    (*(recomp_g32*)GP(va))
#define W32(va, v) (*(recomp_g32*)GP(va) = (uint32_t)(v))

/* The lifted idiv: quotient truncated to 32 bits, 0 where x86 would fault. */
static inline uint32_t idiv64(int64_t n, uint32_t d) {
    int32_t dv = (int32_t)d;
    if (!dv || (dv == -1 && n == INT64_MIN)) return 0;
    return (uint32_t)(int32_t)(n / dv);
}
/* cdq; idiv d */
static inline uint32_t div32(uint32_t a, uint32_t d) { return idiv64((int64_t)(int32_t)a, d); }
/* cdq; shld edx,eax,24; shl eax,24; idiv d  ==  (a << 24) / d over 64 bits */
static inline uint32_t div24(uint32_t a, uint32_t d) { return idiv64((int64_t)(int32_t)a * 16777216, d); }
static inline uint32_t sar(uint32_t v, int n) { return (uint32_t)((int32_t)v >> n); }

/* Depth -> light row of the shade table (+0x100 skips its first row). */
static inline uint32_t light_403C83(uint32_t z) {
    uint32_t a = z > 0xFFFFu ? 0xFFFFu : z;
    a = sar(a ^ 0xFFFFu, 3);
    if (a > 0xFFFu) a = 0xFFFu;
    return a + 0x100u;
}
static inline uint32_t light_40509E(uint32_t z) {
    uint32_t a = z << 2;
    if (a > 0xFFFFFu) a = 0xFFFFFu;
    return sar(a ^ 0xFFFFFu, 8) + 0x100u;
}

typedef struct { uint32_t eax, ebx, ecx, edx, ebp, esi; } span_regs;

/* n pixels from guest dst. esi steps u (and, in the remainder, carries a
 * count in its high half that drops by esi_dec per pixel). */
static inline void span_pixels(span_regs* r, uint32_t dst, int n, uint32_t esi_dec,
                               uint32_t dv, uint32_t dz, uint32_t tex, uint32_t tmask) {
    const uintptr_t mb = (uintptr_t)g_mem_base;
    uint8_t* out = (uint8_t*)(mb + dst);
    const uint32_t thi = tex & 0xFFFF0000u;
    uint32_t ecx = r->ecx, edx = r->edx, ebp = r->ebp, esi = r->esi, eax = r->eax, ebx = r->ebx;
    for (int k = 0; k < n; k++) {
        uint32_t ta = thi | (((edx & 0xFF00u) | ((ecx >> 8) & 0xFFu)) & tmask);
        uint32_t t = *(uint8_t*)(mb + ta);
        eax = (ebp & 0xFFFFFF00u) | t;
        uint32_t s = *(uint8_t*)(mb + (uint32_t)(eax + 0x47D780u));
        out[k] = (uint8_t)s;
        ebx = (ta & 0xFFFFFF00u) | s;
        ecx += esi;
        edx += dv;
        ebp += dz;
        esi -= esi_dec;
    }
    r->ecx = ecx; r->edx = edx; r->ebp = ebp; r->esi = esi; r->eax = eax; r->ebx = ebx;
}

static inline void tm_run(uint32_t va, int wrap64) {
    const uint32_t tmask = wrap64 ? 0x3F3Fu : 0xFFFFu;
    const uint32_t esp = g_esp;
    uint32_t esi = g_esi, edi = g_edi;
    const uint32_t o_esi = g_esi, o_edi = g_edi, o_ebp = g_ebp;
    g_cur_func = va;

    W32(esp - 4, o_esi); W32(esp - 8, o_edi); W32(esp - 12, o_ebp);
    uint32_t eax = R32(esi + 8), ecx = R32(edi + 8), ebx, edx = 0;
    if (eax > ecx) { uint32_t t = eax; eax = ecx; ecx = t; t = esi; esi = edi; edi = t; }
    eax >>= 16; ecx >>= 16;
    ebx = R32(g_ebx * 4 + 0x47BB70u);
    ecx = ecx - eax + 1;
    ebx += eax;
    W32(esp - 16, ebx);
    {
        static const uint32_t off[3] = { 0x18, 0x20, 0x10 };
        for (int i = 0; i < 3; i++) {
            uint32_t a = R32(esi + off[i]), b = R32(edi + off[i]);
            W32(0x53B004u + 4 * i, a);
            W32(0x53B01Cu + 4 * i, b);
            W32(0x53B028u + 4 * i, div32(b - a, ecx) << 4);
        }
    }
    W32(0x53B034u, div24(R32(0x53B004u), R32(0x53B00Cu)));
    W32(0x53B038u, div24(R32(0x53B008u), R32(0x53B00Cu)));
    W32(0x53B058u, wrap64 ? light_40509E(R32(0x53B00Cu)) : light_403C83(R32(0x53B00Cu)));
    edi = ebx;                                       /* pop edi: row pointer */

    while ((int32_t)(ecx - 16) >= 0) {
        ecx -= 16;
        uint32_t u = R32(0x53B004u) + R32(0x53B028u);
        uint32_t v = R32(0x53B008u) + R32(0x53B02Cu);
        uint32_t z = R32(0x53B00Cu) + R32(0x53B030u);
        W32(0x53B010u, u); W32(0x53B014u, v); W32(0x53B018u, z);
        W32(0x53B03Cu, div24(u, z));
        W32(0x53B040u, div24(v, z));
        W32(0x53B05Cu, wrap64 ? light_40509E(z) : light_403C83(z));
        W32(0x53B04Cu, sar(R32(0x53B03Cu) - R32(0x53B034u), 12));
        W32(0x53B050u, sar(R32(0x53B040u) - R32(0x53B038u), 12));
        W32(0x53B060u, sar(R32(0x53B05Cu) - R32(0x53B058u), 4));
        W32(esp - 16, ecx);                          /* push ecx */
        span_regs r = { 0, R32(0x4EBCB4u), sar(R32(0x53B034u), 8), sar(R32(0x53B038u), 8),
                        R32(0x53B058u), R32(0x53B04Cu) };
        span_pixels(&r, edi, 16, 0, R32(0x53B050u), R32(0x53B060u), R32(0x4EBCB4u), tmask);
        edi += 16;
        eax = R32(0x53B05Cu); ebx = R32(0x53B014u); edx = R32(0x53B018u);
        W32(0x53B004u, R32(0x53B010u));
        W32(0x53B008u, ebx);
        W32(0x53B00Cu, edx);
        W32(0x53B058u, eax);
        W32(0x53B034u, R32(0x53B03Cu));
        W32(0x53B038u, R32(0x53B040u));
    }

    uint32_t fk, fa, fb, fcf;
    if (ecx == 0) {                                  /* sub ecx,16 / js / add ecx,16 / je */
        fk = FK_ADD; fa = 0xFFFFFFF0u; fb = 0x10u; fcf = 1;
    } else {
        uint32_t n = ecx;
        W32(0x53B03Cu, div24(R32(0x53B01Cu), R32(0x53B024u)));
        W32(0x53B040u, div24(R32(0x53B020u), R32(0x53B024u)));
        W32(0x53B05Cu, wrap64 ? light_40509E(R32(0x53B024u)) : light_403C83(R32(0x53B024u)));
        W32(0x53B04Cu, sar(div32(R32(0x53B03Cu) - R32(0x53B034u), n), 8));
        W32(0x53B050u, sar(div32(R32(0x53B040u) - R32(0x53B038u), n), 8));
        W32(0x53B060u, div32(R32(0x53B05Cu) - R32(0x53B058u), n));
        uint32_t step = R32(0x53B04Cu) & 0xFFFFu;
        span_regs r = { 0, R32(0x4EBCB4u), sar(R32(0x53B034u), 8), sar(R32(0x53B038u), 8),
                        R32(0x53B058u), step | ((n - 1) << 16) };
        span_pixels(&r, edi, (int)n, 0x10000u, R32(0x53B050u), R32(0x53B060u), R32(0x4EBCB4u), tmask);
        eax = r.eax; ebx = r.ebx; ecx = r.ecx; edx = r.edx;
        fk = FK_CMP; fa = step; fb = 0x10000u; fcf = 1;   /* the sub esi,0x10000 that went negative */
    }

    g_eax = eax; g_ebx = ebx; g_ecx = ecx; g_edx = edx;
    g_esi = o_esi; g_edi = o_edi; g_ebp = o_ebp;
    g_esp = esp + 4;
    g_flag_k = fk; g_flag_a = fa; g_flag_b = fb; g_flag_cf = fcf;
}

static void native_403C83(void) { tm_run(0x00403C83u, 0); }
static void native_40509E(void) { tm_run(0x0040509Eu, 1); }

/* ---- A/B verification ---- */
typedef struct { uint32_t r[8], f[4], cur; } cpu_snap;
static void snap_cpu(cpu_snap* s) {
    s->r[0] = g_eax; s->r[1] = g_ecx; s->r[2] = g_edx; s->r[3] = g_ebx;
    s->r[4] = g_esp; s->r[5] = g_ebp; s->r[6] = g_esi; s->r[7] = g_edi;
    s->f[0] = g_flag_k; s->f[1] = g_flag_a; s->f[2] = g_flag_b; s->f[3] = g_flag_cf;
    s->cur = g_cur_func;
}
static void load_cpu(const cpu_snap* s) {
    g_eax = s->r[0]; g_ecx = s->r[1]; g_edx = s->r[2]; g_ebx = s->r[3];
    g_esp = s->r[4]; g_ebp = s->r[5]; g_esi = s->r[6]; g_edi = s->r[7];
    g_flag_k = s->f[0]; g_flag_a = s->f[1]; g_flag_b = s->f[2]; g_flag_cf = s->f[3];
    g_cur_func = s->cur;
}

static uint32_t g_space_lo, g_space_hi;
static uint8_t *g_snap_pre, *g_snap_lifted;
static int g_verify_n;

static void verify(uint32_t va, void (*lifted)(void), void (*native)(void), uint32_t* calls, uint32_t* checked, uint32_t* bad) {
    uint32_t c = (*calls)++;
    /* the first 64 calls, then a spread sample */
    if (*checked >= (uint32_t)g_verify_n || (c >= 64 && c % 251)) { native(); return; }
    size_t sz = g_space_hi - g_space_lo;
    uint8_t* space = GP(g_space_lo);
    cpu_snap in, a, b;
    snap_cpu(&in);
    memcpy(g_snap_pre, space, sz);
    lifted();
    snap_cpu(&a);
    memcpy(g_snap_lifted, space, sz);
    memcpy(space, g_snap_pre, sz);
    load_cpu(&in);
    native();
    snap_cpu(&b);
    (*checked)++;
    int diff = memcmp(&a, &b, sizeof a) != 0 || memcmp(space, g_snap_lifted, sz) != 0;
    if (diff) {
        (*bad)++;
        static const char* rn[8] = { "eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi" };
        fprintf(stderr, "[verify] %08X call %u MISMATCH (in: esi=%08X edi=%08X ebx=%08X)\n", va, c, in.r[6], in.r[7], in.r[3]);
        for (int i = 0; i < 8; i++)
            if (a.r[i] != b.r[i]) fprintf(stderr, "  %s lifted=%08X native=%08X\n", rn[i], a.r[i], b.r[i]);
        for (int i = 0; i < 4; i++)
            if (a.f[i] != b.f[i]) fprintf(stderr, "  flag[%d] lifted=%08X native=%08X\n", i, a.f[i], b.f[i]);
        if (a.cur != b.cur) fprintf(stderr, "  cur_func lifted=%08X native=%08X\n", a.cur, b.cur);
        int shown = 0;
        for (size_t i = 0; i < sz && shown < 12; i++)
            if (space[i] != g_snap_lifted[i]) {
                fprintf(stderr, "  [%08X] was=%02X lifted=%02X native=%02X\n", (unsigned)(g_space_lo + i),
                        g_snap_pre[i], g_snap_lifted[i], space[i]);
                shown++;
            }
        memcpy(space, g_snap_lifted, sz);            /* carry on with the lifted result */
        load_cpu(&a);
    }
    if (*checked == (uint32_t)g_verify_n || diff)
        fprintf(stderr, "[verify] %08X: %u checked, %u mismatched (of %u calls)\n", va, *checked, *bad, *calls);
}

static uint32_t v1_calls, v1_checked, v1_bad, v2_calls, v2_checked, v2_bad;
static void verify_403C83(void) { verify(0x00403C83u, sub_00403C83, native_403C83, &v1_calls, &v1_checked, &v1_bad); }
static void verify_40509E(void) { verify(0x0040509Eu, sub_0040509E, native_40509E, &v2_calls, &v2_checked, &v2_bad); }

void native_raster_init(uint32_t space_lo, uint32_t space_hi) {
    g_space_lo = space_lo; g_space_hi = space_hi;
    const char* v = host_env("FURY3_VERIFY_RAST");
    g_verify_n = v ? atoi(v) : 0;
    if (g_verify_n > 0) {
        g_snap_pre = (uint8_t*)malloc(space_hi - space_lo);
        g_snap_lifted = (uint8_t*)malloc(space_hi - space_lo);
        if (!g_snap_pre || !g_snap_lifted) { fprintf(stderr, "[verify] no memory for snapshots\n"); g_verify_n = 0; }
        else fprintf(stderr, "[verify] A/B checking %d calls of each native span routine\n", g_verify_n);
    }
}

recomp_func_t native_raster_lookup(uint32_t va) {
    if (host_env("FURY3_LIFTED_RAST")) return NULL;
    switch (va) {
    case 0x00403C83u: return g_verify_n > 0 ? verify_403C83 : native_403C83;
    case 0x0040509Eu: return g_verify_n > 0 ? verify_40509E : native_40509E;
    }
    return NULL;
}
