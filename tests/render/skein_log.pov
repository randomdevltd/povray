// skein_log.pov: a log tapering with v, its bark cracked by a crackle pattern of uv (embedded on a circle so u closes) turning three quarters with v.
#version 3.8;
global_settings { assumed_gamma 1 }

#declare Cracks = function { pattern { crackle } }
#declare Log = skein {
  expressions {
    scale <1, 4, 1>
    extrude { radius function(v) { 0.5 - 0.15*v } }
    displace function(uv) {
      -0.04 * min(1, 20*uv.v, 20*(1 - uv.v)) * max(0, 1 - Cracks(3*cos(2*pi*(uv.u - 0.75*uv.v)), 3*sin(2*pi*(uv.u - 0.75*uv.v)), 9*uv.v)/0.12)
    }
  }
  closed u  ends flat
}

background { rgb <0.93, 0.92, 0.88> }
light_source { <-4, 7, -6> rgb 1 }
light_source { <6, 2, -4> rgb 0.3 }
camera { location <0, 3.4, -7.4> look_at <0, 1.1, 0> angle 40 }
object {
  Log
  pigment { rgb <0.42, 0.3, 0.2> }
  finish { phong 0.15 }
  rotate z*-62  translate <-1.5, 0.3, 0>
}
