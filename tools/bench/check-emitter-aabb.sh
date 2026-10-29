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
cat > "$output_dir/emitter-aabb.mk" <<'MAKE'
.PHONY: check-emitter-aabb
check-emitter-aabb:
	$(CXX) -I$(AABB_SOURCE)/source $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) $(AM_CPPFLAGS) $(CPPFLAGS) $(CXXFLAGS) -UNDEBUG $(EXTRA_CXXFLAGS) $(AABB_SOURCE)/tests/source/emitter_aabb_test.cpp -o $(AABB_BINARY)
	$(AABB_BINARY)
MAKE
make -C "$build_dir/source" -f Makefile -f "$output_dir/emitter-aabb.mk" \
    check-emitter-aabb AABB_SOURCE="$source_root" EXTRA_CXXFLAGS="${EXTRA_CXXFLAGS:-}" AABB_BINARY="$output_dir/emitter-aabb"
