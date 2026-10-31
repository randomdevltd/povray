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
cat > "$output_dir/prepared-transfer.mk" <<'MAKE'
.PHONY: check-prepared-transfer
check-prepared-transfer:
	$(CXX) -I$(AXIS_SOURCE)/source $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) $(AM_CPPFLAGS) $(CPPFLAGS) $(CXXFLAGS) -UNDEBUG $(EXTRA_CXXFLAGS) $(AXIS_SOURCE)/tests/source/prepared_transfer.cpp -o $(AXIS_BINARY)
	$(AXIS_BINARY)
MAKE
make -C "$build_dir/source" -f Makefile -f "$output_dir/prepared-transfer.mk" \
    check-prepared-transfer AXIS_SOURCE="$source_root" EXTRA_CXXFLAGS="${EXTRA_CXXFLAGS:-}" AXIS_BINARY="$output_dir/prepared-transfer"
