#!/bin/sh
set -eu
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"
run() {
    name=$1; version=$2; shift 2
    "$POVRAY" +i"$SRCDIR/tests/render/refraction_frontend.pov" +w48 +h36 +WT1 -a -d -p -v -gp +fp16 File_Gamma=1 +o"$name.ppm" Declare=Version="$version" "$@" > "$name.log" 2>&1 || { cat "$name.log"; exit 1; }
}
same() {
    for name in "$1" "$2"; do tail -c 10368 "$name.ppm" > "$name.pixels"; done
    cmp "$1.pixels" "$2.pixels"
}
refuse() {
    name=$1; message=$2; shift 2
    if "$POVRAY" +i"$SRCDIR/tests/render/refraction_frontend.pov" +w8 +h8 +WT1 -d -p -gp +o"$name.ppm" Declare=Version=3.7 "$@" > "$name.log" 2>&1; then
        echo "refraction_frontend: $name accepted" >&2; exit 1
    fi
    tr -s ' \n' '  ' < "$name.log" | grep -qF "$message"
}
run raw 3.7
run signed 3.7 Declare=Case=1
same raw signed
run doubled 3.7 Declare=Case=2
same raw doubled
run units 3.7 Declare=Units=1000
same raw units
run modern 4.0
same raw modern
run auto 3.7 Declare=Case=5
run pigment 3.7 Declare=Case=6
run constant 3.7 Declare=Case=9
run legacy 3.7 Declare=Case=8
tr -s ' \n' '  ' < legacy.log | grep -qF 'Refraction value unnecessary'
run atmosphere 3.7 Declare=Case=10
tr -s ' \n' '  ' < atmosphere.log | grep -qF 'Atmospheric media do not refract'
tr -s ' \n' '  ' < atmosphere.log | grep -qF 'Media refraction is a compatibility setting'
refuse zero 'Refraction detail must be' Declare=Case=3
refuse negative 'Refraction detail must be' Declare=Case=4
refuse modifiers 'accepts no pattern modifiers' Declare=Case=7
"$POVRAY" +i"$SRCDIR/tests/render/refraction_frontend.pov4" +w48 +h36 +WT1 -a -d -p -v -gp +fp16 File_Gamma=1 +opov4.ppm > pov4.log 2>&1 || { cat pov4.log; exit 1; }
same raw pov4
echo 'refraction_frontend: ok'
