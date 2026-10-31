#!/bin/sh
# identical.sh <povray> <srcdir> [name...]: each 4.0 fixture, its classic twin and its lowered SDL render the same pixels.
set -e
POVRAY=$1; SRCDIR=$2; shift 2
DIR="$SRCDIR/tests/pov4"
NAMES=${*:-"basics components points control mixed loop layered review interop types stills subsurface framing framing_tagged tags_inherit default_media refraction"}
render() {
    "$POVRAY" +i"$1" +L"$DIR" +L"$SRCDIR/include" +w160 +h120 -a -d -p -v -gp +fp16 +o"$2.ppm" $3
    tail -c 115200 "$2.ppm" > "$2.px"
}
for n in $NAMES; do
    render "$DIR/$n.pov" "pov4_${n}_classic"
    render "$DIR/$n.pov4" "pov4_${n}_new" "+GLpov4_${n}_lowered.pov"
    render "pov4_${n}_lowered.pov" "pov4_${n}_lowered"
    cmp "pov4_${n}_classic.px" "pov4_${n}_new.px"
    cmp "pov4_${n}_classic.px" "pov4_${n}_lowered.px"
    echo "pov4 $n: identical"
done
render "$DIR/basics_v37.pov" pov4_v37_classic
render "$DIR/basics.pov4" pov4_v37_new +ML3.7
cmp pov4_v37_classic.px pov4_v37_new.px
echo "pov4 +ML3.7: identical to the classic scene at #version 3.7"
fails() {
    if "$POVRAY" +i"$DIR/$1.pov4" +L"$DIR" +L"$SRCDIR/include" +w16 +h12 -d -p -gp -f $3 > "pov4_$1.log" 2>&1; then
        echo "pov4 $1: expected a parse error"; exit 1
    fi
    tr -d "\n" < "pov4_$1.log" | grep -q "$2"
    echo "pov4 $1: located error"
}
fails bad_top_level "bad_top_level.pov4:3:3: A float cannot appear at the top level"
fails err_reserved "err_reserved.pov4:1:5: Names starting with '__pov4_' are reserved"
fails err_include "err_items.inc4' line 2"
fails err_loop "err_loop.pov4' line 2"
fails err_depth "err_depth.pov4:1:11: Function calls nested deeper than 10000 levels"
fails err_prop "'finish' has no item 'phon'; did you mean 'phong'?"
fails err_subsurface "'subsurface' has no item 'memroy'; did you mean 'memory'?"
fails err_member "has no member 'size'."
fails err_operand "The left operand of '+' must be a number, vector or colour, not a string."
fails err_param "'Scale' uses parameter 'V' as"
fails err_arity "'Place' takes 2 arguments, but this call passes 1."
fails err_logic "'Both' uses parameter 'P' as a number or a vector or a colour, but this call passes a string."
fails err_input_required "err_input_required.pov4:2:1: Input 'Count' has no default, so it must be supplied"
fails err_input_required "err_input_required.pov4:3:15: Input 'Count' is used here as a number" 'Declare=Count="many"'
fails err_input_default "err_input_default.pov4:3:13: The left operand of '\*' must be a number"
fails err_input_unknown "no input 'Qualty'; did you mean 'Quality'?" Declare=Qualty=5
fails ok_input "ok_input.pov4:2:1: Input 'Radius' defaults to a number, but Declare=Radius supplies a string." "Declare=Radius=big Declare=Label=x"
accepts() {
    "$POVRAY" +i"$DIR/$1.pov4" +L"$DIR" +L"$SRCDIR/include" +w16 +h12 -d -p -gp -f $3 > "pov4_$1.log" 2>&1 || { cat "pov4_$1.log"; exit 1; }
    [ -z "$2" ] || tr -d "\n" < "pov4_$1.log" | grep -q "$2"
    echo "pov4 $1: accepted"
}
accepts ok_refraction_components
fails err_refraction_prop "'refraction' has no item 'detial'; did you mean 'detail'?"
accepts ok_vector_builtins
accepts ok_negate
accepts ok_string_compare
accepts ok_debug_statement "pov4 debug statement"
accepts ok_bare_comparisons "pov4 bare comparisons"
accepts ok_input "pov4 input hi early" "Declare=Label=hi"
accepts ok_input "pov4 input C:.work early" 'Declare=Label="C:\work"'
accepts ok_input "Declare=Spare supplies input 'Spare', which the scene never uses." 'Declare=Label="ab" Declare=Spare=1'
rm -f pov4_*.ppm pov4_*.px pov4_*.log pov4_*_lowered.pov
