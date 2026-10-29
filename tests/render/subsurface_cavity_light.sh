#!/bin/sh
set -eu
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"
for seed in 2 5 6; do
    "$POVRAY" +i"$SRCDIR/tests/render/subsurface_cavity_light.pov" +w48 +h36 +wt2 -a -d -p -v -gp +fh \
        Stochastic_Seed="$seed" Declare=SingleSamples=256 +o"cavity$seed.hdr" >"cavity$seed.log" 2>&1 ||
        { cat "cavity$seed.log"; exit 1; }
done
python3 - <<'PY'
import glob
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
            out.append(0.0 if e == 0 else max(r, g, b) * 2.0 ** (e - 136))
    return out
for path in sorted(glob.glob('cavity*.hdr')):
    values = sorted(pixels(path))
    median, peak = values[len(values) // 2], values[-1]
    # Single scattering from a light touching the membrane stays within the diffuse light's range.
    assert median > 0 and peak < 8 * median, (path, median, peak)
PY
echo 'subsurface_cavity_light: single scattering from a light touching the membrane stays bounded'
