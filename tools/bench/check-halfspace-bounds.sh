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
cat > "$output_dir/halfspace-bounds.mk" <<'MAKE'
.PHONY: check-halfspace-bounds
check-halfspace-bounds:
	$(CXX) -I$(BOUNDS_SOURCE)/source $(CPPFLAGS) $(CXXFLAGS) -UNDEBUG $(EXTRA_CXXFLAGS) $(BOUNDS_SOURCE)/tests/source/halfspace_bounds.cpp -o $(BOUNDS_BINARY)
	$(BOUNDS_BINARY)
MAKE
make -C "$build_dir/source" -f Makefile -f "$output_dir/halfspace-bounds.mk" \
    check-halfspace-bounds BOUNDS_SOURCE="$source_root" EXTRA_CXXFLAGS="${EXTRA_CXXFLAGS:-}" BOUNDS_BINARY="$output_dir/halfspace-bounds"
