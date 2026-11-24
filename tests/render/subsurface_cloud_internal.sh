#!/bin/sh
# A volume light buried in a method 2 body has all its light read from the body's point cloud: smooth at few samples,
# the per-pixel paths' mean at many, a thin vein's glow as localised as the paths make it to within the points' spacing, and the same for any thread count
# and scene scale. At four paths a point, where one heavy path can set a point's lean, no pixel goes black or bright; in an
# optically thin host (a 3 mm mean free path through a 2 mm slab) every point gets light at two paths a point.
set -eu
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"
render() {
    name=$1
    shift
    "$POVRAY" +i"$SRCDIR/tests/render/subsurface_cloud_internal.pov" +w48 +h48 +pr -a -d -p -v -gp \
        +fp16 File_Gamma=1 Stochastic_Seed=0 "$@" +o"$name.ppm" >"$name.log" 2>&1 || { cat "$name.log"; exit 1; }
}
render cloud16 +wt4 Declare=Samples=16
render cloud16one +wt1 Declare=Samples=16
render paths16 +wt4 Declare=Samples=16 Declare=Influence=1e9 Declare=Paths=1
render cloud64 +wt4 Declare=Samples=64
render cloud64big +wt4 Declare=Samples=64 Declare=Scale=1000
render paths256 +wt4 Declare=Samples=256 Declare=Influence=1e9 Declare=Paths=1
render bounce64 +wt4 Declare=Samples=64 Declare=Passes=2
render bounce256 +wt4 Declare=Samples=256 Declare=Passes=2 Declare=Influence=1e9 Declare=Paths=1
render vein16 +wt4 +w96 +h24 Declare=Samples=16 Declare=Vein=1 Declare=Cells=16384
render vein256 +wt4 +w96 +h24 Declare=Samples=256 Declare=Vein=1 Declare=Influence=1e9 Declare=Paths=1
render heavy4 +wt4 Declare=Samples=4
THIN="Declare=Vein=1 Declare=Thick=2 Declare=Depth=1 Declare=Radius=0.1 Declare=Translucency=3 Declare=Cells=16384"
render thin2 +wt4 +w96 +h24 $THIN Declare=Samples=2
render thin256 +wt4 +w96 +h24 $THIN Declare=Samples=256 Declare=Influence=1e9 Declare=Paths=1
grep -E 'Method 2 buried lights: [1-9][0-9]* shading points' cloud16.log >/dev/null ||
    { echo "subsurface_cloud_internal: the cloud did not serve the buried light" >&2; cat cloud16.log; exit 1; }
# the vein slab's point spacing, which the cloud's glow is resolved to
SPACING=$(tr '\n' ' ' < vein16.log | sed -n 's/.*points \([0-9.e+-]*\) apart.*/\1/p')
[ -n "$SPACING" ] || { echo "subsurface_cloud_internal: no point spacing in the statistics" >&2; exit 1; }
export SPACING
. "$SRCDIR/tests/render/image.sh"
image_awk '
function luminance(path, v,    n, x, y) {
    n = read_ppm(path, v)
    for (y = 0; y < IMAGE_H; y++) for (x = 0; x < IMAGE_W; x++)
        L[x, y] = 0.2126 * v[3 * (y * IMAGE_W + x)] + 0.7152 * v[3 * (y * IMAGE_W + x) + 1] + 0.0722 * v[3 * (y * IMAGE_W + x) + 2]
}
# over the disc well inside the sphere: the mean, and the noise as 1.4826 x median |L - its 5x5 median| over the mean
function measure(path, label,    v, x, y, dx, dy, k, m, win, res, c, r, sum) {
    luminance(path, v)
    c = (IMAGE_W - 1) / 2; r = 0.3 * IMAGE_W; m = 0; sum = 0
    for (y = 2; y < IMAGE_H - 2; y++) for (x = 2; x < IMAGE_W - 2; x++) {
        if ((x - c) ^ 2 + (y - c) ^ 2 > r ^ 2) continue
        k = 0
        for (dy = -2; dy <= 2; dy++) for (dx = -2; dx <= 2; dx++) win[k++] = L[x + dx, y + dy]
        res[m++] = abs(L[x, y] - median(win, k))
        sum += L[x, y]
    }
    MEAN[label] = sum / m
    NOISE[label] = 1.4826 * median(res, m) / MEAN[label]
    printf "%s: mean %.6g, relative noise %.4f\n", label, MEAN[label], NOISE[label]
}
# the vein glow across the vein, columns averaged over rows and folded; the view is 2 mm wide: where it falls to half its
# value 0.05 mm from the vein axis, past the uncollided core
function glow(path, label,    v, x, y, p, n, at, ref, i, target, w) {
    luminance(path, v)
    n = IMAGE_W
    for (x = 0; x < n; x++) { p[x] = 0; for (y = 0; y < IMAGE_H; y++) p[x] += L[x, y] / IMAGE_H }
    for (x = 0; x < n / 2; x++) { w = (p[n / 2 + x] + p[n / 2 - 1 - x]) / 2; at[x] = (x + 0.5) * 2 / n; q[x] = w }
    for (i = 0; at[i + 1] < 0.05; i++) ;
    ref = q[i] + (q[i + 1] - q[i]) * (0.05 - at[i]) / (at[i + 1] - at[i]); target = ref / 2
    for (; i < n / 2 - 1; i++)
        if (q[i] >= target && q[i + 1] < target) {
            WIDTH[label] = at[i] + (q[i] - target) / (q[i] - q[i + 1]) * (at[i + 1] - at[i])
            break
        }
    GLOW[label] = 0; for (x = 0; x < n; x++) GLOW[label] += p[x] / n
    printf "%s: mean %.6g, glow half-width %.4f mm\n", label, GLOW[label], WIDTH[label]
}
# over the same disc, the darkest and brightest pixels against the median
function extremes(path, label,    v, x, y, m, c, r, a, lo, hi) {
    luminance(path, v)
    c = (IMAGE_W - 1) / 2; r = 0.3 * IMAGE_W; m = 0; lo = 1e30; hi = -1e30
    for (y = 2; y < IMAGE_H - 2; y++) for (x = 2; x < IMAGE_W - 2; x++) {
        if ((x - c) ^ 2 + (y - c) ^ 2 > r ^ 2) continue
        a[m++] = L[x, y]; if (L[x, y] < lo) lo = L[x, y]; if (L[x, y] > hi) hi = L[x, y]
    }
    m = median(a, m)
    DARK[label] = lo / m; BRIGHT[label] = hi / m
    printf "%s: darkest %.3f, brightest %.3f of the median\n", label, DARK[label], BRIGHT[label]
}
# each pixel against the mean over its column of the reference: the first percentile and the share below half
function columns(path, ref, label,    v, x, y, p, a, n, low) {
    luminance(ref, v)
    for (x = 0; x < IMAGE_W; x++) { p[x] = 0; for (y = 0; y < IMAGE_H; y++) p[x] += L[x, y] / IMAGE_H }
    luminance(path, v)
    n = 0; low = 0
    for (y = 0; y < IMAGE_H; y++) for (x = 0; x < IMAGE_W; x++) { a[n] = L[x, y] / p[x]; if (a[n] < 0.5) low++; n++ }
    median(a, n)
    FIRST[label] = a[int(n / 100)]; LOW[label] = low / n
    printf "%s: first percentile %.3f of the paths, %.4f of pixels below half\n", label, FIRST[label], LOW[label]
}
function median(a, n,    i, j, t) {
    for (i = 1; i < n; i++) { t = a[i]; for (j = i - 1; j >= 0 && a[j] > t; j--) a[j + 1] = a[j]; a[j + 1] = t }
    return (n % 2) ? a[(n - 1) / 2] : 0.5 * (a[n / 2 - 1] + a[n / 2])
}
BEGIN {
    measure("cloud16.ppm", "cloud16"); measure("paths16.ppm", "paths16"); measure("cloud64.ppm", "cloud64")
    measure("cloud64big.ppm", "cloud64big"); measure("paths256.ppm", "paths256")
    measure("bounce64.ppm", "bounce64"); measure("bounce256.ppm", "bounce256")
    glow("vein16.ppm", "vein16"); glow("vein256.ppm", "vein256")
    extremes("heavy4.ppm", "heavy4"); columns("thin2.ppm", "thin256.ppm", "thin2")
    # Each cloud point walks its own paths, so the mean over a region carries the noise of its few points: the means are compared at 64
    # samples, where that is a few percent. A scale moves the cells, and with them those draws, so it is compared the same way.
    if (!(MEAN["paths256"] > 0.01)) fail("buried light failed to light the body")
    if (!(NOISE["cloud16"] < 0.25 * NOISE["paths16"])) fail("cloud buried light is not smooth: " NOISE["cloud16"] " against " NOISE["paths16"])
    if (abs(MEAN["cloud64"] / MEAN["paths256"] - 1) > 0.08) fail("cloud buried light mean differs from the paths: " MEAN["cloud64"] / MEAN["paths256"])
    if (!(MEAN["bounce256"] > 1.02 * MEAN["paths256"])) fail("the bounce volume added no light")
    if (abs(MEAN["bounce64"] / MEAN["bounce256"] - 1) > 0.08) fail("cloud bounced buried light differs from the paths: " MEAN["bounce64"] / MEAN["bounce256"])
    if (abs(MEAN["cloud64big"] / MEAN["cloud64"] - 1) > 0.08) fail("cloud buried light changes with scene scale: " MEAN["cloud64big"] / MEAN["cloud64"])
    # The cloud holds the uncollided core too, blurred to its points, so its glow differs from the paths' by up to a spacing.
    if (abs(WIDTH["vein16"] - WIDTH["vein256"]) > ENVIRON["SPACING"]) fail("cloud vein glow is not as localised as the paths: " WIDTH["vein16"] " against " WIDTH["vein256"] " mm, points " ENVIRON["SPACING"] " mm apart")
    if (abs(GLOW["vein16"] / GLOW["vein256"] - 1) > 0.06) fail("cloud vein glow mean differs from the paths: " GLOW["vein16"] / GLOW["vein256"])
    # A heavy path once set the lean of a point to tens of times its value, which drove pixels black or several times too bright.
    if (!(DARK["heavy4"] > 0.1) || !(BRIGHT["heavy4"] < 4)) fail("the lean of a heavy path shows: " DARK["heavy4"] " to " BRIGHT["heavy4"] " of the median")
    # Paths that left the thin slab unscattered once carried nothing, so points at two paths were often dark.
    if (!(FIRST["thin2"] > 0.25) || !(LOW["thin2"] < 0.08)) fail("thin host points without light: " FIRST["thin2"] ", " LOW["thin2"])
    na = read_ppm("cloud16.ppm", a); nb = read_ppm("cloud16one.ppm", b)
    if (!same_values(a, na, b, nb)) fail("cloud buried light depends on the thread count")
}'
echo 'subsurface_cloud_internal: smooth, matches the per-pixel paths and their vein footprint, thread and scale independent'
