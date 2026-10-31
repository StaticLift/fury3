/* The GPU renderer (gpu_gl.c) and the polygon capture that feeds it (gpu_capture.c). */
#ifndef FURY3_GPU_GL_H
#define FURY3_GPU_GL_H
#include <stdint.h>

typedef struct { int32_t x, y, z, u, v, q, sx, sy; } gl_cap_vert;
/* mode: 0 perspective, depth light (0x403C83); 1 the same, 64x64 wrap (0x40509E);
 * 2 perspective, flat light; 3 affine, flat light, depth-tested (models);
 * 4 solid colour, depth-tested; 5 palette index blended across the polygon, carried in U
 * (0x4069A0: lasers). row: into the combined shade table.
 * colour: the solid colour, or for textured modes 1 = texel 0 is transparent. */
typedef struct { uint32_t first, count, span, tex, flat, colour, page, mode, row, depth, layer; } gl_cap_poly;

int  gl_init(void);
void gl_log(const char* msg);
void gl_begin(int w, int h);
void gl_view(int x, int y, int w, int h);
void gl_tables(const uint8_t rgb[768], const uint8_t* flat, const uint8_t* depth);
void gl_depth_layers(const gl_cap_vert* v, const gl_cap_poly* p, uint32_t np);
void gl_polys(const gl_cap_vert* v, const gl_cap_poly* p, uint32_t np, const uint8_t* (*mem)(uint32_t va));
void gl_layer(const uint8_t* idx, int w, int h, int transparent);
void gl_rgba(const uint32_t* argb, int w, int h, int blend);
int  gl_rgba_holds(const uint32_t* argb);
void gl_save_bmp(const char* path);

/* gpu_capture.c */
void host_gpu_enable(void);
void host_gpu_frame(const gl_cap_vert** v, const gl_cap_poly** p, uint32_t* np, const uint8_t** under);
void host_gpu_clear(void);
void host_gpu_radar_marks(const uint8_t* frame, uint8_t* marks);
#endif
