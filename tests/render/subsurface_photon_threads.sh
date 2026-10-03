#!/bin/sh
# subsurface_photon_threads.sh <povray> <srcdir>: photon light diffusing through a subsurface leaf is the same on any thread count and in
# any block order, though each deposit's boundary check is computed by whichever thread meets it first and then reused.
set -e
trap 'rm -f ssph_threads_*' EXIT
W=48; H=36
render() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/subsurface_leaf.pov" +w$W +h$H -d -p -a +fp16 +o"ssph_threads_$name.ppm" \
        Declare=PhotonCount=10000 Declare=Bands=7 "$@" > "ssph_threads_$name.log" 2>&1
    tail -c $((W * H * 6)) "ssph_threads_$name.ppm" > "ssph_threads_$name.px"
}
same() {
    cmp -s "ssph_threads_$1.px" "ssph_threads_$2.px" || { echo "subsurface_photon_threads: $1 and $2 differ"; exit 1; }
}
POVRAY=$1; SRCDIR=$2
render one +wt1
render four +wt4 +bs8
render order +wt4 +rp5 +bs5
same one four
same one order
tr '\n' ' ' < ssph_threads_one.log | tr -s ' ' | grep -q 'boundaries [1-9][0-9]* computed, [1-9][0-9]* reused' ||
    { echo "subsurface_photon_threads: no boundary reuse reported"; exit 1; }
echo "subsurface_photon_threads: photon light is the same on any thread count and in any block order"
