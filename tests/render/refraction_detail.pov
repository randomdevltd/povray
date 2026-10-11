#version Version;
// refraction_detail checks for refraction_detail.sh: Declare=Version=<v> Declare=Case=<n>.
// A 400 x 300 x 200 mm plate (mm_per_unit 100) of 2 mm smoothstep seams, a linear ramp,
// or 0.2 mm sine structure; Declare=Detail=0 means auto (or no setting when Auto=0 too).
// Case 14 stretches the plate to 4000 units for the prepared-step budget.
#ifndef (Case) #declare Case = 0; #end
#ifndef (Detail) #declare Detail = 1; #end
#ifndef (Auto) #declare Auto = 0; #end
#ifndef (Atmosphere) #declare Atmosphere = 0; #end
#ifndef (Res) #declare Res = 0.028; #end
#ifndef (Cells) #declare Cells = 0; #end
global_settings {
  assumed_gamma 1
  mm_per_unit 100
  #if (Atmosphere) atmospheric_ior Atmosphere #end
  #if (Detail > 0 & Case != 3) refraction_detail Detail #end
  #if (Detail <= 0 & Auto) refraction_detail auto #end
  #if (Cells > 0) prepared_media_cells Cells #end
}
camera { orthographic location <0, 0, (Case = 14 ? -2100 : -10)> look_at <0, 0, 0> right x * 4 up y * 3 }
plane { z, (Case = 14 ? 2001 : 4) pigment { gradient x color_map { [0 rgb 0] [1 rgb 1] } scale 4 translate -2 * x } finish { ambient 0 emission 1 diffuse 0 } }
light_source { <-1, 4, -3> rgb 1 #if (Case = 12 | Case = 13) parallel point_at <0, 0, 0> #end }

#declare S = function(T) { select(T, 0, select(T - 1, T * T * (3 - 2 * T), 1)) }
#declare Fade = function(x, y, z) { S((2 - abs(x)) / 0.2) * S((1.5 - abs(y)) / 0.2) * S((1 - abs(z)) / 0.2) }
// 2 mm seams rising 0 to 1 every 10 mm, so each seam is a narrow prism bending rays toward +x.
#declare U = function(x) { x - 0.1 * floor((x + 0.01) / 0.1) }
#declare Seams = function(x, y, z) { S(U(x) / 0.02) * Fade(x, y, z) }
#declare Ramp = function(x, y, z) { ((x + 2) / 4) * Fade(x, y, z) }
// Sine of 0.3 mm period: structure far below any detail the tests set.
#declare Sine = function(x, y, z) { (0.5 + 0.5 * sin(x / 0.00015 * pi / 2)) * Fade(x, y, z) }
#declare Mean = function(x, y, z) { 0.5 * Fade(x, y, z) }
#declare Coefficient = (Case = 5 | Case = 6 ? 0.07 : Case = 7 | Case = 8 | Case = 9 ? 1e-5 : 0.00025);

box {
  <-2, -1.5, (Case = 14 ? -2000 : -1)>, <2, 1.5, (Case = 14 ? 2000 : 1)>
  pigment { rgbt 1 }
  interior {
    ior 1
    media {
      #if (Case >= 10) method 4 #else method 3 #end
      #if (Case = 11) resolution 0.05 #end
      #if (Case = 12 | Case = 13) resolution Res scattering { 1, rgb 0.5 } #end
      #if (Case = 14) resolution 1 scattering { 1, rgb 0.35 } #end
      #if (Case = 3 & Detail > 0) refraction_detail Detail #end
      #if (Case < 12) refraction Coefficient #end
      #if (Case = 5 | Case = 6)
        density { function { Ramp(x, y, z) } }
      #elseif (Case = 7 | Case = 9)
        density { function { Sine(x, y, z) } }
      #elseif (Case = 8)
        density { function { Mean(x, y, z) } }
      #else
        density { function { Seams(x, y, z) } }
      #end
    }
  }
}
