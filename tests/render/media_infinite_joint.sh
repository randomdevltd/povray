#!/bin/sh
set -eu
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"
"$POVRAY" +i"$SRCDIR/tests/render/media_infinite_joint.pov" +w8 +h8 +pr -a -d -p -v -gp \
    +wt2 +fp16 File_Gamma=1 Stochastic_Seed=0 Declare=Draws=512 +oinfinite.ppm \
    > infinite.log 2>&1 || { cat infinite.log; exit 1; }
for field in 'Analytic Infinite Intervals' 'Source Pairs' 'Prepared4 Queries'; do
    grep -Eq "$field:[[:space:]]+[1-9][0-9]*" infinite.log || { cat infinite.log; exit 1; }
done
for field in 'Forbidden4 Procedure' 'Old Integrator'; do
    grep -Eq "$field:[[:space:]]+0([[:space:]]|$)" infinite.log || { cat infinite.log; exit 1; }
done
. "$SRCDIR/tests/render/image.sh"
image_awk -l media_infinite_joint_reference 'BEGIN { media_infinite_joint_check(ARGV[1]) }' infinite.ppm
