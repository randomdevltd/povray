#!/bin/sh
set -eu
if [ "$#" -ne 2 ]; then
    echo 'usage: check-shadow-segment.sh configured-build-directory output-directory' >&2
    exit 2
fi
source_root=$(cd "$(dirname "$0")/../.." && pwd)
build_dir=$(cd "$1" && pwd)
mkdir -p "$2"
output_dir=$(cd "$2" && pwd)
mkdir -p "$output_dir/tmp"
export TMPDIR="$output_dir/tmp"
cat > "$output_dir/shadow-test.mk" <<'MAKE'
SHADOW_SOURCES = core/shape/sphere core/shape/box core/shape/mesh core/shape/isosurface core/shape/blob core/shape/lathe core/shape/sor core/shape/superellipsoid core/shape/heightfield core/shape/fractal core/shape/parametric core/shape/csg core/render/ray core/scene/object
SHADOW_OBJECTS = $(addprefix $(SHADOW_OUTPUT)/,$(addsuffix .o,$(subst /,_,$(SHADOW_SOURCES))))
SHADOW_FLAGS = -I$(SHADOW_ROOT)/source $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) $(AM_CPPFLAGS) $(CPPFLAGS) $(CXXFLAGS) -UNDEBUG
.PHONY: shadow-test shadow-syntax
shadow-test: $(SHADOW_OBJECTS) shadow-syntax
	$(CXX) $(SHADOW_FLAGS) "$(SHADOW_ROOT)/tests/source/shadow_segment.cpp" $(SHADOW_OBJECTS) -Wl,--start-group $(top_builddir)/source/libpovray.a $(top_builddir)/platform/libplatform.a $(ldadd_platformcpu) -Wl,--end-group $(LIBS) -o "$(SHADOW_OUTPUT)/shadow-segment-test"
shadow-syntax:
	$(CXX) $(SHADOW_FLAGS) -fsyntax-only "$(SHADOW_ROOT)/source/core/render/trace.cpp"
define shadow_object
$(SHADOW_OUTPUT)/$(subst /,_,$(1)).o: $(SHADOW_ROOT)/source/$(1).cpp $(SHADOW_ROOT)/source/core/coretypes.h $(SHADOW_ROOT)/source/core/render/ray.h $(SHADOW_ROOT)/source/core/render/shadowsegment.h
	$$(CXX) $$(SHADOW_FLAGS) -c "$$<" -o "$$@"
endef
$(foreach src,$(SHADOW_SOURCES),$(eval $(call shadow_object,$(src))))
MAKE
make -j2 -C "$build_dir/unix" -f Makefile -f "$output_dir/shadow-test.mk" shadow-test \
    SHADOW_ROOT="$source_root" SHADOW_OUTPUT="$output_dir"
"$output_dir/shadow-segment-test"
