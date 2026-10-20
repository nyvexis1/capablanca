#!/bin/sh
# Build Capablanca. Usage: ./build.sh [output.exe]
GXX_DIR=$(ls -d ~/AppData/Local/Microsoft/WinGet/Packages/BrechtSanders.WinLibs*/mingw64/bin 2>/dev/null | head -1)
[ -n "$GXX_DIR" ] && export PATH="$GXX_DIR:$PATH"
OUT=${1:-capablanca.exe}
g++ -O3 -march=native -mno-avx -flto -std=c++20 -Wall -Wextra -static -Wl,--stack,268435456 -DNDEBUG src/*.cpp -o "$OUT"
