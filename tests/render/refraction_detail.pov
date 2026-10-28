#version Version;
// Detail is in scene units; zero omits it unless Auto selects auto. The plate is 4 x 3 x 2 units.
// Cases 12-14 check scattering-grid limits; 15 checks resolved-field convergence.
#ifndef (Case) #declare Case = 0; #end
#ifndef (Detail) #declare Detail = 0.01; #end
#ifndef (Auto) #declare Auto = 0; #end
#ifndef (Atmosphere) #declare Atmosphere = 0; #end
#ifndef (Res) #declare Res = 0.028; #end
#ifndef (Cells) #declare Cells = 0; #end
global_settings {
  assumed_gamma 1
  #if (Atmosphere) atmospheric_ior Atmosphere #end
  #if (Cells > 0) prepared_media_cells Cells #end
}
camera { orthographic location <0, 0, (Case = 14 ? -2100 : -10)> look_at <0, 0, 0> right x * 4 up y * 3 }
plane { z, (Case = 14 ? 2001 : 4) pigment { gradient x color_map { [0 rgb 0] [1 rgb 1] } scale 4 translate -2 * x } finish { ambient 0 emission 1 diffuse 0 } }
light_source { <-1, 4, -3> rgb 1 #if (Case = 12 | Case = 13) parallel media_attenuation on point_at <0, 0, 0> #end }

#declare S = function(T) { select(T, 0, select(T - 1, T * T * (3 - 2 * T), 1)) }
#declare Fade = function(x, y, z) { S((2 - abs(x)) / 0.2) * S((1.5 - abs(y)) / 0.2) * S((1 - abs(z)) / 0.2) }
// A 0.02-unit seam rises from zero to one every 0.1 units.
#declare U = function(x) { x - 0.1 * floor((x + 0.01) / 0.1) }
#declare Seams = function(x, y, z) { S(U(x) / 0.02) * Fade(x, y, z) }
#declare Ramp = function(x, y, z) { ((x + 2) / 4) * Fade(x, y, z) }
// Unresolved oscillations of period 0.0006 probe aliasing; the resolved wave has period 0.48.
#declare Sine = function(x, y, z) { (0.5 + 0.5 * sin(x / 0.00015 * pi / 2 + (Case = 16 ? pi/2 : 0))) * Fade(x, y, z) }
#declare Resolved = function(x, y, z) { (0.5 + 0.5 * sin(2*pi*x/0.48)) * Fade(x, y, z) }
#declare Mean = function(x, y, z) { 0.5 * Fade(x, y, z) }
#declare Coefficient = (Case = 5 | Case = 6 ? 0.07 : Case = 7 | Case = 8 | Case = 9 | Case = 16 ? 1e-5 : 0.00025);

box {
  <-2, -1.5, (Case = 14 ? -2000 : -1)>, <2, 1.5, (Case = 14 ? 2000 : 1)>
  pigment { rgbt 1 }
  interior {
    ior 1
    #if (Case < 12 | Case = 15 | Case = 16)
      refraction {
        strength Coefficient
        #if (Detail > 0) detail Detail #elseif (Auto) detail auto #end
        field { function {
          #if (Case = 5 | Case = 6) Ramp(x, y, z)
          #elseif (Case = 7 | Case = 9 | Case = 16) Sine(x, y, z)
          #elseif (Case = 8) Mean(x, y, z)
          #elseif (Case = 15) Resolved(x, y, z)
          #else Seams(x, y, z)
          #end
        } }
      }
    #end
    #if (Case >= 12 & Case <= 14)
      media {
        method 4
        #if (Case = 12) resolution Res scattering { 1, rgb 0.5 } #end
        #if (Case = 13) resolution 0.032 scattering { 1, rgb 0.5 } #end
        #if (Case = 14) resolution 1 scattering { 1, rgb 0.35 } #end
        density { function { Seams(x, y, z) } }
      }
    #end
  }
}
