/*
 * The GPU renderer: draws the polygons gpu_capture.c records, through OpenGL on a PC
 * and vitaGL on the Vita (FURY3_GL builds).
 *
 * No 3D maths here. The game's U, V and Q are linear across the screen, so vertices go
 * in as screen positions with w = 1, the GPU interpolates them linearly, and the
 * fragment shader does what the span routines do: texel = U*256/Q, then the texture
 * byte through the game's shade table and palette. Colours therefore match the
 * software renderer exactly, at whatever resolution the window has.
 *
 * Everything that isn't a polygon stays software: the frame the game drew in its own
 * buffer goes under the polygons (what it drew first) and over them (the rest).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "gpu_gl.h"

#ifdef __vita__
#include <vitaGL.h>
#include <vitashark.h>
#include <psp2/kernel/sysmem.h>
#else
#include <SDL.h>
#include <SDL_opengl.h>
#include <SDL_opengl_glext.h>
#define GLFN(t, n) static t p_##n;
#define GLFNS \
    GLFN(PFNGLCREATESHADERPROC, glCreateShader) GLFN(PFNGLSHADERSOURCEPROC, glShaderSource) \
    GLFN(PFNGLCOMPILESHADERPROC, glCompileShader) GLFN(PFNGLGETSHADERIVPROC, glGetShaderiv) \
    GLFN(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog) GLFN(PFNGLCREATEPROGRAMPROC, glCreateProgram) \
    GLFN(PFNGLATTACHSHADERPROC, glAttachShader) GLFN(PFNGLBINDATTRIBLOCATIONPROC, glBindAttribLocation) \
    GLFN(PFNGLLINKPROGRAMPROC, glLinkProgram) GLFN(PFNGLGETPROGRAMIVPROC, glGetProgramiv) \
    GLFN(PFNGLUSEPROGRAMPROC, glUseProgram) GLFN(PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation) \
    GLFN(PFNGLUNIFORM1IPROC, glUniform1i) GLFN(PFNGLVERTEXATTRIBPOINTERPROC, glVertexAttribPointer) \
    GLFN(PFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray) GLFN(PFNGLACTIVETEXTUREPROC, glActiveTexture)
GLFNS
#undef GLFN
#define glCreateShader p_glCreateShader
#define glShaderSource p_glShaderSource
#define glCompileShader p_glCompileShader
#define glGetShaderiv p_glGetShaderiv
#define glGetShaderInfoLog p_glGetShaderInfoLog
#define glCreateProgram p_glCreateProgram
#define glAttachShader p_glAttachShader
#define glBindAttribLocation p_glBindAttribLocation
#define glLinkProgram p_glLinkProgram
#define glGetProgramiv p_glGetProgramiv
#define glUseProgram p_glUseProgram
#define glGetUniformLocation p_glGetUniformLocation
#define glUniform1i p_glUniform1i
#define glVertexAttribPointer p_glVertexAttribPointer
#define glEnableVertexAttribArray p_glEnableVertexAttribArray
#define glActiveTexture p_glActiveTexture
#endif

/* Shade rows: the flat table (0x47C770) comes first, then the depth table (0x47D780). */
#define FLAT_ROWS  16
#define DEPTH_ROWS 18
#define SHADE_ROWS (FLAT_ROWS + DEPTH_ROWS)

/* vitaGL runs every shader through its GLSL translator, so both platforms share these.
 * One declaration per line: the translator handles globals line by line. */
#ifdef __vita__
#define SHADER_HEAD ""
#else
#define SHADER_HEAD "#version 120\n"
#endif
static const char VS_POLY[] = SHADER_HEAD
    "attribute vec3 aPos;\n"
    "attribute vec3 aUvq;\n"
    "attribute vec4 aMat;\n"
    "varying vec3 vUvq;\n"
    "varying vec4 vMat;\n"
    "void main() {\n"
    "  gl_Position = vec4(aPos, 1.0);\n"
    "  vUvq = aUvq;\n"
    "  vMat = aMat;\n"
    "}\n";
static const char FS_POLY[] = SHADER_HEAD
    "varying vec3 vUvq;\n"
    "varying vec4 vMat;\n"
    "uniform sampler2D page;\n"
    "uniform sampler2D shade;\n"
    "uniform sampler2D pal;\n"
    "void main() {\n"
    "  float mode = floor(vMat.x + 0.5);\n"
    "  float idx;\n"
    "  float lightRow;\n"
    "  float a;\n"
    "  if (mode > 4.5) {\n"
    "    idx = floor(vUvq.x);\n"
    "    idx = idx - 256.0 * floor(idx / 256.0);\n"
    "  } else if (mode > 3.5) {\n"
    "    idx = floor(vMat.z + 0.5);\n"
    "  } else {\n"
    "    float m = (mode > 0.5 && mode < 1.5) ? 64.0 : 256.0;\n"
    "    vec2 t = floor(256.0 * vUvq.xy / vUvq.z);\n"
    "    t = t - m * floor(t / m);\n"
    "    idx = floor(texture2D(page, (t + 0.5) / 256.0).x * 255.0 + 0.5);\n"
    "    if (vMat.z > 0.5 && idx < 0.5) discard;\n"
    "  }\n"
    "  if (mode < 0.5) {\n"
    "    a = (65535.0 - min(vMat.w, 65535.0)) / 8.0;\n"
    "    lightRow = 16.0 + floor((min(a, 4095.0) + 256.0) / 256.0);\n"
    "  } else if (mode < 1.5) {\n"
    "    a = (1048575.0 - min(vMat.w * 4.0, 1048575.0)) / 256.0;\n"
    "    lightRow = 16.0 + floor((a + 256.0) / 256.0);\n"
    "  } else {\n"
    "    lightRow = floor(vMat.y + 0.5);\n"
    "  }\n"
    "  float s = floor(texture2D(shade, vec2((idx + 0.5) / 256.0, (lightRow + 0.5) / 34.0)).x * 255.0 + 0.5);\n"
    "  gl_FragColor = vec4(texture2D(pal, vec2((s + 0.5) / 256.0, 0.5)).xyz, 1.0);\n"
    "}\n";
static const char VS_QUAD[] = SHADER_HEAD
    "attribute vec2 aPos;\n"
    "attribute vec2 aUv;\n"
    "varying vec2 vUv;\n"
    "void main() {\n"
    "  gl_Position = vec4(aPos, 0.0, 1.0);\n"
    "  vUv = aUv;\n"
    "}\n";
static const char FS_LAYER[] = SHADER_HEAD
    "varying vec2 vUv;\n"
    "uniform sampler2D page;\n"
    "uniform sampler2D pal;\n"
    "void main() {\n"
    "  float i = floor(texture2D(page, vUv).x * 255.0 + 0.5);\n"
    "  float alpha = i > 0.5 ? 1.0 : 0.0;\n"
    "  gl_FragColor = vec4(texture2D(pal, vec2((i + 0.5) / 256.0, 0.5)).xyz, alpha);\n"
    "}\n";
static const char FS_RGBA[] = SHADER_HEAD
    "varying vec2 vUv;\n"
    "uniform sampler2D page;\n"
    "void main() {\n"
    "  gl_FragColor = texture2D(page, vUv);\n"
    "}\n";

/* The Vita's stdio logs have been coming out empty, so this has its own file. */
void gl_log(const char* msg) {
    fprintf(stderr, "[gl] %s\n", msg);
#ifdef __vita__
    FILE* f = fopen("ux0:data/fury3/gpu_log.txt", "a");
    if (f) { fprintf(f, "%s\n", msg); fclose(f); }
#endif
}

static GLuint s_poly, s_layer, s_rgba_prog, s_texShade, s_texPal, s_texLayer, s_texRgba;
static int s_w, s_h, s_rgba_w, s_rgba_h;

#ifdef __vita__
static void shark_log(const char* msg, shark_log_level level, int line) {
    char b[400];
    snprintf(b, sizeof(b), "shark[%d] line %d: %s", (int)level, line, msg);
    gl_log(b);
}
/* Records what the compiler asks for: it reports a failed allocation only as a
 * "fatal internal error". */
static uint32_t spy_calls, spy_failed, spy_total, spy_largest;
static void* spy_malloc(size_t n) {
    void* p = malloc(n);
    spy_calls++; spy_total += (uint32_t)n;
    if (n > spy_largest) spy_largest = (uint32_t)n;
    if (!p || n >= 1024 * 1024) {
        char b[80];
        snprintf(b, sizeof(b), "compiler alloc %u bytes: %s", (unsigned)n, p ? "ok" : "FAILED");
        gl_log(b);
        if (!p) spy_failed++;
    }
    return p;
}
void gl_compiler_spy(void) { shark_set_allocators(spy_malloc, free); }
static void spy_report(const char* where) {
    char b[100];
    snprintf(b, sizeof(b), "%s: %u allocs, %u failed, %u KB total, largest %u KB", where,
             spy_calls, spy_failed, spy_total / 1024, spy_largest / 1024);
    gl_log(b);
}

static void log_mem(const char* where) {
    SceKernelFreeMemorySizeInfo m; char b[120];
    m.size = sizeof(m);
    sceKernelGetFreeMemorySize(&m);
    snprintf(b, sizeof(b), "%s: free user %u KB, cdram %u KB, phycont %u KB", where,
             m.size_user / 1024, m.size_cdram / 1024, m.size_phycont / 1024);
    gl_log(b);
}
/* Compiles a trivial program directly, to tell a broken compiler from rejected source. */
static int shark_probe(void) {
    char b[80];
    const char* src = "float4 main(float2 p : POSITION) : POSITION { return float4(p, 0.0, 1.0); }\n";
    uint32_t size = (uint32_t)strlen(src);     /* in: source length; out: program size */
    SceGxmProgram* p = shark_compile_shader(src, &size, SHARK_VERTEX_SHADER);
    snprintf(b, sizeof(b), "direct compiler test: %s, %u bytes", p ? "ok" : "FAILED", (unsigned)size);
    gl_log(b);
    shark_clear_output();
    return p != NULL;
}
#endif

static GLuint shader(GLenum kind, const char* src) {
    GLuint s = glCreateShader(kind); GLint ok = 0, len = (GLint)strlen(src);
    glShaderSource(s, 1, &src, &len);
    glCompileShader(s);
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    {
        char log[600]; GLsizei n = 0; log[0] = 0;
        glGetShaderInfoLog(s, sizeof(log), &n, log);
        if (n > 0) { gl_log("info log:"); gl_log(log); }
    }
    if (!ok) {
        char log[600]; GLsizei n = 0; log[0] = 0;
        glGetShaderInfoLog(s, sizeof(log), &n, log);
        gl_log("shader failed:"); gl_log(log);
        return 0;
    }
    return s;
}
static GLuint program(const char* vs, const char* fs, int quad) {
    gl_log("prepare vertex");
    GLuint v = shader(GL_VERTEX_SHADER, vs);
    gl_log("prepare fragment");
    GLuint f = shader(GL_FRAGMENT_SHADER, fs);
    if (!v || !f) return 0;
    gl_log("link (the real compile happens here)");
    GLuint p = glCreateProgram(); GLint ok = 0;
    glAttachShader(p, v); glAttachShader(p, f);
    glBindAttribLocation(p, 0, "aPos");
    glBindAttribLocation(p, 1, quad ? "aUv" : "aUvq");
    if (!quad) glBindAttribLocation(p, 2, "aMat");
    glLinkProgram(p);
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { gl_log("link failed"); return 0; }
    return p;
}
static GLuint make_tex(void) {
    GLuint t; glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    return t;
}

int gl_init(void) {
#ifndef __vita__
#define GLFN(t, n) p_##n = (t)SDL_GL_GetProcAddress(#n); if (!p_##n) return 0;
    GLFNS
#undef GLFN
#endif
#ifdef __vita__
    log_mem("after vglInit");
    shark_install_log_cb(shark_log);
    shark_set_warnings_level(SHARK_WARN_SILENT);
    shark_probe();
    spy_report("after direct test");
#endif
    s_poly = program(VS_POLY, FS_POLY, 0);
#ifdef __vita__
    spy_report("after first program");
#endif
    s_layer = program(VS_QUAD, FS_LAYER, 1);
    s_rgba_prog = program(VS_QUAD, FS_RGBA, 1);
    if (!s_poly || !s_layer || !s_rgba_prog) return 0;
    gl_log("programs ready");
    glUseProgram(s_poly);
    glUniform1i(glGetUniformLocation(s_poly, "page"), 0);
    glUniform1i(glGetUniformLocation(s_poly, "shade"), 1);
    glUniform1i(glGetUniformLocation(s_poly, "pal"), 2);
    glUseProgram(s_layer);
    glUniform1i(glGetUniformLocation(s_layer, "page"), 0);
    glUniform1i(glGetUniformLocation(s_layer, "pal"), 2);
    glUseProgram(s_rgba_prog);
    glUniform1i(glGetUniformLocation(s_rgba_prog, "page"), 0);
    s_texShade = make_tex(); s_texPal = make_tex(); s_texLayer = make_tex(); s_texRgba = make_tex();
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glDisable(GL_CULL_FACE);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
#ifndef __vita__
    glClearDepth(0.0);
#else
    glClearDepthf(0.0f);
#endif
    glDepthFunc(GL_GEQUAL);
    gl_log("renderer ready");
    return 1;
}

void gl_begin(int w, int h) {
    s_w = w; s_h = h;
    glViewport(0, 0, w, h);
    glClearColor(0, 0, 0, 1);
    glDepthMask(GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}
void gl_view(int x, int y, int w, int h) { glViewport(x, s_h - y - h, w, h); }

void gl_tables(const uint8_t rgb[768], const uint8_t* flat, const uint8_t* depth) {
    static uint32_t sum_pal, sum_shade; static int have;
    uint32_t a = 2166136261u, b = 2166136261u;
    for (int i = 0; i < 768; i++) a = (a ^ rgb[i]) * 16777619u;
    for (int i = 0; i < 256 * FLAT_ROWS; i += 7) b = (b ^ flat[i]) * 16777619u;
    for (int i = 0; i < 256 * DEPTH_ROWS; i += 7) b = (b ^ depth[i]) * 16777619u;
    if (!have || a != sum_pal) {
        uint8_t px[1024];
        for (int i = 0; i < 256; i++) { px[i * 4] = rgb[i * 3]; px[i * 4 + 1] = rgb[i * 3 + 1]; px[i * 4 + 2] = rgb[i * 3 + 2]; px[i * 4 + 3] = 255; }
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, s_texPal);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
        sum_pal = a;
    }
    if (!have || b != sum_shade) {
        static uint8_t px[256 * SHADE_ROWS];
        memcpy(px, flat, 256 * FLAT_ROWS);
        memcpy(px + 256 * FLAT_ROWS, depth, 256 * DEPTH_ROWS);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, s_texShade);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, 256, SHADE_ROWS, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, px);
        sum_shade = b;
    }
    have = 1;
    glActiveTexture(GL_TEXTURE0);
}

/* Texture pages: 256x256 bytes at a 64 KB boundary; the game rewrites them as its
 * texture cache turns over, so each is checked when first used in a frame. */
#define PAGES 48
static struct { uint32_t va, sum, used, seen; GLuint tex; } s_pages[PAGES];
static uint32_t s_frame;
static GLuint page_tex(uint32_t va, const uint8_t* mem) {
    int free_i = 0;
    for (int i = 0; i < PAGES; i++) {
        if (s_pages[i].va == va) { free_i = i; goto found; }
        if (s_pages[i].used < s_pages[free_i].used) free_i = i;
    }
    if (!s_pages[free_i].tex) s_pages[free_i].tex = make_tex();
    s_pages[free_i].va = va; s_pages[free_i].seen = 0; s_pages[free_i].sum = 0;
found:;
    if (s_pages[free_i].seen != s_frame) {
        uint32_t h = 2166136261u;
        for (int i = 0; i < 65536; i += 61) h = (h ^ mem[i]) * 16777619u;
        glBindTexture(GL_TEXTURE_2D, s_pages[free_i].tex);
        if (h != s_pages[free_i].sum) {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, 256, 256, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, mem);
            s_pages[free_i].sum = h;
        }
        s_pages[free_i].seen = s_frame;
    }
    s_pages[free_i].used = s_frame;
    return s_pages[free_i].tex;
}

typedef struct { float x, y, z, u, v, q, mode, row, colour, depth; } gl_vert;

/* GPU depth, larger in front: one slice per depth layer (gpu_capture.c), and each layer's
 * Q range fills its slice. The mapping is linear, so polygons stay flat. */
#define MAX_LAYERS 1024
static float s_qlo[MAX_LAYERS], s_qscale[MAX_LAYERS], s_slice;
static uint32_t s_nlayers;
void gl_depth_layers(const gl_cap_vert* v, const gl_cap_poly* p, uint32_t np) {
    static float hi[MAX_LAYERS];
    s_nlayers = 0;
    for (uint32_t i = 0; i < np; i++) {
        uint32_t l = p[i].layer < MAX_LAYERS ? p[i].layer : MAX_LAYERS - 1;
        while (s_nlayers <= l) { s_qlo[s_nlayers] = 2147483647.0f; hi[s_nlayers] = -2147483648.0f; s_nlayers++; }
        if (!p[i].depth) continue;
        for (uint32_t k = 0; k < p[i].count; k++) {
            float q = (float)v[p[i].first + k].q;
            if (q < s_qlo[l]) s_qlo[l] = q;
            if (q > hi[l]) hi[l] = q;
        }
    }
    s_slice = 2.0f / (float)(s_nlayers ? s_nlayers : 1);
    for (uint32_t l = 0; l < s_nlayers; l++)
        s_qscale[l] = hi[l] > s_qlo[l] ? 0.9f * s_slice / (hi[l] - s_qlo[l]) : 0.0f;
}

void gl_polys(const gl_cap_vert* v, const gl_cap_poly* p, uint32_t np, const uint8_t* (*mem)(uint32_t va)) {
    static gl_vert out[60000];
    s_frame++;
    glUseProgram(s_poly);
    glEnableVertexAttribArray(2);
    uint32_t i = 0;
    while (i < np) {
        uint32_t page = p[i].page; int depth = p[i].depth, n = 0;
        for (; i < np && p[i].page == page && p[i].depth == depth; i++) {
            const gl_cap_poly* q = &p[i];
            uint32_t l = q->layer < MAX_LAYERS ? q->layer : MAX_LAYERS - 1;
            float base = -1.0f + (float)l * s_slice + 0.05f * s_slice;
            for (uint32_t k = 1; k + 1 < q->count && n + 3 <= 60000; k++) {
                const gl_cap_vert* tri[3] = { &v[q->first], &v[q->first + k], &v[q->first + k + 1] };
                for (int c = 0; c < 3; c++) {
                    const gl_cap_vert* s = tri[c]; gl_vert* o = &out[n++];
                    float qv = (float)s->q;
                    o->x = (float)s->sx / 65536.0f / 160.0f - 1.0f;
                    o->y = 1.0f - (float)s->sy / 65536.0f / 100.0f;
                    /* the rest (the ground) isn't depth-tested, and outside the range would be clipped */
                    o->z = q->depth ? base + (qv - s_qlo[l]) * s_qscale[l] : 0.0f;
                    if (q->mode == 5) { o->u = (float)s->u / 65536.0f; o->v = 0.0f; o->q = 1.0f; }
                    else if (q->mode == 3) { o->u = (float)s->u / 65536.0f; o->v = (float)s->v / 65536.0f; o->q = 256.0f; }
                    else { o->u = (float)s->u; o->v = (float)s->v; o->q = qv; }
                    o->mode = (float)q->mode; o->row = (float)q->row; o->colour = (float)q->colour; o->depth = qv;
                }
            }
        }
        if (page) glBindTexture(GL_TEXTURE_2D, page_tex(page, mem(page)));
        if (depth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(gl_vert), &out[0].x);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(gl_vert), &out[0].u);
        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(gl_vert), &out[0].mode);
        glDrawArrays(GL_TRIANGLES, 0, n);
    }
    glDisable(GL_DEPTH_TEST);
}

static void quad(void) {
    static const float pos[8] = { -1, 1, 1, 1, -1, -1, 1, -1 }, uv[8] = { 0, 0, 1, 0, 0, 1, 1, 1 };
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, pos);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, uv);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void gl_layer(const uint8_t* idx, int w, int h, int transparent) {
    glUseProgram(s_layer);
    glBindTexture(GL_TEXTURE_2D, s_texLayer);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, w, h, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, idx);
    if (transparent) { glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); }
    quad();
    glDisable(GL_BLEND);
}

/* ARGB little-endian (BGRA bytes) on the PC; the Vita's vitaGL wants RGBA, so swap. */
/* argb NULL draws what was last uploaded; gl_rgba_holds says whether that was this image */
static const uint32_t* s_rgba_src;
int gl_rgba_holds(const uint32_t* argb) { return s_rgba_src == argb; }
void gl_rgba(const uint32_t* argb, int w, int h, int blend) {
    glUseProgram(s_rgba_prog);
    glBindTexture(GL_TEXTURE_2D, s_texRgba);
    if (argb) {
    s_rgba_src = argb;
#ifdef __vita__
    static uint32_t* tmp; static int cap;
    if (cap < w * h) { free(tmp); tmp = (uint32_t*)malloc((size_t)w * (size_t)h * 4); cap = w * h; }
    for (int i = 0; i < w * h; i++) { uint32_t c = argb[i]; tmp[i] = (c & 0xFF00FF00u) | (c & 0xFF) << 16 | (c >> 16 & 0xFF); }
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, tmp);
#else
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_BGRA, GL_UNSIGNED_BYTE, argb);
#endif
    }
    s_rgba_w = w; s_rgba_h = h;
    if (blend) { glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); }
    quad();
    glDisable(GL_BLEND);
}

#ifndef __vita__
/* Test screenshots. */
void gl_save_bmp(const char* path) {
    int n = s_w * s_h * 3;
    uint8_t* px = (uint8_t*)malloc((size_t)n);
    if (!px) return;
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, s_w, s_h, GL_BGR, GL_UNSIGNED_BYTE, px);
    FILE* f = fopen(path, "wb");
    if (f) {
        int stride = (s_w * 3 + 3) & ~3;
        uint8_t hd[54] = { 'B', 'M' }; uint32_t v = 54 + (uint32_t)stride * (uint32_t)s_h;
        memcpy(hd + 2, &v, 4); v = 54; memcpy(hd + 10, &v, 4); v = 40; memcpy(hd + 14, &v, 4);
        memcpy(hd + 18, &s_w, 4); memcpy(hd + 22, &s_h, 4); hd[26] = 1; hd[28] = 24;
        fwrite(hd, 1, 54, f);
        for (int y = 0; y < s_h; y++) { fwrite(px + y * s_w * 3, 1, (size_t)s_w * 3, f); fwrite("\0\0\0", 1, (size_t)(stride - s_w * 3), f); }
        fclose(f);
    }
    free(px);
}
#endif
