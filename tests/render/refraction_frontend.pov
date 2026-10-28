#version Version;
#ifndef (Case) #declare Case = 0; #end
#ifndef (Units) #declare Units = 1; #end
#declare field = 4;
#declare detail = 5;
global_settings { assumed_gamma 1 mm_per_unit Units atmospheric_ior 1 }
#ifndef (Method) #declare Method=4; #end
#if (Case=20) media { method Method refraction 0.01 } #end
#if (Case=21) media { method Method refraction_detail 0.01 } #end
#if (Case=22) sphere { 0,1 interior { media { method Method refraction 0.01 } } } #end
#if (Case=23) sphere { 0,1 interior { media { method Method refraction_detail 0.01 } } } #end
camera { orthographic location <0, 0, -6> look_at 0 right x*4 up y*3 }
plane { z, 3 pigment { gradient x color_map { [0 rgb 0] [1 rgb 1] } scale 4 translate -2*x } finish { ambient 0 emission 1 diffuse 0 } }
box {
  <-2,-1.5,-1>, <2,1.5,1>
  pigment { rgbt 1 }
  interior {
    ior 1
    #if (Case = 8)
      refraction 1
    #else
      refraction {
        #if (Case = 1) strength -0.04
        #elseif (Case = 2) strength 0.08
        #else strength 0.04 #end
        #if (Case = 3) detail 0
        #elseif (Case = 4) detail -1
        #elseif (Case = 5) detail auto
        #else detail 0.06 #end
        #if (Case = 9)
        #elseif (Case = 6)
          field { pigment { function { 0.25 } } }
        #elseif (Case = 7)
          field { function { x } scale 2 }
        #elseif (Case = 1)
          field { function { -(2 + x/4) } }
        #elseif (Case = 2)
          field { function { 1 + x/8 } }
        #else
          field { function { 2 + x/4 } }
        #end
      }
    #end
  }
}
