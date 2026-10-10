#!/bin/sh
set -eu
POVRAY=$1
SRCDIR=$2
render() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/render_tags_photon_light_group.pov" +w64 +h32 +pr -a -d -p -v -gp \
        +ss1 +wt2 +fp16 File_Gamma=1 +o"progressive_group_$name.ppm" "$@" \
        > "progressive_group_$name.log" 2>&1 || { cat "progressive_group_$name.log"; exit 1; }
    tail -c 12288 "progressive_group_$name.ppm" > "progressive_group_$name.px"
}
render grouped
render isolated Declare=Reference=1
render global Declare=Reference=1 Declare=Grouped=0
cmp progressive_group_grouped.px progressive_group_isolated.px
cmp progressive_group_isolated.px progressive_group_global.px
render disabled Declare=GroupPhotons=0
render legacy_disabled Declare=Version=3.8
cmp progressive_group_disabled.px progressive_group_legacy_disabled.px
od -An -v -t u1 progressive_group_disabled.px | awk '{for(i=1;i<=NF;i++) if($i!=0) exit 1}'
od -An -v -t u1 progressive_group_grouped.px | awk '{for(i=1;i<=NF;i++) lit=lit || $i>0} END {exit !lit}'
render legacy_enabled Declare=Version=3.8 Declare=GroupPhotons=1 Declare=Reference=1
render legacy_global Declare=Version=3.8 Declare=Grouped=0 Declare=Reference=1
cmp progressive_group_legacy_enabled.px progressive_group_legacy_global.px
render direct_group Declare=GroupPhotons=0 Declare=PhotonOnly=0 Declare=Reference=1
render direct_global Declare=Grouped=0 Declare=SourceRefraction=0 Declare=PhotonOnly=0 Declare=Reference=1
cmp progressive_group_direct_group.px progressive_group_direct_global.px
rm -f progressive_group_*.ppm progressive_group_*.px progressive_group_*.log
echo 'progressive_photon_groups: source membership, cloned groups, outside receivers and versioned switches passed'
