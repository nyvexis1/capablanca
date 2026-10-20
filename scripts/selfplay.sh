#!/bin/sh
# Self-play A/B test with SPRT. Usage:
#   scripts/selfplay.sh <games> "<optsA>" "<optsB>" [tc] [engineA] [engineB]
# opts are fastchess engine options, e.g. "option.SacrificeVision=false option.Aggression=100"
cd "$(dirname "$0")/.."
GAMES=${1:-400}; OPTA=$2; OPTB=$3; TC=${4:-10+0.1}
A=$(cygpath -w "$(realpath ${5:-capablanca.exe})"); B=$(cygpath -w "$(realpath ${6:-capablanca.exe})")
FC=tools/fastchess/fastchess-windows-x86-64/fastchess.exe
STAMP=$(date +%Y%m%d-%H%M%S)-$$
OUTDIR=matches; NAME=selfplay_$STAMP
$FC -engine cmd=$A name=A $OPTA -engine cmd=$B name=B $OPTB \
    -each tc=$TC option.Hash=16 \
    -openings file=tools/books/8moves_v3.pgn format=pgn order=random \
    -rounds $((GAMES / 2)) -games 2 -repeat -concurrency 12 -recover -config outname=$OUTDIR/$NAME.config.json \
    -sprt elo0=${ELO0:-0} elo1=${ELO1:-10} alpha=0.05 beta=0.05 \
    -pgnout file=matches/selfplay_$STAMP.pgn > matches/selfplay_$STAMP.log 2>&1
grep -E "^Elo|^Games|^LLR|SPRT" matches/selfplay_$STAMP.log | tail -4
