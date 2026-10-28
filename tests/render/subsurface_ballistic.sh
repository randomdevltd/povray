#!/bin/sh
# subsurface_ballistic.sh <povray> <srcdir>: direct transmission through an unlit slab.
set -e
trap 'rm -f ssball_*' EXIT
POVRAY=$1; SRCDIR=$2
render() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/subsurface_ballistic.pov" -d -p -a +q9 +pr +fp16 +w8 +h8 +wt1 File_Gamma=1 +o"ssball_$name.ppm" "$@" > "ssball_$name.log" 2>&1
}
for angle in 0 45 70; do
    render clear$angle Declare=Angle=$angle
    render finite$angle Declare=Angle=$angle Declare=Length=2
    render equal$angle Declare=Angle=$angle Declare=Index=1
    render legacy$angle Declare=Angle=$angle Declare=Method=1
    render conserve$angle Declare=Angle=$angle Declare=Conserve=1
done
render opaque Declare=Length=0.01
render wedge Declare=Wedge=1 +w1 +h1
python3 "$SRCDIR/tests/render/subsurface_ballistic_reference.py" .
