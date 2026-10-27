#!/bin/sh
set -eu
if [ "$#" -ne 2 ]; then
    echo 'usage: check-sphere-intersection.sh configured-build-directory output-directory' >&2
    exit 2
fi
source_root=$(cd "$(dirname "$0")/../.." && pwd)
build_dir=$(cd "$1" && pwd)
mkdir -p "$2"
output_dir=$(cd "$2" && pwd)
mkdir -p "$output_dir/tmp"
export TMPDIR="$output_dir/tmp"
cat > "$output_dir/sphere-test.mk" <<'MAKE'
.PHONY: sphere-test
sphere-test:
	$(CXX) $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) $(AM_CPPFLAGS) $(CPPFLAGS) $(CXXFLAGS) -UNDEBUG -ffunction-sections -fdata-sections "$(SPHERE_SOURCE)" "$(SPHERE_TEST)" -Wl,--gc-sections -o "$(SPHERE_BINARY)"
MAKE
make -C "$build_dir/source" -f Makefile -f "$output_dir/sphere-test.mk" sphere-test \
    SPHERE_SOURCE="$source_root/source/core/shape/sphere.cpp" \
    SPHERE_TEST="$source_root/tests/source/sphere_intersection.cpp" SPHERE_BINARY="$output_dir/sphere-intersection-test"
"$output_dir/sphere-intersection-test"
