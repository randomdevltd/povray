// This scene is licensed under CC BY 3.0, http://creativecommons.org/licenses/by/3.0/
// Steam over three mugs: one authored pattern is supplied independently to scattering (the visible vapour) and to an interior refraction field (the hot, thin air carrying it). Left: both, so the tiles behind waver where the steam rises; middle: vapour alone; right: the index field alone, invisible air that still bends the tiles. +w640 +h360
#version 4.0;

global_settings { assumed_gamma 1 } // the steam's wisps are a few millimetres wide
camera { location <0, 1.75, -5.8> look_at <0, 1.55, 0> angle 40 right x * image_width / image_height }
light_source { <-3, 5, -4> rgb 1.1 }
light_source { <4, 3, -2> rgb 0.3 shadowless }

box { <-5, -1, 1.2>, <5, 5, 1.4> pigment { checker rgb <0.82, 0.86, 0.88> rgb <0.25, 0.42, 0.55> scale 0.2 } finish { specular 0.3 } }
box { <-5, -0.2, -3>, <5, 0.8, 1.2> pigment { rgb <0.45, 0.3, 0.18> } }

#declare MugOut = 0.32;
#declare Mug = union {
  difference {
    cylinder { <0, 0.8, 0>, <0, 1.5, 0>, MugOut }
    cylinder { <0, 0.86, 0>, <0, 1.6, 0>, MugOut - 0.04 }
    pigment { rgb <0.85, 0.85, 0.82> } finish { specular 0.4 }
  }
  torus { 0.16, 0.035 rotate x * 90 translate <MugOut + 0.1, 1.15, 0> pigment { rgb <0.85, 0.85, 0.82> } }
  cylinder { <0, 0.86, 0>, <0, 1.4, 0>, MugOut - 0.04 pigment { rgb <0.2, 0.1, 0.05> } finish { specular 0.6 } }
}

#macro SteamPattern()
  function { max(0, 1 - (x * x + z * z) / (0.08 + 0.05 * y * y)) * max(0, min(1, y * 6)) * max(0, 1 - y / 1.4) }
  warp { turbulence <0.5, 0.25, 0.5> octaves 4 lambda 2.4 }
  translate <0, 1.42, 0>
#end
#declare Steam = density { SteamPattern() }
#declare SteamIndex = function { pigment { SteamPattern() } }
#declare Vapour = media { mix add method 3 intervals 1 samples 12 scattering { 4, rgb 3 } absorption 0.2 density { Steam } }
#macro SteamColumn(X, WithVapour, WithBend)
  object { Mug translate x * X }
  cylinder {
    <0, 1.42, 0>, <0, 3, 0>, 0.75
    pigment { rgbt 1 }
    interior {
      #if (WithVapour) media { Vapour } #end
      #if (WithBend) refraction { function { -0.03 * SteamIndex(x, y, z).red } detail 0.1 } #end
    }
    translate x * X
  }
#end
SteamColumn(-1.55, 1, 1)
SteamColumn(0, 1, 0)
SteamColumn(1.55, 0, 1)
