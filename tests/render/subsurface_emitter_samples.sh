#!/bin/sh
set -eu
POVRAY=$1; SRCDIR=$2
trap 'rm -f ssem_*' EXIT
render() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/subsurface_emitter_samples.pov" +L"$SRCDIR/distribution/include" \
        +w24 +h18 +wt2 +pr -a -d -p +fp +o"ssem_$name.ppm" "$@" > "ssem_$name.log" 2>&1
}
draws() { sed -n 's/.*Method 2 irradiance: \([0-9]*\) emitter draws.*/\1/p' "ssem_$1.log"; }
fail() { echo "subsurface_emitter_samples: $*"; exit 1; }
for cloud in 0 1; do
    render four Declare=Samples=4 Declare=Cloud=$cloud
    render many Declare=Samples=64 Declare=Cloud=$cloud
    render scaled Declare=Samples=16 Declare=Multiplier=4 Declare=Cloud=$cloud
    four=$(draws four); many=$(draws many); scaled=$(draws scaled)
    [ -n "$four" ] && [ "$four" -gt 0 ] || fail "no method 2 emitter draws in cloud mode $cloud"
    [ "$many" -eq $((four * 16)) ] || fail "4 to 64 samples drew $four to $many in cloud mode $cloud"
    [ "$scaled" -eq "$many" ] || fail "finish multiplier drew $scaled versus $many in cloud mode $cloud"
    cmp -s ssem_many.ppm ssem_scaled.ppm || fail "equivalent budgets produced different pixels in cloud mode $cloud"
    tr -d ' \r\n' < ssem_four.log > ssem_four.txt
    if [ "$cloud" -eq 0 ]; then
        grep -q ';0cells,0pointsbuilt' ssem_four.txt || fail "the local case built a cloud"
    else
        grep -q ';[1-9][0-9]*cells,[1-9][0-9]*pointsbuilt' ssem_four.txt || fail "the stored case built no cloud"
    fi
    echo "subsurface_emitter_samples: cloud mode $cloud actual draws $four at 4 samples, $many at 64"
done
for source in 1 2; do
    render classic Declare=Source=$source
    render classic_scaled Declare=Source=$source Declare=Multiplier=4
    [ "$(draws classic)" -eq 0 ] || fail "a classic light counted as an emitter"
    cmp -s ssem_classic.ppm ssem_classic_scaled.ppm || fail "finish multiplier changed classic light $source"
done
echo "subsurface_emitter_samples: finish multiplier and classic lights pass"
