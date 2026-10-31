#!/bin/sh
set -eu
if [ "$#" -ne 2 ]; then
    echo 'usage: check-polynomial-distance.sh configured-build-directory output-directory' >&2
    exit 2
fi
source_root=$(cd "$(dirname "$0")/../.." && pwd)
build_dir=$(cd "$1" && pwd)
mkdir -p "$2"
output_dir=$(cd "$2" && pwd)
mkdir -p "$output_dir/tmp"
export TMPDIR="$output_dir/tmp"
cat > "$output_dir/polynomial-test.mk" <<'MAKE'
.PHONY: polynomial-test
polynomial-test:
	$(CXX) -I$(POLYNOMIAL_ROOT)/source $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) $(AM_CPPFLAGS) $(CPPFLAGS) $(CXXFLAGS) -UNDEBUG -ffunction-sections -fdata-sections "$(POLYNOMIAL_ROOT)/source/core/math/polynomialsolver.cpp" "$(POLYNOMIAL_ROOT)/tests/source/polynomial_distance.cpp" -Wl,--gc-sections -o "$(POLYNOMIAL_OUTPUT)/polynomial-distance-test"
MAKE
make -C "$build_dir/source" -f Makefile -f "$output_dir/polynomial-test.mk" polynomial-test \
    POLYNOMIAL_ROOT="$source_root" POLYNOMIAL_OUTPUT="$output_dir"
"$output_dir/polynomial-distance-test"
