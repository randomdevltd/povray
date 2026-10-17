#!/bin/sh
# progressive_radiosity.sh <povray> <srcdir>: radiosity carries a method 2 caustic to a wall that collects no photons.
set -eu
POVRAY=$1
SRCDIR=$2
render() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/progressive_radiosity.pov" +w64 +h48 -a -d -p -v -gp +fp16 File_Gamma=1 \
        +o"progressive_radiosity_$name.ppm" "$@" > "progressive_radiosity_$name.log" 2>&1 ||
        { cat "progressive_radiosity_$name.log"; exit 1; }
    tail -c 18432 "progressive_radiosity_$name.ppm" > "progressive_radiosity_$name.px"
}
render one +wt1
render four +wt4
cmp progressive_radiosity_one.px progressive_radiosity_four.px
render direct Declare=Radiosity=0 +wt2
render dark Declare=Caustic=0 +wt2
render classic Declare=Method=1 +wt2
: > progressive_radiosity_progressive.pov-state
ln -f progressive_radiosity_progressive.pov-state progressive_radiosity_saved.pov-state
render progressive +pr +wt2
state_bytes=$(wc -c < progressive_radiosity_saved.pov-state)
head -c $((state_bytes - 100)) progressive_radiosity_saved.pov-state > progressive_radiosity_resumed.pov-state
render resumed +pr +wt4 +c
cmp progressive_radiosity_progressive.px progressive_radiosity_resumed.px
python3 - <<'PY'
from array import array
import sys

def rows(name, first, last):
    values = array('H')
    values.frombytes(open('progressive_radiosity_' + name + '.px', 'rb').read())
    if sys.byteorder == 'little':
        values.byteswap()
    return sum(values[first * 192:last * 192]) / ((last - first) * 192 * 65535.0)

wall = {name: rows(name, 24, 36) for name in ('one', 'direct', 'dark', 'classic')}
floor = {name: rows(name, 36, 48) for name in ('one', 'direct', 'classic')}
print('progressive_radiosity lower wall: %(one).4f radiosity, %(direct).4f without, %(classic).4f classic' % wall)
assert wall['direct'] == 0.0 and wall['dark'] == 0.0, 'the wall is lit without radiosity or without the caustic'
assert wall['one'] > 0.02, 'radiosity does not carry the caustic to the wall'
assert 0.8 < wall['one'] / wall['classic'] < 1.25, 'the bounced caustic differs from classic photons'
assert 0.8 < floor['one'] / floor['classic'] < 1.25 and floor['direct'] > 0.05
PY
rm -f progressive_radiosity_*.ppm progressive_radiosity_*.px progressive_radiosity_*.log progressive_radiosity_*.pov-state
echo 'progressive_radiosity: bounced caustic, threads, continue and classic agreement passed'
