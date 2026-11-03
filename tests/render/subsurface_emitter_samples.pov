#version 3.7;
#ifndef (Samples) #declare Samples = 4; #end
#ifndef (Multiplier) #declare Multiplier = 1; #end
#ifndef (Cloud) #declare Cloud = 0; #end
#ifndef (Source) #declare Source = 0; #end
global_settings {
  assumed_gamma 1
  mm_per_unit (Cloud ? 1 : 1000)
  subsurface { method 2 samples 4, 1 spacing 2 }
}
camera { location <0,1.5,-4> look_at <0,0.4,0> right x*image_width/image_height angle 45 }
#if (Source = 0)
box { <-1,3,-2>, <1,3.1,0> pigment { rgb 1 } finish { ambient 0 diffuse 0 }
  light { brightness 10 samples Samples shadowless media_interaction off media_attenuation off }
}
#else
light_source { <-2,5,-3> rgb 1.3
  #if (Source = 2) area_light x,z,4,4 #end
}
#end
box { <-0.6,0,-0.6>, <0.6,1,0.6>
  pigment { rgb <0.8,0.4,0.25> }
  finish { ambient 0 diffuse 0.7 light_samples Multiplier
    subsurface { translucency <3,2,1.5> minimum_influence 0 }
  }
  interior { ior 1.4 }
}
