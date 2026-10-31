cmake_minimum_required(VERSION 3.20)

# FURY3_SDL=ON builds the SDL2 + Dear ImGui frontend (windowed presentation + an
# in-game menu: File/Video/Audio/Controls with keyboard + Xbox-controller binding,
# save states). OFF (default) builds the plain Win32/GDI host. The C++ menu pulls
# in the C++ language + SDL2; the recomp itself stays C.
option(FURY3_SDL "Build the SDL2 + Dear ImGui frontend (menu/controls GUI)" OFF)
if(FURY3_SDL)
  project(fury3 C CXX)
else()
  project(fury3 C)
endif()

# Static recompilation of Fury3 (1995). Build the runtime + import bridges +
# the generated lifted code into a single host executable.
#
#   1. py -3 run_lift.py          # generate src/recomp/gen/  (if not present)
#   2. py -3 gen_imports.py       # generate src/runtime/imports_gen.c
#   3. cmake -B build -G Ninja && cmake --build build
#   4. build/fury3 <path-to-FURY3.EXE> [--run]

set(CMAKE_C_STANDARD 11)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

file(GLOB GEN_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/recomp/gen/recomp_*.c")

# PS Vita: configure with -DCMAKE_TOOLCHAIN_FILE=$VITASDK/share/vita.toolchain.cmake
# (which sets VITA). The Vita build is SDL-only and, for now, without the ImGui menu
# (it needs a keyboard to open); menu_overlay_stub.c supplies the defaults.
if(VITA)
  set(FURY3_SDL ON CACHE BOOL "" FORCE)
  set(FURY3_MENU OFF CACHE BOOL "" FORCE)
endif()
option(FURY3_MENU "SDL frontend: include the Dear ImGui menu overlay" ON)

if(NOT WIN32 AND NOT FURY3_SDL)
  message(FATAL_ERROR "Non-Windows builds need the SDL2 frontend: -DFURY3_SDL=ON")
endif()
if(VITA)
  set(HOST_OS_SOURCES src/runtime/host_os_vita.c)
elseif(WIN32)
  set(HOST_OS_SOURCES src/runtime/host_os_win32.c)
else()
  set(HOST_OS_SOURCES src/runtime/host_os_posix.c)
endif()

add_executable(fury3
  src/runtime/recomp_runtime.c
  src/runtime/imports_gen.c
  src/runtime/shims_impl.c
  src/runtime/native_raster.c
  src/runtime/game_setup.c
  src/runtime/cinepak.c
  src/runtime/gpu_capture.c
  ${HOST_OS_SOURCES}
  ${GEN_SOURCES}
)

# RECOMP_RETADDR=0u: benign dummy return address (a stray stack leak can't become a
#   multi-gigabyte size argument during bring-up).
# RECOMP_TRACE (Debug only): a per-function ring-buffer backtrace for crash
#  forensics. It costs a call on every lifted function entry, so Release omits it.
target_compile_definitions(fury3 PRIVATE RECOMP_RETADDR=0u $<$<CONFIG:Debug>:RECOMP_TRACE>)

target_include_directories(fury3 PRIVATE
  src/recomp
  src/recomp/gen
  src/runtime
)

# Fast lifted code: guest registers and the memory base as per-function locals,
# non-volatile guest memory (see recomp_types.h). Lifted sources only -- the
# hand-written shims work on the register globals directly.
option(FURY3_LOCAL_REGS "Lifted code keeps guest registers in per-function locals" ON)
option(FURY3_FAST_MEM "Lifted code uses non-volatile guest memory off a local base" ON)
set(FURY3_GEN_DEFS "")
if(FURY3_LOCAL_REGS)
  list(APPEND FURY3_GEN_DEFS RECOMP_LOCAL_REGS)
endif()
if(FURY3_FAST_MEM)
  list(APPEND FURY3_GEN_DEFS RECOMP_FAST_MEM)
endif()
if(FURY3_GEN_DEFS)
  set_source_files_properties(${GEN_SOURCES} PROPERTIES COMPILE_DEFINITIONS "${FURY3_GEN_DEFS}")
endif()

# Generated code is intentionally not warning-clean; silence the noise.
target_compile_options(fury3 PRIVATE -w)
# GCC/Clang: guest memory is one byte array read at mixed widths (MEM8/16/32
# through differently-typed pointers) and guest arithmetic wraps like x86, so
# type-based alias analysis and signed-overflow assumptions must be off. (MSVC
# does neither, which is why RECOMP_FAST_MEM is safe there without flags.)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(fury3 PRIVATE -fno-strict-aliasing -fwrapv)
endif()

# The host process must leave the original game's VA range [0x00400000 ..] free
# so the loader can map it fixed (g_mem_base == 0). Link high + no ASLR.
if(WIN32)
  if(MSVC)
    target_link_options(fury3 PRIVATE /BASE:0x70000000 /DYNAMICBASE:NO)
    # Release keeps a PDB so FURY3_PROF=2 (native sampling) can name functions.
    target_compile_options(fury3 PRIVATE $<$<CONFIG:Release>:/Zi>)
    target_link_options(fury3 PRIVATE $<$<CONFIG:Release>:/DEBUG /OPT:REF /OPT:ICF>)
    target_link_libraries(fury3 PRIVATE dbghelp)
  else()
    target_link_options(fury3 PRIVATE -Wl,--image-base,0x70000000)
  endif()
  target_link_libraries(fury3 PRIVATE gdi32 user32 winmm comdlg32)
elseif(NOT VITA)
  find_package(Threads REQUIRED)
  target_link_libraries(fury3 PRIVATE Threads::Threads m)
endif()

# ---- SDL2 frontend (+ optional Dear ImGui menu) ----
if(FURY3_SDL)
  set(CMAKE_CXX_STANDARD 17)
  find_package(SDL2 REQUIRED)
  target_compile_definitions(fury3 PRIVATE FURY3_SDL=1)
  # GPU renderer: OpenGL on PCs, vitaGL on the Vita
  target_sources(fury3 PRIVATE src/runtime/gpu_gl.c)
  target_compile_definitions(fury3 PRIVATE FURY3_GL=1)
  if(VITA)
    target_link_libraries(fury3 PRIVATE vitaGL vitashark SceShaccCgExt mathneon taihen_stub SceGxm_stub SceDisplay_stub SceSysmodule_stub SceCommonDialog_stub SceShaccCg_stub SceKernelDmacMgr_stub SceCtrl_stub SceTouch_stub SceAppMgr_stub SceRazorHud_stub SceRazorCapture_stub SceIme_stub stdc++)
  elseif(WIN32)
    target_link_libraries(fury3 PRIVATE opengl32)
  else()
    find_package(OpenGL REQUIRED)
    target_link_libraries(fury3 PRIVATE OpenGL::GL)
  endif()
  target_include_directories(fury3 PRIVATE ${SDL2_INCLUDE_DIRS})
  if(TARGET SDL2::SDL2-static AND VITA)
    target_link_libraries(fury3 PRIVATE SDL2::SDL2-static)
  else()
    target_link_libraries(fury3 PRIVATE ${SDL2_LIBRARIES} SDL2::SDL2)
  endif()
  if(FURY3_MENU)
    set(IMGUI ${CMAKE_SOURCE_DIR}/frontend/third_party/imgui)
    set(IMGUI_SOURCES
      ${IMGUI}/imgui.cpp ${IMGUI}/imgui_draw.cpp ${IMGUI}/imgui_tables.cpp ${IMGUI}/imgui_widgets.cpp
      ${IMGUI}/backends/imgui_impl_sdl2.cpp ${IMGUI}/backends/imgui_impl_sdlrenderer2.cpp)
    target_sources(fury3 PRIVATE src/runtime/menu_overlay.cpp ${IMGUI_SOURCES})
    target_include_directories(fury3 PRIVATE ${IMGUI} ${IMGUI}/backends)
    set_source_files_properties(${IMGUI_SOURCES} src/runtime/menu_overlay.cpp PROPERTIES COMPILE_OPTIONS "-w")
  else()
    target_sources(fury3 PRIVATE src/runtime/menu_overlay_stub.c)
  endif()
endif()

# ---- PS Vita packaging: eboot.bin (SELF) + installable VPK ----
# Game data is not packaged: copy gamedata/ and FURY3.EXE to ux0:data/fury3/.
if(VITA)
  include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)
  set(VITA_APP_NAME "Fury3")
  set(VITA_TITLEID  "FURY00003")
  set(VITA_VERSION  "01.01")
  # The title art isn't in the public source, so builds from it use tools/release/livearea.
  set(VITA_ART src/vita/sce_sys)
  if(NOT EXISTS "${CMAKE_SOURCE_DIR}/${VITA_ART}")
    set(VITA_ART tools/release/livearea)
  endif()
  target_link_libraries(fury3 PRIVATE ScePower_stub SceAppMgr_stub)
  target_link_options(fury3 PRIVATE "-Wl,-T,${CMAKE_SOURCE_DIR}/src/vita/sce_gap.ld")
  vita_create_self(eboot.bin fury3 UNSAFE)
  vita_create_vpk(fury3.vpk ${VITA_TITLEID} eboot.bin
    VERSION ${VITA_VERSION}
    NAME ${VITA_APP_NAME}
    FILE ${VITA_ART}/icon0.png sce_sys/icon0.png
         ${VITA_ART}/livearea/contents/bg.png sce_sys/livearea/contents/bg.png
         ${VITA_ART}/livearea/contents/startup.png sce_sys/livearea/contents/startup.png
         ${VITA_ART}/livearea/contents/template.xml sce_sys/livearea/contents/template.xml)
endif()
