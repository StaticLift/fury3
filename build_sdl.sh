#!/usr/bin/env bash
# Build the SDL2 + Dear ImGui frontend variant of the Fury3 recomp.
#   - C recomp/runtime compiled with gcc (-DFURY3_SDL)
#   - imgui + menu_overlay compiled with g++
#   - linked with g++ + SDL2
# Output: build/fury3_sdl.exe   (the plain GDI build is build/fury3[_dbg].exe)
set -e
export PATH="/c/msys64/mingw64/bin:$PATH"
cd "$(dirname "$0")"
mkdir -p build/obj

IMG=frontend/third_party/imgui
SDLINC="-I/c/msys64/mingw64/include/SDL2"
CINC="-Isrc/recomp -Isrc/recomp/gen -Isrc/runtime"
CDEF="-DRECOMP_RETADDR=0u -DFURY3_SDL"

echo "[1/4] C++ : imgui + menu_overlay"
for f in $IMG/imgui.cpp $IMG/imgui_draw.cpp $IMG/imgui_tables.cpp $IMG/imgui_widgets.cpp \
         $IMG/backends/imgui_impl_sdl2.cpp $IMG/backends/imgui_impl_sdlrenderer2.cpp; do
  o="build/obj/$(basename ${f%.cpp}).o"
  [ "$o" -nt "$f" ] 2>/dev/null || g++ -O2 -w -std=c++17 -I$IMG -I$IMG/backends $SDLINC -c "$f" -o "$o"
done
g++ -O2 -w -std=c++17 -I$IMG -I$IMG/backends -Isrc/runtime $SDLINC -c src/runtime/menu_overlay.cpp -o build/obj/menu_overlay.o

echo "[2/4] C : runtime + import bridges"
gcc -O2 -w $CDEF $CINC $SDLINC -c src/runtime/recomp_runtime.c -o build/obj/recomp_runtime.o
gcc -O2 -w $CDEF $CINC          -c src/runtime/imports_gen.c    -o build/obj/imports_gen.o
gcc -O2 -w $CDEF $CINC          -c src/runtime/shims_impl.c     -o build/obj/shims_impl.o

echo "[3/4] C : generated lifted code"
GENOBJS=""
for f in src/recomp/gen/recomp_*.c; do
  o="build/obj/$(basename ${f%.c}).o"
  [ "$o" -nt "$f" ] 2>/dev/null || gcc -O2 -w $CDEF $CINC -c "$f" -o "$o"
  GENOBJS="$GENOBJS $o"
done

echo "[4/4] link (g++ + SDL2)"
g++ -O2 build/obj/recomp_runtime.o build/obj/imports_gen.o build/obj/shims_impl.o $GENOBJS \
    build/obj/imgui.o build/obj/imgui_draw.o build/obj/imgui_tables.o build/obj/imgui_widgets.o \
    build/obj/imgui_impl_sdl2.o build/obj/imgui_impl_sdlrenderer2.o build/obj/menu_overlay.o \
    -Wl,--image-base,0x70000000 -o build/fury3_sdl.exe \
    -lmingw32 -lSDL2 -lgdi32 -luser32 -lwinmm -lcomdlg32 -limm32 -lole32 -loleaut32 -lversion -lsetupapi
echo "OK -> build/fury3_sdl.exe"
