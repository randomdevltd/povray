// Equal generated flux over the same emitting support, despite container and cap representation.
#version 3.8;
#ifndef (Case) #declare Case = 1; #end
global_settings { assumed_gamma 1 }
camera { orthographic location <0, 8, 0> sky z look_at 0 right x * 2 up z * 2 }
plane { y, 0 pigment { rgb 1 } finish { ambient 0 emission 0 diffuse 1 } no_shadow }
#declare Support = function {
  select(max(abs(x) - 0.5, max(abs(y - 1.5) - 0.5, abs(z) - 0.4)), 1, 1, 0)
};
union {
  #switch (Case)
    #case (1) box { <-0.5, 1, -0.4>, <0.5, 2, 0.4> } #break
    #case (2) box { <-1, 0.5, -0.8>, <1, 2.5, 0.8> } #break
    #case (3) cylinder { <0, 1, 0>, <0, 2, 0>, 0.3 } #break
    #case (4) cylinder { <0, 1, 0>, <0, 2, 0>, 0.3 open } #break
    #case (5) box { <-0.5, 1, -0.5>, <0, 2, 0.5> } #break
    #case (6) box { <-0.5, 1, -0.5>, <0.5, 2, 0.5> clipped_by { plane { x, 0 } } } #break
  #end
  pigment { rgbt 1 } hollow no_image
  interior { light {
    #if (Case = 1 | Case = 2)
      pigment { function { Support(x, y, z) } color_map { [0 rgb 0] [1 rgb 1] } }
    #end
    brightness 10 lumens samples 1024 } }
}
