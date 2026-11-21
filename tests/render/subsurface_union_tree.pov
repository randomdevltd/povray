// Ten touching slices of one subsurface body, enough for the union to index them in its child tree. Case 0: each slice
// looks up its own flesh where light enters and single scattering crosses the seams. Case 1: each slice holds a copy of
// one material and the body's whole volume is a light (a volume light takes a uniform flesh).
#version 3.7;
#ifndef (Case) #declare Case=0; #end
global_settings { assumed_gamma 1 mm_per_unit 8 subsurface { method 2 samples 4,2 } }
camera { orthographic location <0, 0, -4> look_at 0 right x*2 up y*1 }
background { rgb 0 }
#if (Case = 0) light_source { <-2, 6, -6> rgb 1 } #end
#macro Wax(Flesh)
    material { texture { pigment { rgb <0.93, 0.95, 1> } finish { ambient 0 diffuse 0.95 subsurface {
        method 2 translucency 1 #if (Case = 0) pigment { rgb Flesh } volume_sampling 1 #end } } } interior { ior 1.31 } }
#end
union {
    #for (I, 0, 9)
        box { <-1 + I * 0.2, -0.5, -0.5>, <-0.8 + I * 0.2, 0.5, 0.5> #if (mod(I, 2) = 0) Wax(0.8) #else Wax(0.3) #end }
    #end
    #if (Case = 1) interior { light { pigment { rgb 1 } brightness 3 nits samples 4 } } #end
}
