#!/bin/sh
# subsurface_cloud.sh <povray> <srcdir>: a plane, a light group's part, a thin wall and a smooth mesh (by its face's area) are served
# by the point cloud, a cloud past a cap the scene sets stops the render and an uncapped render never speaks of one, the cloud and
# image do not depend on threads, a panorama is served.
set -e
trap 'rm -f sscloud_*' EXIT
POVRAY=$1; SRCDIR=$2
render() {
    name=$1; scene=$2; shift 2
    "$POVRAY" +i"$scene" +L"$SRCDIR/distribution/include" -d -p -a +fp16 +o"sscloud_$name.ppm" "$@" > "sscloud_$name.log" 2>&1
}
text() { tr -d ' \r\n' < "sscloud_$1.log" > "sscloud_$1.txt"; }
fail() { echo "subsurface_cloud: $*"; exit 1; }
has() { grep -q -- "$2" "sscloud_$1.txt"; }
warned() { has "$1" "Subsurfacemethod2fellbacktomethod1for[0-9]*of[0-9]*shadingpointsas$2"; }
served() { sed -n 's/.*Method2pointcloud:\([0-9]*\)shadingpointstried,\([0-9]*\)served.*/\1 \2/p' "sscloud_$1.txt"; }
share() { served "$1" | { read tried got; [ "$tried" -gt 0 ] && [ $((got * 100)) -ge $((tried * $2)) ]; }; }
built() { sed -n 's/.*Method2pointcloudatspacing[0-9.]*:[0-9.]*MBagainstthecap,peak[0-9.]*MBwithrowsbeingcast;[0-9]*cells,\([0-9]*\)pointsbuilt,\([0-9]*\)read;.*/\1 \2/p' "sscloud_$1.txt"; }
held() { sed -n 's/.*Method2pointcloudatspacing[0-9.]*:\([0-9.]*\)MBagainstthecap.*/\1/p' "sscloud_$1.txt"; }

case=$SRCDIR/tests/render/subsurface_cloud.pov
render plain "$case" +w64 +h48 +wt2 Declare=Case=0; text plain
has plain 'Subsurfacemethod2fellback' && fail "a closed box warned of a fallback"
has plain 'memorycap' && fail "a render with no cap set spoke of a memory cap"
share plain 95 || fail "a closed box was not served by the cloud: $(served plain)"
if render capped "$case" +w64 +h48 +wt2 Declare=Case=1; then fail "a cloud past its memory cap did not stop the render"; fi
text capped
has capped 'Subsurfacecloudsneedatleast[0-9.]*MBatspacing0.02after[0-9]*%oftheimage,pasttheirmemorycapof1MB' ||
    fail "no error names the memory cap"
has capped 'Raisethecapabovememory[0-9.]*,orsetspacing[0-9.]*ormore(about[0-9.]*MB' || fail "the memory error gives no remedies"
if render cell "$case" +w64 +h48 +wt2 Declare=Case=5; then fail "a cell past the memory cap did not stop the render"; fi
text cell
has cell 'Subsurfacecloudsneedatleast1[.0-9]*MBatspacing0.0001after' || fail "a cell past the cap was not stopped as it cast"
for c in 2:plane 3:group 4:thin; do
    render ${c#*:} "$case" +w64 +h48 +wt2 Declare=Case=${c%:*}; text ${c#*:}
    has ${c#*:} 'Subsurfacemethod2fellback' && fail "the ${c#*:} warned of a fallback"
    has ${c#*:} 'Subsurfacemethod2left' && fail "the ${c#*:} left shading points without subsurface light"
    share ${c#*:} 100 || fail "the ${c#*:} was not served by the cloud: $(served ${c#*:})"
done

flux=$SRCDIR/tests/render/subsurface_photon_flux.pov
for m in 1:flat 2:smooth; do
    render ${m#*:} "$flux" +w64 +h48 +wt2 Declare=Photons=0 Declare=Direct=1 Declare=MeshMode=${m%:*}; text ${m#*:}
    share ${m#*:} 100 || fail "the ${m#*:} mesh was not served by the cloud: $(served ${m#*:})"
done
centre() { tail -c 18432 "sscloud_$1.ppm" | od -An -v -tu1 | awk '{ for (k = 1; k <= NF; k++) b[n++] = $k }
    END { for (i = 0; i < n / 2; i++) { p = int(i / 3); if (p % 64 >= 16 && p % 64 < 48 && p >= 768 && p < 2304) s += b[2 * i] * 256 + b[2 * i + 1] } print s }'; }
ratio=$(echo "$(centre smooth) $(centre flat)" | awk '{ printf "%.3f", $1 / $2 }')
awk -v r="$ratio" 'BEGIN { exit !(r > 0.975 && r < 1.01) }' ||
    fail "a smooth mesh's points did not take their face's area: smooth over flat $ratio, expected 0.993 from its normals' cosines (0.898 when points took the interpolated normal's area)"

bench=$SRCDIR/tools/bench/sslt-projection.pov
render mercator "$bench" +w96 +h48 +wt2 Declare=Camera=0 Declare=Count=400; text mercator
has mercator 'Subsurfacemethod2fellback' && fail "the user-defined panorama warned of a fallback"
share mercator 98 || fail "the user-defined panorama was not served by the cloud: $(served mercator)"
built mercator | { read points read; [ "$points" -gt 0 ] && [ "$read" -gt 0 ] && [ "$read" -le "$points" ]; } ||
    fail "the statistics do not report points built and read: $(built mercator)"
cap=$(held mercator | awk '{print int($1) + 1}')
render near "$bench" +w96 +h48 +wt2 Declare=Camera=0 Declare=Count=400 Declare=Memory=$cap || fail "a cloud just under its cap stopped the render"
text near
[ "$(built near)" = "$(built mercator)" ] || fail "a cloud just under its cap was built differently"
render coarse "$bench" +w96 +h48 +wt2 Declare=Camera=0 Declare=Count=400 Declare=Spacing=2; text coarse
fine=$(built mercator | cut -d' ' -f1); coarse=$(built coarse | cut -d' ' -f1)
[ $((coarse * 8)) -ge "$fine" ] && [ $((coarse * 2)) -le "$fine" ] || fail "spacing 2 did not build about a quarter of the points: $fine, $coarse"
render one "$bench" +w160 +h90 +wt1 Declare=Camera=1 Declare=Count=1000; text one
render many "$bench" +w160 +h90 Declare=Camera=1 Declare=Count=1000; text many
tail -c 86400 sscloud_one.ppm > sscloud_one.px; tail -c 86400 sscloud_many.ppm > sscloud_many.px
cmp -s sscloud_one.px sscloud_many.px || fail "one thread and many rendered different images"
[ -n "$(built one)" ] && [ "$(built one)" = "$(built many)" ] || fail "one thread and many built different clouds"
echo "subsurface_cloud: the cloud serves every case, the memory cap stops the render, and the cloud is the same on any thread count"
