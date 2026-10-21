#!/bin/sh
# Build Capablanca. Usage: ./build.sh [output.exe]
GXX_DIR=$(ls -d ~/AppData/Local/Microsoft/WinGet/Packages/BrechtSanders.WinLibs*/mingw64/bin 2>/dev/null | head -1)
[ -n "$GXX_DIR" ] && export PATH="$GXX_DIR:$PATH"
# -mno-avx: MinGW GCC emits aligned AVX stack moves without realigning the Win64 stack,
# which crashes randomly. Keep it until we switch compilers.
OUT=${1:-capablanca.exe}
# Compile the default network into the binary when there is one
EMBED=""; [ -f nets/default.nnue ] && EMBED="-DNNUE_EMBED=\"nets/default.nnue\""
g++ $EMBED -O3 -march=native -mno-avx -flto -std=c++20 -Wall -Wextra -static -Wl,--stack,268435456 -DNDEBUG src/*.cpp -o "$OUT"
