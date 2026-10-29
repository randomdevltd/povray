#!/bin/sh
set -eu
POVRAY=$1
SRCDIR=$2
render() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/media_prepared_photons.pov" +w12 +h12 +pr -a -d -p -v -gp \
        +wt2 +ss0 +fp16 File_Gamma=1 +o"media_prepared_photons_$name.ppm" "$@" \
        > "media_prepared_photons_$name.log" 2>&1 || { cat "media_prepared_photons_$name.log"; exit 1; }
}
for method in 1 2; do
    render "${method}_clear" Declare=PhotonMethod="$method" Declare=Absorbing=0
    render "${method}_constant" Declare=PhotonMethod="$method"
    render "${method}_varying" Declare=PhotonMethod="$method" Declare=Varying=1
done
render 2_inside_clear Declare=PhotonMethod=2 Declare=InsideSource=1 Declare=Absorbing=0
render 2_inside_constant Declare=PhotonMethod=2 Declare=InsideSource=1
render 2_inside_varying Declare=PhotonMethod=2 Declare=InsideSource=1 Declare=Varying=1
. "$SRCDIR/tests/render/image.sh"
# Parallel Beer integrals: [1,2] gives (1,5/8); [1,3/2] gives (1/2,9/32).
image_awk '
function pixels(stem, v,    n, path, text, line) {
    n = read_samples(stem ".ppm", v)
    if (IMAGE_W != 12 || IMAGE_H != 12) fail(stem ": wrong dimensions " IMAGE_W "x" IMAGE_H)
    if (n != 12 * 12 * 3) fail(stem ": incomplete raster")
    if (!(min_of(v, n) > 128)) fail(stem ": dark pixels make attenuation vacuous")
    if (!(max_of(v, n) < 65000)) fail(stem ": clipping prevents attenuation comparison")
    path = stem ".log"
    text = ""
    while ((getline line < path) > 0) text = text line "\n"
    close(path)
    if (!match(text, /Surface photons stored:[[:space:]]*[0-9]+/) || !(substr(text, RSTART + 23, RLENGTH - 23) + 0 > 0))
        fail(stem ": no surface photon evidence")
    return n
}
BEGIN {
    split("1||1.0|0.625 2||1.0|0.625 2|inside_|0.5|0.28125", runs, " ")
    split(".2 .4 .6", sigma, " ")
    for (r = 1; r <= 3; r++) {
        split(runs[r], run, "|")
        prefix = "media_prepared_photons_" run[1] "_" run[2]
        n = pixels(prefix "clear", clear)
        for (k = 0; k < 2; k++) {
            kind = k ? "varying" : "constant"
            pixels(prefix kind, measured)
            for (c = 0; c < 3; c++) {
                expected = exp(-sigma[c + 1] * run[3 + k])
                error = total = 0
                low = high = measured[c] / clear[c]
                for (i = c; i < n; i += 3) {
                    ratio = measured[i] / clear[i]
                    total += ratio
                    if (abs(ratio - expected) > error) error = abs(ratio - expected)
                    if (ratio < low) low = ratio
                    if (ratio > high) high = ratio
                }
                # Fixed 3% flux budget covers photon-map quantization and 16-bit output.
                if (!(error < 0.03))
                    fail(sprintf("method %s %s%s channel %d: expected %.6f, ratios %.6f to %.6f", run[1], run[2], kind, c, expected, low, high))
                printf "method %s %s%s channel %d: expected %.6f, measured %.6f, max error %.6f\n", run[1], run[2], kind, c, expected, total / (n / 3), error
            }
        }
    }
}'
if "$POVRAY" +i"$SRCDIR/tests/render/media_prepared_photons.pov" +w12 +h12 +pr -a -d -p -v -gp \
    +wt2 -f Declare=PhotonMethod=1 Declare=InsideSource=1 Declare=NativePointInside=1 \
    > media_prepared_photons_native_inside.log 2>&1; then
    cat media_prepared_photons_native_inside.log; exit 1
fi
grep -Fq 'Native photon method 1 requires source-origin support outside prepared scattering media; use photon method 2.' media_prepared_photons_native_inside.log
rm -f media_prepared_photons_*.ppm media_prepared_photons_*.log
echo 'media_prepared_photons: finite constant and varying photon-ray Beer attenuation passed'
