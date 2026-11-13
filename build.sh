#!/bin/sh
# Build Capablanca. Usage: ./build.sh [output.exe] [rook|queen]
# rook = 256-wide net (nets/default.nnue), queen = 512-wide net (nets/queen.nnue)
GXX_DIR=$(ls -d ~/AppData/Local/Microsoft/WinGet/Packages/BrechtSanders.WinLibs*/mingw64/bin 2>/dev/null | head -1)
[ -n "$GXX_DIR" ] && export PATH="$GXX_DIR:$PATH"
# -mno-avx: MinGW GCC emits aligned AVX stack moves without realigning the Win64 stack,
# which crashes randomly. Keep it until we switch compilers.
# ARCH=x86-64-v2 builds a portable binary (any CPU with POPCNT/SSE4.2) for releases.
OUT=${1:-capablanca.exe}
# Compile the default network into the binary when there is one
NET=nets/default.nnue; WIDTH=""
[ "$2" = queen ] && NET=nets/queen.nnue && WIDTH=-DNNUE_HIDDEN=512
EMBED=""; [ -f $NET ] && EMBED="-DNNUE_EMBED=\"$NET\""
g++ $EMBED $WIDTH -O3 -march=${ARCH:-native} -mno-avx -flto -std=c++20 -Wall -Wextra -static -Wl,--stack,268435456 -DNDEBUG src/*.cpp -o "$OUT"
