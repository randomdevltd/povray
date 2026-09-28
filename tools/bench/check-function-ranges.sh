#!/usr/bin/env bash
# Build against an existing configured Unix build, using its compiler and libraries.
set -euo pipefail
if [ "$#" -ne 2 ]; then
  echo "usage: $0 build-directory output-directory" >&2
  exit 2
fi
source_root=$(cd "$(dirname "$0")/../.." && pwd)
build_dir=$(cd "$1" && pwd)
mkdir -p "$2"
output_dir=$(cd "$2" && pwd)
mkdir -p "$output_dir/tmp"
export TMPDIR="$output_dir/tmp"

cat > "$output_dir/check-function-ranges.mk" <<'MAKE'
.PHONY: check-function-ranges
check-function-ranges:
	$(CXX) $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) $(AM_CPPFLAGS) $(CPPFLAGS) $(CXXFLAGS) -c "$(RANGE_SOURCE)" -o "$(RANGE_OBJECT)"
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o "$(RANGE_BINARY)" "$(RANGE_OBJECT)" $(LDADD) $(LIBS)
	"$(RANGE_BINARY)"
MAKE

make -C "$build_dir/unix" -f Makefile -f "$output_dir/check-function-ranges.mk" \
  check-function-ranges \
  RANGE_SOURCE="$source_root/tools/bench/check-function-ranges.cpp" \
  RANGE_OBJECT="$output_dir/check-function-ranges.o" \
  RANGE_BINARY="$output_dir/check-function-ranges"
