// This scene is licensed under CC BY 3.0, http://creativecommons.org/licenses/by/3.0/
// Steam over a mug: one density drives a scattering medium (the visible vapour) and a refraction-only medium (the hot, thin air carrying it), so the tiles behind waver where the steam rises. Declare=Bend=0 drops the refraction. +w480 +h480
#version 4.0;
#ifndef (Bend) #declare Bend = 1; #end

global_settings { assumed_gamma 1 }
camera { location <0, 1.6, -3.2> look_at <0, 1.45, 0> angle 34 }
light_source { <-3, 5, -4> rgb 1.1 }
light_source { <4, 3, -2> rgb 0.3 shadowless }

box { <-4, -1, 1.2>, <4, 5, 1.4> pigment { checker rgb <0.82, 0.86, 0.88> rgb <0.25, 0.42, 0.55> scale 0.2 } finish { specular 0.3 } }
box { <-4, -0.2, -3>, <4, 0.8, 1.2> pigment { rgb <0.45, 0.3, 0.18> } }

#declare MugOut = 0.32;
difference {
  cylinder { <0, 0.8, 0>, <0, 1.5, 0>, MugOut }
  cylinder { <0, 0.86, 0>, <0, 1.6, 0>, MugOut - 0.04 }
  pigment { rgb <0.85, 0.85, 0.82> } finish { specular 0.4 }
}
torus { 0.16, 0.035 rotate x * 90 translate <MugOut + 0.1, 1.15, 0> pigment { rgb <0.85, 0.85, 0.82> } }
cylinder { <0, 0.86, 0>, <0, 1.4, 0>, MugOut - 0.04 pigment { rgb <0.2, 0.1, 0.05> } finish { specular 0.6 } }

#declare Steam = density {
  function { max(0, 1 - (x * x + z * z) / (0.08 + 0.05 * y * y)) * max(0, min(1, y * 6)) * max(0, 1 - y / 1.4) }
  warp { turbulence <0.5, 0.25, 0.5> octaves 4 lambda 2.4 }
  translate <0, 1.42, 0>
}
cylinder {
  <0, 1.42, 0>, <0, 3, 0>, 0.85
  pigment { rgbt 1 }
  interior {
    media { mix add method 3 intervals 1 samples 12 scattering { 4, rgb 3 } absorption 0.2 density { Steam } }
    #if (Bend) media { method 3 refraction -0.03 density { Steam } } #end
  }
}
