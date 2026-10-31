#!/bin/sh
# photon_target_weights.sh <povray> <srcdir>: method 2 moves a light's samples between its targets by weight, never its light.
set -eu
POVRAY=$1
SRCDIR=$2
trap 'rm -f photon_target_weights_*' EXIT
render() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/photon_target_rig.pov" +w160 +h90 -a -d -p +v -gp +fp16 File_Gamma=1 \
        +o"photon_target_weights_$name.ppm" Declare=PhotonQuality=0.25 Declare=Bands=3 Declare=PhotonOnly=1 "$@" \
        > "photon_target_weights_$name.log" 2>&1 || { cat "photon_target_weights_$name.log"; exit 1; }
    tail -c 86400 "photon_target_weights_$name.ppm" > "photon_target_weights_$name.px"
    tr '\n' ' ' < "photon_target_weights_$name.log" | tr -s ' ' | grep -o 'Photon targets (set 0): [^.]*' \
        > "photon_target_weights_$name.targets"
}
refuse() {
    name=$1; scene=$2; shift 2
    if "$POVRAY" +i"$scene" +w8 +h8 -a -d -p -v -f "$@" \
        > "photon_target_weights_$name.log" 2>&1; then
        echo "photon_target_weights: $name rendered" >&2
        exit 1
    fi
    tr '\n' ' ' < "photon_target_weights_$name.log" | tr -s ' ' | grep -q 'share weight' || { cat "photon_target_weights_$name.log"; exit 1; }
}
for parallel in 0 1; do
    render "even$parallel" Declare=Parallel=$parallel
    render "canopy$parallel" Declare=Parallel=$parallel Declare=CanopyWeight=4
    render "patch$parallel" Declare=Parallel=$parallel Declare=PatchWeight=4
    render "patch${parallel}_one" Declare=Parallel=$parallel Declare=PatchWeight=4 +wt1
    cmp "photon_target_weights_patch$parallel.px" "photon_target_weights_patch${parallel}_one.px"
    cmp "photon_target_weights_patch$parallel.targets" "photon_target_weights_patch${parallel}_one.targets"
    render "split$parallel" Declare=Parallel=$parallel Declare=PatchWeight=4 Declare=SplitPatch=1
    cmp "photon_target_weights_patch$parallel.targets" "photon_target_weights_split$parallel.targets"
    cmp "photon_target_weights_patch$parallel.px" "photon_target_weights_split$parallel.px"
done
. "$SRCDIR/tests/render/image.sh"
image_awk '
function split_share(name,    path, text, line, count, rest, aimed) {
    path = "photon_target_weights_" name ".targets"
    text = ""
    while ((getline line < path) > 0) text = text line "\n"
    close(path)
    count = 0
    rest = text
    while (match(rest, /aimed [0-9]+/)) {
        aimed[++count] = substr(rest, RSTART + 6, RLENGTH - 6) + 0
        rest = substr(rest, RSTART + RLENGTH)
    }
    if (count != 2) fail(path ": expected two aimed counts in: " text)
    AIMED = aimed[1] + aimed[2]
    return aimed[2] / AIMED
}
function mean(name,    v, n) {
    n = read_words("photon_target_weights_" name ".px", v, 0)
    return sum_of(v, n) / n
}
BEGIN {
    split("even canopy patch", names, " ")
    for (parallel = 0; parallel < 2; parallel++) {
        even = split_share("even" parallel)
        for (i = 1; i <= 3; i++) {
            name = names[i]
            got = split_share(name parallel)
            ratio = mean(name parallel) / mean("even" parallel)
            printf "photon_target_weights: parallel %d %s: patch share %.4f (even %.4f), %d aimed, image mean %.4f of even\n",
                parallel, name, got, even, AIMED, ratio
            # half the samples split by weight: weight 4 moves the patch share of that half from 1/2 to 4/5 or 1/5
            if (!(name == "even" || (got - even) * (name == "patch" ? 1 : -1) > 0.1)) fail("parallel " parallel " " name ": patch share " got " did not move from " even)
            if (AIMED != 65536) fail("parallel " parallel " " name ": " AIMED " aimed, expected 65536")
            if (!(abs(ratio - 1) < 0.03)) fail("parallel " parallel " " name ": image mean " ratio " of even, limit 3%")
        }
    }
}'
refuse negative "$SRCDIR/tests/render/photon_target_rig.pov" Declare=PatchWeight=-1
refuse infinite "$SRCDIR/tests/render/photon_target_rig.pov" Declare=CanopyWeight=1e300
cat > photon_target_weights_nan.pov <<'SCENE'
#version 3.8;
#declare Big = 1e300 * 1e300;
global_settings { assumed_gamma 1 photons { method 2 } }
light_source { <0, 5, 0> rgb 1 }
sphere { 0, 1 pigment { rgbf 1 } photons { target Big - Big refraction on } }
SCENE
refuse nan photon_target_weights_nan.pov
"$POVRAY" +i"$SRCDIR/tests/render/photon_target_rig.pov" +w8 +h8 -a -d -p -v -f Declare=PatchWeight=0 \
    > photon_target_weights_zero.log 2>&1 || { cat photon_target_weights_zero.log; exit 1; }
echo 'photon_target_weights: weighted splits, parallel and point suns, thread equality, flux, split unions as one target and invalid weights passed'
