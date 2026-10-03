// skein_crease_tail.pov: a cone folded past the end of its crease's arc, seen close to the straight tail; skein.sh bounds the solver's work.
#version 3.8;
global_settings { assumed_gamma 1 }

#declare Tail = skein {
  expressions {
    scale <1, 2, 1>
    extrude { radius function(v) { 0.4*(1 - v) } }
    crease { axis path { <0, 1, -1>, <0, 1, 1> }  radius 0.35  angle 97 }
  }
  closed u  ends open
}

background { rgb 0.3 }
light_source { <-3, 6, -4> rgb 1 }
camera { location <0, 1.6, -4> look_at <0.3, 1.4, 0> angle 20 }
object { Tail pigment { rgb 1 } }
