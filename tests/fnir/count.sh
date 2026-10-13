#!/bin/bash
# count.sh BASE NEW SRCDIR FIRST LAST [JOBS]: user-space instructions (ICOUNT=tools/bench/icount) to render each
# generated scene at 48x36 on one thread, minus a 1x1 render of it for parsing; prints seed, kind, base, new.
set -u
if [ "${1:-}" = "--one" ]; then
    S=$2
    O="$FNIR_OUT/$S"
    mkdir -p "$O"
    kind=$(python3 "$SRCDIR/tests/fnir/gen.py" "$S" "$O/s.pov" | cut -d' ' -f1)
    set -o pipefail
    run() {
        timeout "$4" "$ICOUNT" -q "$1" +i"$O/s.pov" +L"$SRCDIR/include" -w$2 -h$3 -a -d -p -gp \
            +fp16 +WT1 +o"$O/count.ppm" 2>&1 > /dev/null | tail -1
    }
    render() {
        local full parse
        full=$(run "$1" 48 36 "$2") && parse=$(run "$1" 1 1 "$2") || return 1
        case "$full$parse" in *[!0-9]*|'') return 1;; esac
        echo $((full - parse))
    }
    # scenes the base build cannot render in BASE_TIMEOUT seconds are left out, as fuzz.sh does
    b=$(render "$BASE" "${BASE_TIMEOUT:-60}") && n=$(render "$NEW" "${NEW_TIMEOUT:-900}") &&
        printf "%s\t%s\t%s\t%s\n" "$S" "$kind" "$b" "$n"
    rm -f "$O/count.ppm"
    exit 0
fi

if [ $# -lt 5 ] || [ -z "${ICOUNT:-}" ]; then
    sed -n '2,3s/^# //p' "$0"
    exit 2
fi
export BASE=$(readlink -f "$1") NEW=$(readlink -f "$2") SRCDIR=$(readlink -f "$3") ICOUNT=$(readlink -f "$ICOUNT")
export FNIR_OUT=${FNIR_OUT:-$(mktemp -d "${TMPDIR:-/tmp}/fnir-count.XXXXXX")}
mkdir -p "$FNIR_OUT" || exit 2
seq "$4" "$5" | xargs -P "${6:-4}" -I{} "$0" --one {} | sort -n
