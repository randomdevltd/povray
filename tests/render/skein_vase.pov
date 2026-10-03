// skein_vase.pov: a vase whose radius is a scalar path along v, closed at the foot and open at the lip.
#version 3.8;
global_settings { assumed_gamma 1 }

#declare Vase = skein {
  expressions {
    scale <1, 2.2, 1>
    extrude { radius path { natural_spline 0.35, 0.55, 0.6, 0.42, 0.22, 0.26, 0.38 } }
  }
  closed u  ends flat, open
}

background { rgb <0.93, 0.92, 0.88> }
light_source { <-4, 7, -6> rgb 1 }
light_source { <6, 2, -4> rgb 0.3 }
camera { location <0, 3.1, -5.2> look_at <0, 1.1, 0> angle 45 }
object {
  Vase
  texture { pigment { rgb <0.2, 0.42, 0.55> } finish { phong 0.7 phong_size 60 reflection 0.05 } }
  interior_texture { pigment { rgb <0.85, 0.82, 0.75> } }
}
