// skein_envelope_uv.pov: a curled sheet thickened by envelope with round edges, uv-mapped: front checks blue, back checks red, rims yellow.
#version 3.8;
global_settings { assumed_gamma 1 }

#declare Rim = 1/64;
#declare Sheet = skein {
  expressions {
    translate <-0.5, 0, 0>  scale <1.6, 1, 1>
    crease { axis path { <0.15, 0, 0>, <0.15, 1, 0> }  radius 0.16  angle 220 }
    envelope { thickness function(uv) { 0.07*pow(1 - pow(2*uv.v - 1, 8), 0.25) }  edge round }
  }
  closed u  ends sealed
}
#declare Checks = function { mod(floor(x*32) + floor(y*10), 2) }
#declare Front = pigment { function { Checks(x, y, 0) } color_map { [0 rgb <0.15, 0.3, 0.75>] [1 rgb <0.85, 0.9, 1>] } }
#declare Back = pigment { function { Checks(x, y, 0) } color_map { [0 rgb <0.7, 0.15, 0.1>] [1 rgb <1, 0.85, 0.8>] } }
#declare Edge = pigment { gradient y  color_map { [0 rgb <1, 0.8, 0.1>] [0.5 rgb <1, 0.8, 0.1>] [0.5 rgb <0.4, 0.3, 0>] [1 rgb <0.4, 0.3, 0>] } scale 0.1 }

background { rgb <0.93, 0.92, 0.88> }
light_source { <-3, 5, -6> rgb 1 }
light_source { <4, 2, -3> rgb 0.35 }
camera { location <0.9, 1.6, -2.2> look_at <0.2, 0.45, 0.05> angle 40 }
object {
  Sheet
  texture {
    uv_mapping
    pigment {
      gradient x
      pigment_map { [Rim Edge] [Rim Front] [0.5 - Rim Front] [0.5 - Rim Edge] [0.5 + Rim Edge] [0.5 + Rim Back] [1 - Rim Back] [1 - Rim Edge] }
    }
    finish { phong 0.3 }
  }
}
