#!/bin/sh
# bounce_passes.sh <povray> <srcdir>: surface bounce passes add light; the result is alike on 1 and 8 threads, and the
# points and power per area alike with the scene scaled by 1000; a mirror or highlights export nothing, radiosity does not change the export; lumens hold
# the stated power at every pass; a bounce block with a pigment or emission is a parse error.
set -e
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1"); SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d); trap 'rm -rf "$WORK"' EXIT; cd "$WORK"
W=32; H=24
render() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/bounce_passes.pov" +w$W +h$H +pr -a -d -p +v -gp +fp16 File_Gamma=1 \
        +o"$name.ppm" "$@" > "$name.log" 2>&1 || { cat "$name.log"; echo "bounce_passes: $name failed" >&2; exit 1; }
    tail -c $(( 6 * W * H )) "$name.ppm" > "$name.pixels"
}
fail() { echo "bounce_passes: $1" >&2; exit 1; }
sum() { od -An -v -tu2 --endian=big -w6 "$1.pixels" | awk '{ s += $1 + $2 + $3 } END { print s }'; }
# the log with the console's wrapped lines joined
joined() { tr -d '\r' < "$1.log" | awk '/^ / { printf "%s", $0; next } { printf "\n%s", $0 } END { print "" }'; }
# the field power each pass of a wall left, "pass power scale" per line, the left wall first
powers() { joined "$1" | grep -o 'Bounce o[0-9]* field: power [^ ]* before scaling, scaled by [^ ]*' |
    awk '{ sub("o", "", $2); print $2, $5, $10 }'; }
nodes() { joined "$1" | grep -o 'Bounce o1: [0-9]* nodes, [0-9]* levels'; }
render one Declare=Passes=1 +wt4
render two Declare=Passes=2 +wt4
[ "$(sum two)" -gt "$(sum one)" ] || fail "a second pass added no light"
[ "$(powers two | wc -l)" -eq 4 ] || { powers two; fail "two walls over two passes did not each leave two fields"; }
render serial Declare=Passes=2 +wt1
render wide Declare=Passes=2 +wt8
cmp -s serial.pixels wide.pixels || fail "1 and 8 threads differ"
cmp -s serial.pixels two.pixels || fail "1 and 4 threads differ"
# Scaled by 1000, the walls place the same points and export a million times the power, to float precision. (The image
# itself is not compared: a surface light's own sampling at render time differs with scale, bounce or not.)
render big Declare=Passes=2 Declare=Scale=1000 +wt4
[ "$(nodes two)" = "$(nodes big)" ] || { nodes two; nodes big; fail "the scene scaled by 1000 placed different points"; }
powers two > a.txt; powers big > b.txt; paste -d ' ' a.txt b.txt > scaled.txt
awk '{ r = $5 / ($2 * 1e6); if (r < 1 - 1e-5 || r > 1 + 1e-5) exit 1; n++ } END { if (n != 4) exit 1 }' scaled.txt ||
    { cat scaled.txt; fail "the scene scaled by 1000 exported a different power per area"; }
render mirror Declare=Mirror=1
[ "$(powers mirror | awk 'NR == 1 { print ($2 == 0) }')" = 1 ] || { powers mirror; fail "a mirror wall exported light"; }
render highlight Declare=Highlight=1
[ "$(powers highlight | awk 'NR == 1 { print ($2 == 0) }')" = 1 ] || { powers highlight; fail "a wall with highlights only exported light"; }
render plain
render radiosity Declare=Radiosity=1
[ "$(powers plain)" = "$(powers radiosity)" ] || { powers plain; powers radiosity; fail "radiosity changed what the walls export"; }
render lumens Declare=Passes=3 Declare=Lumens=50
powers lumens | awk '{ p = $2 * $3; if (p < 49.999 || p > 50.001) exit 1; n++ } END { if (n != 6) exit 1 }' ||
    { powers lumens; fail "a wall stated in lumens did not hold its power at every pass"; }
render mixed_off Declare=Mixed=1 Declare=Passes=2 Declare=VolumeGain=0
render mixed_on Declare=Mixed=1 Declare=Passes=2
powers mixed_off > mixed_off.txt; powers mixed_on > mixed_on.txt
paste mixed_off.txt mixed_on.txt | awk '
    $1 == 1 && $2 != $5 { exit 1 }
    $1 == 2 && $5 > $2 { added++ }
    END { if (added != 2) exit 1 }' || fail "volume light did not reach the next surface pass"
render volume_one Declare=Mixed=1 Declare=Passes=1 Declare=VolumePasses=1
render volume_two Declare=Mixed=1 Declare=Passes=1 Declare=VolumePasses=2
[ "$(powers volume_one)" = "$(powers volume_two)" ] || fail "a volume pass changed a completed surface pass"
[ "$(sum volume_two)" -gt "$(sum volume_one)" ] || fail "surface light did not reach the next volume order"
render volume_three Declare=Mixed=1 Declare=Passes=1 Declare=VolumePasses=3
cmp -s volume_two.pixels volume_three.pixels || fail "a completed surface field was counted again in a later volume order"
render mixed_lumens Declare=Mixed=1 Declare=Passes=3 Declare=VolumePasses=4 Declare=Lumens=50
powers mixed_lumens | awk '{ p = $2 * $3; if (p < 49.999 || p > 50.001) exit 1; n++ } END { if (n != 6) exit 1 }' ||
    fail "mixed volume passes changed the surface lumens target"
render mixed_ss_off Declare=Mixed=1 Declare=MixedSSLT=1 Declare=Passes=2 Declare=VolumeGain=0
render mixed_ss_on Declare=Mixed=1 Declare=MixedSSLT=1 Declare=Passes=2
powers mixed_ss_off > mixed_ss_off.txt; powers mixed_ss_on > mixed_ss_on.txt
paste mixed_ss_off.txt mixed_ss_on.txt | awk '
    $1 == 1 && $2 != $5 { exit 1 }
    $1 == 2 && $5 > $2 { added++ }
    END { if (added != 2) exit 1 }' || fail "volume light did not reach the next method 2 surface pass"
for input in Pigment Emission; do
    "$POVRAY" +i"$SRCDIR/tests/render/bounce_passes.pov" +w8 +h6 -d -p -f Declare=$input=1 > "$input.log" 2>&1 &&
        fail "a bounce block with $input was accepted"
    grep -q "takes no pigment or emission" "$input.log" || { cat "$input.log"; fail "a bounce block with $input gave the wrong error"; }
done
echo "bounce_passes: pass"
