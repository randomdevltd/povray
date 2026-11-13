// Bounce preparation among many separate objects: translucent stalks with buried lights, a bounce floor, blades.
#version 3.7;
#ifndef (Stalks) #declare Stalks = 6; #end
#ifndef (Blades) #declare Blades = 600; #end
#ifndef (Nearby) #declare Nearby = 24; #end
#ifndef (Samples) #declare Samples = 8; #end
#ifndef (Floor) #declare Floor = 1; #end
// Cells=N sets every surface light's node budget; without it each takes the renderer's default.
#ifdef (Cells) #default { light { domain_cells Cells } } #end
global_settings { assumed_gamma 1 subsurface { method 2 samples 8, 8 spacing 0.5 } }
camera { location <0, 1.1, -3.2> look_at <0, 0.45, 0> angle 48 }
background { rgb 0.01 }
#declare R = seed(7);
#for (K, 0, Stalks - 1)
  #declare O = <(K - (Stalks - 1) / 2) * 0.42, 0, 0.3 * rand(R)>;
  cylinder { <0, 0, 0>, <0, 1, 0>, 0.05
    pigment { rgb <0.8, 0.5, 0.3> }
    finish { ambient 0 diffuse 1 subsurface { translucency <0.04, 0.07, 0.12> } }
    interior { ior 1.4 }
    light { emission bounce { passes 1 } samples Samples }
    translate O }
  sphere { <0, 0.8, 0>, 0.03 hollow no_image pigment { rgbt 1 } finish { ambient 0 diffuse 0 }
    interior { ior off light { brightness 400 samples 8 attenuation { cutoff 0 } } }
    translate O }
#end
#for (K, 0, Nearby - 1)
  #declare T = K * 2 * pi / Nearby;
  sphere { <sin(T) * 1.4, 0.9 + 0.3 * cos(3 * T), cos(T) * 0.9 + 0.3>, 0.015 hollow no_image
    pigment { rgbt 1 } finish { ambient 0 diffuse 0 }
    interior { light { brightness 150 samples 4 attenuation { cutoff 0 } } } }
#end
#for (K, 1, Blades)
  #declare P = <4 * rand(R) - 2, 0, 3 * rand(R) - 0.8>;
  box { <-0.004, 0, -0.001>, <0.004, 0.08 + 0.25 * rand(R), 0.001>
    rotate y * 360 * rand(R) rotate z * (20 * rand(R) - 10)
    pigment { rgb <0.2, 0.5, 0.15> } finish { ambient 0 diffuse 0.8 } translate P }
#end
box { <-2.5, -0.02, -1.5>, <2.5, 0, 2.5> pigment { rgb 0.7 } finish { ambient 0 diffuse 1 }
  #if (Floor) light { bounce samples Samples } #end }
