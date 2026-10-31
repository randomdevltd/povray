#!/bin/sh
set -eu
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"
render() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/physical_media_phase.pov" +w4 +h4 +wt1 +pr -a -d -p -v -gp +fp16 File_Gamma=1.0 +o"$name.ppm" "$@" >"$name.log" 2>&1 || { cat "$name.log"; exit 1; }
}
for eccentricity in 1 -1 1.5; do
    if "$POVRAY" +i"$SRCDIR/tests/render/physical_media_phase.pov" +w1 +h1 +wt1 -f -d -p -v -gp Declare=HG=1 Declare=Eccentricity="$eccentricity" Declare=SceneVersion=3.8 >eccentricity.log 2>&1; then
        echo "physical_media_phase: eccentricity $eccentricity accepted" >&2; exit 1
    fi
    tr '\n' ' ' <eccentricity.log | tr -s '[:space:]' ' ' | grep -qF 'eccentricity must lie between -1 and 1' || { cat eccentricity.log; exit 1; }
    "$POVRAY" +i"$SRCDIR/tests/render/physical_media_phase.pov" +w1 +h1 +wt1 -f -d -p -v -gp Declare=HG=1 Declare=Eccentricity="$eccentricity" >eccentricity.log 2>&1 || { cat eccentricity.log; exit 1; }
    tr '\n' ' ' <eccentricity.log | tr -s '[:space:]' ' ' | grep -qF 'a scene of version 3.7 or older keeps it' || { cat eccentricity.log; exit 1; }
done
render point Declare=Source=1
render volume16 Declare=Source=2 Declare=Draws=16
render volume64 Declare=Source=2 Declare=Draws=64
render surface_point Declare=Source=1 Declare=Surface=1
render surface_volume Declare=Source=2 Declare=Surface=1 Declare=Draws=1024
render physical
render zero Declare=Zero=1
render density Declare=Density=2
render forward Declare=HG=1
render backward Declare=HG=1 Declare=Reverse=1
render legacy Declare=Method=3
render multiply Declare=Modifier=1
render subtract Declare=Modifier=2
render absorbed Declare=Absorb=1
render absorber_zero Declare=Absorb=1 Declare=Zero=1
. "$SRCDIR/tests/render/image.sh"
image_awk '
function expect(name, r, g, b) {
    NAMES[++COUNT] = name
    EXPECTED[name, 0] = r; EXPECTED[name, 1] = g; EXPECTED[name, 2] = b
}
BEGIN {
    split(".0875 .1375 .15", s, " ")
    point = atan2(100, 1) - atan2(99, 1)
    expect("point", s[1] * 5000.5 * point, s[2] * 5000.5 * point, s[3] * 5000.5 * point)
    expect("volume16", s[1] * 50 / 99, s[2] * 50 / 99, s[3] * 50 / 99)
    expect("volume64", s[1] * 50 / 99, s[2] * 50 / 99, s[3] * 50 / 99)
    expect("surface_point", .5, .5, .5)
    expect("surface_volume", .5, .5, .5)
    expect("physical", .0875, .1375, .15)
    expect("zero", .0875, .1375, .15)
    expect("multiply", s[1] * (.5 + .5 * .5), s[2] * (.5 + .5 * .25), s[3] * (.5 + .5 * .75))
    expect("subtract", s[1] * (1 - .5 * .05 / .3), s[2] * (1 - .5 * .1 / .4), s[3] * (1 - .5 * .1 / .4))
    expect("absorbed", s[1] * exp(-.1), s[2] * exp(-.2), s[3] * exp(-.3))
    expect("absorber_zero", s[1] * exp(-.1), s[2] * exp(-.2), s[3] * exp(-.3))
    expect("density", .1375, .1625, .15)
    expect("forward", .2, .475, .6)
    expect("backward", (.2 + .1 * 2 / 9) / 4, (.1 + .3 * 2 / 9) / 4, .4 * 2 / 9 / 4)
    expect("legacy", 1.299372013 * .3, 1.299372013 * .4, 1.299372013 * .4)
    for (k = 1; k <= COUNT; k++) {
        name = NAMES[k]
        n = read_tail_words(name ".ppm", samples, 48)
        tolerance = name ~ /^(volume|surface)/ ? 0.002 : 0.00015
        line = name
        for (ch = 0; ch < 3; ch++) {
            total = 0
            for (i = ch; i < n; i += 3) total += samples[i]
            value = total / 16 / 65535
            if (!(abs(value - EXPECTED[name, ch]) < tolerance))
                fail(sprintf("%s channel %d: %.9g, expected %.9g within %g", name, ch, value, EXPECTED[name, ch], tolerance))
            line = line sprintf(" %.9g", value)
        }
        print line
    }
}'
