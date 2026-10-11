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
# render_file <name> <scene file> <args...>: like render, for a scene copied into the work directory
render_file() {
    name=$1; scene=$2; shift 2
    "$POVRAY" +i"$scene" +w$W +h$H -a -d -p -v -gp +fp16 File_Gamma=1.0 +o"refraction_detail_$name.ppm" Declare=Version=4.0 "$@" \
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

# 2 mm seams in a 400 mm plate: auto (the default, setting omitted or named) derives its detail from the plate's
# bounds, notes so once, and 12.5 mm (an extent of 4 units over 32) is that auto detail written out.
render unset Declare=Case=1 Declare=Detail=0
says "renders with refraction_detail auto" unset || { echo "refraction_detail: no auto note without the setting" >&2; exit 1; }
render auto Declare=Case=1 Declare=Detail=0 Declare=Auto=1
same unset auto
render auto_12_5 Declare=Case=1 Declare=Detail=12.5
same unset auto_12_5
render set Declare=Case=1
if says "renders with refraction_detail auto" set; then echo "refraction_detail: the auto note fires with the setting" >&2; exit 1; fi
render medium Declare=Case=3
if says "renders with refraction_detail auto" medium; then echo "refraction_detail: the auto note fires with a medium's own setting" >&2; exit 1; fi
differs set unset
region set medium 0 95 0 71
# One thread and four agree with itself, under the setting and under auto.
render threads1 Declare=Case=1 +WT1
render threads4 Declare=Case=1 +WT4
same threads1 threads4
render auto_t1 Declare=Case=1 Declare=Detail=0 +WT1
render auto_t4 Declare=Case=1 Declare=Detail=0 +WT4
same auto_t1 auto_t4
# Details well below the 2 mm seam width refract it the same; the averaging span D/2 is part of the
# detail, so a detail near the seam width smooths its edges where a finer one does not.
render fine Declare=Case=1 Declare=Detail=0.2
render finer Declare=Case=1 Declare=Detail=0.05
region fine finer 0 95 0 71
render fine_t4 Declare=Case=1 Declare=Detail=0.2 +WT4
same fine fine_t4

# A linear ramp has a scale-free gradient, so details well below its variation scale leave the
# deflection where it was; auto still notes what it derived.
render ramp Declare=Case=5 Declare=Detail=0
says "renders with refraction_detail auto" ramp || { echo "refraction_detail: no auto note for a large medium" >&2; exit 1; }
render ramp_set Declare=Case=6
render ramp_fine Declare=Case=6 Declare=Detail=0.2
region ramp_set ramp_fine 0 95 0 71

# 0.3 mm sine structure under a 1 mm detail refracts as its smooth mean, not as noise; auto's 12.5 mm detail
# averages it the same way, which is the point of a default.
render sine Declare=Case=7
render mean Declare=Case=8
region sine mean 0 95 0 71
render sine_auto Declare=Case=9 Declare=Detail=0
region sine_auto mean 0 95 0 71
# No version gates the feature: the auto note sounds and the detail works the same before 4.0.
render v37_set Version=3.7 Declare=Case=1
region set v37_set 0 95 0 71
render v37_auto Version=3.7 Declare=Case=1 Declare=Detail=0
says "renders with refraction_detail auto" v37_auto || { echo "refraction_detail: no auto note before version 4.0" >&2; exit 1; }

# Method 4 derives its grid width from the detail, and warns when an explicit resolution is coarser than half of it.
render derived Declare=Case=10 Declare=Detail=6
if says "using classic sampling" derived; then echo "refraction_detail: the derived grid fell back" >&2; exit 1; fi
if says "coarser than half this medium" derived; then echo "refraction_detail: the derived grid warned" >&2; exit 1; fi
render coarse Declare=Case=11
says "coarser than half this medium" coarse || { echo "refraction_detail: no warning for a coarse explicit resolution" >&2; exit 1; }
if says "using classic sampling" coarse; then echo "refraction_detail: the coarse grid fell back" >&2; exit 1; fi

# A grid beyond the cell limit is coarsened to the widest step that fits, never dropped to classic sampling.
render overcap Declare=Case=12
says "density grid at the requested resolution" overcap || { echo "refraction_detail: no coarsening message for an over-limit grid" >&2; exit 1; }
says "Set prepared_media_cells to at least" overcap || { echo "refraction_detail: the coarsening message names no cap to set" >&2; exit 1; }
if says "using classic sampling" overcap; then echo "refraction_detail: the over-limit grid fell back" >&2; exit 1; fi
# The fitted step is the bisection the renderer runs, replicated here; written explicitly it renders byte-identically.
fitted=$(awk 'function ce(x) { return (x > int(x)) ? int(x) + 1 : int(x) }
    BEGIN { lo = 0.028; hi = 4; limit = 1000000;
        for (i = 0; i < 48; i++) { c = (lo + hi) * 0.5;
            if (ce(4 / c) * ce(3 / c) * ce(2 / c) > limit) lo = c; else hi = c }
        printf "%.17g", hi * 1.00001 }')
says "built at $(printf '%.6g' "$fitted" 2>/dev/null || awk -v f="$fitted" 'BEGIN { printf "%.6g", f }') instead" overcap ||
    { echo "refraction_detail: the fitted step the renderer chose is not the one computed here ($fitted)" >&2; exit 1; }
sed "s/#declare Res = 0.028;/#declare Res = $fitted;/" "$SRCDIR/tests/render/refraction_detail.pov" > refraction_detail_fitted.pov
render_file fitted_explicit "$WORK/refraction_detail_fitted.pov" Declare=Case=12
same overcap fitted_explicit
# A higher limit, from the scene or from the command line, keeps the requested grid: the larger applies.
# Ten million covers the density grid and the parallel light's optical-depth table together.
render cap_scene Declare=Case=12 Declare=Cells=10000000
render cap_machine Declare=Case=12 Prepared_Media_Cells=10000000
same cap_scene cap_machine
if says "coarsened its preparation" cap_scene; then echo "refraction_detail: the scene cap did not lift the limit" >&2; exit 1; fi
if says "coarsened its preparation" cap_machine; then echo "refraction_detail: the machine option did not lift the limit" >&2; exit 1; fi

# A per-light optical-depth table beyond the cell limit is coarsened the same way, not dropped; a scene cap
# of ten million holds its 3.2 million cells without coarsening.
render optical Declare=Case=13
says "optical-depth grid at the requested resolution" optical ||
    { echo "refraction_detail: no coarsening message for an over-limit optical-depth table" >&2; exit 1; }
says "Set prepared_media_cells to at least" optical ||
    { echo "refraction_detail: the optical coarsening message names no cap to set" >&2; exit 1; }
if says "using classic sampling" optical; then echo "refraction_detail: the over-limit optical table fell back" >&2; exit 1; fi
render optical_cap Declare=Case=13 Declare=Cells=10000000
if says "coarsened its preparation" optical_cap; then echo "refraction_detail: the scene cap did not lift the optical table's limit" >&2; exit 1; fi

# A march longer than the step budget runs coarsened at a wider resolution rather than falling back.
render steps Declare=Case=14
says "runs coarsened at resolution" steps || { echo "refraction_detail: no coarsening message for a long march" >&2; exit 1; }
if says "using classic sampling" steps; then echo "refraction_detail: the long march fell back" >&2; exit 1; fi
echo "refraction_detail: ok"
