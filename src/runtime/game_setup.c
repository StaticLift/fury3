/*
 * Finding the player's Fury3 files.
 *
 * The port ships no game data. The player supplies the FURY3 folder from their
 * own Fury3 CD (Microsoft, 1995): either the folder itself, copied to
 * <home>FURY3/, or a disc image (.iso, or the .bin of a .bin/.cue rip) placed in
 * <home>, from which the FURY3 folder is extracted on first launch. The
 * recompiled code is a translation of one specific FURY3.EXE, so that file is
 * checked by SHA-1; the two POD archives are checked by size.
 *
 * Required (from the CD's FURY3 folder):
 *   FURY3.EXE           645,120 bytes  SHA-1 64C42DD37CEDF97CB9F7434C0386896F8F437A93
 *   SYSTEM/FURY3.POD 44,453,636 bytes
 *   SYSTEM/STARTUP.POD 4,194,399 bytes
 *   SYSTEM/FURY3.INI, SYSTEM/FURY3.INF
 * Optional: SYSTEM/STORY/*.AVI (the cinematics).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "host_os.h"

void host_message(const char* title, const char* text, int percent, int wait);

static const char EXE_SHA1[] = "64c42dd37cedf97cb9f7434c0386896f8f437a93";
static const struct { const char* rel; uint64_t size; } REQUIRED[] = {
    { "FURY3.EXE",          645120 },
    { "SYSTEM/FURY3.POD",   44453636 },
    { "SYSTEM/STARTUP.POD", 4194399 },
    { "SYSTEM/FURY3.INI",   0 },
    { "SYSTEM/FURY3.INF",   0 },
};
#define N_REQUIRED (int)(sizeof(REQUIRED) / sizeof(REQUIRED[0]))

/* ---- SHA-1 ---- */
typedef struct { uint32_t h[5]; uint64_t len; uint8_t buf[64]; uint32_t n; } sha1_ctx;
static uint32_t rol(uint32_t x, int k) { return (x << k) | (x >> (32 - k)); }
static void sha1_block(sha1_ctx* c, const uint8_t* p) {
    uint32_t w[80], a = c->h[0], b = c->h[1], d = c->h[3], e = c->h[4], cc = c->h[2];
    for (int i = 0; i < 16; i++) w[i] = (uint32_t)p[4*i] << 24 | (uint32_t)p[4*i+1] << 16 | (uint32_t)p[4*i+2] << 8 | p[4*i+3];
    for (int i = 16; i < 80; i++) w[i] = rol(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);
    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20)      { f = (b & cc) | (~b & d);          k = 0x5A827999; }
        else if (i < 40) { f = b ^ cc ^ d;                   k = 0x6ED9EBA1; }
        else if (i < 60) { f = (b & cc) | (b & d) | (cc & d); k = 0x8F1BBCDC; }
        else             { f = b ^ cc ^ d;                   k = 0xCA62C1D6; }
        uint32_t t = rol(a, 5) + f + e + k + w[i];
        e = d; d = cc; cc = rol(b, 30); b = a; a = t;
    }
    c->h[0] += a; c->h[1] += b; c->h[2] += cc; c->h[3] += d; c->h[4] += e;
}
static void sha1_init(sha1_ctx* c) {
    static const uint32_t iv[5] = { 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0 };
    memcpy(c->h, iv, sizeof(iv)); c->len = 0; c->n = 0;
}
static void sha1_update(sha1_ctx* c, const uint8_t* p, size_t n) {
    c->len += n;
    while (n) {
        uint32_t take = 64 - c->n; if (take > n) take = (uint32_t)n;
        memcpy(c->buf + c->n, p, take); c->n += take; p += take; n -= take;
        if (c->n == 64) { sha1_block(c, c->buf); c->n = 0; }
    }
}
static void sha1_hex(sha1_ctx* c, char out[41]) {
    uint64_t bits = c->len * 8; uint8_t pad = 0x80, z = 0, l[8];
    sha1_update(c, &pad, 1);
    while (c->n != 56) sha1_update(c, &z, 1);
    for (int i = 0; i < 8; i++) l[i] = (uint8_t)(bits >> (56 - 8 * i));
    sha1_update(c, l, 8);
    for (int i = 0; i < 5; i++) sprintf(out + 8 * i, "%08x", c->h[i]);
}
static int file_sha1(const char* path, char out[41]) {
    FILE* f = fopen(path, "rb"); if (!f) return 0;
    sha1_ctx c; sha1_init(&c);
    uint8_t buf[16384]; size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) sha1_update(&c, buf, n);
    fclose(f); sha1_hex(&c, out); return 1;
}

/* ---- checking a candidate folder ---- */
static void join(char* out, size_t outsz, const char* a, const char* b) {
    snprintf(out, outsz, "%s%s%s", a, (*a && a[strlen(a) - 1] != '/') ? "/" : "", b);
    os_resolve_case(out, outsz);
}
/* 1 = complete and verified; 0 = absent; -1 = present but wrong (message set). */
static int check_folder(const char* root, char* why, size_t whysz) {
    char p[512]; uint64_t size; int dir, found = 0;
    for (int i = 0; i < N_REQUIRED; i++) {
        join(p, sizeof(p), root, REQUIRED[i].rel);
        if (!os_file_info(p, &size, &dir) || dir) {
            snprintf(why, whysz, "Missing %s", REQUIRED[i].rel);
            continue;
        }
        found++;
        if (REQUIRED[i].size && size != REQUIRED[i].size) {
            snprintf(why, whysz, "%s is %llu bytes, expected %llu. This is not the file from the Fury3 CD.",
                     REQUIRED[i].rel, (unsigned long long)size, (unsigned long long)REQUIRED[i].size);
            return -1;
        }
    }
    if (found == 0) return 0;
    if (found < N_REQUIRED) return -1;
    char sha[41];
    join(p, sizeof(p), root, "FURY3.EXE");
    if (!file_sha1(p, sha) || strcmp(sha, EXE_SHA1) != 0) {
        snprintf(why, whysz, "FURY3.EXE does not match the Fury3 CD version this port was built from (SHA-1 %s).", sha);
        return -1;
    }
    return 1;
}

/* ---- ISO 9660 extraction from a disc image ---- */
typedef struct { FILE* f; long sector_size, data_offset; uint64_t done, total; } disc;
static int disc_read(disc* d, uint32_t lba, uint8_t out[2048]) {
    if (fseek(d->f, (long)lba * d->sector_size + d->data_offset, SEEK_SET) != 0) return 0;
    return fread(out, 1, 2048, d->f) == 2048;
}
static int disc_open(disc* d, const char* path) {
    static const long layouts[][2] = { { 2048, 0 }, { 2352, 16 }, { 2352, 24 } };   /* ISO, raw MODE1, raw MODE2 */
    uint8_t s[2048];
    d->f = fopen(path, "rb");
    if (!d->f) return 0;
    for (int i = 0; i < 3; i++) {
        d->sector_size = layouts[i][0]; d->data_offset = layouts[i][1];
        if (disc_read(d, 16, s) && s[0] == 1 && !memcmp(s + 1, "CD001", 5)) return 1;
    }
    fclose(d->f); d->f = NULL; return 0;
}
static uint32_t le32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

/* Calls fn for each entry of the directory at (lba, size); stops when fn returns nonzero. */
typedef int (*dir_fn)(disc* d, const char* name, int is_dir, uint32_t lba, uint32_t size, void* ctx);
static int disc_walk(disc* d, uint32_t lba, uint32_t size, dir_fn fn, void* ctx) {
    uint8_t s[2048];
    for (uint32_t off = 0; off < size; off += 2048) {
        if (!disc_read(d, lba + off / 2048, s)) return -1;
        for (uint32_t i = 0; i < 2048 && s[i]; i += s[i]) {
            const uint8_t* r = s + i;
            int nl = r[32];
            if (nl == 1 && (r[33] == 0 || r[33] == 1)) continue;     /* "." and ".." */
            char name[64]; int k = 0;
            for (; k < nl && k < 63 && r[33 + k] != ';'; k++) name[k] = (char)r[33 + k];
            name[k] = 0;
            if (k && name[k - 1] == '.') name[k - 1] = 0;             /* "README." */
            int rc = fn(d, name, (r[25] & 2) != 0, le32(r + 2), le32(r + 10), ctx);
            if (rc) return rc;
        }
    }
    return 0;
}
static int sum_sizes(disc* d, const char* name, int is_dir, uint32_t lba, uint32_t size, void* ctx) {
    (void)name;
    if (is_dir) return disc_walk(d, lba, size, sum_sizes, ctx) < 0 ? -1 : 0;
    *(uint64_t*)ctx += size; return 0;
}
static int extract_entry(disc* d, const char* name, int is_dir, uint32_t lba, uint32_t size, void* ctx) {
    const char* dir = (const char*)ctx;
    char path[512]; snprintf(path, sizeof(path), "%s/%s", dir, name);
    if (is_dir) {
        if (!os_make_dir(path)) return -1;
        return disc_walk(d, lba, size, extract_entry, path) < 0 ? -1 : 0;
    }
    FILE* o = fopen(path, "wb"); if (!o) return -1;
    uint8_t s[2048];
    for (uint32_t off = 0; off < size; off += 2048) {
        if (!disc_read(d, lba + off / 2048, s)) { fclose(o); return -1; }
        uint32_t n = size - off < 2048 ? size - off : 2048;
        if (fwrite(s, 1, n, o) != n) { fclose(o); return -1; }
        d->done += n;
        if ((d->done & 0xFFFFF) < n) {          /* about every MB */
            char t[96]; snprintf(t, sizeof(t), "Copying %s", name);
            host_message("Setting up Fury3 from your disc image", t, (int)(d->done * 100 / (d->total ? d->total : 1)), 0);
        }
    }
    fclose(o);
    return 0;
}
typedef struct { uint32_t lba, size; int found; } find_ctx;
static int find_fury3(disc* d, const char* name, int is_dir, uint32_t lba, uint32_t size, void* ctx) {
    (void)d;
    find_ctx* f = (find_ctx*)ctx;
    if (is_dir && !strcmp(name, "FURY3")) { f->lba = lba; f->size = size; f->found = 1; return 1; }
    return 0;
}
/* Extracts the image's FURY3 folder to <home>FURY3. 1 on success, 0 = not a Fury3 disc, -1 = error. */
static int extract_image(const char* image, const char* home) {
    disc d; uint8_t pvd[2048];
    if (!disc_open(&d, image)) return 0;
    if (!disc_read(&d, 16, pvd)) { fclose(d.f); return 0; }
    const uint8_t* root = pvd + 156;
    find_ctx fc = { 0, 0, 0 };
    disc_walk(&d, le32(root + 2), le32(root + 10), find_fury3, &fc);
    if (!fc.found) { fclose(d.f); return 0; }
    d.done = 0; d.total = 0;
    disc_walk(&d, fc.lba, fc.size, sum_sizes, &d.total);
    char dest[512]; snprintf(dest, sizeof(dest), "%sFURY3", home);
    int rc = os_make_dir(dest) ? disc_walk(&d, fc.lba, fc.size, extract_entry, dest) : -1;
    fclose(d.f);
    return rc == 0 ? 1 : -1;
}
typedef struct { const char* home; char found[8][256]; int n; } image_scan;
static void scan_images(const char* name, int is_dir, uint64_t size, void* ctx) {
    image_scan* s = (image_scan*)ctx;
    size_t n = strlen(name);
    if (is_dir || n < 5 || s->n >= 8 || size < 50u * 1024 * 1024) return;
    const char* ext = name + n - 4;
    char e[5]; for (int i = 0; i < 4; i++) e[i] = (char)tolower((unsigned char)ext[i]); e[4] = 0;
    if (!strcmp(e, ".iso") || !strcmp(e, ".bin") || !strcmp(e, ".img"))
        snprintf(s->found[s->n++], sizeof(s->found[0]), "%s%s", s->home, name);
}

static const char HOW_TO[] =
    "Fury3 needs the game files from your own Fury3 CD (Microsoft, 1995).\n"
    "Either copy the FURY3 folder from the CD to %s\n"
    "or put your disc image (.iso, or the .bin of a .bin/.cue) in %s\n"
    "and start the game again.";

int game_setup(const char* home, char* root, size_t rootsz, char* exe, size_t exesz) {
    static const char* candidates[] = { "FURY3" };
    char why[256] = "", msg[1024];
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < (int)(sizeof(candidates) / sizeof(candidates[0])); i++) {
            char r[512]; join(r, sizeof(r), home, candidates[i]);
            int ok = check_folder(r, why, sizeof(why));
            if (ok == 1) {
                snprintf(root, rootsz, "%s", r);
                join(exe, exesz, r, "FURY3.EXE");
                char data[512]; join(data, sizeof(data), r, "DATA");
                os_make_dir(data);                         /* saved games go here */
                return 1;
            }
            if (ok < 0) {
                snprintf(msg, sizeof(msg), "%s\n\nFolder: %s", why, r);
                host_message("Fury3 game files problem", msg, -1, 1);
                return 0;
            }
        }
        if (pass) break;
        image_scan s; s.home = home; s.n = 0;
        os_list_dir(home, scan_images, &s);
        int extracted = 0;
        for (int i = 0; i < s.n && !extracted; i++) {
            host_message("Setting up Fury3 from your disc image", s.found[i], 0, 0);
            int rc = extract_image(s.found[i], home);
            if (rc < 0) {
                snprintf(msg, sizeof(msg), "Could not copy the game files out of\n%s\n(is the memory card full?)", s.found[i]);
                host_message("Fury3 setup failed", msg, -1, 1);
                return 0;
            }
            extracted = rc == 1;
        }
        if (!extracted) break;
    }
    snprintf(msg, sizeof(msg), HOW_TO, home, home);
    host_message("Fury3 game files not found", msg, -1, 1);
    return 0;
}
