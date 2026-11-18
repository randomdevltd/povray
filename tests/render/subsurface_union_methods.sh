#!/bin/sh
# Method 1 and disabled subsurface retain ordinary union splitting.
set -eu
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"
check() {
    name=$1; version=$2; global=$3; finish=$4; expected=$5; split=${6:-on}
    cat >scene.pov <<SCENE
#version $version;
global_settings { assumed_gamma 1 $global }
camera { location <0, 0, -5> look_at 0 }
union {
    sphere { <-0.5, 0, 0>, 1 }
    sphere { <0.5, 0, 0>, 1 }
    split_union $split
    pigment { rgb 0.5 }
    finish { ambient 0 diffuse 0.5 subsurface { translucency 0.2 $finish } }
    interior { ior 1.4 }
}
SCENE
    "$POVRAY" +iscene.pov +w1 +h1 +wt1 +pr -a -d -p -f >"$name.log" 2>&1 || { cat "$name.log"; exit 1; }
    got=$(sed -n 's/.*Finite Objects: *\([0-9]*\).*/\1/p' "$name.log")
    [ "$got" = "$expected" ] || { cat "$name.log"; echo "$name: expected $expected finite objects, got $got" >&2; exit 1; }
}
check legacy_default 3.7 'subsurface { samples 1, 1 }' '' 2
check legacy_whole 3.7 'subsurface { samples 1, 1 }' '' 1 off
check legacy_explicit 4.0 'subsurface { method 1 samples 1, 1 }' '' 2
check finish_legacy 4.0 'subsurface { method 2 samples 1, 1 }' 'method 1' 2
check disabled 3.7 '' 'method 2' 2
check cloud_default 4.0 'subsurface { samples 1, 1 }' '' 1
check cloud_explicit_legacy_version 3.7 'subsurface { method 2 samples 1, 1 }' '' 1
check finish_cloud_legacy_version 3.7 'subsurface { method 1 samples 1, 1 }' 'method 2' 1
echo 'subsurface_union_methods: ok'
