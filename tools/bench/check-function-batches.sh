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

cat > "$output_dir/check-function-batches.mk" <<'MAKE'
.PHONY: check-function-batches
check-function-batches:
	$(CXX) $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) $(AM_CPPFLAGS) $(CPPFLAGS) $(CXXFLAGS) -c "$(BATCH_SOURCE)" -o "$(BATCH_OBJECT)"
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o "$(BATCH_BINARY)" "$(BATCH_OBJECT)" $(LDADD) $(LIBS)
	"$(BATCH_BINARY)"
MAKE

make -C "$build_dir/unix" -f Makefile -f "$output_dir/check-function-batches.mk" \
  check-function-batches \
  BATCH_SOURCE="$source_root/tools/bench/check-function-batches.cpp" \
  BATCH_OBJECT="$output_dir/check-function-batches.o" \
  BATCH_BINARY="$output_dir/check-function-batches"
