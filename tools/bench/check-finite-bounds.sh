#!/bin/sh
set -eu
if [ "$#" -ne 2 ]; then
    echo "usage: $0 configured-build-directory output-directory" >&2
    exit 2
fi
source_root=$(cd "$(dirname "$0")/../.." && pwd)
build_dir=$(cd "$1" && pwd)
mkdir -p "$2"
output_dir=$(cd "$2" && pwd)
mkdir -p "$output_dir/tmp" "$output_dir/cache"
export TMPDIR="$output_dir/tmp"
cat > "$output_dir/finite-bounds.mk" <<'MAKE'
BOUNDS_FLAGS = -I$(BOUNDS_SOURCE)/source $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) $(AM_CPPFLAGS) $(CPPFLAGS) $(CXXFLAGS) -UNDEBUG $(EXTRA_CXXFLAGS)
.PHONY: check-finite-bounds
check-finite-bounds:
	$(CXX) $(BOUNDS_FLAGS) $(BOUNDS_SOURCE)/tests/source/finite_bounds.cpp -o $(BOUNDS_OUTPUT)/finite-bounds
	$(BOUNDS_OUTPUT)/finite-bounds
	$(CXX) $(BOUNDS_FLAGS) $(BOUNDS_SOURCE)/tests/source/finite_geometry_bounds.cpp -Wl,--start-group $(top_builddir)/source/libpovray.a $(top_builddir)/platform/libplatform.a $(ldadd_platformcpu) -Wl,--end-group $(LIBS) -o $(BOUNDS_OUTPUT)/finite-geometry-bounds
	$(BOUNDS_OUTPUT)/finite-geometry-bounds $(BOUNDS_OUTPUT)/cache
MAKE
make -C "$build_dir/unix" -f Makefile -f "$output_dir/finite-bounds.mk" \
    check-finite-bounds BOUNDS_SOURCE="$source_root" BOUNDS_OUTPUT="$output_dir" EXTRA_CXXFLAGS="${EXTRA_CXXFLAGS:-}"
