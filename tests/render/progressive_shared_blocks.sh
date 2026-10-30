#!/bin/sh
# progressive_shared_blocks.sh <povray> <srcdir>: +PR pixels stay the same when idle threads help finish blocks.
set -e
POVRAY=$1; SRCDIR=$2
W=67; H=43
trap 'rm -f progressive_shared_*' EXIT
render() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/random_effects.pov" +L"$SRCDIR/distribution/include" +w$W +h$H -d -p -v -gp +fp16 \
        +o"progressive_shared_$name.ppm" +pr "$@" > "progressive_shared_$name.log" 2>&1 || { cat "progressive_shared_$name.log"; exit 1; }
    tail -c $((W * H * 6)) "progressive_shared_$name.ppm" > "progressive_shared_$name.px"
}
# one block holds the whole image, so every thread past the first works only by helping it
for case in "plain -a" "m4 +a0.1 +am4"; do
    set -- $case; kind=$1; shift
    render "$kind-wt1" "$@" +wt1 +bs64
    for threads in 3 8 32; do
        render "$kind-wt$threads" "$@" +wt$threads +bs64
        cmp "progressive_shared_$kind-wt1.px" "progressive_shared_$kind-wt$threads.px"
    done
    render "$kind-blocks" "$@" +wt8 +bs4
    cmp "progressive_shared_$kind-wt1.px" "progressive_shared_$kind-blocks.px"
done
grep -q "Passes: " progressive_shared_plain-wt8.log || { echo "progressive_shared_blocks: no pass timeline"; exit 1; }
echo "progressive_shared_blocks: pixels match across thread counts and block sizes"
