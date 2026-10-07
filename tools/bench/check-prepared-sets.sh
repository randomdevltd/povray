#!/bin/sh
set -eu
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
make -C "$build_dir/unix" -f Makefile -f "$source_root/tools/bench/check-prepared-sets.mk" \
  check-prepared-sets \
  PREPARED_SOURCE="$source_root/tools/bench/check-prepared-sets.cpp" \
  PREPARED_OBJECT="$output_dir/check-prepared-sets.o" \
  PREPARED_BINARY="$output_dir/check-prepared-sets"
