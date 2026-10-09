#!/bin/sh
# media_refraction.sh <povray> <srcdir>: media refraction, curved rays and the precedence of surface indices.
set -e
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1"); SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d); trap 'rm -rf "$WORK"' EXIT; cd "$WORK"
W=96; H=72
render() {
    name=$1; shift; version=4.0
    case $1 in Version=*) version=${1#Version=}; shift;; esac
    "$POVRAY" +i"$SRCDIR/tests/render/media_refraction.pov" +w$W +h$H -a -d -p -v -gp +fp16 File_Gamma=1.0 +o"media_refraction_$name.ppm" Declare=Version=$version "$@" \
        > "media_refraction_$name.log" 2>&1 || { cat "media_refraction_$name.log"; exit 1; }
}
says() {
    tr -s ' \n' '  ' < "media_refraction_$2.log" | grep -q "$1"
}
# value <name> <col> <row>: red channel of a pixel, 0..1
value() {
    tail -c $(( (W * H - ($3 * W + $2)) * 6 )) "media_refraction_$1.ppm" | head -c 2 | od -An -tu1 | awk '{ printf "%.4f", ($1 * 256 + $2) / 65535 }'
}
expect() {
    got=$(value $1 $2 $3)
    awk -v a="$got" -v b="$4" -v t="${5:-0.004}" 'BEGIN { exit !(a - b <= t && b - a <= t) }' ||
        { echo "media_refraction: $1 at $2,$3 is $got, expected $4" >&2; exit 1; }
}
same() {
    for name in $1 $2; do tail -c $(( 6 * W * H )) media_refraction_$name.ppm > media_refraction_$name.pixels; done
    cmp -s "media_refraction_$1.pixels" "media_refraction_$2.pixels" || { echo "media_refraction: $1 differs from $2" >&2; exit 1; }
}
differs() {
    if (same $1 $2 2> /dev/null); then echo "media_refraction: $1 matches $2" >&2; exit 1; fi
}
# region <a> <b> <col0> <col1> <row0> <row1>: the two images agree within 0.004 over a block of pixels
region() {
    for name in $1 $2; do tail -c $(( 6 * W * H )) media_refraction_$name.ppm | od -An -v -tu1 -w6 > media_refraction_$name.values; done
    paste -d' ' media_refraction_$1.values media_refraction_$2.values | awk -v w=$W -v c0=$3 -v c1=$4 -v r0=$5 -v r1=$6 '
        { c = (NR - 1) % w; r = int((NR - 1) / w) }
        c >= c0 && c <= c1 && r >= r0 && r <= r1 {
            for (k = 1; k <= 5; k += 2) { d = (($k * 256 + $(k + 1)) - ($(k + 6) * 256 + $(k + 7))) / 65535; if (d > 0.004 || d < -0.004) bad++ }
        }
        END { if (bad) { print bad " channels differ"; exit 1 } }' >&2 || { echo "media_refraction: $1 differs from $2" >&2; exit 1; }
}

render constant Declare=Case=1
render uniform Declare=Case=2
region constant uniform 0 95 0 71
says "Media refraction changes by 0.5 across a surface" constant || { echo "media_refraction: no jump warning" >&2; exit 1; }

render lens Declare=Case=3
render none Declare=Case=4
says "Curved Rays" lens || { echo "media_refraction: the lens does not curve rays" >&2; exit 1; }
if says "across a surface" lens; then echo "media_refraction: a faded density warns of a jump" >&2; exit 1; fi
# Rows where the box holds no density: neither refraction nor Fresnel reflection shows.
for name in lens none; do tail -c $(( 6 * W * H )) media_refraction_$name.ppm | head -c $(( 6 * W * 4 )) > media_refraction_${name}_top; done
cmp -s media_refraction_lens_top media_refraction_none_top || { echo "media_refraction: the faded box shows at its edge" >&2; exit 1; }
if says "Curved Rays" none; then echo "media_refraction: curved rays without media refraction" >&2; exit 1; fi

render rise Declare=Case=5 Declare=Ramp=1
render prepared Declare=Case=6 Declare=Ramp=1
render ramp Declare=Case=4 Declare=Ramp=1
expect ramp 48 36 0.4948
expect rise 48 36 0.5918 0.006
expect prepared 48 36 0.5918 0.006

for blend in 7:subtract 8:multiply 9:replace; do
    render ${blend#*:} Declare=Case=${blend%:*} Declare=Ramp=1
    region ${blend#*:} ramp 42 54 30 42
done
render outranked Declare=Case=10 Declare=Ramp=1
region outranked rise 44 52 32 40

render hidden Declare=Case=11
render water Declare=Case=15
render inside Declare=Case=12
render ball Declare=Case=13
same hidden water
says "placed after it does not refract" hidden || { echo "media_refraction: no warning for an outranked surface" >&2; exit 1; }
differs inside water
render late_backdrop Declare=Case=16
render air_box Declare=Case=17
same late_backdrop ball
same air_box ball
for case in 18:bubble 19:inside_glass; do
    render ${case#*:} Declare=Case=${case%:*}
    render ${case#*:}_37 Version=3.7 Declare=Case=${case%:*}
    region ${case#*:} ${case#*:}_37 0 95 0 71
done
render hidden_37 Version=3.7 Declare=Case=11
render inside_37 Version=3.7 Declare=Case=12
region hidden_37 inside_37 0 95 0 71

render only Declare=Case=14
if says "Media Samples" only; then echo "media_refraction: a refraction-only medium is sampled" >&2; exit 1; fi
echo "media_refraction: ok"
