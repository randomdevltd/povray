// skein_fold_box.pov: a sheet folded to a hinge's normals, seen close to the folded half; skein.sh bounds the solver's work.
#version 3.8;
global_settings { assumed_gamma 1 }

#declare Turn = function(u) { min(max(1.6*u - 0.8, 0)/0.12, radians(120)) }
#declare Folded = skein {
  expressions {
    translate <-0.5, 0, 0>  scale <1.6, 1, 1>
    fold { from <0.25, 0.5>  perturb { x function(u) { -sin(Turn(u)) }  z function(u) { cos(Turn(u)) } } }
  }
}

background { rgb 0.3 }
light_source { <-3, 6, -4> rgb 1 }
camera { location <0.6, 0.5, -2.5> look_at <0.05, 0.5, 0.3> angle 25 }
object { Folded pigment { rgb 1 } double_illuminate }
