// Small subsurface boxes all round the camera, seen through a user-defined panorama or a native camera; see doc/PERF.md.
// Camera 0 is projections.inc's Mercator from a small sphere of origins; 1 native spherical; 2 cylinder 1; 3 perspective.
#version 4.0;
#ifndef (Camera) #declare Camera = 0; #end
#ifndef (Count) #declare Count = 400; #end
#ifndef (Method) #declare Method = 2; #end
#ifndef (Diffuse) #declare Diffuse = 40; #end
#ifndef (Single) #declare Single = 10; #end
#ifndef (Spacing) #declare Spacing = 1; #end
#ifndef (Memory) #declare Memory = 1024; #end
#ifndef (Translucency) #declare Translucency = 0.1; #end
global_settings {
  assumed_gamma 1.0
  mm_per_unit 1
  subsurface { samples Diffuse, Single method Method spacing Spacing memory Memory }
}
#include "projections.inc"

#switch (Camera)
  #case (0)
    CylindricalProjectionCamera(ProjectionMercator, -60, 60, ProjectionSphereOrigin, 0.05, 0, <0, 0, 0>, z, x, y)
    #break
  #case (1)
    camera { spherical location <0, 0, 0> look_at z angle 360, 120 }
    #break
  #case (2)
    camera { cylinder 1 location <0, 0, 0> look_at z angle 360 up y * 4 right x * 2 }
    #break
  #else
    camera { perspective location <0, 0, 0> look_at z angle 90 right x * image_width / image_height }
#end

light_source { <3, 12, -4>, rgb 1.2 }
light_source { <-6, -2, 5>, rgb 0.4 }
sky_sphere { pigment { gradient y colour_map { [0 rgb <0.05, 0.05, 0.08>] [1 rgb <0.35, 0.45, 0.7>] } } }

#declare Seed = seed(4321);
#for (I, 1, Count)
  #local Azimuth = 360 * rand(Seed);
  #local Elevation = degrees(asin(1.6 * rand(Seed) - 0.8));
  #local Distance = 1.2 + 1.3 * rand(Seed);
  box {
    -0.5, 0.5
    scale <0.25 + 0.2 * rand(Seed), 0.25 + 0.2 * rand(Seed), 0.25 + 0.2 * rand(Seed)>
    rotate <360 * rand(Seed), 360 * rand(Seed), 0>
    translate z * Distance
    rotate <-Elevation, Azimuth, 0>
    pigment { rgb <0.85, 0.55 + 0.3 * rand(Seed), 0.4> }
    finish { ambient 0 diffuse 0.15 subsurface { translucency Translucency * <1, 0.7, 0.5> #ifdef (MinimumInfluence) minimum_influence MinimumInfluence #end } }
    interior { ior 1.4 }
  }
#end
