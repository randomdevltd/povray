#!/bin/sh
# subsurface_ballistic.sh <povray> <srcdir>: direct transmission through an unlit slab.
set -e
trap 'rm -f ssball_*' EXIT
POVRAY=$1; SRCDIR=$2
render() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/subsurface_ballistic.pov" -d -p -a +q9 +pr +fp16 +w8 +h8 +wt1 File_Gamma=1 +o"ssball_$name.ppm" "$@" > "ssball_$name.log" 2>&1
}
for angle in 0 45 70; do
    render clear$angle Declare=Angle=$angle
    render finite$angle Declare=Angle=$angle Declare=Length=2
    render equal$angle Declare=Angle=$angle Declare=Index=1
    render legacy$angle Declare=Angle=$angle Declare=Method=1
    render conserve$angle Declare=Angle=$angle Declare=Conserve=1
done
render opaque Declare=Length=0.01
render wedge Declare=Wedge=1 +w1 +h1
. "$SRCDIR/tests/render/image.sh"
# Unpolarised Fresnel interfaces and Beer extinction, independent of the renderer.
image_awk '
function transmission(cosine, n_from, n_to,    sine2, rs, rp) {
    sine2 = (n_from / n_to) ^ 2 * (1 - cosine ^ 2)
    if (sine2 >= 1) { TRANSMITTED = 0.0; return 0.0 }
    TRANSMITTED = sqrt(1 - sine2)
    rs = (n_from * cosine - n_to * TRANSMITTED) / (n_from * cosine + n_to * TRANSMITTED)
    rp = (n_to * cosine - n_from * TRANSMITTED) / (n_to * cosine + n_from * TRANSMITTED)
    return 1 - (rs * rs + rp * rp) / 2
}
function check(name, expected, sized,    v, n, i, error) {
    n = read_ppm("ssball_" name ".ppm", v)
    if (sized && (IMAGE_W != 8 || IMAGE_H != 8)) fail(name ": " IMAGE_W "x" IMAGE_H ", expected 8x8")
    error = 0
    for (i = 0; i < n; i++) if (abs(v[i] - expected) > error) error = abs(v[i] - expected)
    if (!(error < 8 / 65535))
        fail(sprintf("%s: expected %.7g, pixels %.7g to %.7g, error %.3g", name, expected, min_of(v, n), max_of(v, n), error))
}
BEGIN {
    PI = atan2(0, -1)
    split("clear finite equal legacy conserve", kinds, " ")
    for (a = 0; a < 3; a++) {
        angle = a == 0 ? 0 : a == 1 ? 45 : 70
        cosine = cos(angle * (PI / 180))
        entry = transmission(cosine, 1.0, 1.44); inside = TRANSMITTED
        exit_ = transmission(inside, 1.44, 1.0); outside = TRANSMITTED
        if (!(abs(outside - cosine) < 1e-12)) fail("angle " angle ": exit cosine " outside " differs from entry " cosine)
        for (k = 1; k <= 5; k++) {
            # At reflectance 1 both inversion tables end at alpha=1, hence sigma_t=1/Length.
            path = 1 / (kinds[k] == "equal" ? cosine : inside)
            expected = exp(-path / (kinds[k] == "finite" ? 2 : 1e9))
            if (kinds[k] != "equal" && kinds[k] != "legacy") expected *= entry * exit_
            check(kinds[k] angle, expected, 1)
        }
    }
    check("opaque", 0.0, 0)
    check("wedge", transmission(1.0, 1.0, 1.44) * transmission(cos(30 * (PI / 180)), 1.44, 1.0) * exp(-1e-9), 0)
    print "subsurface_ballistic: Fresnel interfaces, finite extinction, equal indices and method 1 agree"
}'
