#!/bin/sh
# Fixed sample budgets, thread determinism, small/large hosts, and visible bounced light.
set -eu
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"
render() {
    name=$1; shift
    timeout 30 "$POVRAY" +i"$SRCDIR/tests/render/surface_gather_samples.pov" +w32 +h24 +wt2 +bs4 +pr -a -d -p +fp16 +o"$name.ppm" "$@" >"$name.log" 2>&1 ||
        { cat "$name.log"; exit 1; }
    tail -c 4608 "$name.ppm" >"$name.rgb"
}
render stress
grep -q 'Preparation: bounce finished' stress.log
sed -n 's/.*Bounce o[0-9]*: \([0-9]*\) nodes.*/\1/p' stress.log |
    awk '$1 > 1536 { bad=1 } END { exit (bad || NR==0) }'
render dark Declare=WithBounce=0
if cmp -s stress.rgb dark.rgb; then echo 'surface_gather_samples: no visible bounced light'; exit 1; fi
for samples in 1 4 8 64; do
    render "samples$samples" Declare=Cells=32 Declare=Gather=$samples
done
render serial Declare=Cells=32 +wt1
render parallel Declare=Cells=32 +wt4
cmp serial.rgb parallel.rgb
render small Declare=Cells=16 Declare=Gather=4 Declare=S=0.001
render large Declare=Cells=16 Declare=Gather=4 Declare=S=1000
render exported Declare=Cells=32 Declare=Export=1
render globalone Declare=Cells=32 Declare=Gather=4 Declare=Nearby=0 Declare=Export=1 Declare=HideHosts=1 Declare=SS=1
render globalsixteen Declare=Cells=32 Declare=Gather=4 Declare=Nearby=0 Declare=Export=1 Declare=HideHosts=1 Declare=SS=16
cmp globalone.rgb globalsixteen.rgb
echo 'surface_gather_samples: ok'
