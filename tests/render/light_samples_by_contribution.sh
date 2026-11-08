#!/bin/sh
set -eu
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"
SCENE="$SRCDIR/tests/render/light_samples_by_contribution.pov"
render() {
    name=$1; scene=$2; shift 2
    "$POVRAY" +i"$scene" +w24 +h18 +wt2 +pr -a -d -p -gp +fp16 File_Gamma=1 +o"$name.ppm" "$@" >"$name.log" 2>&1 || { cat "$name.log"; exit 1; }
    tail -c 2592 "$name.ppm" >"$name.rgb"
}
fail() { echo "light_samples_by_contribution: $*" >&2; exit 1; }
same() { cmp "$1.rgb" "$2.rgb" || fail "$1 and $2 differ"; }
draws() { awk '/Surface Draws:/ { print $NF }' "$1.log"; }
shadows() { awk '/Shadow Ray Tests:/ { print $4 }' "$1.log"; }
mean() { od -An -v -tu1 "$1.rgb" | awk '{ for (i = 1; i <= NF; i++) if (n++ % 2) s += hi * 256 + $i; else hi = $i } END { print s / (n / 2) / 65535 }'; }
fewer() { awk -v a="$2" -v b="$3" 'BEGIN { exit !(a < b) }' || fail "$1: $2 is not fewer than $3"; }

# before 4.0 a finish samples in full unless it says by_contribution; light_samples without a mode keeps the default
render none "$SCENE"
render plain "$SCENE" Declare=Mode=1
same none plain
render full "$SCENE" Declare=Mode=4
same none full
# by_contribution takes fewer surface-light draws and classic area-light shadow rays where hits are seen through others
render contribution "$SCENE" Declare=Mode=2
[ -n "$(draws none)" ] || fail "no surface draw statistics"
fewer "surface draws" "$(draws contribution)" "$(draws none)"
fewer "shadow rays" "$(shadows contribution)" "$(shadows none)"
# from 4.0 a finish samples by contribution unless it says full; light_samples without a mode keeps that default
render v40 "$SCENE" Declare=Version=4.0
render v40_contribution "$SCENE" Declare=Version=4.0 Declare=Mode=2
render v40_plain "$SCENE" Declare=Version=4.0 Declare=Mode=1
render v40_full "$SCENE" Declare=Version=4.0 Declare=Mode=4
same v40 v40_contribution
same v40 v40_plain
[ "$(draws v40)" = "$(draws v40_contribution)" ] || fail "4.0 without a mode drew $(draws v40), not by_contribution's $(draws v40_contribution)"
fewer "4.0 default surface draws" "$(draws v40)" "$(draws v40_full)"
# a finish's own light_samples full opts out of a default that samples by contribution
render opt_out "$SCENE" Declare=Mode=2 Declare=OptOut=1
same none opt_out
# the weight follows the ray's path alone, so the thread count does not move a pixel
render contribution_one "$SCENE" Declare=Mode=2 +wt1
render contribution_four "$SCENE" Declare=Mode=2 +wt4
same contribution contribution_one
same contribution contribution_four
# a floor seen only directly carries the whole pixel everywhere: by_contribution is full sampling
render floor "$SCENE" Declare=Slab=0 Declare=Mirror=0
render floor_contribution "$SCENE" Declare=Slab=0 Declare=Mirror=0 Declare=Mode=2
same floor floor_contribution
[ "$(draws floor)" = "$(draws floor_contribution)" ] || fail "a directly seen floor drew $(draws floor_contribution), not $(draws floor)"
[ "$(shadows floor)" = "$(shadows floor_contribution)" ] || fail "a directly seen floor traced $(shadows floor_contribution) shadow rays, not $(shadows floor)"
# a clear slab the camera sees directly is not starved: alone under the surface light with nothing traced past it, it
# draws exactly what full sampling draws (its opacity of 0.1 would have left it 2 of 16)
SLAB="Declare=Floor=0 Declare=Mirror=0 Declare=Classic=0 Declare=Depth=1"
render slab "$SCENE" $SLAB
render slab_contribution "$SCENE" $SLAB Declare=Mode=2
same slab slab_contribution
[ "$(draws slab)" = "$(draws slab_contribution)" ] || fail "a directly seen clear slab drew $(draws slab_contribution), not $(draws slab)"
# layers of one hit share an area light's shadow: a heavy opaque floor under a light clear layer samples it again on its own
# grid, not on the clear layer's coarse one (a forced full 9x9 grid, so shadow rays count grid points)
GRID="Declare=Slab=0 Declare=Mirror=0 Declare=Panel=0 Declare=Jitter=0 Declare=Adaptive=4 Declare=Mode=2"
render single "$SCENE" $GRID
render layered "$SCENE" $GRID Declare=Layers=2
awk -v a="$(shadows layered)" -v b="$(shadows single)" 'BEGIN { exit !(a >= b) }' ||
    fail "the layered floor traced $(shadows layered) shadow rays, fewer than the floor alone's $(shadows single)"
# fewer draws add noise, not bias: with many draws the surface light's image means agree
render fine "$SCENE" Declare=Classic=0 Declare=Samples=2048
render fine_contribution "$SCENE" Declare=Classic=0 Declare=Samples=2048 Declare=Mode=2
awk -v a="$(mean fine)" -v b="$(mean fine_contribution)" 'BEGIN { r = a / b; exit !(r > 0.995 && r < 1.005) }' ||
    fail "by_contribution mean $(mean fine_contribution) is not within 0.5% of full $(mean fine)"
# the 4.0 language samples by contribution unstated and lowers to the same render
render pov4 "$SRCDIR/tests/render/light_samples_by_contribution.pov4"
render mirror_contribution "$SCENE" Declare=Slab=0 Declare=Mode=2
same mirror_contribution pov4
render mirror "$SCENE" Declare=Slab=0
fewer "4.0 surface draws" "$(draws pov4)" "$(draws mirror)"
# by_contribution is a value of light_samples, not a finish item of its own
printf 'sphere { 0, 1 finish { by_contribution } }\n' >bare.pov4
if "$POVRAY" +ibare.pov4 +w8 +h8 -a -d -p -f >bare.log 2>&1; then
    fail "a bare by_contribution was accepted"
fi
{ grep -q "Unknown name" bare.log && grep -q "'by_contribution'" bare.log; } || { cat bare.log; fail "wrong error for a bare by_contribution"; }
# an unknown word after light_samples is refused
if "$POVRAY" +i"$SCENE" +w8 +h8 -a -d -p -f Declare=Mode=3 >bad.log 2>&1; then
    fail "light_samples 1 sometimes was accepted"
fi
grep -q "Parse Error" bad.log || { cat bad.log; fail "no parse error for an unknown word"; }
