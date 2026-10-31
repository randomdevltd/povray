#!/bin/sh
set -eu
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"
render() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/media_emission_gi.pov" +w12 +h12 +wt1 +pr -a -d -p -v -gp \
        +fp16 File_Gamma=1 +o"$name.ppm" "$@" > "$name.log" 2>&1 || { cat "$name.log"; exit 1; }
}
render appearance_only Declare=Glow=50
render lit_low Declare=ExplicitLight=1 Declare=Sigma=0.25
render lit_high Declare=ExplicitLight=1 Declare=Sigma=0.25 Declare=Glow=50
render modified_low Declare=ExplicitLight=1 Declare=Sigma=0.25 Declare=Modified=1
render modified_high Declare=ExplicitLight=1 Declare=Sigma=0.25 Declare=Modified=1 Declare=Glow=50
render scatter_none Declare=Spot=1
render scatter_on Declare=Spot=1 Declare=Sigma=0.5
render scatter_no_gi Declare=Spot=1 Declare=Sigma=0.5 Declare=Radio=0
render visible_low Declare=ViewGlow=1 Declare=Radio=0 Declare=Glow=0.01
render visible_high Declare=ViewGlow=1 Declare=Radio=0 Declare=Glow=0.1
. "$SRCDIR/tests/render/image.sh"
image_awk '
function mean(name,    v, n) {
    n = read_ppm(name ".ppm", v)
    return sum_of(v, n) / n
}
BEGIN {
    split("appearance_only scatter_none scatter_no_gi", dark, " ")
    for (i = 1; i <= 3; i++) {
        n = read_ppm(dark[i] ".ppm", v)
        if (max_of(v, n) != 0) fail(dark[i] ": mean " mean(dark[i]) ", expected black")
    }
    split("lit_low lit_high modified_low modified_high", pairs, " ")
    for (i = 1; i <= 4; i += 2) {
        na = read_ppm(pairs[i] ".ppm", a)
        nb = read_ppm(pairs[i + 1] ".ppm", b)
        if (!same_values(a, na, b, nb))
            fail(pairs[i] " and " pairs[i + 1] " differ: means " mean(pairs[i]) ", " mean(pairs[i + 1]))
    }
    if (!(mean("scatter_on") > 0.0001)) fail("physical scattered light did not reach the gather: mean " mean("scatter_on"))
    if (!(mean("visible_low") > 0 && mean("visible_high") > 5 * mean("visible_low")))
        fail("visible glow: low mean " mean("visible_low") ", high mean " mean("visible_high") ", expected high above 5x low")
    print "media_emission_gi: appearance excluded from gathering, physical scattering retained, visible glow preserved"
}'
