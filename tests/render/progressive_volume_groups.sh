#!/bin/sh
# progressive_volume_groups.sh <povray> <srcdir>: media photons follow light-group membership and global_lights off.
set -eu
POVRAY=$1
SRCDIR=$2
render() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/progressive_volume_groups.pov" +w80 +h40 -a -d -p -v -gp +fp16 File_Gamma=1 \
        +o"progressive_volume_groups_$name.ppm" "$@" > "progressive_volume_groups_$name.log" 2>&1 ||
        { cat "progressive_volume_groups_$name.log"; exit 1; }
    tail -c 19200 "progressive_volume_groups_$name.ppm" > "progressive_volume_groups_$name.px"
}
render one +wt1
render four +wt4
cmp progressive_volume_groups_one.px progressive_volume_groups_four.px
render left Declare=Right=0 +wt2
render right Declare=Left=0 +wt2
render group_lamp Declare=RightLamp=0 +wt2
render global_lamp Declare=LeftLamp=0 +wt2
render open_global Declare=Grouped=0 Declare=LeftLamp=0 +wt2
render open_group Declare=Grouped=0 Declare=RightLamp=0 +wt2
python3 - <<'PY'
from array import array
import sys

def half(name, side):
    values = array('H')
    values.frombytes(open('progressive_volume_groups_' + name + '.px', 'rb').read())
    if sys.byteorder == 'little':
        values.byteswap()
    return [v for i, v in enumerate(values) if (i // 3 % 80 >= 40) == (side == 'right')]

def mean(values):
    return sum(values) / (len(values) * 65535.0)

def close(a, b):
    return max(abs(x - y) for x, y in zip(a, b)) <= 3

assert max(half('group_lamp', 'right')) == 0, 'a group light lit media outside its group'
assert max(half('global_lamp', 'left')) == 0, 'a global light lit media in a global_lights off group'
assert mean(half('open_global', 'left')) > 0.001 and mean(half('open_group', 'right')) > 0.001, 'the lamps do not reach the other box'
assert close(half('group_lamp', 'left'), half('left', 'left')), 'the group box differs from the group alone'
assert close(half('global_lamp', 'right'), half('right', 'right')), 'the global box differs from the global half alone'
for side in ('left', 'right'):
    together, alone = mean(half('one', side)), mean(half(side, side))
    assert alone > 0.01 and abs(together / alone - 1) < 0.02, side + ' box changes when both lamps shoot'
PY
rm -f progressive_volume_groups_*.ppm progressive_volume_groups_*.px progressive_volume_groups_*.log
echo 'progressive_volume_groups: group and global media lighting, isolation, controls and threads passed'
