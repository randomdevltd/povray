#!/bin/sh
set -e
POVRAY=$1; SRCDIR=$2
trap 'rm -f photon_ring_threads_* photon_ring_threads.ph' EXIT

render() {
    scene=$1; name=$2; threads=$3
    "$POVRAY" +i"$SRCDIR/tests/render/photon_ring_$scene.pov" +L"$SRCDIR/tests/render" \
        +w32 +h24 +wt"$threads" +pr -a -d -p -v -gp +fp16 +o"photon_ring_threads_$name.ppm" \
        Declare=SaveMap=1 \
        > "photon_ring_threads_$name.log" 2>&1 || { cat "photon_ring_threads_$name.log"; exit 1; }
    mv photon_ring_threads.ph "photon_ring_threads_$name.ph"
    tail -c 4608 "photon_ring_threads_$name.ppm" > "photon_ring_threads_$name.px"
    sed -n '/^Number of photons shot:/p; /^Surface photons stored:/p; /^Media photons stored:/p' \
        "photon_ring_threads_$name.log" | tr -s ' ' > "photon_ring_threads_$name.counts"
}

for scene in autostop point spotlight area cylinder multi_target many_targets; do
    render "$scene" "$scene-wt1" 1
    render "$scene" "$scene-wt8" 8
    cmp "photon_ring_threads_$scene-wt1.px" "photon_ring_threads_$scene-wt8.px"
    cmp "photon_ring_threads_$scene-wt1.counts" "photon_ring_threads_$scene-wt8.counts"
    cmp "photon_ring_threads_$scene-wt1.ph" "photon_ring_threads_$scene-wt8.ph"
done
render multi_target multi_target-wt64 64
cmp photon_ring_threads_multi_target-wt1.px photon_ring_threads_multi_target-wt64.px
cmp photon_ring_threads_multi_target-wt1.counts photon_ring_threads_multi_target-wt64.counts
cmp photon_ring_threads_multi_target-wt1.ph photon_ring_threads_multi_target-wt64.ph
echo "photon_ring_threads: pixels, photon counts, and maps match across thread counts"
