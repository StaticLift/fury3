/*
 * Polygon capture: the first stage of a GPU renderer.
 *
 * Every textured polygon goes through sub_00424530(verts, count), reached only
 * through the thunk at 0x401CCB. A vertex is 0x20 bytes: +0 x, +4 y, +8 z (view
 * space), +0xC u, +0x10 v, +0x18 sx, +0x1C sy (16.16 screen; sx == -1 until
 * projected, which the routine does). The texture is [0x4EBCB4]; the span routine
 * for the polygon's material is [0x4EBD28].
 *
 * FURY3_GPUTRACE=1 records each frame's polygons, prints per-frame statistics, and
 * with FURY3_SNAP writes a wireframe made only from the capture beside each snapshot.
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
#include <string.h>
#include "gpu_gl.h"

extern void sub_00424530(void);
const char* host_env(const char* name);

#define MAX_POLYS 16384
#define MAX_VERTS 65536
typedef gl_cap_vert cap_vert;
typedef gl_cap_poly cap_poly;
static cap_vert s_v[MAX_VERTS];
static cap_poly s_p[MAX_POLYS];
static uint32_t s_nv, s_np, s_frames;
static int s_on = -1, s_gpu = 0;
static uint8_t s_under[64000];
static int s_have_under;

void host_gpu_enable(void) { s_gpu = 1; }
void host_gpu_frame(const gl_cap_vert** v, const gl_cap_poly** p, uint32_t* np, const uint8_t** under) {
    *v = s_v; *p = s_p; *np = s_np; *under = s_have_under ? s_under : NULL;
}
static uint32_t s_rt, s_layer;
/* The radar globe is the frame's last polygons, in the corner x >= 262, y <= 48, over the
 * HUD panel; the blips go on over the globe. Both panel and blips end up in the 2D layer,
 * so the corner is kept from just before the globe, and what changes after it is blips. */
#define RADAR_X 262
#define RADAR_Y 49
static uint8_t s_radar[RADAR_Y][320 - RADAR_X];
static int s_radar_run;
static void radar_note(uint32_t verts, uint32_t n) {
    for (uint32_t i = 0; i < n; i++)
        if ((int32_t)MEM32(verts + i * 0x20 + 0x18) < (RADAR_X << 16) || (int32_t)MEM32(verts + i * 0x20 + 0x1C) > ((RADAR_Y - 1) << 16)) { s_radar_run = 0; return; }
    if (s_radar_run || !s_rt) return;
    s_radar_run = 1;
    for (int y = 0; y < RADAR_Y; y++)
        for (int x = 0; x < 320 - RADAR_X; x++) s_radar[y][x] = MEM8(s_rt + y * 320 + RADAR_X + x);
}
void host_gpu_radar_marks(const uint8_t* frame, uint8_t* marks) {
    memset(marks, 0, 64000);
    for (int y = 0; y < RADAR_Y; y++)
        for (int x = 0; x < 320 - RADAR_X; x++) {
            uint8_t c = frame[y * 320 + RADAR_X + x];
            if (c != s_radar[y][x]) marks[y * 320 + RADAR_X + x] = c;
        }
}
/* The game can draw into its frame once and rely on it staying (the briefing text is
 * typed a character at a time), so the buffer gets back what take_under() took. */
void host_gpu_clear(void) {
    if (s_have_under)
        for (int i = 0; i < 64000; i++) if (!MEM8(s_rt + i)) MEM8(s_rt + i) = s_under[i];
    s_np = 0; s_nv = 0; s_have_under = 0; s_layer = 0; s_radar_run = 0;
}

/* What the game drew before its first polygon (the horizon haze) goes under the GPU's
 * polygons. The buffer is cleared so that what remains at present is only what was
 * drawn over them. The render target is row 0 of the rasterizer's row table. */
static void take_under(void) {
    uint32_t rt = MEM32(0x0047BB70u);
    if (!rt) return;
    for (int i = 0; i < 64000; i++) s_under[i] = MEM8(rt + i);
    for (int i = 0; i < 64000; i++) MEM8(rt + i) = 0;
    s_rt = rt; s_have_under = 1;
}

/* Which rule the span routine follows, and its row in the combined shade table. */
static void classify(cap_poly* p) {
    p->page = p->tex & 0xFFFF0000u; p->depth = 0;
    p->row = p->flat >> 8 & 15;
    if (p->span == 0x004036CFu || p->span == 0x00404420u || p->span == 0x00405E7Au || p->span == 0x00405E8Eu)
        p->colour = 1;                          /* these skip texel 0: colour-key transparency */
    else if (p->span != 0x00406DE2u && p->span != 0x00406E40u)
        p->colour = 0;
    switch (p->span) {
    case 0x00403C83u: case 0x00404420u: case 0x00404C41u: p->mode = 0; break;
    case 0x0040509Eu: case 0x004058E4u: p->mode = 1; break;
    case 0x00405EA2u: case 0x0040629Fu: p->mode = 3; p->depth = 1; break;
    case 0x00406DE2u: case 0x00406E40u: p->mode = 4; p->depth = 1; p->page = 0; p->row = 0; break;
    case 0x00405E7Au: case 0x00405E8Eu: p->mode = 2; p->row = 0; break;
    case 0x004069A0u: p->mode = 5; p->page = 0; p->row = 0; break;
    default: p->mode = 2; break;
    }
}

/* The thunk at 0x4013F7 clears the depth buffer in a model's screen rectangle before the
 * model draws, so depth only sorts a model against itself: the game's draw order sorts
 * models against each other, and their depths need not compare (the player's ship has its
 * own camera). Each clear starts a new layer in front of everything drawn so far. */
void sub_0044A470(void);
void sub_004013F7(void) { s_layer++; sub_0044A470(); }

/* The thunk at 0x401974 draws a model. A mod model can hold a cheap face list, for far away
 * and for the shadow pass (which only marks coverage), marked by a chunk after its first 0x17:
 * 0x1B, 0, 0, 'LOD1', offset of the list, distance (tools/mod/glb2fury.py --lod). 0x1B is
 * skipped; to use the list the chunk becomes 0x0B, a call to it, followed by an end marker.
 * Sharing the vertices keeps it in step with animation and the moving wings. */
void sub_00420490(void);
void sub_00401974(void) {
    static int off = -1;
    uint32_t m = MEM32(g_esp + 4);
    if (off < 0) off = host_env("FURY3_NOLOD") != NULL;
    if (m && !off && MEM32(m) == 0x14 && MEM32(m + 8) == 2) {
        uint32_t c = m + 20 + MEM32(m + 16) * 12 + 12;
        if (MEM32(c - 12) == 0x17 && MEM32(c + 12) == 0x31444F4Cu) { /* "LOD1" */
            int64_t x = (int32_t)MEM32(0x004EBBD8u), y = (int32_t)MEM32(0x004EBBDCu), z = (int32_t)MEM32(0x004EBBE0u);
            int64_t d = (int32_t)MEM32(c + 20);
            int far = MEM32(0x0053E7F0u) || x * x + y * y + z * z > d * d;
            MEM32(c) = far ? 0x0Bu : 0x1Bu;
            MEM32(c + 4) = far ? MEM32(c + 16) : 0;
        }
    }
    sub_00420490();
}

/* the thunk at 0x401CCB, stack as the game called it */
void sub_00401CCB(void) {
    uint32_t verts = MEM32(g_esp + 4), n = MEM32(g_esp + 8);
    uint32_t span = MEM32(0x004EBD28u), tex = MEM32(0x004EBCB4u), flat = MEM32(0x004EBCC0u);
    /* 0x406FDA only marks a coverage mask, which the game reads back: leave it to the game */
    if (s_gpu && tex && span != 0x00406FDAu && span != 0x00402EF0u && n >= 3 && s_np < MAX_POLYS && s_nv + n <= MAX_VERTS) {
        if (!s_np) take_under();
        radar_note(verts, n);
        MEM32(0x00502420u)++;
        for (uint32_t i = 0, a = verts; i < n; i++, a += 0x20)
            if (MEM32(a + 0x18) == 0xFFFFFFFFu) {     /* project, as the routine would */
                int32_t z = (int32_t)MEM32(a + 8);
                if (z) {
                    MEM32(a + 0x18) = (uint32_t)((int64_t)(int32_t)MEM32(a) * (int32_t)MEM32(0x004EBAE0u) / z) + MEM32(0x004EBB9Cu);
                    MEM32(a + 0x1C) = (uint32_t)((int64_t)(int32_t)MEM32(a + 4) * (int32_t)MEM32(0x004EBAE4u) / z) + MEM32(0x004EBBA0u);
                }
            }
        g_esp += 4;                                   /* the routine's ret */
    } else {
        sub_00424530();
        if (s_gpu) return;
    }
    if (s_on < 0) s_on = host_env("FURY3_GPUTRACE") != NULL;
    if ((!s_on && !s_gpu) || n < 3 || s_np >= MAX_POLYS || s_nv + n > MAX_VERTS) return;
    cap_poly* p = &s_p[s_np++];
    p->layer = s_layer; p->first = s_nv; p->count = n; p->span = span; p->tex = tex; p->flat = flat; p->colour = MEM32(0x004EB60Cu) & 255;
    classify(p);
    for (uint32_t i = 0; i < n; i++, verts += 0x20) {
        cap_vert* v = &s_v[s_nv++];
        v->x = (int32_t)MEM32(verts); v->y = (int32_t)MEM32(verts + 4); v->z = (int32_t)MEM32(verts + 8);
        v->u = (int32_t)MEM32(verts + 0xC); v->v = (int32_t)MEM32(verts + 0x10); v->q = (int32_t)MEM32(verts + 0x14);
        v->sx = (int32_t)MEM32(verts + 0x18); v->sy = (int32_t)MEM32(verts + 0x1C);
    }
}

static void line(uint8_t* px, int w, int h, int x0, int y0, int x1, int y1, const uint8_t* c) {
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y1 - y0 : y0 - y1;
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, e = dx - dy;
    for (int guard = 0; guard < 4000; guard++) {
        if (x0 >= 0 && y0 >= 0 && x0 < w && y0 < h) memcpy(px + 3 * ((h - 1 - y0) * w + x0), c, 3);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * e;
        if (e2 > -dy) { e -= dy; x0 += sx; }
        if (e2 < dx) { e += dx; y0 += sy; }
    }
}

/* Test painter: fills the captured polygons at 640x400 from the vertex data alone,
 * the way a GPU would. U, V and Z are linear across the screen; texel = U*256/Z; the
 * two shaded mappers look the texel up in the shade table by depth. */
void host_gpu_palette(uint8_t rgb[768]);
static uint32_t light_of(uint32_t span, uint32_t z) {
    uint32_t a;
    if (span == 0x0040509Eu) { a = z << 2; if (a > 0xFFFFFu) a = 0xFFFFFu; return (uint32_t)((int32_t)(a ^ 0xFFFFFu) >> 8) + 0x100u; }
    a = z > 0xFFFFu ? 0xFFFFu : z; a = (uint32_t)((int32_t)(a ^ 0xFFFFu) >> 3); if (a > 0xFFFu) a = 0xFFFu;
    return a + 0x100u;
}
void host_gpu_paint(const char* path) {
    enum { W = 640, H = 400 };
    static uint8_t px[W * H * 3];
    static float zb[W * H];
    uint8_t pal[768];
    if (s_on != 1) return;
    host_gpu_palette(pal);
    memset(px, 0, sizeof(px)); memset(zb, 0, sizeof(zb));
    for (uint32_t i = 0; i < s_np; i++) {
        const cap_poly* p = &s_p[i];
        int solid = p->span == 0x00406DE2u || p->span == 0x00406E40u;
        int model = p->span == 0x00405EA2u || p->span == 0x0040629Fu;     /* affine, depth-buffered */
        if (p->span == 0x00406FDAu) continue;                             /* coverage mask only */
        if (!p->tex && !solid) continue;
        uint32_t base = p->tex & 0xFFFF0000u, mask = p->span == 0x0040509Eu ? 0x3F3Fu : 0xFFFFu;
        uint32_t flat = (p->span == 0x00405E7Au || p->span == 0x00405E8Eu) ? 0 : p->flat & 0xFFFFFF00u;
        for (uint32_t k = 1; k + 1 < p->count; k++) {
            const cap_vert* a = &s_v[p->first], *b = &s_v[p->first + k], *c = &s_v[p->first + k + 1];
            double ax = a->sx / 32768.0, ay = a->sy / 32768.0, bx = b->sx / 32768.0, by = b->sy / 32768.0, cx = c->sx / 32768.0, cy = c->sy / 32768.0;
            double area = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
            if (area == 0) continue;
            double lo = ax < bx ? ax : bx, hi = ax > bx ? ax : bx, tp = ay < by ? ay : by, bt = ay > by ? ay : by;
            if (cx < lo) lo = cx; if (cx > hi) hi = cx; if (cy < tp) tp = cy; if (cy > bt) bt = cy;
            int x0 = lo < 0 ? 0 : (int)lo, x1 = hi >= W ? W - 1 : (int)hi, y0 = tp < 0 ? 0 : (int)tp, y1 = bt >= H ? H - 1 : (int)bt;
            for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) {
                double px0 = x + .5, py0 = y + .5;
                double w1 = ((px0 - ax) * (cy - ay) - (py0 - ay) * (cx - ax)) / area;
                double w2 = ((bx - ax) * (py0 - ay) - (by - ay) * (px0 - ax)) / area;
                double w0 = 1 - w1 - w2;
                if (w0 < 0 || w1 < 0 || w2 < 0) continue;
                double U = w0 * a->u + w1 * b->u + w2 * c->u, V = w0 * a->v + w1 * b->v + w2 * c->v, Q = w0 * a->q + w1 * b->q + w2 * c->q;
                if (Q <= 0) continue;
                uint32_t tu = (uint32_t)(int64_t)(model ? U / 65536.0 : U * 256.0 / Q) & 255, tv = (uint32_t)(int64_t)(model ? V / 65536.0 : V * 256.0 / Q) & 255;
                if (model) { if (Q < zb[y * W + x]) continue; zb[y * W + x] = (float)Q; }
                uint32_t idx = solid ? p->colour : MEM8(base + (((tv << 8) | tu) & mask)), z = (uint32_t)(int64_t)Q;
                if (p->span == 0x00403C83u || p->span == 0x0040509Eu) idx = MEM8(((light_of(p->span, z) & 0xFFFFFF00u) | idx) + 0x47D780u);
                else idx = MEM8((flat | idx) + 0x47C770u);
                uint8_t* o = px + 3 * ((H - 1 - y) * W + x);
                o[0] = pal[idx * 3 + 2]; o[1] = pal[idx * 3 + 1]; o[2] = pal[idx * 3];
            }
        }
    }
    FILE* f = fopen(path, "wb");
    if (!f) return;
    uint8_t hd[54] = { 'B', 'M' };
    uint32_t sz = 54 + sizeof(px), v;
    memcpy(hd + 2, &sz, 4); v = 54; memcpy(hd + 10, &v, 4); v = 40; memcpy(hd + 14, &v, 4);
    v = W; memcpy(hd + 18, &v, 4); v = H; memcpy(hd + 22, &v, 4);
    hd[26] = 1; hd[28] = 24;
    fwrite(hd, 1, 54, f); fwrite(px, 1, sizeof(px), f); fclose(f);
}

/* Writes the current frame's polygons as a 640x400 BMP. */
void host_gpu_dump(const char* path) {
    enum { W = 640, H = 400 };
    static uint8_t px[W * H * 3];
    if (s_on != 1) return;
    memset(px, 16, sizeof(px));
    for (uint32_t i = 0; i < s_np; i++) {
        const cap_poly* p = &s_p[i];
        uint32_t h = (p->span >> 2) * 2654435761u;
        uint8_t c[3] = { (uint8_t)(90 + (h >> 25)), (uint8_t)(90 + ((h >> 17) & 127)), (uint8_t)(90 + ((h >> 9) & 127)) };
        for (uint32_t k = 0; k < p->count; k++) {
            const cap_vert* a = &s_v[p->first + k], *b = &s_v[p->first + (k + 1) % p->count];
            line(px, W, H, a->sx >> 15, a->sy >> 15, b->sx >> 15, b->sy >> 15, c);
        }
    }
    FILE* f = fopen(path, "wb");
    if (!f) return;
    uint8_t hd[54] = { 'B', 'M' };
    uint32_t sz = 54 + sizeof(px), v;
    memcpy(hd + 2, &sz, 4); v = 54; memcpy(hd + 10, &v, 4); v = 40; memcpy(hd + 14, &v, 4);
    v = W; memcpy(hd + 18, &v, 4); v = H; memcpy(hd + 22, &v, 4);
    hd[26] = 1; hd[28] = 24;
    fwrite(hd, 1, 54, f); fwrite(px, 1, sizeof(px), f); fclose(f);
}

void host_gpu_end_frame(void) {
    if (s_on != 1) return;
    s_frames++;
    if (s_frames % 120 == 0) {
        uint32_t tex[64], ntex = 0, span[32], nsp = 0, tri = 0, maxn = 0;
        for (uint32_t i = 0; i < s_np; i++) {
            uint32_t k;
            for (k = 0; k < ntex && tex[k] != s_p[i].tex; k++) {}
            if (k == ntex && ntex < 64) tex[ntex++] = s_p[i].tex;
            for (k = 0; k < nsp && span[k] != s_p[i].span; k++) {}
            if (k == nsp && nsp < 32) span[nsp++] = s_p[i].span;
            tri += s_p[i].count - 2;
            if (s_p[i].count > maxn) maxn = s_p[i].count;
        }
        fprintf(stderr, "[gputrace] frame %u: %u polys, %u tris, %u verts, biggest %u-gon, %u textures, %u span kinds:",
                s_frames, s_np, tri, s_nv, maxn, ntex, nsp);
        for (uint32_t k = 0; k < nsp; k++) fprintf(stderr, " %06X", span[k]);
        fprintf(stderr, "\n");
    }
    if (!s_gpu) { s_np = 0; s_nv = 0; }
}
