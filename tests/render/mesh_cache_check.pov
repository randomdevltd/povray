// mesh_cache.sh: one isosurface_mesh and one skein_mesh, kept in mesh_cache_dir under the working directory when Cache is 1.
#version 4.0;
#include "functions.inc"
#ifndef (Amp) #declare Amp = 0.2; #end
#ifdef (Cache) global_settings { mesh_cache "mesh_cache_dir" } #end
camera { location <0, 1, -6> look_at 0 }
light_source { <-4, 6, -8> rgb 1 }
#declare Bumpy = function(x, y, z) { sqrt(x*x + y*y + z*z) - 1 + Amp * (f_noise3d(3*x, 3*y, 3*z) - 0.5) }
object { isosurface_mesh { function { Bumpy(x, y, z) } contained_by { sphere { 0, 1.3 } } max_gradient 2.5 max_angle 20 } translate -x*1.4 pigment { rgb 1 } }
object {
  skein_mesh {
    expressions { scale <1, 2, 1> extrude { radius function(u, v) { 0.3 * (1 + Amp * sin(2*pi*(3*u + 4*v))) } } }
    closed u
    ends flat
    max_angle 20
  }
  translate <1.4, -1, 0> pigment { rgb <1, 0.5, 0.2> }
}
