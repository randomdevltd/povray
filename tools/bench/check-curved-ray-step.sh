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
cat > "$output_dir/curved-ray-step.mk" <<'MAKE'
.PHONY: check-curved-ray-step
check-curved-ray-step:
	$(CXX) -I$(AXIS_SOURCE)/source $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) $(AM_CPPFLAGS) $(CPPFLAGS) $(CXXFLAGS) -UNDEBUG $(EXTRA_CXXFLAGS) $(AXIS_SOURCE)/tests/source/curved_ray_step.cpp -o $(AXIS_BINARY)
	$(AXIS_BINARY)
MAKE
make -C "$build_dir/source" -f Makefile -f "$output_dir/curved-ray-step.mk" \
    check-curved-ray-step AXIS_SOURCE="$source_root" EXTRA_CXXFLAGS="${EXTRA_CXXFLAGS:-}" AXIS_BINARY="$output_dir/curved-ray-step"
