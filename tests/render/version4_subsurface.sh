#!/usr/bin/env bash
set -euo pipefail
POVRAY=${1:?pass the POV-Ray binary}
SCENES=$(cd -- "$(dirname -- "$0")" && pwd)
OUTPUT=${2:?pass a writable output directory}
mkdir -p "$OUTPUT"
for version in 38 40; do
    for method in 0 1 2; do
        "$POVRAY" "+I$SCENES/version4_subsurface_${version}.pov" "+L$SCENES" "+O$OUTPUT/version4_${version}_${method}.ppm" +W96 +H72 +FP16 +WT1 +PR -A -D "Declare=Method=$method" > "$OUTPUT/version4_${version}_${method}.log" 2>&1
    done
    default="$OUTPUT/version4_${version}_0.ppm"
    explicit="$OUTPUT/version4_${version}_$([ "$version" = 40 ] && echo 2 || echo 1).ppm"
    cmp "$default" "$explicit"
done
if cmp -s "$OUTPUT/version4_38_0.ppm" "$OUTPUT/version4_40_0.ppm"; then
    echo 'version4_subsurface: version-gated defaults did not change the image' >&2
    exit 1
fi
