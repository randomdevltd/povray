#!/bin/sh
# isosurface_mesh.sh <povray> <srcdir>: isosurface_mesh agrees with its isosurface in hits, inside() and kept edges, and is closed unless open.
set -e
POVRAY=$1; SRCDIR=$2
"$POVRAY" +i"$SRCDIR/tests/render/isosurface_mesh_check.pov" +L"$SRCDIR/tests/render" +L"$SRCDIR/include" +w8 +h8 -d -p -v -gp -f > isosurface_mesh.log 2>&1 || { cat isosurface_mesh.log; exit 1; }
# six meshes, each built once: five closed, the open one with a rim
test "$(grep -c '^isosurface_mesh: [0-9]* triangles' isosurface_mesh.log)" = 6 || { cat isosurface_mesh.log; echo "isosurface_mesh: not six builds"; exit 1; }
test "$(grep -c '^isosurface_mesh: .* 0 open edges' isosurface_mesh.log)" = 5 || { cat isosurface_mesh.log; echo "isosurface_mesh: not five closed meshes"; exit 1; }
grep '^isosurface_mesh: [0-9]* triangles' isosurface_mesh.log
