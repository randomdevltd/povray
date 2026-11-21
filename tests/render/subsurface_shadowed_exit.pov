// A slit thinner than a pixel between two blocks of one subsurface body. The rays dip, so one row of pixels sees the
// lower block's top face inside the slit, in the upper block's shadow; light entering the lit front a little away still
// diffuses to it. Upper=0 removes the upper block, leaving the same face open to the light.
#version 3.7;
#ifndef (Upper) #declare Upper=1; #end
global_settings { assumed_gamma 1 mm_per_unit 8 subsurface { method 2 samples 8,3 } }
camera { orthographic location <0, 0.009, -4> direction <0, -0.012, 1> right x*0.128 up y*1.6 }
light_source { <-2, 8, -8> rgb 1.2 area_light x*2, y*2, 4, 4 }
light_source { <3, -2, -8> rgb 0.3 }
background { rgb 0 }
#declare Wax = material { texture { pigment { rgb <0.93, 0.95, 1> }
    finish { ambient 0 diffuse 0.95 subsurface { method 2 translucency 4 } } } interior { ior 1.31 } }
union {
    box { <-3, -1.2, 0>, <3, -0.0485, 2> }
    #if (Upper) box { <-3, -0.0455, 0>, <3, 1.2, 2> } #end
    material { Wax }
}
