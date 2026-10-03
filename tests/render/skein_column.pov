// skein_column.pov: a fluted column turned a quarter turn, its flutes a sine of u whose phase runs back a quarter with v, on a hexagonal plinth.
#version 3.8;
global_settings { assumed_gamma 1 }

#declare Column = skein {
  expressions {
    scale <1, 3, 1>
    extrude { radius function(u, v) { 0.42 + 0.03*sin(2*pi*12*(u - v/4)) } }
  }
  closed u  ends flat
}
#declare Plinth = skein {
  expressions {
    scale <1, 0.3, 1>
    extrude { radius function(u) { 0.6*cos(pi/6) / cos(mod(2*pi*u, pi/3) - pi/6) } }
  }
  closed u  ends flat
  translate <0, -0.3, 0>
}

background { rgb <0.93, 0.92, 0.88> }
light_source { <-4, 7, -6> rgb 1 }
light_source { <6, 2, -4> rgb 0.3 }
camera { location <0, 2.4, -7> look_at <0, 1.3, 0> angle 45 }
union {
  object { Column }
  object { Plinth }
  pigment { rgb <0.88, 0.86, 0.8> }
  finish { phong 0.3 }
}
