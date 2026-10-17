#!/bin/sh
# photon_target_weights.sh <povray> <srcdir>: method 2 splits a light's samples between its targets by their target weights.
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
    grep -q 'share weight' "photon_target_weights_$name.log" || { cat "photon_target_weights_$name.log"; exit 1; }
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
python3 - <<'PY'
import re
from pathlib import Path

def split(name):
    text = Path(f'photon_target_weights_{name}.targets').read_text()
    aimed = [int(a) for a in re.findall(r'aimed (\d+)', text)]
    assert len(aimed) == 2, text
    return aimed[1] / sum(aimed), sum(aimed)

def mean(name):
    data = Path(f'photon_target_weights_{name}.px').read_bytes()
    return sum(256 * data[i] + data[i + 1] for i in range(0, len(data), 2)) / (len(data) / 2)

for parallel, isotropic in ((0, 0.1), (1, 0.0)):
    for name, share in (('even', 0.5), ('canopy', 0.2), ('patch', 0.8)):
        got, aimed = split(f'{name}{parallel}')
        print(f'photon_target_weights: parallel {parallel} {name}: patch share {got:.4f} (expected {share}), '
              f'{aimed} aimed, image mean {mean(name + str(parallel)) / mean("even" + str(parallel)):.4f} of even')
        assert abs(got - share) < 0.002, (parallel, name, got)
        assert abs(aimed - 65536 * (1 - isotropic)) < 16, (parallel, name, aimed)
        assert abs(mean(name + str(parallel)) / mean('even' + str(parallel)) - 1) < 0.03, (parallel, name)
PY
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
