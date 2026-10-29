#!/bin/sh
set -eu
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"
render() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/refraction_prepared_ray.pov" +w8 +h8 +pr -a -d -p -v -gp \
        +wt2 +fp16 File_Gamma=1 Stochastic_Seed=0 +o"$name.ppm" "$@" > "$name.log" 2>&1 || { cat "$name.log"; exit 1; }
}
render constant Declare=Bend=0
render linear
render absorbed Declare=Sigma=0.7
render native Declare=Split=1
for name in linear absorbed native; do
    grep -Eq 'Curved Rays:[[:space:]]+[1-9][0-9]*' "$name.log" || { cat "$name.log"; exit 1; }
    grep -Eq 'Bending Steps:[[:space:]]+[1-9][0-9]*' "$name.log" || { cat "$name.log"; exit 1; }
done
python3 "$SRCDIR/tests/render/refraction_prepared_ray_reference.py" "$WORK"
