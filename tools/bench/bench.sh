#!/usr/bin/env bash
# bench.sh <povray> <reps> <povray args...>: median user-space cycles of the render, minus a 1-pixel run of the same scene.
set -uo pipefail
POV=$1; REPS=$2; shift 2
PCOUNT=${PCOUNT:-$(dirname "$0")/pcount}
[ -x "$PCOUNT" ] || cc -O2 -o "$PCOUNT" "$(dirname "$0")/pcount.c" || exit 1
count() { "$PCOUNT" -- "$POV" "$@" -D 2>&1 >/dev/null | tr '\r' '\n' | awk '$1=="pcount" && $2=="cycles" {print $3}'; }
median() { sort -n | awk '{v[NR]=$1} END {print (NR % 2) ? v[(NR+1)/2] : (v[NR/2] + v[NR/2+1]) / 2}'; }
parse=$(count "$@" +SR1 +ER1 +SC1 +EC1)
full=$(for ((r = 0; r < REPS; r++)); do count "$@"; done | median)
[ -n "$parse" ] && [ -n "$full" ] || { echo 'no counts: is kernel.perf_event_paranoid above 2?' >&2; exit 1; }
awk -v f="$full" -v p="$parse" 'BEGIN {printf "full %.1f Gcycles  parse %.1f  trace %.1f\n", f/1e9, p/1e9, (f-p)/1e9}'
