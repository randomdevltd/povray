#!/usr/bin/env bash
set -euo pipefail
if [ "$#" -ne 2 ]; then
  echo "usage: $0 configured-build-directory output-executable" >&2
  exit 2
fi
source_root=$(cd "$(dirname "$0")/../.." && pwd)
build_dir=$(cd "$1" && pwd)
mkdir -p "$(dirname "$2")"
output_dir=$(cd "$(dirname "$2")" && pwd)
output_binary="$output_dir/$(basename "$2")"
mkdir -p "$output_dir/tmp"
export TMPDIR="$output_dir/tmp"
cat > "$output_dir/make-photon-flux.mk" <<'MAKE'
.PHONY: photon-flux
photon-flux:
	$(CXX) $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) $(AM_CPPFLAGS) $(CPPFLAGS) $(CXXFLAGS) "$(FLUX_SOURCE)" -o "$(FLUX_BINARY)"
MAKE
make -C "$build_dir/source" -f Makefile -f "$output_dir/make-photon-flux.mk" photon-flux \
  FLUX_SOURCE="$source_root/tools/bench/make-photon-flux.cpp" FLUX_BINARY="$output_binary"
