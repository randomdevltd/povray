#!/bin/sh
set -eu
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"
render() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/surface_part_accuracy.pov" +w32 +h32 +wt2 -a -d -p -v -gp +fh Stochastic_Seed=0 \
        +o"$name.hdr" "$@" >"$name.log" 2>&1 || { cat "$name.log"; exit 1; }
}
for part in 0 1; do
    render "lit0$part" Declare=Union=$part
    render "open0$part" Declare=Union=$part Declare=Shadowless=1
    render "lit1$part" Declare=Subsurface=1 Declare=Union=$part
done
. "$SRCDIR/tests/render/image.sh"
image_awk '
function total(name,    r, g, bl, e, n, i, sum) {
    n = read_hdr(name ".hdr", r, g, bl, e)
    sum = 0
    for (i = 0; i < n; i++) sum += e[i] == 0 ? 0.0 : (r[i] + g[i] + bl[i]) * 2 ^ (e[i] - 136)
    return sum
}
BEGIN {
    for (part = 0; part < 2; part++) {
        lit = total("lit0" part); opened = total("open0" part)
        # Nothing lies between the light and the lit side of a convex receiver, so any shadow there is its own.
        if (!(lit > 0 && abs(lit / opened - 1) < 0.002)) fail("part " part ": lit " lit ", shadowless " opened ", limit 0.2%")
    }
    for (sslt = 0; sslt < 2; sslt++) {
        alone = total("lit" sslt "0"); joined = total("lit" sslt "1")
        if (!(abs(joined / alone - 1) < 0.01)) fail("subsurface " sslt ": alone " alone ", as a union part " joined ", limit 1%")
    }
}'
echo 'surface_part_accuracy: an isosurface lit alone and as a union part shows no self-shadow and the same light'
