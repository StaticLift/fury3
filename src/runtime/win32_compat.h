/*
 * The few Win32 names the portable parts of the runtime use when <windows.h> is
 * not available. The game itself only ever sees these as numbers (window
 * messages, virtual-key codes, palette entries), so they are spelled out here.
 */
#ifndef FURY3_WIN32_COMPAT_H
#define FURY3_WIN32_COMPAT_H
#ifndef _WIN32
#include <stdint.h>

typedef void* HWND;
typedef void* HDC;
typedef struct { uint8_t rgbBlue, rgbGreen, rgbRed, rgbReserved; } RGBQUAD;

#define WM_PAINT      0x000F
#define WM_CLOSE      0x0010
#define WM_DESTROY    0x0002
#define WM_KEYDOWN    0x0100
#define WM_KEYUP      0x0101
#define WM_SYSKEYDOWN 0x0104
#define WM_COMMAND    0x0111

#define VK_BACK       0x08
#define VK_TAB        0x09
#define VK_RETURN     0x0D
#define VK_SHIFT      0x10
#define VK_CONTROL    0x11
#define VK_MENU       0x12
#define VK_ESCAPE     0x1B
#define VK_SPACE      0x20
#define VK_LEFT       0x25
#define VK_UP         0x26
#define VK_RIGHT      0x27
#define VK_DOWN       0x28
#define VK_F1         0x70
#define VK_F2         0x71
#define VK_F3         0x72
#define VK_F4         0x73
#define VK_F5         0x74
#define VK_F6         0x75
#define VK_OEM_COMMA  0xBC
#define VK_OEM_PERIOD 0xBE

#endif
#endif
