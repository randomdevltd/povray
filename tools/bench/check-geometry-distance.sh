#!/bin/sh
set -eu
if [ "$#" -ne 2 ]; then
    echo 'usage: check-geometry-distance.sh configured-build-directory output-directory' >&2
    exit 2
fi
source_root=$(cd "$(dirname "$0")/../.." && pwd)
build_dir=$(cd "$1" && pwd)
mkdir -p "$2"
output_dir=$(cd "$2" && pwd)
mkdir -p "$output_dir/tmp"
export TMPDIR="$output_dir/tmp"
cat > "$output_dir/geometry-test.mk" <<'MAKE'
SHAPES = blob box isosurface cone disc lathe lemon mesh plane polygon prism quadric skein sor sphere spheresweep superellipsoid torus triangle
GEOMETRY_OBJECTS = $(addprefix $(GEOMETRY_OUTPUT)/,$(addsuffix .o,$(SHAPES)))
GEOMETRY_OBJECTS += $(GEOMETRY_OUTPUT)/boundingbox.o
GEOMETRY_OBJECTS += $(GEOMETRY_OUTPUT)/ray.o $(GEOMETRY_OUTPUT)/object.o
GEOMETRY_FLAGS = -I$(GEOMETRY_ROOT)/source $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) $(AM_CPPFLAGS) $(CPPFLAGS) $(CXXFLAGS) -UNDEBUG
.PHONY: geometry-test
geometry-test: $(GEOMETRY_OBJECTS)
	$(CXX) $(GEOMETRY_FLAGS) "$(GEOMETRY_ROOT)/tests/source/geometry_distance.cpp" $(GEOMETRY_OBJECTS) -Wl,--start-group $(top_builddir)/source/libpovray.a $(top_builddir)/platform/libplatform.a $(ldadd_platformcpu) -Wl,--end-group $(LIBS) -o "$(GEOMETRY_OUTPUT)/geometry-distance-test"
$(GEOMETRY_OUTPUT)/%.o: $(GEOMETRY_ROOT)/source/core/shape/%.cpp $(GEOMETRY_ROOT)/source/core/math/intersectiondepth.h $(GEOMETRY_ROOT)/source/core/bounding/boundingbox.h $(GEOMETRY_ROOT)/source/core/render/ray.h
	$(CXX) $(GEOMETRY_FLAGS) -c "$<" -o "$@"
$(GEOMETRY_OUTPUT)/boundingbox.o: $(GEOMETRY_ROOT)/source/core/bounding/boundingbox.cpp $(GEOMETRY_ROOT)/source/core/bounding/boundingbox.h
	$(CXX) $(GEOMETRY_FLAGS) -c "$<" -o "$@"
$(GEOMETRY_OUTPUT)/ray.o: $(GEOMETRY_ROOT)/source/core/render/ray.cpp $(GEOMETRY_ROOT)/source/core/render/ray.h
	$(CXX) $(GEOMETRY_FLAGS) -c "$<" -o "$@"
$(GEOMETRY_OUTPUT)/object.o: $(GEOMETRY_ROOT)/source/core/scene/object.cpp $(GEOMETRY_ROOT)/source/core/render/ray.h
	$(CXX) $(GEOMETRY_FLAGS) -c "$<" -o "$@"
MAKE
make -j2 -C "$build_dir/unix" -f Makefile -f "$output_dir/geometry-test.mk" geometry-test \
    GEOMETRY_ROOT="$source_root" GEOMETRY_OUTPUT="$output_dir"
"$output_dir/geometry-distance-test"
