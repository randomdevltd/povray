#!/bin/sh
set -eu
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"
render() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/surface_part_accuracy.pov" +w32 +h32 +wt2 -a -d -p -v -gp +fh Stochastic_Seed=0 \
        +o"$name.hdr" "$@" >"$name.log" 2>&1 || { cat "$name.log"; exit 1; }
}
for part in 0 1; do
    render "lit0$part" Declare=Union=$part
    render "open0$part" Declare=Union=$part Declare=Shadowless=1
    render "lit1$part" Declare=Subsurface=1 Declare=Union=$part
done
python3 - <<'PY'
def pixels(path):
    data = open(path, 'rb').read()
    head, _, rest = data.partition(b'\n\n')
    size, _, body = rest.partition(b'\n')
    height, width = int(size.split()[1]), int(size.split()[3])
    out, i = [], 0
    for _ in range(height):
        assert body[i] == 2 and body[i + 1] == 2
        i += 4
        channels = [[] for _ in range(4)]
        for c in range(4):
            while len(channels[c]) < width:
                n = body[i]; i += 1
                if n > 128:
                    channels[c] += [body[i]] * (n - 128); i += 1
                else:
                    channels[c] += list(body[i:i + n]); i += n
        for r, g, b, e in zip(*channels):
            out.append(0.0 if e == 0 else (r + g + b) * 2.0 ** (e - 136))
    return out
total = lambda name: sum(pixels(name + '.hdr'))
for part in '01':
    lit, opened = total('lit0' + part), total('open0' + part)
    # Nothing lies between the light and the lit side of a convex receiver, so any shadow there is its own.
    assert lit > 0 and abs(lit / opened - 1) < 0.002, (part, lit, opened)
for sslt in '01':
    alone, joined = total('lit' + sslt + '0'), total('lit' + sslt + '1')
    assert abs(joined / alone - 1) < 0.01, (sslt, alone, joined)
PY
echo 'surface_part_accuracy: an isosurface lit alone and as a union part shows no self-shadow and the same light'
