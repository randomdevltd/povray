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
cat > "$output_dir/surface-boxes.mk" <<'MAKE'
SURFACE_FLAGS = -I$(SURFACE_SOURCE)/source $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) $(AM_CPPFLAGS) $(CPPFLAGS) $(CXXFLAGS) -UNDEBUG
.PHONY: check-surface-boxes
check-surface-boxes:
	$(CXX) $(SURFACE_FLAGS) $(SURFACE_SOURCE)/tests/source/surface_boxes.cpp -Wl,--start-group $(top_builddir)/source/libpovray.a $(top_builddir)/platform/libplatform.a $(ldadd_platformcpu) -Wl,--end-group $(LIBS) -o $(SURFACE_OUTPUT)/surface-boxes
	$(SURFACE_OUTPUT)/surface-boxes
MAKE
make -C "$build_dir/unix" -f Makefile -f "$output_dir/surface-boxes.mk" \
    check-surface-boxes SURFACE_SOURCE="$source_root" SURFACE_OUTPUT="$output_dir"
