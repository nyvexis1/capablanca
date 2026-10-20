#!/bin/sh
# Deterministic regression checks: FEN | expected bestmove | expected score prefix
# Waits for bestmove (up to 30 s) and requires the full depth to have completed.
cd "$(dirname "$0")/.."
ENG=${1:-./capablanca.exe}
DEPTH=8
TMP=$(mktemp)
fail=0
while IFS='|' read -r fen move score; do
    : > "$TMP"
    (printf "position fen $fen\ngo depth $DEPTH\n"
     i=0; while [ $i -lt 300 ] && ! grep -q bestmove "$TMP"; do sleep 0.1; i=$((i + 1)); done
     printf "quit\n") | $ENG > "$TMP"
    out=$(cat "$TMP")
    best=$(echo "$out" | grep bestmove | awk '{print $2}')
    last=$(echo "$out" | grep "^info depth" | tail -1)
    sc=$(echo "$last" | sed 's/.* score \([a-z]* -*[0-9]*\).*/\1/')
    d=$(echo "$last" | awk '{print $3}')
    case "$sc" in "$score"*) ok=1;; *) ok=0;; esac
    # A mate found early may end the search before DEPTH; otherwise the full depth must be reached.
    case "$sc" in mate*) ;; *) [ "$d" = "$DEPTH" ] || ok=0;; esac
    if [ -n "$best" ] && [ "$best" = "$move" ] && [ $ok = 1 ]; then echo "PASS $move ($sc, depth $d)  $fen"
    else echo "FAIL got ${best:-no bestmove} ($sc, depth $d), want $move ($score)  $fen"; fail=1; fi
done <<'LIST'
7k/5K1n/7B/8/8/8/8/8 w - - 0 1|h6g7|mate 1
7k/8/5KQ1/8/8/8/8/8 w - - 99 1|g6g7|mate 1
6k1/5ppp/8/8/8/8/5PPP/3R2K1 w - - 0 1|d1d8|mate 1
LIST
rm -f "$TMP"
exit $fail
