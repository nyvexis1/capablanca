#!/bin/sh
# Fast node-limited self-play to produce games for Texel tuning.
# Usage: scripts/datagen.sh <games> [nodes] [engine.exe] [concurrency]
cd "$(dirname "$0")/.."
GAMES=${1:-10000}; NODES=${2:-8000}; CONC=${4:-14}
ENG=$(cygpath -w "$(realpath ${3:-capablanca.exe})")
FC=tools/fastchess/fastchess-windows-x86-64/fastchess.exe
STAMP=$(date +%Y%m%d-%H%M%S)
mkdir -p data
$FC -engine cmd=$ENG name=A -engine cmd=$ENG name=B \
    -each tc=inf nodes=$NODES option.Hash=16 option.SacrificeVision=0 \
    -openings file=tools/books/8moves_v3.pgn format=pgn order=random \
    -rounds $((GAMES / 2)) -games 2 -repeat -concurrency $CONC -recover \
    -resign movecount=4 score=1000 -draw movenumber=40 movecount=8 score=10 \
    -pgnout file=data/datagen_$STAMP.pgn > data/datagen_$STAMP.log 2>&1
tail -3 data/datagen_$STAMP.log
rm -f config.json
