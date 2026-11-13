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
    "$POVRAY" +i"$SRCDIR/tests/render/surface_domain_cells.pov" +w48 +h32 +wt2 +pr -a -d -p +fp16 +o"$name.ppm" "$@" >"$name.log" 2>&1 ||
        { cat "$name.log"; exit 1; }
    tail -c 9216 "$name.ppm" >"$name.rgb"
    grep -q 'Preparation: bounce finished' "$name.log" || { cat "$name.log"; exit 1; }
    sed -n 's/.*Surface light total: \([0-9]*\) nodes across \([0-9]*\) lights.*/\1 \2/p' "$name.log" |
        awk -v n="${CAP:-64}" '$1 < $2 || $1 > n { bad=1 } END { exit (bad || NR!=1) }' || { cat "$name.log"; exit 1; }
    sed -n 's/.*Bounce o[0-9]*: \([0-9]*\) nodes.*/\1/p' "$name.log" |
        awk -v n="${CAP:-64}" '$1 > n { bad=1 } END { exit (bad || NR==0) }' || { cat "$name.log"; exit 1; }
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
# The built-in 2D budget equals an explicit 4096, independent of either volume default.
# Use a diffuse surface here so this default check does not require buried-source walks.
for language in pov pov4; do
    if [ "$language" = pov ]; then prefix='#'; echo '#version 3.7;' >budget.$language
    else prefix=''; echo '// 4.0 syntax' >budget.$language; fi
    cat >>budget.$language <<SCENE
${prefix}default { media { domain_cells 8 } interior { light { domain_cells 27 } } }
global_settings { assumed_gamma 1 }
camera { location <0,0,-3> look_at 0 }
sphere { 0, 1 pigment { rgb 1 } finish { ambient 0 diffuse 1 } light { bounce { passes 2 } samples 1 } }
light_source { <2,3,-4>, rgb 1 }
SCENE
    for kind in builtin explicit; do
        if [ "$kind" = explicit ]; then sed 's/bounce {/domain_cells 4096 bounce {/' budget.$language >explicit.$language
        else cp budget.$language builtin.$language; fi
        "$POVRAY" +i$kind.$language +w8 +h8 +wt1 +pr -a -d -p +fp16 +o$kind.ppm >$kind.log 2>&1 || { cat $kind.log; exit 1; }
        tail -c 384 $kind.ppm >$kind.rgb
        sed -n 's/.*Bounce o[0-9]*: \([0-9]*\) nodes.*/\1/p' $kind.log >$kind.nodes
        awk '$1 > 4096 { bad=1 } END { exit (bad || NR!=2) }' $kind.nodes || { cat $kind.log; exit 1; }
    done
    cmp builtin.rgb explicit.rgb
    cmp builtin.nodes explicit.nodes
done
# Both defaults in the same block are also accepted by the 4.0 static checker.
cat >defaults.pov4 <<'SCENE'
default { interior { light { domain_cells 8 } } light { domain_cells 1 } }
global_settings { assumed_gamma 1 }
camera { location <0,0,-3> look_at 0 }
sphere { 0, 1 pigment { rgb 1 } light { bounce { passes 2 } } }
light_source { <2,3,-4>, rgb 1 }
SCENE
"$POVRAY" +idefaults.pov4 +w8 +h8 +wt1 +pr -a -d -p -f >defaults.log 2>&1 || { cat defaults.log; exit 1; }
echo 'surface_domain_cells: ok'
