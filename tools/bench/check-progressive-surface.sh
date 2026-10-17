#!/bin/sh
set -eu
if [ "$#" -ne 2 ]; then
    echo 'usage: check-progressive-surface.sh configured-build-directory output-directory' >&2
    exit 2
fi
source_root=$(cd "$(dirname "$0")/../.." && pwd)
build_dir=$(cd "$1" && pwd)
mkdir -p "$2"
output_dir=$(cd "$2" && pwd)
mkdir -p "$output_dir/tmp"
export TMPDIR="$output_dir/tmp"
cat > "$output_dir/surface-test.mk" <<'MAKE'
.PHONY: surface-test
surface-test:
	$(CXX) $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) $(AM_CPPFLAGS) $(CPPFLAGS) $(CXXFLAGS) -UNDEBUG "$(SURFACE_SOURCE)" -o "$(SURFACE_BINARY)"
MAKE
make -C "$build_dir/source" -f Makefile -f "$output_dir/surface-test.mk" surface-test \
    SURFACE_SOURCE="$source_root/tests/source/progressive_surface.cpp" SURFACE_BINARY="$output_dir/progressive-surface-test"
"$output_dir/progressive-surface-test"
