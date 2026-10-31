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
render volume_legacy Declare=Source=2 Declare=Method=3
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
python3 - <<'PYTHON'
import struct
import math
from pathlib import Path
expected = {
    "point": tuple(s*5000.5*(math.atan(100)-math.atan(99)) for s in (.0875,.1375,.15)),
    "volume16": tuple(s*50/99 for s in (.0875,.1375,.15)),
    "volume64": tuple(s*50/99 for s in (.0875,.1375,.15)),
    "volume_legacy": tuple(s*50/99 for s in (.0875,.1375,.15)),
    "surface_point": (.5,.5,.5),
    "surface_volume": (.5,.5,.5),
    "physical": (.0875, .1375, .15),
    "zero": (.0875, .1375, .15),
    "multiply": tuple(s*(.5+.5*f) for s,f in zip((.0875,.1375,.15),(.5,.25,.75))),
    "subtract": tuple(s*(1-.5*a/t) for s,a,t in zip((.0875,.1375,.15),(.05,.1,.1),(.3,.4,.4))),
    "absorbed": tuple(s*math.exp(-a) for s,a in zip((.0875,.1375,.15),(.1,.2,.3))),
    "absorber_zero": tuple(s*math.exp(-a) for s,a in zip((.0875,.1375,.15),(.1,.2,.3))),
    "density": (.1375, .1625, .15),
    "forward": (.2, .475, .6),
    "backward": ((.2+.1*2/9)/4, (.1+.3*2/9)/4, .4*2/9/4),
    "legacy": tuple(1.299372013 * s for s in (.3,.4,.4)),
}
for name, reference in expected.items():
    data=Path(name+".ppm").read_bytes()[-4*4*6:]
    samples=struct.unpack(">48H", data)
    actual=[sum(samples[ch::3])/16/65535 for ch in range(3)]
    for ch, (value, target) in enumerate(zip(actual, reference)):
        assert abs(value-target)<(0.002 if name.startswith(("volume", "surface")) else 0.00015), (name,ch,value,target)
    print(name, actual)
PYTHON
