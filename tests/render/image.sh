# Sourced by render suites: image_awk [-l LIBRARY]... PROGRAM [ARG...] runs PROGRAM after image.awk and
# each tests/render/LIBRARY.awk; set AWK to choose the interpreter.
image_awk() {
    image_program=$(cat "$SRCDIR/tests/render/image.awk")
    while [ "$1" = -l ]; do
        image_program="$image_program
$(cat "$SRCDIR/tests/render/$2.awk")"
        shift 2
    done
    image_program="$image_program
$1"
    shift
    ${AWK:-awk} "$image_program" "$@"
}
