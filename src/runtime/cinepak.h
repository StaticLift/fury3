/* Cinepak ("cvid") video decoder (cinepak.c). */
#ifndef FURY3_CINEPAK_H
#define FURY3_CINEPAK_H
#include <stdint.h>
#include <stddef.h>

#define CVID_MAX_STRIPS 32
typedef struct { uint32_t v1[256][4], v4[256][4]; } cvid_strip;
typedef struct { cvid_strip strip[CVID_MAX_STRIPS]; } cvid_ctx;

/* Decodes one frame over the previous one in fb (w*h ARGB pixels). */
void cvid_decode(cvid_ctx* c, const uint8_t* data, size_t size, uint32_t* fb, int w, int h);
#endif
