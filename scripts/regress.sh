#!/bin/sh
# Deterministic regression checks: FEN | expected bestmove | expected score prefix
cd "$(dirname "$0")/.."
ENG=${1:-./capablanca.exe}
fail=0
while IFS='|' read -r fen move score; do
    out=$( (printf "position fen $fen\ngo depth 8\n"; sleep 1; printf "quit\n") | $ENG )
    best=$(echo "$out" | grep bestmove | awk '{print $2}')
    sc=$(echo "$out" | grep "^info depth" | tail -1 | sed 's/.* score \([a-z]* -*[0-9]*\).*/\1/')
    case "$sc" in "$score"*) ok=1;; *) ok=0;; esac
    if [ "$best" = "$move" ] && [ $ok = 1 ]; then echo "PASS $move ($sc)  $fen"; else echo "FAIL got $best ($sc), want $move ($score)  $fen"; fail=1; fi
done <<'LIST'
7k/5K1n/7B/8/8/8/8/8 w - - 0 1|h6g7|mate 1
7k/8/5KQ1/8/8/8/8/8 w - - 99 1|g6g7|mate 1
6k1/5ppp/8/8/8/8/5PPP/3R2K1 w - - 0 1|d1d8|mate 1
LIST
exit $fail
