#!/bin/sh
# Match Capablanca against reduced-strength Stockfish.
# Usage: scripts/match.sh <stockfish_elo> <games> [tc] [engine.exe] [concurrency]
cd "$(dirname "$0")/.."
# Run fastchess and both engines below normal priority so long matches do not make the PC
# stutter. Lowering only Capablanca (CAPABLANCA_PRIORITY) lost ~70% of games on time against a
# normal-priority Stockfish, so the whole process tree is lowered: children inherit it.
# Stockfish's default 10 ms Move Overhead then flags under load; 100 ms keeps results about chess.
powershell -NoProfile -Command "(Get-Process -Id $(cat /proc/$$/winpid)).PriorityClass='BelowNormal'" >/dev/null
ELO=${1:-1320}; GAMES=${2:-40}; TC=${3:-10+0.1}; CONC=${5:-6}
ENG=$(cygpath -w "$(realpath ${4:-capablanca.exe})")
FC=tools/fastchess/fastchess-windows-x86-64/fastchess.exe
SF=$(cygpath -w "$(realpath tools/stockfish/stockfish/stockfish-windows-x86-64-avx2.exe)")
STAMP=$(date +%Y%m%d-%H%M%S)-$$
OUTDIR=matches; NAME=vs_sf${ELO}_$STAMP
$FC -engine cmd=$ENG name=Capablanca \
    -engine cmd=$SF name=SF$ELO option.UCI_LimitStrength=true option.UCI_Elo=$ELO \
    "option.Move Overhead=100" \
    -each tc=$TC option.Hash=16 option.Threads=1 \
    -openings file=tools/books/8moves_v3.pgn format=pgn order=random \
    -rounds $((GAMES / 2)) -games 2 -repeat -concurrency $CONC \
    -recover -config outname=$OUTDIR/$NAME.config.json -pgnout file=matches/vs_sf${ELO}_$STAMP.pgn
