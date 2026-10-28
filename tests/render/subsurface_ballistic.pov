#version 3.7;
#ifndef (Method) #declare Method = 2; #end
#ifndef (Angle) #declare Angle = 0; #end
#ifndef (Index) #declare Index = 1.44; #end
#ifndef (Length) #declare Length = 1e9; #end
#ifndef (Wedge) #declare Wedge = 0; #end
#ifndef (Conserve) #declare Conserve = 0; #end
global_settings {
  assumed_gamma 1.0
  mm_per_unit 1
  subsurface { samples 0, 0 method Method radiosity 0 }
}
camera { orthographic location <0, 0, -10> direction z right x * 0.1 up y * 0.1 }
background { rgb 0 }
box { <-4, -2, 10>, <4, 2, 10.1> pigment { rgb 1 } finish { ambient 0 diffuse 0 emission 1 } }
intersection {
  box { <-100, -100, 0>, <100, 100, 1 + 99 * Wedge> }
  #if (Wedge) plane { z, 0 rotate y * 30 translate z } #end
  pigment { rgb 1 }
  finish {
    ambient 0 diffuse 1
    subsurface { translucency Length }
    #if (Conserve) reflection { 0, 1 fresnel } conserve_energy #end
  }
  interior { ior Index }
  rotate y * Angle
}
