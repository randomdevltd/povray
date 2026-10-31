#!/bin/sh
set -eu
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"
render() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/subsurface_cavity_light.pov" +w48 +h36 +wt2 -a -d -p -v -gp +fh +o"$name.hdr" "$@" \
        >"$name.log" 2>&1 || { cat "$name.log"; exit 1; }
}
for seed in 2 5 6; do
    render "cavity$seed" Stochastic_Seed="$seed" Declare=SingleSamples=256
done
render touch_few Declare=SingleSamples=16
render touch_many Declare=SingleSamples=512
render far_few Declare=SingleSamples=16 Declare=Far=1
render far_many Declare=SingleSamples=512 Declare=Far=1
. "$SRCDIR/tests/render/image.sh"
image_awk '
function pixels(path, out,    r, g, bl, e, n, i) {
    n = read_hdr(path, r, g, bl, e)
    for (i = 0; i < n; i++) out[i] = e[i] == 0 ? 0.0 : max2(max2(r[i], g[i]), bl[i]) * 2 ^ (e[i] - 136)
    return n
}
function mean(path,    v, n) {
    n = pixels(path, v)
    return sum_of(v, n) / n
}
function sort(v, n,    gap, i, j, t) {
    for (gap = int(n / 2); gap > 0; gap = int(gap / 2))
        for (i = gap; i < n; i++)
            for (j = i; j >= gap && v[j - gap] > v[j]; j -= gap) { t = v[j]; v[j] = v[j - gap]; v[j - gap] = t }
}
BEGIN {
    for (c = 0; c < 2; c++) {
        kind = c ? "far" : "touch"
        few = mean(kind "_few.hdr"); many = mean(kind "_many.hdr")
        # Few and many single-scattering samples estimate the same light, touching the membrane or well clear of it.
        if (!(abs(few / many - 1) < 0.03)) fail(kind ": mean " few " with few samples, " many " with many; limit 3%")
    }
    for (a = 1; a < ARGC; a++) {
        n = pixels(ARGV[a], values)
        sort(values, n)
        median = values[int(n / 2)]; peak = values[n - 1]
        # Single scattering from a light touching the membrane stays within the range of the diffuse light.
        if (!(median > 0 && peak < 8 * median)) fail(ARGV[a] ": median " median ", peak " peak "; peak must stay below 8 medians")
    }
}' cavity*.hdr
echo 'subsurface_cavity_light: single scattering from a light touching the membrane stays bounded'
