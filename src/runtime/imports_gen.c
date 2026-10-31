/* Fury3 Recompilation - import bridge layer - AUTO-GENERATED */
/* Regenerate: py -3 gen_imports.py */
#define RECOMP_GENERATED_CODE
#include "recomp_types.h"
#include "imports.h"

/* 160 imports across 5 DLLs */

/* GDI32.dll!DeleteObject  (1 args) */
static void imp_DeleteObject(void) { IMPORT_STUB("DeleteObject"); RET(0); STDRET(1); }
extern void imp_StretchBlt(void);  /* GDI32.dll!StretchBlt - hand-written in shims_impl.c */
/* GDI32.dll!SetStretchBltMode  (2 args) */
static void imp_SetStretchBltMode(void) { IMPORT_STUB("SetStretchBltMode"); RET(0); STDRET(2); }
extern void imp_BitBlt(void);  /* GDI32.dll!BitBlt - hand-written in shims_impl.c */
extern void imp_SetDIBColorTable(void);  /* GDI32.dll!SetDIBColorTable - hand-written in shims_impl.c */
extern void imp_CreatePalette(void);  /* GDI32.dll!CreatePalette - hand-written in shims_impl.c */
/* GDI32.dll!GetSystemPaletteEntries  (4 args) */
static void imp_GetSystemPaletteEntries(void) { IMPORT_STUB("GetSystemPaletteEntries"); RET(0); STDRET(4); }
extern void imp_CreateDIBSection(void);  /* GDI32.dll!CreateDIBSection - hand-written in shims_impl.c */
extern void imp_SelectObject(void);  /* GDI32.dll!SelectObject - hand-written in shims_impl.c */
extern void imp_CreateCompatibleDC(void);  /* GDI32.dll!CreateCompatibleDC - hand-written in shims_impl.c */
/* GDI32.dll!PatBlt  (6 args) */
static void imp_PatBlt(void) { IMPORT_STUB("PatBlt"); RET(0); STDRET(6); }
extern void imp_DeleteDC(void);  /* GDI32.dll!DeleteDC - hand-written in shims_impl.c */
/* GDI32.dll!DPtoLP  (3 args) */
static void imp_DPtoLP(void) { IMPORT_STUB("DPtoLP"); RET(0); STDRET(3); }
/* GDI32.dll!GetObjectA  (3 args) */
static void imp_GetObjectA(void) { IMPORT_STUB("GetObjectA"); RET(0); STDRET(3); }
/* GDI32.dll!SetMapMode  (2 args) */
static void imp_SetMapMode(void) { IMPORT_STUB("SetMapMode"); RET(0); STDRET(2); }
/* GDI32.dll!GetMapMode  (1 args) */
static void imp_GetMapMode(void) { IMPORT_STUB("GetMapMode"); RET(0); STDRET(1); }
/* GDI32.dll!RealizePalette  (1 args) */
static void imp_RealizePalette(void) { IMPORT_STUB("RealizePalette"); RET(1); STDRET(1); }
/* GDI32.dll!SelectPalette  (3 args) */
static void imp_SelectPalette(void) { IMPORT_STUB("SelectPalette"); RET(1); STDRET(3); }
/* GDI32.dll!GetDeviceCaps  (2 args) */
static void imp_GetDeviceCaps(void) { IMPORT_STUB("GetDeviceCaps"); RET(0); STDRET(2); }
/* GDI32.dll!GetStockObject  (1 args) */
static void imp_GetStockObject(void) { IMPORT_STUB("GetStockObject"); RET(1); STDRET(1); }
/* KERNEL32.dll!lstrcpyA  (2 args) */
static void imp_lstrcpyA(void) { IMPORT_STUB("lstrcpyA"); RET(0); STDRET(2); }
extern void imp_GlobalAlloc(void);  /* KERNEL32.dll!GlobalAlloc - hand-written in shims_impl.c */
extern void imp_GlobalLock(void);  /* KERNEL32.dll!GlobalLock - hand-written in shims_impl.c */
/* KERNEL32.dll!FindResourceA  (3 args) */
static void imp_FindResourceA(void) { IMPORT_STUB("FindResourceA"); RET(1); STDRET(3); }
/* KERNEL32.dll!lstrlenA  (1 args) */
static void imp_lstrlenA(void) { IMPORT_STUB("lstrlenA"); RET(0); STDRET(1); }
extern void imp_GlobalFree(void);  /* KERNEL32.dll!GlobalFree - hand-written in shims_impl.c */
extern void imp_WritePrivateProfileStringA(void);  /* KERNEL32.dll!WritePrivateProfileStringA - hand-written in shims_impl.c */
extern void imp_GetPrivateProfileStringA(void);  /* KERNEL32.dll!GetPrivateProfileStringA - hand-written in shims_impl.c */
extern void imp_GetPrivateProfileIntA(void);  /* KERNEL32.dll!GetPrivateProfileIntA - hand-written in shims_impl.c */
extern void imp_GetModuleHandleA(void);  /* KERNEL32.dll!GetModuleHandleA - hand-written in shims_impl.c */
extern void imp_GetCurrentDirectoryA(void);  /* KERNEL32.dll!GetCurrentDirectoryA - hand-written in shims_impl.c */
extern void imp_GetModuleFileNameA(void);  /* KERNEL32.dll!GetModuleFileNameA - hand-written in shims_impl.c */
/* KERNEL32.dll!SetCurrentDirectoryA  (1 args) */
static void imp_SetCurrentDirectoryA(void) { IMPORT_STUB("SetCurrentDirectoryA"); RET(0); STDRET(1); }
extern void imp_GetVersion(void);  /* KERNEL32.dll!GetVersion - hand-written in shims_impl.c */
extern void imp_GlobalMemoryStatus(void);  /* KERNEL32.dll!GlobalMemoryStatus - hand-written in shims_impl.c */
extern void imp_LoadLibraryA(void);  /* KERNEL32.dll!LoadLibraryA - hand-written in shims_impl.c */
extern void imp_GetProcAddress(void);  /* KERNEL32.dll!GetProcAddress - hand-written in shims_impl.c */
extern void imp_CreateThread(void);  /* KERNEL32.dll!CreateThread - hand-written in shims_impl.c */
extern void imp_SetThreadPriority(void);  /* KERNEL32.dll!SetThreadPriority - hand-written in shims_impl.c */
/* KERNEL32.dll!GetCurrentProcess  (0 args) */
static void imp_GetCurrentProcess(void) { IMPORT_STUB("GetCurrentProcess"); RET(1); STDRET(0); }
/* KERNEL32.dll!LoadModule  (2 args) */
static void imp_LoadModule(void) { IMPORT_STUB("LoadModule"); RET(0); STDRET(2); }
/* KERNEL32.dll!FreeLibrary  (1 args) */
static void imp_FreeLibrary(void) { IMPORT_STUB("FreeLibrary"); RET(0); STDRET(1); }
extern void imp_ExitProcess(void);  /* KERNEL32.dll!ExitProcess - hand-written in shims_impl.c */
/* KERNEL32.dll!LoadResource  (2 args) */
static void imp_LoadResource(void) { IMPORT_STUB("LoadResource"); RET(1); STDRET(2); }
/* KERNEL32.dll!LockResource  (1 args) */
static void imp_LockResource(void) { IMPORT_STUB("LockResource"); RET(1); STDRET(1); }
/* KERNEL32.dll!MultiByteToWideChar  (6 args) */
static void imp_MultiByteToWideChar(void) { IMPORT_STUB("MultiByteToWideChar"); RET(0); STDRET(6); }
extern void imp_GetFileType(void);  /* KERNEL32.dll!GetFileType - hand-written in shims_impl.c */
extern void imp_GetStdHandle(void);  /* KERNEL32.dll!GetStdHandle - hand-written in shims_impl.c */
extern void imp_VirtualAlloc(void);  /* KERNEL32.dll!VirtualAlloc - hand-written in shims_impl.c */
extern void imp_VirtualFree(void);  /* KERNEL32.dll!VirtualFree - hand-written in shims_impl.c */
extern void imp_ReadFile(void);  /* KERNEL32.dll!ReadFile - hand-written in shims_impl.c */
extern void imp_CloseHandle(void);  /* KERNEL32.dll!CloseHandle - hand-written in shims_impl.c */
extern void imp_GetCommandLineA(void);  /* KERNEL32.dll!GetCommandLineA - hand-written in shims_impl.c */
extern void imp_GetEnvironmentStrings(void);  /* KERNEL32.dll!GetEnvironmentStrings - hand-written in shims_impl.c */
extern void imp_GetStartupInfoA(void);  /* KERNEL32.dll!GetStartupInfoA - hand-written in shims_impl.c */
/* KERNEL32.dll!DeleteFileA  (1 args) */
static void imp_DeleteFileA(void) { IMPORT_STUB("DeleteFileA"); RET(0); STDRET(1); }
/* KERNEL32.dll!GetLastError  (0 args) */
static void imp_GetLastError(void) { IMPORT_STUB("GetLastError"); RET(0); STDRET(0); }
/* KERNEL32.dll!RtlUnwind  (4 args) */
static void imp_RtlUnwind(void) { IMPORT_STUB("RtlUnwind"); RET(0); STDRET(4); }
/* KERNEL32.dll!WideCharToMultiByte  (8 args) */
static void imp_WideCharToMultiByte(void) { IMPORT_STUB("WideCharToMultiByte"); RET(0); STDRET(8); }
extern void imp_WriteFile(void);  /* KERNEL32.dll!WriteFile - hand-written in shims_impl.c */
/* KERNEL32.dll!UnhandledExceptionFilter  (1 args) */
static void imp_UnhandledExceptionFilter(void) { IMPORT_STUB("UnhandledExceptionFilter"); RET(0); STDRET(1); }
extern void imp_GetACP(void);  /* KERNEL32.dll!GetACP - hand-written in shims_impl.c */
extern void imp_GetOEMCP(void);  /* KERNEL32.dll!GetOEMCP - hand-written in shims_impl.c */
extern void imp_GetCPInfo(void);  /* KERNEL32.dll!GetCPInfo - hand-written in shims_impl.c */
extern void imp_SetStdHandle(void);  /* KERNEL32.dll!SetStdHandle - hand-written in shims_impl.c */
/* KERNEL32.dll!FlushFileBuffers  (1 args) */
static void imp_FlushFileBuffers(void) { IMPORT_STUB("FlushFileBuffers"); RET(0); STDRET(1); }
extern void imp_CreateFileA(void);  /* KERNEL32.dll!CreateFileA - hand-written in shims_impl.c */
/* KERNEL32.dll!SetEndOfFile  (1 args) */
static void imp_SetEndOfFile(void) { IMPORT_STUB("SetEndOfFile"); RET(0); STDRET(1); }
/* KERNEL32.dll!RaiseException  (4 args) */
static void imp_RaiseException(void) { IMPORT_STUB("RaiseException"); RET(0); STDRET(4); }
extern void imp_SetFilePointer(void);  /* KERNEL32.dll!SetFilePointer - hand-written in shims_impl.c */
/* KERNEL32.dll!CreateDirectoryA  (2 args) */
static void imp_CreateDirectoryA(void) { IMPORT_STUB("CreateDirectoryA"); RET(0); STDRET(2); }
/* KERNEL32.dll!FindClose  (1 args) */
static void imp_FindClose(void) { IMPORT_STUB("FindClose"); RET(0); STDRET(1); }
/* KERNEL32.dll!FileTimeToSystemTime  (2 args) */
static void imp_FileTimeToSystemTime(void) { IMPORT_STUB("FileTimeToSystemTime"); RET(0); STDRET(2); }
/* KERNEL32.dll!FileTimeToLocalFileTime  (2 args) */
static void imp_FileTimeToLocalFileTime(void) { IMPORT_STUB("FileTimeToLocalFileTime"); RET(0); STDRET(2); }
/* KERNEL32.dll!GetDriveTypeA  (1 args) */
static void imp_GetDriveTypeA(void) { IMPORT_STUB("GetDriveTypeA"); RET(0); STDRET(1); }
/* KERNEL32.dll!FindFirstFileA  (2 args) */
static void imp_FindFirstFileA(void) { IMPORT_STUB("FindFirstFileA"); RET(0); STDRET(2); }
/* KERNEL32.dll!GetTimeZoneInformation  (1 args) */
static void imp_GetTimeZoneInformation(void) { IMPORT_STUB("GetTimeZoneInformation"); RET(0); STDRET(1); }
/* KERNEL32.dll!GetFullPathNameA  (4 args) */
static void imp_GetFullPathNameA(void) { IMPORT_STUB("GetFullPathNameA"); RET(0); STDRET(4); }
/* KERNEL32.dll!GetLogicalDrives  (0 args) */
static void imp_GetLogicalDrives(void) { IMPORT_STUB("GetLogicalDrives"); RET(0); STDRET(0); }
/* KERNEL32.dll!Sleep  (1 args) */
static void imp_Sleep(void) { IMPORT_STUB("Sleep"); RET(0); STDRET(1); }
/* KERNEL32.dll!SetEnvironmentVariableA  (2 args) */
static void imp_SetEnvironmentVariableA(void) { IMPORT_STUB("SetEnvironmentVariableA"); RET(0); STDRET(2); }
/* USER32.dll!GetDlgItemTextA  (4 args) */
static void imp_GetDlgItemTextA(void) { IMPORT_STUB("GetDlgItemTextA"); RET(0); STDRET(4); }
/* USER32.dll!SetRectEmpty  (1 args) */
static void imp_SetRectEmpty(void) { IMPORT_STUB("SetRectEmpty"); RET(0); STDRET(1); }
/* USER32.dll!UpdateWindow  (1 args) */
static void imp_UpdateWindow(void) { IMPORT_STUB("UpdateWindow"); RET(0); STDRET(1); }
/* USER32.dll!EnableMenuItem  (3 args) */
static void imp_EnableMenuItem(void) { IMPORT_STUB("EnableMenuItem"); RET(0); STDRET(3); }
/* USER32.dll!GetFocus  (0 args) */
static void imp_GetFocus(void) { IMPORT_STUB("GetFocus"); RET(0); STDRET(0); }
/* USER32.dll!GetParent  (1 args) */
static void imp_GetParent(void) { IMPORT_STUB("GetParent"); RET(0); STDRET(1); }
/* USER32.dll!ShowCursor  (1 args) */
static void imp_ShowCursor(void) { IMPORT_STUB("ShowCursor"); RET(0); STDRET(1); }
/* USER32.dll!SetCursorPos  (2 args) */
static void imp_SetCursorPos(void) { IMPORT_STUB("SetCursorPos"); RET(0); STDRET(2); }
/* USER32.dll!SetCursor  (1 args) */
static void imp_SetCursor(void) { IMPORT_STUB("SetCursor"); RET(0); STDRET(1); }
extern void imp_GetDC(void);  /* USER32.dll!GetDC - hand-written in shims_impl.c */
/* USER32.dll!SetTimer  (4 args) */
static void imp_SetTimer(void) { IMPORT_STUB("SetTimer"); RET(1); STDRET(4); }
/* USER32.dll!KillTimer  (2 args) */
static void imp_KillTimer(void) { IMPORT_STUB("KillTimer"); RET(0); STDRET(2); }
extern void imp_MessageBoxA(void);  /* USER32.dll!MessageBoxA - hand-written in shims_impl.c */
/* USER32.dll!SendMessageA  (4 args) */
static void imp_SendMessageA(void) { IMPORT_STUB("SendMessageA"); RET(0); STDRET(4); }
/* USER32.dll!GetClientRect  (2 args) */
static void imp_GetClientRect(void) { IMPORT_STUB("GetClientRect"); RET(0); STDRET(2); }
/* USER32.dll!AdjustWindowRect  (3 args) */
static void imp_AdjustWindowRect(void) { IMPORT_STUB("AdjustWindowRect"); RET(0); STDRET(3); }
/* USER32.dll!SetFocus  (1 args) */
static void imp_SetFocus(void) { IMPORT_STUB("SetFocus"); RET(0); STDRET(1); }
/* USER32.dll!GetDesktopWindow  (0 args) */
static void imp_GetDesktopWindow(void) { IMPORT_STUB("GetDesktopWindow"); RET(1); STDRET(0); }
/* USER32.dll!GetWindowRect  (2 args) */
static void imp_GetWindowRect(void) { IMPORT_STUB("GetWindowRect"); RET(0); STDRET(2); }
/* USER32.dll!MoveWindow  (6 args) */
static void imp_MoveWindow(void) { IMPORT_STUB("MoveWindow"); RET(0); STDRET(6); }
/* USER32.dll!IsDialogMessageA  (2 args) */
static void imp_IsDialogMessageA(void) { IMPORT_STUB("IsDialogMessageA"); RET(0); STDRET(2); }
extern void imp_PeekMessageA(void);  /* USER32.dll!PeekMessageA - hand-written in shims_impl.c */
extern void imp_TranslateAcceleratorA(void);  /* USER32.dll!TranslateAcceleratorA - hand-written in shims_impl.c */
/* USER32.dll!TranslateMessage  (1 args) */
static void imp_TranslateMessage(void) { IMPORT_STUB("TranslateMessage"); RET(0); STDRET(1); }
extern void imp_DispatchMessageA(void);  /* USER32.dll!DispatchMessageA - hand-written in shims_impl.c */
/* USER32.dll!EnableWindow  (2 args) */
static void imp_EnableWindow(void) { IMPORT_STUB("EnableWindow"); RET(0); STDRET(2); }
/* USER32.dll!GetDlgItem  (2 args) */
static void imp_GetDlgItem(void) { IMPORT_STUB("GetDlgItem"); RET(0); STDRET(2); }
/* USER32.dll!SetDlgItemTextA  (3 args) */
static void imp_SetDlgItemTextA(void) { IMPORT_STUB("SetDlgItemTextA"); RET(0); STDRET(3); }
extern void imp_DialogBoxParamA(void);  /* USER32.dll!DialogBoxParamA - hand-written in shims_impl.c */
/* USER32.dll!CheckDlgButton  (3 args) */
static void imp_CheckDlgButton(void) { IMPORT_STUB("CheckDlgButton"); RET(0); STDRET(3); }
/* USER32.dll!IsDlgButtonChecked  (2 args) */
static void imp_IsDlgButtonChecked(void) { IMPORT_STUB("IsDlgButtonChecked"); RET(0); STDRET(2); }
/* USER32.dll!WinHelpA  (4 args) */
static void imp_WinHelpA(void) { IMPORT_STUB("WinHelpA"); RET(0); STDRET(4); }
/* USER32.dll!EndDialog  (2 args) */
static void imp_EndDialog(void) { IMPORT_STUB("EndDialog"); RET(0); STDRET(2); }
/* USER32.dll!OffsetRect  (3 args) */
static void imp_OffsetRect(void) { IMPORT_STUB("OffsetRect"); RET(0); STDRET(3); }
/* USER32.dll!SetWindowPos  (7 args) */
static void imp_SetWindowPos(void) { IMPORT_STUB("SetWindowPos"); RET(0); STDRET(7); }
/* USER32.dll!PostMessageA  (4 args) */
static void imp_PostMessageA(void) { fprintf(stderr, "[PostMessageA] msg=0x%X wparam=0x%X\n", ARG(1), ARG(2)); RET(0); STDRET(4); }
/* USER32.dll!DestroyWindow  (1 args) */
static void imp_DestroyWindow(void) { IMPORT_STUB("DestroyWindow"); RET(0); STDRET(1); }
/* USER32.dll!FindWindowA  (2 args) */
static void imp_FindWindowA(void) { IMPORT_STUB("FindWindowA"); RET(0); STDRET(2); }
/* USER32.dll!GetLastActivePopup  (1 args) */
static void imp_GetLastActivePopup(void) { IMPORT_STUB("GetLastActivePopup"); RET(0); STDRET(1); }
/* USER32.dll!BringWindowToTop  (1 args) */
static void imp_BringWindowToTop(void) { IMPORT_STUB("BringWindowToTop"); RET(0); STDRET(1); }
/* USER32.dll!IsIconic  (1 args) */
static void imp_IsIconic(void) { IMPORT_STUB("IsIconic"); RET(0); STDRET(1); }
/* USER32.dll!SetForegroundWindow  (1 args) */
static void imp_SetForegroundWindow(void) { IMPORT_STUB("SetForegroundWindow"); RET(0); STDRET(1); }
/* USER32.dll!LoadIconA  (2 args) */
static void imp_LoadIconA(void) { IMPORT_STUB("LoadIconA"); RET(1); STDRET(2); }
extern void imp_RegisterClassA(void);  /* USER32.dll!RegisterClassA - hand-written in shims_impl.c */
/* USER32.dll!LoadCursorA  (2 args) */
static void imp_LoadCursorA(void) { IMPORT_STUB("LoadCursorA"); RET(1); STDRET(2); }
/* USER32.dll!LoadMenuA  (2 args) */
static void imp_LoadMenuA(void) { IMPORT_STUB("LoadMenuA"); RET(1); STDRET(2); }
extern void imp_LoadAcceleratorsA(void);  /* USER32.dll!LoadAcceleratorsA - hand-written in shims_impl.c */
/* USER32.dll!GetWindowLongA  (2 args) */
static void imp_GetWindowLongA(void) { IMPORT_STUB("GetWindowLongA"); RET(0); STDRET(2); }
/* USER32.dll!GetSystemMetrics  (1 args) */
static void imp_GetSystemMetrics(void) { IMPORT_STUB("GetSystemMetrics"); RET(1); STDRET(1); }
/* USER32.dll!PostQuitMessage  (1 args) */
static void imp_PostQuitMessage(void) { IMPORT_STUB("PostQuitMessage"); RET(0); STDRET(1); }
extern void imp_InvalidateRect(void);  /* USER32.dll!InvalidateRect - hand-written in shims_impl.c */
/* USER32.dll!DefWindowProcA  (4 args) */
static void imp_DefWindowProcA(void) { IMPORT_STUB("DefWindowProcA"); RET(0); STDRET(4); }
extern void imp_CreateWindowExA(void);  /* USER32.dll!CreateWindowExA - hand-written in shims_impl.c */
/* USER32.dll!BeginPaint  (2 args) */
static void imp_BeginPaint(void) { IMPORT_STUB("BeginPaint"); RET(1); STDRET(2); }
/* USER32.dll!LoadBitmapA  (2 args) */
static void imp_LoadBitmapA(void) { IMPORT_STUB("LoadBitmapA"); RET(1); STDRET(2); }
/* USER32.dll!EndPaint  (2 args) */
static void imp_EndPaint(void) { IMPORT_STUB("EndPaint"); RET(0); STDRET(2); }
extern void imp_ReleaseDC(void);  /* USER32.dll!ReleaseDC - hand-written in shims_impl.c */
/* USER32.dll!ShowWindow  (2 args) */
static void imp_ShowWindow(void) { IMPORT_STUB("ShowWindow"); RET(0); STDRET(2); }
/* USER32.dll!CopyRect  (2 args) */
static void imp_CopyRect(void) { IMPORT_STUB("CopyRect"); RET(0); STDRET(2); }
/* USER32.dll!CheckMenuItem  (3 args) */
static void imp_CheckMenuItem(void) { IMPORT_STUB("CheckMenuItem"); RET(0); STDRET(3); }
extern void imp_waveOutOpen(void);  /* WINMM.dll!waveOutOpen - hand-written in shims_impl.c */
/* WINMM.dll!mciGetErrorStringA  (3 args) */
extern void imp_mciGetErrorStringA(void);  /* WINMM.dll!mciGetErrorStringA - hand-written in shims_impl.c */
/* WINMM.dll!mciSendStringA  (4 args) */
extern void imp_mciSendStringA(void);  /* WINMM.dll!mciSendStringA - hand-written in shims_impl.c */
extern void imp_joyGetNumDevs(void);  /* WINMM.dll!joyGetNumDevs - hand-written in shims_impl.c */
extern void imp_waveOutReset(void);  /* WINMM.dll!waveOutReset - hand-written in shims_impl.c */
extern void imp_waveOutUnprepareHeader(void);  /* WINMM.dll!waveOutUnprepareHeader - hand-written in shims_impl.c */
extern void imp_waveOutWrite(void);  /* WINMM.dll!waveOutWrite - hand-written in shims_impl.c */
extern void imp_waveOutGetNumDevs(void);  /* WINMM.dll!waveOutGetNumDevs - hand-written in shims_impl.c */
extern void imp_waveOutGetDevCapsA(void);  /* WINMM.dll!waveOutGetDevCapsA - hand-written in shims_impl.c */
extern void imp_waveOutClose(void);  /* WINMM.dll!waveOutClose - hand-written in shims_impl.c */
extern void imp_waveOutPrepareHeader(void);  /* WINMM.dll!waveOutPrepareHeader - hand-written in shims_impl.c */
extern void imp_timeGetTime(void);  /* WINMM.dll!timeGetTime - hand-written in shims_impl.c */
extern void imp_timeEndPeriod(void);  /* WINMM.dll!timeEndPeriod - hand-written in shims_impl.c */
extern void imp_timeBeginPeriod(void);  /* WINMM.dll!timeBeginPeriod - hand-written in shims_impl.c */
extern void imp_joyGetPos(void);  /* WINMM.dll!joyGetPos - hand-written in shims_impl.c */
extern void imp_joyGetDevCapsA(void);  /* WINMM.dll!joyGetDevCapsA - hand-written in shims_impl.c */
/* comdlg32.dll!GetSaveFileNameA  (1 args) */
extern void imp_GetSaveFileNameA(void);  /* comdlg32.dll!GetSaveFileNameA - hand-written in shims_impl.c */
/* comdlg32.dll!CommDlgExtendedError  (0 args) */
extern void imp_CommDlgExtendedError(void);  /* comdlg32.dll!CommDlgExtendedError - hand-written in shims_impl.c */
/* comdlg32.dll!GetOpenFileNameA  (1 args) */
extern void imp_GetOpenFileNameA(void);  /* comdlg32.dll!GetOpenFileNameA - hand-written in shims_impl.c */

const recomp_dispatch_entry_t fury3_import_bridges[] = {
    { 0x00545320u, imp_DeleteObject },
    { 0x00545324u, imp_StretchBlt },
    { 0x00545328u, imp_SetStretchBltMode },
    { 0x0054532Cu, imp_BitBlt },
    { 0x00545330u, imp_SetDIBColorTable },
    { 0x00545334u, imp_CreatePalette },
    { 0x00545338u, imp_GetSystemPaletteEntries },
    { 0x0054533Cu, imp_CreateDIBSection },
    { 0x00545340u, imp_SelectObject },
    { 0x00545344u, imp_CreateCompatibleDC },
    { 0x00545348u, imp_PatBlt },
    { 0x0054534Cu, imp_DeleteDC },
    { 0x00545350u, imp_DPtoLP },
    { 0x00545354u, imp_GetObjectA },
    { 0x00545358u, imp_SetMapMode },
    { 0x0054535Cu, imp_GetMapMode },
    { 0x00545360u, imp_RealizePalette },
    { 0x00545364u, imp_SelectPalette },
    { 0x00545368u, imp_GetDeviceCaps },
    { 0x0054536Cu, imp_GetStockObject },
    { 0x00545374u, imp_lstrcpyA },
    { 0x00545378u, imp_GlobalAlloc },
    { 0x0054537Cu, imp_GlobalLock },
    { 0x00545380u, imp_FindResourceA },
    { 0x00545384u, imp_lstrlenA },
    { 0x00545388u, imp_GlobalFree },
    { 0x0054538Cu, imp_WritePrivateProfileStringA },
    { 0x00545390u, imp_GetPrivateProfileStringA },
    { 0x00545394u, imp_GetPrivateProfileIntA },
    { 0x00545398u, imp_GetModuleHandleA },
    { 0x0054539Cu, imp_GetCurrentDirectoryA },
    { 0x005453A0u, imp_GetModuleFileNameA },
    { 0x005453A4u, imp_SetCurrentDirectoryA },
    { 0x005453A8u, imp_GetVersion },
    { 0x005453ACu, imp_GlobalMemoryStatus },
    { 0x005453B0u, imp_LoadLibraryA },
    { 0x005453B4u, imp_GetProcAddress },
    { 0x005453B8u, imp_CreateThread },
    { 0x005453BCu, imp_SetThreadPriority },
    { 0x005453C0u, imp_GetCurrentProcess },
    { 0x005453C4u, imp_LoadModule },
    { 0x005453C8u, imp_FreeLibrary },
    { 0x005453CCu, imp_ExitProcess },
    { 0x005453D0u, imp_LoadResource },
    { 0x005453D4u, imp_LockResource },
    { 0x005453D8u, imp_MultiByteToWideChar },
    { 0x005453DCu, imp_GetFileType },
    { 0x005453E0u, imp_GetStdHandle },
    { 0x005453E4u, imp_VirtualAlloc },
    { 0x005453E8u, imp_VirtualFree },
    { 0x005453ECu, imp_ReadFile },
    { 0x005453F0u, imp_CloseHandle },
    { 0x005453F4u, imp_GetCommandLineA },
    { 0x005453F8u, imp_GetEnvironmentStrings },
    { 0x005453FCu, imp_GetStartupInfoA },
    { 0x00545400u, imp_DeleteFileA },
    { 0x00545404u, imp_GetLastError },
    { 0x00545408u, imp_RtlUnwind },
    { 0x0054540Cu, imp_WideCharToMultiByte },
    { 0x00545410u, imp_WriteFile },
    { 0x00545414u, imp_UnhandledExceptionFilter },
    { 0x00545418u, imp_GetACP },
    { 0x0054541Cu, imp_GetOEMCP },
    { 0x00545420u, imp_GetCPInfo },
    { 0x00545424u, imp_SetStdHandle },
    { 0x00545428u, imp_FlushFileBuffers },
    { 0x0054542Cu, imp_CreateFileA },
    { 0x00545430u, imp_SetEndOfFile },
    { 0x00545434u, imp_RaiseException },
    { 0x00545438u, imp_SetFilePointer },
    { 0x0054543Cu, imp_CreateDirectoryA },
    { 0x00545440u, imp_FindClose },
    { 0x00545444u, imp_FileTimeToSystemTime },
    { 0x00545448u, imp_FileTimeToLocalFileTime },
    { 0x0054544Cu, imp_GetDriveTypeA },
    { 0x00545450u, imp_FindFirstFileA },
    { 0x00545454u, imp_GetTimeZoneInformation },
    { 0x00545458u, imp_GetFullPathNameA },
    { 0x0054545Cu, imp_GetLogicalDrives },
    { 0x00545460u, imp_Sleep },
    { 0x00545464u, imp_SetEnvironmentVariableA },
    { 0x0054546Cu, imp_GetDlgItemTextA },
    { 0x00545470u, imp_SetRectEmpty },
    { 0x00545474u, imp_UpdateWindow },
    { 0x00545478u, imp_EnableMenuItem },
    { 0x0054547Cu, imp_GetFocus },
    { 0x00545480u, imp_GetParent },
    { 0x00545484u, imp_ShowCursor },
    { 0x00545488u, imp_SetCursorPos },
    { 0x0054548Cu, imp_SetCursor },
    { 0x00545490u, imp_GetDC },
    { 0x00545494u, imp_SetTimer },
    { 0x00545498u, imp_KillTimer },
    { 0x0054549Cu, imp_MessageBoxA },
    { 0x005454A0u, imp_SendMessageA },
    { 0x005454A4u, imp_GetClientRect },
    { 0x005454A8u, imp_AdjustWindowRect },
    { 0x005454ACu, imp_SetFocus },
    { 0x005454B0u, imp_GetDesktopWindow },
    { 0x005454B4u, imp_GetWindowRect },
    { 0x005454B8u, imp_MoveWindow },
    { 0x005454BCu, imp_IsDialogMessageA },
    { 0x005454C0u, imp_PeekMessageA },
    { 0x005454C4u, imp_TranslateAcceleratorA },
    { 0x005454C8u, imp_TranslateMessage },
    { 0x005454CCu, imp_DispatchMessageA },
    { 0x005454D0u, imp_EnableWindow },
    { 0x005454D4u, imp_GetDlgItem },
    { 0x005454D8u, imp_SetDlgItemTextA },
    { 0x005454DCu, imp_DialogBoxParamA },
    { 0x005454E0u, imp_CheckDlgButton },
    { 0x005454E4u, imp_IsDlgButtonChecked },
    { 0x005454E8u, imp_WinHelpA },
    { 0x005454ECu, imp_EndDialog },
    { 0x005454F0u, imp_OffsetRect },
    { 0x005454F4u, imp_SetWindowPos },
    { 0x005454F8u, imp_PostMessageA },
    { 0x005454FCu, imp_DestroyWindow },
    { 0x00545500u, imp_FindWindowA },
    { 0x00545504u, imp_GetLastActivePopup },
    { 0x00545508u, imp_BringWindowToTop },
    { 0x0054550Cu, imp_IsIconic },
    { 0x00545510u, imp_SetForegroundWindow },
    { 0x00545514u, imp_LoadIconA },
    { 0x00545518u, imp_RegisterClassA },
    { 0x0054551Cu, imp_LoadCursorA },
    { 0x00545520u, imp_LoadMenuA },
    { 0x00545524u, imp_LoadAcceleratorsA },
    { 0x00545528u, imp_GetWindowLongA },
    { 0x0054552Cu, imp_GetSystemMetrics },
    { 0x00545530u, imp_PostQuitMessage },
    { 0x00545534u, imp_InvalidateRect },
    { 0x00545538u, imp_DefWindowProcA },
    { 0x0054553Cu, imp_CreateWindowExA },
    { 0x00545540u, imp_BeginPaint },
    { 0x00545544u, imp_LoadBitmapA },
    { 0x00545548u, imp_EndPaint },
    { 0x0054554Cu, imp_ReleaseDC },
    { 0x00545550u, imp_ShowWindow },
    { 0x00545554u, imp_CopyRect },
    { 0x00545558u, imp_CheckMenuItem },
    { 0x00545560u, imp_waveOutOpen },
    { 0x00545564u, imp_mciGetErrorStringA },
    { 0x00545568u, imp_mciSendStringA },
    { 0x0054556Cu, imp_joyGetNumDevs },
    { 0x00545570u, imp_waveOutReset },
    { 0x00545574u, imp_waveOutUnprepareHeader },
    { 0x00545578u, imp_waveOutWrite },
    { 0x0054557Cu, imp_waveOutGetNumDevs },
    { 0x00545580u, imp_waveOutGetDevCapsA },
    { 0x00545584u, imp_waveOutClose },
    { 0x00545588u, imp_waveOutPrepareHeader },
    { 0x0054558Cu, imp_timeGetTime },
    { 0x00545590u, imp_timeEndPeriod },
    { 0x00545594u, imp_timeBeginPeriod },
    { 0x00545598u, imp_joyGetPos },
    { 0x0054559Cu, imp_joyGetDevCapsA },
    { 0x005455A4u, imp_GetSaveFileNameA },
    { 0x005455A8u, imp_CommDlgExtendedError },
    { 0x005455ACu, imp_GetOpenFileNameA },
};
const uint32_t fury3_import_bridge_count = 160;

/* IAT slot VAs to self-patch so RECOMP_ITAIL(MEM32(slot)) resolves. */
const uint32_t fury3_iat_slots[] = {
    0x00545320u,
    0x00545324u,
    0x00545328u,
    0x0054532Cu,
    0x00545330u,
    0x00545334u,
    0x00545338u,
    0x0054533Cu,
    0x00545340u,
    0x00545344u,
    0x00545348u,
    0x0054534Cu,
    0x00545350u,
    0x00545354u,
    0x00545358u,
    0x0054535Cu,
    0x00545360u,
    0x00545364u,
    0x00545368u,
    0x0054536Cu,
    0x00545374u,
    0x00545378u,
    0x0054537Cu,
    0x00545380u,
    0x00545384u,
    0x00545388u,
    0x0054538Cu,
    0x00545390u,
    0x00545394u,
    0x00545398u,
    0x0054539Cu,
    0x005453A0u,
    0x005453A4u,
    0x005453A8u,
    0x005453ACu,
    0x005453B0u,
    0x005453B4u,
    0x005453B8u,
    0x005453BCu,
    0x005453C0u,
    0x005453C4u,
    0x005453C8u,
    0x005453CCu,
    0x005453D0u,
    0x005453D4u,
    0x005453D8u,
    0x005453DCu,
    0x005453E0u,
    0x005453E4u,
    0x005453E8u,
    0x005453ECu,
    0x005453F0u,
    0x005453F4u,
    0x005453F8u,
    0x005453FCu,
    0x00545400u,
    0x00545404u,
    0x00545408u,
    0x0054540Cu,
    0x00545410u,
    0x00545414u,
    0x00545418u,
    0x0054541Cu,
    0x00545420u,
    0x00545424u,
    0x00545428u,
    0x0054542Cu,
    0x00545430u,
    0x00545434u,
    0x00545438u,
    0x0054543Cu,
    0x00545440u,
    0x00545444u,
    0x00545448u,
    0x0054544Cu,
    0x00545450u,
    0x00545454u,
    0x00545458u,
    0x0054545Cu,
    0x00545460u,
    0x00545464u,
    0x0054546Cu,
    0x00545470u,
    0x00545474u,
    0x00545478u,
    0x0054547Cu,
    0x00545480u,
    0x00545484u,
    0x00545488u,
    0x0054548Cu,
    0x00545490u,
    0x00545494u,
    0x00545498u,
    0x0054549Cu,
    0x005454A0u,
    0x005454A4u,
    0x005454A8u,
    0x005454ACu,
    0x005454B0u,
    0x005454B4u,
    0x005454B8u,
    0x005454BCu,
    0x005454C0u,
    0x005454C4u,
    0x005454C8u,
    0x005454CCu,
    0x005454D0u,
    0x005454D4u,
    0x005454D8u,
    0x005454DCu,
    0x005454E0u,
    0x005454E4u,
    0x005454E8u,
    0x005454ECu,
    0x005454F0u,
    0x005454F4u,
    0x005454F8u,
    0x005454FCu,
    0x00545500u,
    0x00545504u,
    0x00545508u,
    0x0054550Cu,
    0x00545510u,
    0x00545514u,
    0x00545518u,
    0x0054551Cu,
    0x00545520u,
    0x00545524u,
    0x00545528u,
    0x0054552Cu,
    0x00545530u,
    0x00545534u,
    0x00545538u,
    0x0054553Cu,
    0x00545540u,
    0x00545544u,
    0x00545548u,
    0x0054554Cu,
    0x00545550u,
    0x00545554u,
    0x00545558u,
    0x00545560u,
    0x00545564u,
    0x00545568u,
    0x0054556Cu,
    0x00545570u,
    0x00545574u,
    0x00545578u,
    0x0054557Cu,
    0x00545580u,
    0x00545584u,
    0x00545588u,
    0x0054558Cu,
    0x00545590u,
    0x00545594u,
    0x00545598u,
    0x0054559Cu,
    0x005455A4u,
    0x005455A8u,
    0x005455ACu,
};
const uint32_t fury3_iat_slot_count = 160;
