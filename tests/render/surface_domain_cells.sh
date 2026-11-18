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
render passes2 Declare=Passes=2
# A budget of one point reads one line at the start, too few to meet the capsule: no bounce, and a warning saying so.
"$POVRAY" +i"$SRCDIR/tests/render/surface_domain_cells.pov" +w48 +h32 +wt2 +pr -a -d -p +fp16 +osingle.ppm Declare=Cells=1 \
    Declare=Passes=2 >single.log 2>&1 || { cat single.log; exit 1; }
tr '\n' ' ' <single.log | tr -s '[:space:]' ' ' | grep -q 'Surface light total: 0 nodes across 1 lights' &&
    tr '\n' ' ' <single.log | tr -s '[:space:]' ' ' | grep -q 'found no surface on the cone or cylinder.*0 points of 1,' ||
    { cat single.log; echo 'surface_domain_cells: a budget of 1 did not end with no points and a warning'; exit 1; }
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
# More disconnected pieces than seed slots must still obey one shared budget.
cat >seeds.pov <<'SCENE'
#version 3.7;
global_settings { assumed_gamma 1 }
camera { location <4,5,-8> look_at <4,0,4> }
light_source { <4,10,4>, rgb 1 }
union {
  #for (J,0,7)
    #for (I,0,7)
      sphere { <I,0,J>, 0.3 }
    #end
  #end
  pigment { rgb 1 }
  finish { ambient 0 diffuse 0 emission 1 }
  light { bounce { passes 2 } domain_cells 4 samples 1 }
}
SCENE
for threads in 1 2; do
    "$POVRAY" +iseeds.pov +w8 +h8 +wt$threads +pr -a -d -p +fp16 +oseeds$threads.ppm >seeds$threads.log 2>&1 ||
        { cat seeds$threads.log; exit 1; }
    sed -n 's/.*Bounce o[0-9]*: \([0-9]*\) nodes.*/\1/p' seeds$threads.log |
        awk '$1 < 1 || $1 > 4 { bad=1 } END { exit (bad || NR!=2) }' ||
        { cat seeds$threads.log; exit 1; }
    tr '\n' ' ' <seeds$threads.log | tr -s '[:space:]' ' ' |
        sed -n 's/.*points stand for area \([^,]*\), exported power \([^;]*\);.*/\1 \2/p' |
        awk 'BEGIN { area=64*4*atan2(0,-1)*0.3*0.3 }
             $1 < area/4 || $1 > area*4 || $2 < $1*0.99 || $2 > $1*1.01 { bad=1 }
             END { exit (bad || NR!=1) }' ||
        { cat seeds$threads.log; echo 'surface_domain_cells: sparse seeds lost area or constant emission'; exit 1; }
    tail -c 384 seeds$threads.ppm >seeds$threads.rgb
done
cmp seeds1.rgb seeds2.rgb
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
