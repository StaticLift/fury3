/*
 * Cinepak ("cvid") video decoder, for the game's AVI cinematics.
 *
 * A frame is a set of horizontal strips. Each strip carries two codebooks of
 * 2x2 pixel blocks (v1: one entry stretched over a 4x4 block; v4: four entries,
 * one per 2x2 quadrant) and a stream of 4x4 blocks that use, skip or replace
 * what is on screen. Codebooks and pixels persist between frames, so the
 * decoder keeps them in cvid_ctx and draws over the previous frame in `fb`.
 */
#include <string.h>
#include "cinepak.h"

static uint32_t rb16(const uint8_t* p) { return (uint32_t)p[0] << 8 | p[1]; }
static uint32_t rb24(const uint8_t* p) { return (uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | p[2]; }
static uint32_t rb32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static uint32_t clip8(int v) { return v < 0 ? 0u : v > 255 ? 255u : (uint32_t)v; }
static uint32_t rgb(int y, int u, int v) {
    return 0xFF000000u | clip8(y + v * 2) << 16 | clip8(y - u / 2 - v) << 8 | clip8(y + u * 2);
}

/* Chunk ids: bit 0 = entries are selected by flag words (partial update),
 * bit 2 = 4 bytes per entry (luma only) rather than 6 (Y0-3, U, V). */
static void read_codebook(uint32_t (*book)[4], int id, const uint8_t* d, const uint8_t* end) {
    uint32_t flag = 0, mask = 0;
    int n = (id & 4) ? 4 : 6;
    for (int i = 0; i < 256; i++) {
        if ((id & 1) && !(mask >>= 1)) {
            if (d + 4 > end) return;
            flag = rb32(d); d += 4; mask = 0x80000000u;
        }
        if ((id & 1) && !(flag & mask)) continue;
        if (d + n > end) return;
        int u = n == 6 ? (int8_t)d[4] : 0, v = n == 6 ? (int8_t)d[5] : 0;
        for (int k = 0; k < 4; k++) book[i][k] = rgb(d[k], u, v);
        d += n;
    }
}

static void put_block(uint32_t* fb, int w, int h, int x, int y, const uint32_t* a, const uint32_t* b,
                      const uint32_t* c, const uint32_t* e) {
    if (x + 4 > w || y + 4 > h) return;
    uint32_t* r = fb + y * w;
    r[x] = a[0]; r[x + 1] = a[1]; r[x + 2] = b[0]; r[x + 3] = b[1]; r += w;
    r[x] = a[2]; r[x + 1] = a[3]; r[x + 2] = b[2]; r[x + 3] = b[3]; r += w;
    r[x] = c[0]; r[x + 1] = c[1]; r[x + 2] = e[0]; r[x + 3] = e[1]; r += w;
    r[x] = c[2]; r[x + 1] = c[3]; r[x + 2] = e[2]; r[x + 3] = e[3];
}
static void put_v1(uint32_t* fb, int w, int h, int x, int y, const uint32_t* p) {
    uint32_t a[4] = { p[0], p[0], p[0], p[0] }, b[4] = { p[1], p[1], p[1], p[1] };
    uint32_t c[4] = { p[2], p[2], p[2], p[2] }, e[4] = { p[3], p[3], p[3], p[3] };
    put_block(fb, w, h, x, y, a, b, c, e);
}

/* Chunk ids: 0x30 mixes v1 and v4 by flag bits, 0x31 also has per-block skip
 * bits, 0x32 is all v1. */
static void read_vectors(const cvid_strip* s, int id, const uint8_t* d, const uint8_t* end,
                         int x1, int y1, int x2, int y2, uint32_t* fb, int w, int h) {
    uint32_t flag = 0, mask = 0;
    for (int y = y1; y < y2; y += 4)
        for (int x = x1; x < x2; x += 4) {
            if ((id & 1) && !(mask >>= 1)) {
                if (d + 4 > end) return;
                flag = rb32(d); d += 4; mask = 0x80000000u;
            }
            if ((id & 1) && !(flag & mask)) continue;               /* keep the old pixels */
            if (!(id & 2) && !(mask >>= 1)) {
                if (d + 4 > end) return;
                flag = rb32(d); d += 4; mask = 0x80000000u;
            }
            if ((id & 2) || !(flag & mask)) {
                if (d >= end) return;
                put_v1(fb, w, h, x, y, s->v1[*d++]);
            } else {
                if (d + 4 > end) return;
                put_block(fb, w, h, x, y, s->v4[d[0]], s->v4[d[1]], s->v4[d[2]], s->v4[d[3]]);
                d += 4;
            }
        }
}

void cvid_decode(cvid_ctx* c, const uint8_t* d, size_t size, uint32_t* fb, int w, int h) {
    const uint8_t* end = d + size;
    if (size < 10) return;                                         /* a repeated frame */
    int flags = d[0], strips = (int)rb16(d + 8), y0 = 0;
    d += 10;
    for (int i = 0; i < strips && i < CVID_MAX_STRIPS && d + 12 <= end; i++) {
        cvid_strip* s = &c->strip[i];
        int y1 = (int)rb16(d + 4), x1 = (int)rb16(d + 6), y2 = (int)rb16(d + 8), x2 = (int)rb16(d + 10);
        if (!y1) { y1 = y0; y2 += y0; }                            /* y1 of 0 = below the previous strip */
        int len = (int)rb24(d + 1) - 12;
        d += 12;
        if (len < 0) return;
        const uint8_t* se = d + len > end ? end : d + len;
        if (i > 0 && !(flags & 1)) memcpy(s, &c->strip[i - 1], sizeof(*s));
        const uint8_t* p = d;
        while (p + 4 <= se) {
            int id = p[0], n = (int)rb24(p + 1) - 4;
            p += 4;
            const uint8_t* ce = n < 0 || p + n > se ? se : p + n;
            if (id >= 0x20 && id <= 0x27) read_codebook(id & 2 ? s->v1 : s->v4, id, p, ce);
            else if (id >= 0x30 && id <= 0x32) { read_vectors(s, id, p, ce, x1, y1, x2, y2, fb, w, h); break; }
            p = ce;
        }
        d = se;
        y0 = y2;
    }
}
