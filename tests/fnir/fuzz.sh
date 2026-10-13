#!/bin/bash
# fuzz.sh BASE NEW SRCDIR FIRST LAST [JOBS]: renders generated scenes FIRST..LAST with two builds and compares the
# 16-bit pixel payloads; one line per seed, exit 1 on a mismatch. Output stays in $FNIR_OUT (default under $TMPDIR).
set -u
if [ "${1:-}" = "--one" ]; then
    S=$2
    O="$FNIR_OUT/$S"
    mkdir -p "$O"
    kind=$(python3 "$SRCDIR/tests/fnir/gen.py" "$S" "$O/s.pov" | cut -d' ' -f1)
    W=48; H=36; THREADS=+WT1
    [ "$kind" = "pigment-batch" ] && [ $((S % 2)) = 0 ] && THREADS=+WT2
    args="+i$O/s.pov +L$SRCDIR/include -w$W -h$H -a -d -p -gp +fp16 $THREADS"
    timeout -s KILL "${BASE_TIMEOUT:-40}" "$BASE" $args +o"$O/base.ppm" > /dev/null 2> "$O/base.log"
    rb=$?
    if [ $rb != 0 ]; then
        printf "%s\t%s\tbase-failed-%s\n" "$S" "$kind" "$rb"
        rm -f "$O"/*.ppm
        exit 0
    fi
    timeout -s KILL "${NEW_TIMEOUT:-300}" "$NEW" $args +o"$O/new.ppm" > /dev/null 2> "$O/new.log"
    rn=$?
    if [ $rn = 124 ] || [ $rn = 137 ]; then r=TIMEOUT
    elif [ $rn != 0 ]; then r=CRASH$rn
    elif cmp -s <(tail -c $((W * H * 6)) "$O/base.ppm") <(tail -c $((W * H * 6)) "$O/new.ppm"); then r=ok
    else r=DIFFER
    fi
    [ $r = ok ] && rm -f "$O"/*.ppm
    printf "%s\t%s\t%s\n" "$S" "$kind" "$r"
    exit 0
fi

if [ $# -lt 5 ]; then
    sed -n '2,3s/^# //p' "$0"
    exit 2
fi
export BASE=$(readlink -f "$1") NEW=$(readlink -f "$2") SRCDIR=$(readlink -f "$3")
export FNIR_OUT=${FNIR_OUT:-$(mktemp -d "${TMPDIR:-/tmp}/fnir-fuzz.XXXXXX")}
mkdir -p "$FNIR_OUT" || exit 2
seq "$4" "$5" | xargs -P "${6:-4}" -I{} "$0" --one {} | sort -n | tee "$FNIR_OUT/results.tsv"
cut -f3 "$FNIR_OUT/results.tsv" | sort | uniq -c >&2
echo "scenes and logs: $FNIR_OUT" >&2
[ -s "$FNIR_OUT/results.tsv" ] && ! grep -qE "DIFFER|CRASH|TIMEOUT" "$FNIR_OUT/results.tsv"
