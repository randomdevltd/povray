// Profile 0 is equirectangular; 1 Mercator; 2 Miller; 3 stereographic; 4 equal-area; 5 outward origin cylinder.
#version 4.0;
#ifndef (Profile) #declare Profile = 1; #end
#ifndef (Relayout) #declare Relayout = 0; #end
global_settings { assumed_gamma 1.0 }
#include "projections.inc"

#if (Profile = 0)
  #declare Projection = CylindricalProjectionCamera(ProjectionEquirectangular, -64, 77, ProjectionSingleOrigin, 0, 0, <0, 1.2, 0>, z, x, y);
#elseif (Profile = 1)
  #declare Projection = CylindricalProjectionCamera(ProjectionMercator, -64, 77, ProjectionSingleOrigin, 0, 0, <0, 1.2, 0>, z, x, y);
#elseif (Profile = 2)
  #declare Projection = CylindricalProjectionCamera(ProjectionMiller, -64, 77, ProjectionSingleOrigin, 0, 0, <0, 1.2, 0>, z, x, y);
#elseif (Profile = 3)
  #declare Projection = CylindricalProjectionCamera(ProjectionStereographic, -64, 77, ProjectionSingleOrigin, 0, 0, <0, 1.2, 0>, z, x, y);
#elseif (Profile = 4)
  #declare Projection = CylindricalProjectionCamera(ProjectionEqualArea, -64, 77, ProjectionSingleOrigin, 0, 0, <0, 1.2, 0>, z, x, y);
#else
  #declare Projection = CylindricalProjectionCamera(ProjectionMercator, -35, 40, ProjectionCylinderOrigin, 4, 0, <0, 1.2, 0>, z, x, y);
#end
camera { Projection }

sky_sphere { pigment { gradient y colour_map { [0.49 rgb <0.6, 0.75, 1>] [0.5 rgb <0.08, 0.15, 0.3>] } } }
plane { y, 0 pigment { checker rgb 0.18 rgb 0.7 scale 0.5 } finish { diffuse 0.8 } }
light_source { <-8, 15, -10> rgb 1.5 }

#macro Marker(P, S, C)
  union {
    cylinder { <0, 0, 0>, <0, 1.5, 0>, 0.09 }
    sphere { <0, 1.65, 0>, 0.22 }
    cylinder { <0, 1.15, 0>, <0.55, 1.45, 0.18>, 0.07 }
    cone { <0.55, 1.45, 0.18>, 0.16, <0.9, 1.58, 0.3>, 0 }
    pigment { rgb C }
    scale S translate P
  }
#end

#declare Radius = array[3] { 3, 6, 12 };
#for (R, 0, 2)
  #for (A, 0, 7)
    #local D = Radius[R];
    #local T = radians(A * 45 + R * 7);
    #if (Relayout) #local S = D / 6; #else #local S = 1; #end
    Marker(<sin(T) * D, 0, cos(T) * D>, S, <0.25 + 0.3 * R, 0.2 + 0.08 * A, 1 - 0.25 * R>)
  #end
#end

#for (A, 0, 11)
  #local T = radians(A * 30);
  cylinder { <sin(T) * 14, 0, cos(T) * 14>, <sin(T) * 14, 8, cos(T) * 14>, 0.035 pigment { rgb <1, 0.2, 0.1> } }
#end
