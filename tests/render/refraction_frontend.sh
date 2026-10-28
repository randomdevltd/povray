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
run off 3.7 Declare=Case=10
run signed_negative 3.7 Declare=Case=12
run constant 3.7 Declare=Case=11
run optical_scalar 3.7 Declare=Case=8
tr -s ' \n' '  ' < optical_scalar.log | grep -qF 'Refraction value unnecessary'
refuse zero 'Refraction detail must be' Declare=Case=3
refuse negative 'Refraction detail must be' Declare=Case=4
for version in 3.7 4.0; do
    for case in 6 7 9 13 14 15; do
        if "$POVRAY" +i"$SRCDIR/tests/render/refraction_frontend.pov" +w8 +h8 -f -d -gp Declare=Version="$version" Declare=Case="$case" > invalid.log 2>&1; then
            echo 'refraction_frontend: invalid refraction item accepted' >&2; exit 1
        fi
    done
done
for version in 3.7 4.0; do
    for method in 1 2 3 4; do
        for case in 20 21 22 23; do
            if "$POVRAY" +i"$SRCDIR/tests/render/refraction_frontend.pov" +w8 +h8 -f -d -gp \
                Declare=Version="$version" Declare=Method="$method" Declare=Case="$case" > invalid.log 2>&1; then
                echo 'refraction_frontend: unsupported media context accepted' >&2; exit 1
            fi
        done
    done
done
for method in 1 2 3 4; do
    for file in err_media_refraction err_media_refraction_detail; do
        if "$POVRAY" +i"$SRCDIR/tests/pov4/$file.pov4" +w8 +h8 -f -d -gp Declare=Method="$method" > invalid.log 2>&1; then
            echo 'refraction_frontend: unsupported media context accepted in pov4' >&2; exit 1
        fi
    done
done
"$POVRAY" +i"$SRCDIR/tests/render/refraction_frontend.pov4" +w48 +h36 +WT1 -a -d -p -v -gp +fp16 File_Gamma=1 +opov4.ppm > pov4.log 2>&1 || { cat pov4.log; exit 1; }
same raw pov4
"$POVRAY" +i"$SRCDIR/tests/pov4/refraction_off.pov4" +w48 +h36 +WT1 -a -d -p -v -gp +fp16 File_Gamma=1 +ooff_pov4.ppm > off_pov4.log 2>&1 || { cat off_pov4.log; exit 1; }
same off off_pov4
if grep -q 'Refraction value unnecessary' off_pov4.log; then cat off_pov4.log; exit 1; fi

for version in 3.7 4.0; do
    "$POVRAY" +i"$SRCDIR/tests/render/interior_all_options.pov" +w8 +h8 +q0 -f -d -gp \
        Declare=Version="$version" > "all_options_$version.log" 2>&1 || { cat "all_options_$version.log"; exit 1; }
done
"$POVRAY" +i"$SRCDIR/tests/pov4/interior_all_options.pov4" +w8 +h8 +q0 -f -d -gp \
    > all_options_pov4.log 2>&1 || { cat all_options_pov4.log; exit 1; }
echo 'refraction_frontend: ok'
