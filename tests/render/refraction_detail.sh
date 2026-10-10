#!/bin/sh
# refraction_detail.sh <povray> <srcdir>: media refraction_detail — the smallest index detail, in mm, that must refract.
set -e
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1"); SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d); trap 'rm -rf "$WORK"' EXIT; cd "$WORK"
W=96; H=72
render() {
    name=$1; shift; version=4.0
    case $1 in Version=*) version=${1#Version=}; shift;; esac
    if [ "$version" = 3.7 ]; then set -- Declare=Atmosphere=1.00029 "$@"; fi
    "$POVRAY" +i"$SRCDIR/tests/render/refraction_detail.pov" +w$W +h$H -a -d -p -v -gp +fp16 File_Gamma=1.0 +o"refraction_detail_$name.ppm" Declare=Version=$version "$@" \
        > "refraction_detail_$name.log" 2>&1 || { cat "refraction_detail_$name.log"; exit 1; }
}
says() {
    tr -s ' \n' '  ' < "refraction_detail_$2.log" | grep -q "$1"
}
# value <name> <col> <row>: red channel of a pixel, 0..1
value() {
    tail -c $(( (W * H - ($3 * W + $2)) * 6 )) "refraction_detail_$1.ppm" | head -c 2 | od -An -tu1 | awk '{ printf "%.4f", ($1 * 256 + $2) / 65535 }'
}
expect() {
    got=$(value $1 $2 $3)
    awk -v a="$got" -v b="$4" -v t="${5:-0.004}" 'BEGIN { exit !(a - b <= t && b - a <= t) }' ||
        { echo "refraction_detail: $1 at $2,$3 is $got, expected $4" >&2; exit 1; }
}
same() {
    for name in $1 $2; do tail -c $(( 6 * W * H )) refraction_detail_$name.ppm > refraction_detail_$name.pixels; done
    cmp -s "refraction_detail_$1.pixels" "refraction_detail_$2.pixels" || { echo "refraction_detail: $1 differs from $2" >&2; exit 1; }
}
differs() {
    if (same $1 $2 2> /dev/null); then echo "refraction_detail: $1 matches $2" >&2; exit 1; fi
}
# region <a> <b> <col0> <col1> <row0> <row1>: the two images agree within 0.004 over a block of pixels
region() {
    for name in $1 $2; do tail -c $(( 6 * W * H )) refraction_detail_$name.ppm | od -An -v -tu1 -w6 > refraction_detail_$name.values; done
    paste -d' ' refraction_detail_$1.values refraction_detail_$2.values | awk -v w=$W -v c0=$3 -v c1=$4 -v r0=$5 -v r1=$6 '
        { c = (NR - 1) % w; r = int((NR - 1) / w) }
        c >= c0 && c <= c1 && r >= r0 && r <= r1 {
            for (k = 1; k <= 5; k += 2) { d = (($k * 256 + $(k + 1)) - ($(k + 6) * 256 + $(k + 7))) / 65535; if (d > 0.004 || d < -0.004) bad++ }
        }
        END { if (bad) { print bad " channels differ"; exit 1 } }' >&2 || { echo "refraction_detail: $1 differs from $2" >&2; exit 1; }
}

# 2 mm seams in a 400 mm plate: without refraction_detail the note points at the step size, and rays dither.
render unset Declare=Case=1 Declare=Detail=0
says "smallest such structure" unset || { echo "refraction_detail: no note without the setting" >&2; exit 1; }
render set Declare=Case=1
if says "smallest such structure" set; then echo "refraction_detail: the note fires with the setting" >&2; exit 1; fi
render medium Declare=Case=3
if says "smallest such structure" medium; then echo "refraction_detail: the note fires with a medium's own setting" >&2; exit 1; fi
differs set unset
region set medium 0 95 0 71
# The setting must not change an image byte where it is absent: the same case on one thread and four agrees with itself.
render threads1 Declare=Case=1 +WT1
render threads4 Declare=Case=1 +WT4
same threads1 threads4
render unset_t1 Declare=Case=1 Declare=Detail=0 +WT1
render unset_t4 Declare=Case=1 Declare=Detail=0 +WT4
same unset_t1 unset_t4
# Detail at or below the seam width refracts it faithfully: a 0.2 mm detail renders the same field.
render fine Declare=Case=1 Declare=Detail=0.2
region set fine 0 95 0 71
render fine_t4 Declare=Case=1 Declare=Detail=0.2 +WT4
same fine fine_t4

# A linear ramp has a scale-free gradient: the setting leaves the deflection where it was.
render ramp Declare=Case=5 Declare=Detail=0
says "smallest such structure" ramp || { echo "refraction_detail: no note for a large medium without the setting" >&2; exit 1; }
render ramp_set Declare=Case=6
region ramp ramp_set 0 95 0 71

# 0.2 mm sine structure under a 1 mm detail refracts as its smooth mean, not as noise.
render sine Declare=Case=7
render mean Declare=Case=8
region sine mean 0 95 0 71
render sine_unset Declare=Case=9 Declare=Detail=0
says "smallest such structure" sine_unset || { echo "refraction_detail: no note for the unset sine" >&2; exit 1; }
differs sine_unset sine
render v37_set Version=3.7 Declare=Case=1
region set v37_set 0 95 0 71
render v37_unset Version=3.7 Declare=Case=1 Declare=Detail=0
if says "smallest such structure" v37_unset; then echo "refraction_detail: the note fired before version 4.0" >&2; exit 1; fi

# Method 4 derives its grid width from the detail, and warns when an explicit resolution is coarser than half of it.
render derived Declare=Case=10 Declare=Detail=6
if says "using classic sampling" derived; then echo "refraction_detail: the derived grid fell back" >&2; exit 1; fi
if says "coarser than half this medium" derived; then echo "refraction_detail: the derived grid warned" >&2; exit 1; fi
render coarse Declare=Case=11
says "coarser than half this medium" coarse || { echo "refraction_detail: no warning for a coarse explicit resolution" >&2; exit 1; }
if says "using classic sampling" coarse; then echo "refraction_detail: the coarse grid fell back" >&2; exit 1; fi
echo "refraction_detail: ok"
