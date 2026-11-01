#!/bin/sh
# prepared_source_cells.sh <povray> <srcdir>: the source-domain cell budget parses, accepts and rejects.
set -eu
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"
W=32; H=24
scene=$SRCDIR/tests/render/prepared_source_cells.pov
render() {
    name=$1; shift
    "$POVRAY" +i"$scene" +L"$SRCDIR/distribution/include" +w$W +h$H +wt1 -a -d -p -v -gp +fp16 File_Gamma=1 +o"$name.ppm" "$@" >"$name.log" 2>&1 || { cat "$name.log"; exit 1; }
}
brightness() {
    tail -c $(( 6 * W * H )) "$1.ppm" | od -An -v -tu1 -w6 | awk '{ s += ($1 * 256 + $2) / 65535; n++ } END { printf "%.5f", s / n }'
}
# default and lowered budgets both light the floor through the media
render unset
render lowered Declare=Cells=65536
for name in unset lowered; do
    value=$(brightness $name)
    awk -v v="$value" 'BEGIN { exit !(v > 0.002) }' || { cat "$name.log"; echo "prepared_source_cells: $name render is black"; exit 1; }
done
write() {
    { if [ "$2" = pov ]; then echo '#version 3.8;'; else echo '// line 1'; fi
      echo 'global_settings { assumed_gamma 1 }'
      printf '%s\n' "$3"
    } >"$1.$2"
}
# the item is accepted in the 4.0 language too, where the static checker knows it
write accepted pov4 'camera { location <0, 2, -4> look_at 0 right x up y }
global_settings { prepared_source_cells 65536 }'
"$POVRAY" +iaccepted.pov4 +w1 +h1 +wt1 -f -d -p -v -gp >accepted.pov4.log 2>&1 || { cat accepted.pov4.log; exit 1; }
reject() {
    name=$1; value=$2
    for language in pov pov4; do
        write invalid "$language" "global_settings { prepared_source_cells $value }"
        if "$POVRAY" +i"invalid.$language" +w1 +h1 +wt1 -f -d -p -v -gp >"$name.$language.log" 2>&1; then
            echo "prepared_source_cells: expected $name in $language to fail" >&2; exit 1
        fi
        tr '\n' ' ' <"$name.$language.log" | tr -s '[:space:]' ' ' | grep -F "prepared_source_cells must be an integer from 1 to 1000000000." >/dev/null ||
            { cat "$name.$language.log"; exit 1; }
    done
}
reject zero 0
reject negative -5
reject fractional 1.5
reject too_large 1000000001
echo 'prepared_source_cells: ok'
