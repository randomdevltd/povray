#!/bin/sh
# Per-light surface budgets, independent defaults, small and rescaled buried-source gathers.
set -eu
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"
render() {
    name=$1; shift
    timeout 30 "$POVRAY" +i"$SRCDIR/tests/render/surface_domain_cells.pov" +w48 +h32 +wt2 +pr -a -d -p +fp16 +o"$name.ppm" "$@" >"$name.log" 2>&1 ||
        { cat "$name.log"; exit 1; }
    tail -c 9216 "$name.ppm" >"$name.rgb"
    grep -q 'Preparation: bounce finished' "$name.log" || { cat "$name.log"; exit 1; }
    sed -n 's/.*Bounce o[0-9]*: \([0-9]*\) nodes.*/\1/p' "$name.log" |
        awk -v n="${CAP:-64}" '$1 > n { exit 1 }' || { cat "$name.log"; exit 1; }
}
render stated
od -An -v -tu2 --endian=big stated.rgb | awk '{ for(i=1;i<=NF;i++) { if(NR==1 && i==1) lo=$i; if($i<lo) lo=$i; if($i>hi) hi=$i } } END { exit !(hi > 2*lo) }' ||
    { echo 'surface_domain_cells: the capsule did not light the image'; exit 1; }
CAP=67 render independent Declare=Extra=1
cmp stated.rgb independent.rgb
render defaulted Declare=UseDefault=1
cmp stated.rgb defaulted.rgb
render medium Declare=MediaCells=8
cmp stated.rgb medium.rgb
render small Declare=S=0.001
render large Declare=S=1000
render exported Declare=Export=1 Declare=Passes=2
CAP=1 render single Declare=Cells=1 Declare=Export=1 Declare=Passes=2
# Both defaults in the same block are also accepted by the 4.0 static checker.
cat >defaults.pov4 <<'SCENE'
default { interior { light { domain_cells 8 } } light { domain_cells 1 } }
global_settings { assumed_gamma 1 }
camera { location <0,0,-3> look_at 0 }
sphere { 0, 1 pigment { rgb 1 } light { bounce { passes 2 } } }
light_source { <2,3,-4>, rgb 1 }
SCENE
timeout 30 "$POVRAY" +idefaults.pov4 +w8 +h8 +wt1 +pr -a -d -p -f >defaults.log 2>&1 || { cat defaults.log; exit 1; }
echo 'surface_domain_cells: ok'
