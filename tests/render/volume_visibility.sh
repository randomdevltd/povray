#!/bin/sh
set -eu
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"
render() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/volume_visibility.pov" +w12 +h12 +pr -a -d -p -v -gp \
        +fp16 File_Gamma=1 +o"$name.ppm" "$@" > "$name.log" 2>&1 || { cat "$name.log"; exit 1; }
}
render clear_sphere
render opaque_sphere Declare=Opaque=1
render small_opaque_sphere Declare=Opaque=1 Declare=SceneScale=0.1 Declare=Cache=1
render clear_slab Declare=Kind=1
render blocked_slab Declare=Kind=1 Declare=Blocker=1 Declare=Cache=1
render cached_blocked_slab Declare=Kind=1 Declare=Blocker=1 Declare=Cache=2
render scaled_blocked_slab Declare=Kind=1 Declare=Blocker=1 Declare=Cache=2 Declare=SceneScale=10
python3 - "$SRCDIR/tests/render" <<'PY'
import sys
sys.dont_write_bytecode = True
sys.path.insert(0, sys.argv[1])
from pathlib import Path
from volume_transport_reference import read_ppm
for name in ['clear_sphere', 'clear_slab']:
    values = read_ppm(Path(name + '.ppm'))[2]
    assert sum(values) / len(values) > 0.01, (name, max(values))
for name in ['opaque_sphere', 'small_opaque_sphere', 'blocked_slab', 'cached_blocked_slab', 'scaled_blocked_slab']:
    values = read_ppm(Path(name + '.ppm'))[2]
    assert max(values) == 0, (name, max(values), sum(values) / len(values))
print('volume_visibility: opaque enclosing and external surfaces block the full sampled connection')
PY
