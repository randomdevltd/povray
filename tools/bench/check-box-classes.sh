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
cat > "$output_dir/box-classes.mk" <<'MAKE'
BOXCLASS_FLAGS = -I$(BOXCLASS_SOURCE)/source $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) $(AM_CPPFLAGS) $(CPPFLAGS) $(CXXFLAGS) -UNDEBUG
.PHONY: check-box-classes
check-box-classes:
	$(CXX) $(BOXCLASS_FLAGS) $(BOXCLASS_SOURCE)/tests/source/box_classes.cpp -Wl,--start-group $(top_builddir)/source/libpovray.a $(top_builddir)/platform/libplatform.a $(ldadd_platformcpu) -Wl,--end-group $(LIBS) -o $(BOXCLASS_OUTPUT)/box-classes
	$(BOXCLASS_OUTPUT)/box-classes
MAKE
make -C "$build_dir/unix" -f Makefile -f "$output_dir/box-classes.mk" \
    check-box-classes BOXCLASS_SOURCE="$source_root" BOXCLASS_OUTPUT="$output_dir"
