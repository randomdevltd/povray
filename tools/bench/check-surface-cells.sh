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
mkdir -p "$output_dir/tmp"
export TMPDIR="$output_dir/tmp"
cat > "$output_dir/surface-cells.mk" <<'MAKE'
CELLS_FLAGS = -I$(CELLS_SOURCE)/source $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) $(AM_CPPFLAGS) $(CPPFLAGS) $(CXXFLAGS) -UNDEBUG
.PHONY: check-surface-cells
check-surface-cells:
	$(CXX) $(CELLS_FLAGS) $(CELLS_SOURCE)/tests/source/surface_cells.cpp -Wl,--start-group $(top_builddir)/source/libpovray.a $(top_builddir)/platform/libplatform.a $(ldadd_platformcpu) -Wl,--end-group $(LIBS) -o $(CELLS_OUTPUT)/surface-cells
	$(CELLS_OUTPUT)/surface-cells
MAKE
make -C "$build_dir/unix" -f Makefile -f "$output_dir/surface-cells.mk" \
    check-surface-cells CELLS_SOURCE="$source_root" CELLS_OUTPUT="$output_dir"
