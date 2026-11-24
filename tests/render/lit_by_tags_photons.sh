#!/bin/sh
set -eu
POVRAY=$1
SRCDIR=$2
render() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/lit_by_tags_photons.pov" +w64 +h32 +pr -a -d -p -v -gp \
        +ss1 +wt2 +fp16 File_Gamma=1 +o"lit_by_tags_photons_$name.ppm" "$@" \
        > "lit_by_tags_photons_$name.log" 2>&1 || { cat "lit_by_tags_photons_$name.log"; exit 1; }
    tail -c 12288 "lit_by_tags_photons_$name.ppm" > "lit_by_tags_photons_$name.px"
}
deposits() {
    tr '\n' ' ' < "lit_by_tags_photons_$1.log" | tr -s ' ' | grep -o '[0-9]* deposits'
}
# photons aim only at targets their light lights and land only on surfaces it lights: the far copies take none
render restricted
render isolated Declare=Reference=1
render unrestricted Declare=Reference=1 Declare=Restricted=0
cmp -s lit_by_tags_photons_restricted.px lit_by_tags_photons_isolated.px || { echo 'the restricted receiver differs from the isolated one' >&2; exit 1; }
[ "$(deposits restricted)" = "$(deposits isolated)" ] || { echo 'the lamp stored photons on a surface it does not light' >&2; exit 1; }
render outside_receiver Declare=OutsideLens=0
cmp lit_by_tags_photons_outside_receiver.px lit_by_tags_photons_isolated.px
cmp lit_by_tags_photons_isolated.px lit_by_tags_photons_unrestricted.px
od -An -v -t u1 lit_by_tags_photons_restricted.px | awk '{for(i=1;i<=NF;i++) lit=lit || $i>0} END {exit !lit}'
# unrestricted, the far lens focuses photons onto the far receiver too
render everywhere Declare=Restricted=0
if cmp -s lit_by_tags_photons_everywhere.px lit_by_tags_photons_isolated.px; then echo 'the far copies took no photons unrestricted' >&2; exit 1; fi
rm -f lit_by_tags_photons_*.ppm lit_by_tags_photons_*.px lit_by_tags_photons_*.log
echo 'lit_by_tags_photons: aiming, landing and outside receivers passed'
