#!/bin/sh
set -eu
POVRAY=$1
SRCDIR=$2
render() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/progressive_emitter_parity.pov" +w32 +h24 +pr -a -d -p -v -gp \
        +fp16 File_Gamma=1 +o"progressive_emitter_$name.ppm" "$@" \
        > "progressive_emitter_$name.log" 2>&1 || { cat "progressive_emitter_$name.log"; exit 1; }
}
render rectangle Declare=Source=1 +wt1
render oriented Declare=Source=2 +wt1
render oriented_four Declare=Source=2 +wt4
render oriented_power Declare=Source=2 Declare=Power=0.08 +wt2
render open +wt1
render aperture Declare=Aperture=1 +wt1
render blocked Declare=Aperture=2 +wt2
render half Declare=Aperture=3 +wt2
render aperture_four Declare=Aperture=1 +wt4
render aperture_power Declare=Aperture=1 Declare=Power=0.08 +wt2
render wall Declare=Blocker=1 +wt2
render downstream_wall Declare=Aperture=4 Declare=Blocker=2 +wt2
render inside_aperture Declare=Aperture=5 +wt2
render crossings Declare=Aperture=4 Declare=Blocker=1 +wt2
render medium Declare=Medium=1 Declare=MediumTop=3 +wt2
render aperture_medium Declare=Aperture=1 Declare=Medium=1 +wt2
render skipped_medium Declare=SkippedMedium=1 +wt2
render aperture_skipped_medium Declare=Aperture=1 Declare=SkippedMedium=1 +wt2
render oriented_aperture Declare=Source=2 Declare=Aperture=4 Declare=Blocker=1 +wt2
render oriented_open Declare=Source=2 +wt2
render parallel Declare=Source=3 +wt1
render parallel_aperture Declare=Source=3 Declare=Aperture=4 Declare=Blocker=1 +wt4
for name in open aperture blocked crossings oriented; do
    case "$name" in
        open) options='' ;;
        aperture) options='Declare=Aperture=1' ;;
        blocked) options='Declare=Aperture=2' ;;
        crossings) options='Declare=Aperture=4 Declare=Blocker=1' ;;
        oriented) options='Declare=Source=2' ;;
    esac
    render "classic_$name" Declare=Method=1 +wt2 $options
done
. "$SRCDIR/tests/render/image.sh"
image_awk '
function values(name, v) {
    return read_tail_words("progressive_emitter_" name ".ppm", v, 32 * 24 * 3)
}
function power(name,    v) {
    return sum_of(v, values(name, v))
}
function equal(a, b, tolerance,    va, vb, n, i, error) {
    n = values(a, va)
    values(b, vb)
    error = 0
    for (i = 0; i < n; i++) if (abs(va[i] - vb[i]) > error) error = abs(va[i] - vb[i])
    if (!(error <= tolerance)) fail(a " and " b " differ by " error ", limit " tolerance)
}
BEGIN {
    equal("rectangle", "oriented", 0)
    equal("oriented", "oriented_four", 0)
    equal("aperture", "aperture_four", 0)
    count = split("open aperture open crossings open inside_aperture oriented_open oriented_aperture parallel parallel_aperture " \
        "medium aperture_medium open aperture_skipped_medium classic_open classic_aperture classic_open classic_crossings", pairs, " ")
    for (i = 1; i < count; i += 2) equal(pairs[i], pairs[i + 1], 2)
    count = split("oriented aperture medium parallel classic_oriented", names, " ")
    for (i = 1; i <= count; i++) if (!(power(names[i]) > 100)) fail(names[i] ": power " power(names[i]) ", expected above 100")
    count = split("blocked wall downstream_wall classic_blocked", names, " ")
    for (i = 1; i <= count; i++) if (power(names[i]) != 0) fail(names[i] ": power " power(names[i]) ", expected 0")
    split("oriented oriented_power aperture aperture_power", pairs, " ")
    for (i = 1; i < 4; i += 2) {
        n = values(pairs[i + 1], high)
        ratio = sum_of(high, n) / power(pairs[i])
        if (!(max_of(high, n) < 65535 && 1.99 < ratio && ratio < 2.01))
            fail(pairs[i] " to " pairs[i + 1] ": power ratio " ratio ", peak " max_of(high, n))
    }
    ratio = power("half") / power("open")
    if (!(0.4 < ratio && ratio < 0.6)) fail("half aperture power: " ratio)
    if (!(power("medium") < 0.9 * power("open"))) fail("medium power " power("medium") " is not below 0.9 of open " power("open"))
    if (!(power("skipped_medium") < 0.9 * power("open")))
        fail("skipped_medium power " power("skipped_medium") " is not below 0.9 of open " power("open"))
    printf "half aperture / open power: %.6f\n", ratio
}'
rm -f progressive_emitter_*.ppm progressive_emitter_*.log progressive_emitter_*.pov-state
echo 'progressive_emitter_parity: active sources, power, threads, aperture misses, CSG crossings, blockers and interiors passed'
