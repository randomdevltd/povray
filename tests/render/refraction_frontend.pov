#version Version;
#ifndef (Case) #declare Case=0; #end
#ifndef (Units) #declare Units=1; #end
#ifndef (Method) #declare Method=4; #end
#declare detail=5;
global_settings { assumed_gamma 1 mm_per_unit Units atmospheric_ior 1 }
#if (Case=20) media { method Method refraction 0.01 } #end
#if (Case=21) media { method Method refraction_detail 0.01 } #end
#if (Case=22) sphere { 0,1 interior { media { method Method refraction 0.01 } } } #end
#if (Case=23) sphere { 0,1 interior { media { method Method refraction_detail 0.01 } } } #end
camera { orthographic location <0,0,-6> look_at 0 right x*4 up y*3 }
plane { z,3 pigment { gradient x color_map { [0 rgb 0] [1 rgb 1] } scale 4 translate -2*x } finish { ambient 0 emission 1 diffuse 0 } }
// A patterned texture keeps an interior's own classic refraction value, which a plain finish would replace.
box { <-2,-1.5,-1>,<2,1.5,1>
 #if (Case=16 | Case=17) texture { checker texture { pigment { rgbf 1 } } texture { pigment { rgbf <1,0.5,1,1> } } }
 #else pigment { rgbt 1 } #end
 interior { ior 1
  #if (Case=8) refraction 1
  #elseif (Case=10 | Case=17) refraction off
  #elseif (Case=16) refraction 0
  #else
   refraction {
    #if (Case=3) detail 0
    #elseif (Case=4) detail -1
    #elseif (Case=5) detail auto
    #else detail 0.06 #end
    #switch(Case)
    #case(1) function { -0.04*(-(2+x/4)) } #break
    #case(2) function { 0.02*(2+x/4) } function { 0.02*(2+x/4) } #break
    #case(6) pigment { rgb 0.25 } #break
    #case(7) function { x } scale 2 #break
    #case(9) #break
    #case(11) function { 0.04 } #break
    #case(12) function { -0.04*(2+x/4) } #break
    #case(13) strength 0.04 function { x } #break
    #case(14) field { function { x } } #break
    #case(15) off function { x } #break
    #else function { 0.04*(2+x/4) }
    #end
   }
  #end
 }
}
