#version 3.8;
#ifndef (Check) #declare Check = 0; #end
global_settings { assumed_gamma 1 #if (Check = 7) photons { count 1000 gather 5, 20 } #end }

#declare View = camera { location <0, 0, -4> look_at 0 right x*4/3 }
#declare ScreenValue = function { pattern { pigment_pattern { screen { camera { View } fallback { rgb 0.25 } } } } }
#declare ScreenPigment = pigment { screen { camera { View } fallback { rgb 0.25 } } }

#if (Check = 0)
  #declare Object = skein { expressions { scale 2 } pigment { ScreenPigment } }
#elseif (Check = 1)
  #declare Object = isosurface { function { x*x + y*y + z*z - 1 } pigment { ScreenPigment } }
#elseif (Check = 2)
  #declare Object = skein_mesh { expressions { scale 2 } pigment { ScreenPigment } }
#elseif (Check = 3)
  #declare Object = isosurface_mesh { function { x*x + y*y + z*z - 1 } pigment { ScreenPigment } }
#elseif (Check = 4)
  #declare Object = skein_mesh { expressions { displace function(u, v) { ScreenValue(u, v, 0) } } }
#elseif (Check = 5)
  #declare Object = isosurface_mesh { function { x*x + y*y + z*z - 1 + ScreenValue(x, y, z) } }
#elseif (Check = 6)
  #declare Object = box {
    -1, 1  hollow  pigment { rgbt 1 }
    interior { media { method 4 density { function { ScreenValue(x, y, z) } } } }
  }
#elseif (Check = 7)
  #declare Object = sphere { 0, 1 pigment { ScreenPigment } photons { target reflection on } }
  light_source { <0, 0, -4> rgb 1 photons { reflection on } }
#elseif (Check = 8)
  #declare Object = sphere {
    0, 1  pigment { rgb 0.8 }
    normal { pigment_pattern { ScreenPigment } 0.2 }
    photons { target reflection on }
  }
  light_source { <0, 0, -4> rgb 1 photons { reflection on } }
#elseif (Check = 9)
  #declare Object = skein { expressions { displace function(u, v) { ScreenValue(u, v, 0) } } }
#elseif (Check = 10)
  #declare Object = isosurface { function { x*x + y*y + z*z - 1 + ScreenValue(x, y, z) } }
#elseif (Check = 11)
  #declare Object = parametric { function { u }, function { v }, function { ScreenValue(u, v, 0) } <0, 0>, <1, 1> }
#else
  #declare Object = height_field { function 4, 4 { ScreenValue(x, y, z) } }
#end

camera { View }
object { Object }
