// skein_fold_crumple.pov: a sheet folded to crumpled facets whose normals jump and kink at crackle cracks; skein.sh bounds the solver's work.
#version 3.8;
global_settings { assumed_gamma 1 }

#declare CellValue = function { pattern { crackle solid } }
#declare CrackDistance = function { pattern { crackle form <-1, 1, 0> } }
#declare Sign = function(u, v) { select(CellValue(3*u, 3*v, 0) - 0.5, -1, 1) }
#declare Ease = function(u, v) { min(1, CrackDistance(3*u, 3*v, 0)/0.08) }
#declare Tilt = function(u, v) { 0.3*Sign(u, v)*Ease(u, v) }
#declare Crumpled = skein {
  expressions {
    translate <-0.5, -0.5, 0>  scale <2, 2, 1>
    fold { from <0.5, 0.5>  perturb { x function(u, v) { Tilt(u, v)*(u - 0.5) }  y function(u, v) { Tilt(u, v)*(v - 0.5) } } }
  }
}

background { rgb 0.3 }
light_source { <3, 6, 5> rgb 1 }
camera { location <1.6, 2.6, 2.4> look_at <0, 0, 0> angle 45 }
object { Crumpled rotate -90*x pigment { rgb 1 } }
