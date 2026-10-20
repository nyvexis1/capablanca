#!/bin/sh
# NNUE data generation: N idle-priority worker processes, each appending to its own file.
# Usage: scripts/nnue_datagen.sh [workers] [nodes]   (stop any time; finished games are kept)
cd "$(dirname "$0")/.."
N=${1:-12}; NODES=${2:-5000}
mkdir -p data/nnue bin
cp capablanca.exe bin/capablanca-dg.exe   # own copy, so rebuilding the engine doesn't clash
BASE=$(date +%s)
for i in $(seq 1 $N); do
    bin/capablanca-dg.exe datagen data/nnue/gen_${BASE}_$i.bin $((BASE * 100 + i)) $NODES > data/nnue/gen_${BASE}_$i.log 2>&1 &
done
wait
