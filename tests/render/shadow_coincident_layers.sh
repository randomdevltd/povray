#!/bin/sh
# shadow_coincident_layers.sh <povray> <srcdir>: a shadow through translucent media layers sharing a face counts every face.
set -e
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1"); SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d); trap 'rm -rf "$WORK"' EXIT; cd "$WORK"
W=33; H=33
render() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/shadow_coincident_layers.pov" +w$W +h$H -a -d -p -v -gp +fp16 File_Gamma=1.0 +o"$name.ppm" "$@" \
        > "$name.log" 2>&1 || { cat "$name.log"; echo "shadow_coincident_layers: $name failed" >&2; exit 1; }
    tail -c $(( 6 * W * H )) "$name.ppm" > "$name.pixels"
}
# centre <name>: the red channel at the centre pixel, under both layers
centre() { od -An -tu1 -j $(( 6 * (W * (H / 2) + W / 2) )) -N2 "$1.pixels" | awk '{ print $1 * 256 + $2 }'; }
for mesh in 0 1; do
    render touch$mesh Declare=Mesh=$mesh +wt1
    render touch${mesh}_4 Declare=Mesh=$mesh +wt4
    render apart$mesh Declare=Mesh=$mesh Declare=Gap=0.01 +wt1
    cmp -s touch$mesh.pixels touch${mesh}_4.pixels || { echo "shadow_coincident_layers: Mesh=$mesh differs on 4 threads" >&2; exit 1; }
    t=$(centre touch$mesh); a=$(centre apart$mesh)
    awk -v t="$t" -v a="$a" -v m="$mesh" 'BEGIN { if (!(a > 0) || (t - a) * (t - a) > (0.01 * a) * (0.01 * a)) {
        printf "shadow_coincident_layers: Mesh=%d face to face %d, apart %d\n", m, t, a > "/dev/stderr"; exit 1 } }'
done
echo "shadow_coincident_layers: layers sharing a face shadow as layers apart, mesh or box"
