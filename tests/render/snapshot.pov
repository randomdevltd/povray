// A colour ramp across the frame, so no coarser snapshot can equal the finished image; tests/render/snapshot.sh renders it.
#version 3.7;
global_settings { assumed_gamma 1 }
camera { location <0.5, 0.5, -1> look_at <0.5, 0.5, 0> angle 60 }
plane { z, 0 pigment { gradient x color_map { [0 rgb <0, 0.2, 1>] [1 rgb <1, 0.6, 0>] } } finish { emission 1 diffuse 0 } }
