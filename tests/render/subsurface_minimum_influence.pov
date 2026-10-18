// A wax sphere seen only in a faint mirror, and with Direct=1 another seen directly; Reflect sets the mirror's weight,
// Default a #default finish's minimum_influence and Influence the wax finish's own.
#version 4.0;
#ifndef (Direct) #declare Direct = 0; #end
#ifndef (Reflect) #declare Reflect = 0.001; #end
global_settings {
  assumed_gamma 1
  adc_bailout 0.0001
  mm_per_unit 20
  subsurface { samples 8, 2 method 2 }
}
#ifdef (Default) #default { finish { subsurface { minimum_influence Default } } } #end

camera { location <0, 1, -3> look_at <0, 0.6, 0> angle 45 right x * image_width / image_height }
light_source { <-2, 4, -2> rgb 1.4 }
background { rgb 0.2 }
box { <-3, -0.1, 1>, <3, 3, 1.05> pigment { rgb 1 } finish { ambient 0 diffuse 0 reflection Reflect } }

#declare Wax = material {
  texture {
    pigment { rgb <0.9, 0.6, 0.4> }
    finish { ambient 0 diffuse 0.8 subsurface { translucency <1.5, 0.9, 0.5> #ifdef (Influence) minimum_influence Influence #end } }
  }
  interior { ior 1.4 }
}
sphere { <0, 0.6, -4.5>, 0.5 material { Wax } }
#if (Direct) sphere { <0.9, 0.5, 0>, 0.4 material { Wax } } #end
