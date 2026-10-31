#!/bin/sh
# check-progressive-photons.sh REFERENCE CANDIDATE... [--size WIDTH HEIGHT] [--rect X0 Y0 X1 Y1]: one JSON line per
# linear 16-bit PPM candidate against an optionally supersampled PPM or Radiance HDR reference.
set -eu
SRCDIR=$(cd "$(dirname "$0")/../.." && pwd)
. "$SRCDIR/tests/render/image.sh"
usage() {
    echo 'usage: check-progressive-photons.sh REFERENCE CANDIDATE... [--size WIDTH HEIGHT] [--rect X0 Y0 X1 Y1]' >&2
    exit 2
}
size=
rect=
left=$#
while [ "$left" -gt 0 ]; do
    case "$1" in
        --size) [ "$left" -ge 3 ] || usage; size="$2 $3"; shift 3; left=$((left - 3)) ;;
        --rect) [ "$left" -ge 5 ] || usage; rect="$2 $3 $4 $5"; shift 5; left=$((left - 5)) ;;
        *) set -- "$@" "$1"; shift; left=$((left - 1)) ;;
    esac
done
[ "$#" -ge 2 ] || usage
image_awk '
function load(path, v,    b, n, signature, i, r, g, bl, e) {
    n = file_bytes(path, b)
    signature = ""
    for (i = 0; i < n && b[i] != 10 && i < 16; i++) signature = signature sprintf("%c", b[i])
    if (signature == "P6") return read_ppm(path, v)
    if (signature != "#?RADIANCE") fail(path ": expected Radiance HDR or linear PPM")
    n = read_hdr(path, r, g, bl, e)
    for (i = 0; i < n; i++) {
        v[3 * i] = e[i] ? (r[i] + 0.5) * 2 ^ (e[i] - 136) : 0
        v[3 * i + 1] = e[i] ? (g[i] + 0.5) * 2 ^ (e[i] - 136) : 0
        v[3 * i + 2] = e[i] ? (bl[i] + 0.5) * 2 ^ (e[i] - 136) : 0
    }
    return 3 * n
}
function reduce(values, rw, rh, width, height, result,    sx, sy, x, y, c, source, target, i) {
    if (rw % width || rh % height || rw < width || rh < height) fail("reference must be an integer supersample of the candidate")
    sx = int(rw / width); sy = int(rh / height)
    for (i = 0; i < width * height * 3; i++) result[i] = 0
    for (y = 0; y < rh; y++)
        for (x = 0; x < rw; x++) {
            source = 3 * (y * rw + x)
            target = 3 * (int(y / sy) * width + int(x / sx))
            for (c = 0; c < 3; c++) result[target + c] += values[source + c]
        }
    for (i = 0; i < width * height * 3; i++) result[i] /= sx * sy
    return width * height * 3
}
function basename(path) {
    sub(/.*\//, "", path)
    return path
}
BEGIN {
    split(ARGV[1], size, " "); split(ARGV[2], rect, " ")
    load(ARGV[3], reference); rw = IMAGE_W; rh = IMAGE_H
    for (argument = 4; argument < ARGC; argument++) {
        candidate = ARGV[argument]
        n = load(candidate, values); width = IMAGE_W; height = IMAGE_H
        if (ARGV[1] != "") {
            n = reduce(values, width, height, size[1], size[2], reduced)
            width = size[1] + 0; height = size[2] + 0
            for (i = 0; i < n; i++) values[i] = reduced[i]
        }
        reduce(reference, rw, rh, width, height, expected)
        count = 0
        if (ARGV[2] != "") {
            if (!(0 <= rect[1] && rect[1] < rect[3] && rect[3] <= width && 0 <= rect[2] && rect[2] < rect[4] && rect[4] <= height))
                fail("rectangle must lie inside the compared image")
            for (y = rect[2]; y < rect[4]; y++)
                for (x = rect[1]; x < rect[3]; x++)
                    for (c = 0; c < 3; c++) { a[count] = values[3 * (y * width + x) + c]; b[count++] = expected[3 * (y * width + x) + c] }
        } else
            for (i = 0; i < n; i++) { a[count] = values[i]; b[count++] = expected[i] }
        mse = mean = reference_mean = 0
        for (i = 0; i < count; i++) { mse += (a[i] - b[i]) ^ 2; mean += a[i]; reference_mean += b[i] }
        mse /= count; mean /= count; reference_mean /= count
        printf "{\"candidate\": \"%s\", \"mse\": %.17g, \"rms\": %.17g, \"mean\": %.17g, \"reference_mean\": %.17g, ",
            basename(candidate), mse, sqrt(mse), mean, reference_mean
        printf "\"energy_ratio\": %s, \"peak\": %.17g, \"reference_peak\": %.17g}\n",
            reference_mean ? sprintf("%.17g", mean / reference_mean) : "null", max_of(a, count), max_of(b, count)
    }
}' "$size" "$rect" "$@"
