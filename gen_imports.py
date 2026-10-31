#!/usr/bin/env python3
"""
Generate the Fury3 import-bridge layer (Phase 4).

The lifted code reaches each Win32 import through its IAT slot VA. This emits a
C shim for every imported function and a {iat_va -> shim} bridge table the runtime
installs. Shims are stdcall-correct: each pops the right number of argument slots
(plus the dummy return address) so the simulated stack stays balanced even while a
shim is still a stub. Critical shims (render/audio/input/io) get real bodies over
time; everything else logs once and returns a benign default.

Output: src/runtime/imports_gen.c   (committed; regenerate via this script)
"""
import sys, os, json
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'tools', 'tools', 'pe'))
from pe_analyze import analyze_pe, build_iat_map

# stdcall argument-slot counts for every import Fury3 uses (1 slot = 4 bytes).
ARGC = {
 # GDI32
 'DeleteObject':1,'StretchBlt':11,'SetStretchBltMode':2,'BitBlt':9,'SetDIBColorTable':4,
 'CreatePalette':1,'GetSystemPaletteEntries':4,'CreateDIBSection':6,'SelectObject':2,
 'CreateCompatibleDC':1,'PatBlt':6,'DeleteDC':1,'DPtoLP':3,'GetObjectA':3,'SetMapMode':2,
 'GetMapMode':1,'RealizePalette':1,'SelectPalette':3,'GetDeviceCaps':2,'GetStockObject':1,
 # KERNEL32
 'lstrcpyA':2,'GlobalAlloc':2,'GlobalLock':1,'FindResourceA':3,'lstrlenA':1,'GlobalFree':1,
 'WritePrivateProfileStringA':4,'GetPrivateProfileStringA':6,'GetPrivateProfileIntA':4,
 'GetModuleHandleA':1,'GetCurrentDirectoryA':2,'GetModuleFileNameA':3,'SetCurrentDirectoryA':1,
 'GetVersion':0,'GlobalMemoryStatus':1,'LoadLibraryA':1,'GetProcAddress':2,'CreateThread':6,
 'SetThreadPriority':2,'GetCurrentProcess':0,'LoadModule':2,'FreeLibrary':1,'ExitProcess':1,
 'LoadResource':2,'LockResource':1,'MultiByteToWideChar':6,'GetFileType':1,'GetStdHandle':1,
 'VirtualAlloc':4,'VirtualFree':3,'ReadFile':5,'CloseHandle':1,'GetCommandLineA':0,
 'GetEnvironmentStrings':0,'GetStartupInfoA':1,'DeleteFileA':1,'GetLastError':0,'RtlUnwind':4,
 'WideCharToMultiByte':8,'WriteFile':5,'UnhandledExceptionFilter':1,'GetACP':0,'GetOEMCP':0,
 'GetCPInfo':2,'SetStdHandle':2,'FlushFileBuffers':1,'CreateFileA':7,'SetEndOfFile':1,
 'RaiseException':4,'SetFilePointer':4,'CreateDirectoryA':2,'FindClose':1,'FileTimeToSystemTime':2,
 'FileTimeToLocalFileTime':2,'GetDriveTypeA':1,'FindFirstFileA':2,'GetTimeZoneInformation':1,
 'GetFullPathNameA':4,'GetLogicalDrives':0,'Sleep':1,'SetEnvironmentVariableA':2,
 # USER32
 'GetDlgItemTextA':4,'SetRectEmpty':1,'UpdateWindow':1,'EnableMenuItem':3,'GetFocus':0,
 'GetParent':1,'ShowCursor':1,'SetCursorPos':2,'SetCursor':1,'GetDC':1,'SetTimer':4,'KillTimer':2,
 'MessageBoxA':4,'SendMessageA':4,'GetClientRect':2,'AdjustWindowRect':3,'SetFocus':1,
 'GetDesktopWindow':0,'GetWindowRect':2,'MoveWindow':6,'IsDialogMessageA':2,'PeekMessageA':5,
 'TranslateAcceleratorA':3,'TranslateMessage':1,'DispatchMessageA':1,'EnableWindow':2,'GetDlgItem':2,
 'SetDlgItemTextA':3,'DialogBoxParamA':5,'CheckDlgButton':3,'IsDlgButtonChecked':2,'WinHelpA':4,
 'EndDialog':2,'OffsetRect':3,'SetWindowPos':7,'PostMessageA':4,'DestroyWindow':1,'FindWindowA':2,
 'GetLastActivePopup':1,'BringWindowToTop':1,'IsIconic':1,'SetForegroundWindow':1,'LoadIconA':2,
 'RegisterClassA':1,'LoadCursorA':2,'LoadMenuA':2,'LoadAcceleratorsA':2,'GetWindowLongA':2,
 'GetSystemMetrics':1,'PostQuitMessage':1,'InvalidateRect':3,'DefWindowProcA':4,'CreateWindowExA':12,
 'BeginPaint':2,'LoadBitmapA':2,'EndPaint':2,'ReleaseDC':2,'ShowWindow':2,'CopyRect':2,'CheckMenuItem':3,
 # WINMM
 'waveOutOpen':6,'mciGetErrorStringA':3,'mciSendStringA':4,'joyGetNumDevs':0,'waveOutReset':1,
 'waveOutUnprepareHeader':3,'waveOutWrite':3,'waveOutGetNumDevs':0,'waveOutGetDevCapsA':3,
 'waveOutClose':1,'waveOutPrepareHeader':3,'timeGetTime':0,'timeEndPeriod':1,'timeBeginPeriod':1,
 'joyGetPos':2,'joyGetDevCapsA':3,
 # comdlg32
 'GetSaveFileNameA':1,'CommDlgExtendedError':0,'GetOpenFileNameA':1,
}

# Imports with hand-written bodies in src/runtime/shims_impl.c. The generator
# emits an `extern` declaration for these instead of a stub, so the bridge table
# points at the real implementation.
MANUAL = {
 'GetVersion','GetCommandLineA','GetModuleFileNameA','GetModuleHandleA','GetStartupInfoA',
 'GetStdHandle','GetFileType','GetACP','GetOEMCP','GetCPInfo','GetEnvironmentStrings',
 'VirtualAlloc','VirtualFree','GlobalAlloc','GlobalLock','GlobalFree','GlobalMemoryStatus',
 'LoadLibraryA','GetProcAddress','WriteFile','SetStdHandle','GetCurrentDirectoryA',
 'ExitProcess','MessageBoxA','timeGetTime','timeBeginPeriod','timeEndPeriod',
 'CreateFileA','ReadFile','SetFilePointer','CloseHandle','GetFileType',
 'GetPrivateProfileStringA','GetPrivateProfileIntA','WritePrivateProfileStringA',
 # saved games: the common file dialogs pick the single save slot
 'GetSaveFileNameA','GetOpenFileNameA','CommDlgExtendedError',
 # the AVI cinematics' MCI commands (no video playback yet: report each movie finished)
 'mciSendStringA','mciGetErrorStringA',
 # display / render path
 'CreateWindowExA','GetDC','ReleaseDC','CreateDIBSection','SetDIBColorTable',
 'StretchBlt','BitBlt','PeekMessageA','GetMessageA','RegisterClassA','InvalidateRect','CreatePalette',
 # memory-DC model: the game selects its current DIB into a memory DC and BitBlts
 # from that DC, so presenting needs to know which DIB each DC holds
 'SelectObject','CreateCompatibleDC','DeleteDC',
 # sound effects: waveOut on SDL2 queued audio (host_wave_* in recomp_runtime.c)
 'waveOutGetNumDevs','waveOutGetDevCapsA','waveOutOpen','waveOutPrepareHeader',
 'waveOutUnprepareHeader','waveOutWrite','waveOutReset','waveOutClose',
 # front-end input: accelerator keys (F2=New Game etc.) -> WM_COMMAND; modal info dialogs;
 # DispatchMessageA must reach the game's WndProc (some loops poll a WndProc-set flag)
 'TranslateAcceleratorA','LoadAcceleratorsA','DialogBoxParamA','DispatchMessageA',
 # joystick: pass through to the real WINMM joystick API so a physical stick /
 # gamepad works (flight controls). Hand-written in shims_impl.c.
 'joyGetNumDevs','joyGetPos','joyGetDevCapsA',
 # thread/priority: the game sanity-checks SetThreadPriority(GetCurrentProcess(),1)
 # EXPECTS failure; CreateThread must return a handle distinct from the process
 # pseudo-handle (1). Hand-written so a gen regen can't revert them to stubs.
 'CreateThread','SetThreadPriority',
}

# Shims that should return a non-zero "success/handle" default so early init code
# that checks the return value keeps going. (Stubs still — just friendlier defaults.)
NONZERO_DEFAULT = {
 'GetModuleHandleA','GetDC','CreateCompatibleDC','CreateDIBSection','CreatePalette','GetStockObject',
 'SelectObject','CreateWindowExA','LoadIconA','LoadCursorA','GlobalAlloc','GlobalLock',
 'GetCurrentProcess','GetStdHandle','BeginPaint','GetVersion','SelectPalette','LoadResource','LockResource',
 'FindResourceA','GetSystemMetrics','GetDesktopWindow',
 # report success so non-fatal startup steps don't abort
 'WritePrivateProfileStringA','LoadMenuA','SetTimer','LoadBitmapA',
 'CreateCompatibleBitmap','GetDC','RealizePalette','SetDIBColorTable','CreateThread',
 'SetThreadPriority','SetTimer','GetCurrentProcess',
}

def main():
    exe = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__),'analysis','FURY3.EXE')
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(__file__),'src','runtime','imports_gen.c')
    info = analyze_pe(exe)
    iat = build_iat_map(info)

    rows = sorted(iat.items())  # (va, (dll, name))
    missing = sorted({n for _,(_,n) in rows if n not in ARGC})
    if missing:
        print(f"WARNING: {len(missing)} imports without an ARGC entry (default 0): {missing}")

    L = []
    L.append('/* Fury3 Recompilation - import bridge layer - AUTO-GENERATED */')
    L.append('/* Regenerate: py -3 gen_imports.py */')
    L.append('#define RECOMP_GENERATED_CODE')
    L.append('#include "recomp_types.h"')
    L.append('#include "imports.h"')
    L.append('')
    L.append(f'/* {len(rows)} imports across {len({d for _,(d,_) in rows})} DLLs */')
    L.append('')
    seen = {}
    for va,(dll,name) in rows:
        argc = ARGC.get(name, 0)
        ret = 1 if name in NONZERO_DEFAULT else 0
        fn = f'imp_{name}'
        if name in seen:        # same name imported twice -> reuse shim
            continue
        seen[name] = fn
        if name in MANUAL:
            L.append(f'extern void {fn}(void);  /* {dll}!{name} - hand-written in shims_impl.c */')
            continue
        L.append(f'/* {dll}!{name}  ({argc} args) */')
        L.append(f'static void {fn}(void) {{ IMPORT_STUB("{name}"); RET({ret}); STDRET({argc}); }}')
    L.append('')
    L.append('const recomp_dispatch_entry_t fury3_import_bridges[] = {')
    for va,(dll,name) in rows:
        L.append(f'    {{ 0x{va:08X}u, {seen[name]} }},')
    L.append('};')
    L.append(f'const uint32_t fury3_import_bridge_count = {len(rows)};')
    L.append('')
    L.append('/* IAT slot VAs to self-patch so RECOMP_ITAIL(MEM32(slot)) resolves. */')
    L.append('const uint32_t fury3_iat_slots[] = {')
    for va,_ in rows:
        L.append(f'    0x{va:08X}u,')
    L.append('};')
    L.append(f'const uint32_t fury3_iat_slot_count = {len(rows)};')
    L.append('')

    os.makedirs(os.path.dirname(out), exist_ok=True)
    open(out,'w').write('\n'.join(L))
    print(f'Wrote {out}: {len(rows)} bridges ({len(seen)} unique shims)')

if __name__ == '__main__':
    main()
