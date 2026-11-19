// Two touching blocks of one subsurface body, each with its own copy of one material, and the body's whole volume a light.
#version 3.7;
global_settings { assumed_gamma 1 mm_per_unit 8 subsurface { method 2 samples 4,2 } }
camera { orthographic location <0, 0, -4> look_at 0 right x*2 up y*1 }
background { rgb 0 }
#declare Wax = material { texture { pigment { rgb <0.93, 0.95, 1> }
    finish { ambient 0 diffuse 0.95 subsurface { method 2 translucency 1 } } } interior { ior 1.31 } }
union {
    box { <-1, -0.5, -0.5>, <0, 0.5, 0.5> material { Wax } }
    box { <0, -0.5, -0.5>, <1, 0.5, 0.5> material { Wax } }
    interior { light { pigment { rgb 1 } brightness 3 nits samples 4 } }
}
