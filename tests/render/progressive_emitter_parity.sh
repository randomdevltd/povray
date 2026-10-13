#!/bin/sh
set -eu
POVRAY=$1
SRCDIR=$2
render() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/progressive_emitter_parity.pov" +w32 +h24 +pr -a -d -p -v -gp \
        +fp16 File_Gamma=1 +o"progressive_emitter_$name.ppm" "$@" \
        > "progressive_emitter_$name.log" 2>&1 || { cat "progressive_emitter_$name.log"; exit 1; }
}
render rectangle Declare=Source=1 +wt1
render oriented Declare=Source=2 +wt1
render oriented_four Declare=Source=2 +wt4
render oriented_power Declare=Source=2 Declare=Power=0.08 +wt2
render open +wt1
render aperture Declare=Aperture=1 +wt1
render blocked Declare=Aperture=2 +wt2
render half Declare=Aperture=3 +wt2
render aperture_four Declare=Aperture=1 +wt4
render aperture_power Declare=Aperture=1 Declare=Power=0.08 +wt2
render wall Declare=Blocker=1 +wt2
render crossings Declare=Aperture=4 Declare=Blocker=1 +wt2
render medium Declare=Medium=1 +wt2
render aperture_medium Declare=Aperture=1 Declare=Medium=1 +wt2
render oriented_aperture Declare=Source=2 Declare=Aperture=4 Declare=Blocker=1 +wt2
render oriented_open Declare=Source=2 +wt2
render parallel Declare=Source=3 +wt1
render parallel_aperture Declare=Source=3 Declare=Aperture=4 Declare=Blocker=1 +wt4
for name in open aperture blocked crossings oriented; do
    case "$name" in
        open) options='' ;;
        aperture) options='Declare=Aperture=1' ;;
        blocked) options='Declare=Aperture=2' ;;
        crossings) options='Declare=Aperture=4 Declare=Blocker=1' ;;
        oriented) options='Declare=Source=2' ;;
    esac
    render "classic_$name" Declare=Method=1 +wt2 $options
done
python3 - <<'PY'
from array import array
from pathlib import Path
import sys

def values(name):
    output = array('H')
    output.frombytes(Path(f'progressive_emitter_{name}.ppm').read_bytes()[-32*24*6:])
    if sys.byteorder == 'little':
        output.byteswap()
    return output

def equal(a, b, tolerance=2):
    error = max(abs(x-y) for x, y in zip(values(a), values(b)))
    assert error <= tolerance, (a, b, error)

equal('rectangle', 'oriented', 0)
equal('oriented', 'oriented_four', 0)
equal('aperture', 'aperture_four', 0)
for a, b in [('open', 'aperture'), ('open', 'crossings'), ('oriented_open', 'oriented_aperture'),
             ('parallel', 'parallel_aperture'), ('medium', 'aperture_medium'),
             ('classic_open', 'classic_aperture'), ('classic_open', 'classic_crossings')]:
    equal(a, b)
for name in ['oriented', 'aperture', 'medium', 'parallel', 'classic_oriented']:
    assert sum(values(name)) > 100, name
for name in ['blocked', 'wall', 'classic_blocked']:
    assert sum(values(name)) == 0, name
for low, high in [('oriented', 'oriented_power'), ('aperture', 'aperture_power')]:
    ratio = sum(values(high)) / sum(values(low))
    assert max(values(high)) < 65535 and 1.99 < ratio < 2.01, (low, high, ratio)
ratio = sum(values('half')) / sum(values('open'))
assert 0.4 < ratio < 0.6, ('half aperture power', ratio)
assert sum(values('medium')) < 0.9 * sum(values('open'))
print(f'half aperture / open power: {ratio:.6f}')
PY
rm -f progressive_emitter_*.ppm progressive_emitter_*.log progressive_emitter_*.pov-state
echo 'progressive_emitter_parity: active sources, power, threads, aperture misses, CSG crossings, blockers and interiors passed'
