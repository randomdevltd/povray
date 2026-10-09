#!/bin/sh
# media_solid.sh <povray> <srcdir>: interior media inside non-hollow objects, per language version, and media_blend.
set -e
POVRAY=$1; SRCDIR=$2
render() {
    name=$1; shift
    "$POVRAY" +i"$SRCDIR/tests/render/media_solid.pov" +w96 +h72 -a -d -p -v -gp +fp16 File_Gamma=1.0 +o"media_solid_$name.ppm" "$@" > "media_solid_$name.log" 2>&1 ||
        { cat "media_solid_$name.log"; exit 1; }
}
says() {
    tr -s ' \n' '  ' < "media_solid_$2.log" | grep -q "$1"
}
present() {
    says "$1" "$2" || { echo "media_solid: $2 does not warn: $1" >&2; cat "media_solid_$2.log" >&2; exit 1; }
}
absent() {
    if says "$1" "$2"; then echo "media_solid: $2 warns: $1" >&2; exit 1; fi
}
red() {
    tail -c $(( (96 * 72 - (36 * 96 + $2)) * 6 )) "media_solid_$1.ppm" | head -c 2 | od -An -tu1 | awk '{ printf "%.4f", ($1 * 256 + $2) / 65535 }'
}
expect() {
    name=$1; col=$2; want=$3; ref=${4:-1}
    got=$(awk -v a="$(red $name $col)" -v r="$ref" 'BEGIN { printf "%.4f", a / r }')
    awk -v a="$got" -v b="$want" 'BEGIN { exit !(a - b < 0.004 && b - a < 0.004) }' ||
        { echo "media_solid: $name at column $col is $got, expected $want" >&2; exit 1; }
}
render 40_front Declare=Version=4.0 Declare=Hollow=0
render 40_flipped Declare=Version=4.0 Declare=Hollow=0 Declare=Backdrop=2
render 40_hollow Declare=Version=4.0 Declare=Backdrop=1
render 40_background Declare=Version=4.0 Declare=Hollow=0 Declare=Backdrop=0
render 37_front Declare=Version=3.7
render 37_flipped Declare=Version=3.7 Declare=Backdrop=2
render 37_solid Declare=Version=3.7 Declare=Hollow=0 Declare=Backdrop=2
for name in 40_front 40_flipped 40_hollow 40_background 37_flipped; do expect $name 48 0.3679; done
expect 37_front 48 1
expect 37_solid 48 1
present "camera is inside non-hollow plane" 37_front
present "non-hollow object carries interior media" 37_solid
absent "camera is inside" 40_front
absent "carries interior media" 40_front
render add Declare=Version=4.0 Declare=Nested=1
render inner Declare=Version=4.0 Declare=Nested=1 Declare=Blend=1
render default_inner Declare=Version=4.0 Declare=Nested=1 Declare=DefaultBlend=1
render subtract Declare=Version=4.0 Declare=Nested=1 Declare=Blend=2
render multiply Declare=Version=4.0 Declare=Nested=1 Declare=Blend=3
render clear Declare=Version=4.0 Declare=Nested=1 Declare=Blend=4
render sibling Declare=Version=4.0 Declare=Nested=1 Declare=Blend=4 Declare=Sibling=1
render csg Declare=Version=4.0 Declare=Nested=1 Declare=Blend=4 Declare=Csg=1
render csg_inner Declare=Version=4.0 Declare=Nested=1 Declare=Blend=1 Declare=Csg=1
render lit_add Declare=Version=4.0 Declare=Nested=1 Declare=Lit=1
render lit_clear Declare=Version=4.0 Declare=Nested=1 Declare=Blend=4 Declare=Lit=1
expect add 48 0.1353
expect inner 48 0.2231
expect default_inner 48 0.2231
expect subtract 48 0.6065
expect multiply 48 0.4966
expect clear 48 0.6065
expect sibling 48 0.3679
expect csg 48 0.6065
expect csg_inner 48 0.2231
for name in lit_add lit_clear; do expect $name 66 0.1353 "$(red $name 2)"; done
expect lit_add 48 0.0183 "$(red lit_add 2)"
expect lit_clear 48 0.3679 "$(red lit_clear 2)"
for blend in 0 2 3; do render varying_$blend Declare=Version=4.0 Declare=Nested=1 Declare=Varying=1 Declare=Blend=$blend; done
for blend in 0 2 3; do tail -c 41472 media_solid_varying_$blend.ppm > media_solid_varying_$blend.px; done
for blend in 2 3; do
    if cmp -s media_solid_varying_0.px media_solid_varying_$blend.px; then echo "media_solid: varying blend $blend matches add" >&2; exit 1; fi
done
for name in add inner default_inner subtract multiply clear sibling csg csg_inner; do expect $name 66 0.3679; done
for name in add inner default_inner subtract multiply clear sibling csg csg_inner lit_add lit_clear varying_0 varying_2 varying_3; do
    absent "classic sampling" $name
done
rm -f media_solid_*.ppm media_solid_*.px media_solid_*.log
echo "media_solid: interior media render inside non-hollow objects under 4.0, and media_blend combines nested media"
