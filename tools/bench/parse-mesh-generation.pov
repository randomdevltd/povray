#version 4.0;
global_settings { assumed_gamma 1 }

#ifndef (MeshKind) #declare MeshKind = 1; #end
#ifndef (MeshAngle) #declare MeshAngle = 1; #end
#ifndef (MeshMinSize) #declare MeshMinSize = -1; #end
#ifndef (MeshCopies) #declare MeshCopies = 1; #end
#ifndef (PlaceCopies) #declare PlaceCopies = 0; #end
#ifndef (VaryOptions) #declare VaryOptions = 0; #end

#include "functions.inc"
#declare Rough = function(x, y, z) { sqrt(x*x + y*y + z*z) - 1 + 0.3 * (f_noise3d(3*x, 3*y, 3*z) - 0.5) }
#for (Copy, 1, MeshCopies)
  #if (MeshKind = 1)
    #declare Built = skein_mesh {
      expressions { extrude { radius 0.5 } extrude { axis x radius 2 } }
      closed uv
      max_angle MeshAngle*(1 + VaryOptions*(Copy - 1)/MeshCopies)
      #if (MeshMinSize > 0) min_size MeshMinSize #end
    }
  #elseif (MeshKind = 3)
    #declare Built = skein_mesh {
      expressions {
        scale <1, 4, 1>
        extrude { radius function(u, v) { 0.1 * (1 + 0.1 * sin(2*pi*(3*u + 16*v))) } }
        displace function(u, v) { 0.02 * sin(pi*v) * sin(2*pi*(13*u - 51*v)) }
      }
      closed u
      ends flat
      max_angle MeshAngle*(1 + VaryOptions*(Copy - 1)/MeshCopies)
      #if (MeshMinSize > 0) min_size MeshMinSize #end
    }
  #else
    #declare Built = isosurface_mesh {
      function { Rough(x, y, z) }
      contained_by { sphere { 0, 1.3 } }
      max_gradient 2.5
    }
  #end
  #if (PlaceCopies) object { Built translate <3*(Copy - 1), 0, 0> } #end
#end

camera { location <0, 3, -6> look_at 0 }
light_source { <-3, 5, -4> rgb 1 }
object { Built pigment { rgb <0.5, 0.7, 1> } }
