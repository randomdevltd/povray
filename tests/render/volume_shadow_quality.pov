// Fixed-flux bulb and controlled shadow features; measure linear HDR with anti-aliasing off.
#version 3.8;
#ifndef (Case) #declare Case = 1; #end
#ifndef (Samples) #declare Samples = 64; #end
#ifndef (Shape) #declare Shape = 0; #end
#ifndef (Rotation) #declare Rotation = 0; #end
global_settings { assumed_gamma 1 ambient_light rgb 0 }
camera {
  orthographic location <0, 4, 0> direction -y sky z look_at 0
  right x * 0.025 up z * 0.003125
  rotate y * Rotation
}
plane { y, 0 pigment { rgb 1 } finish { ambient 0 emission 0 diffuse 1 specular 0 } }
union {
  #if (Shape = 0)
    sphere { <0, 1.8, 0>, 0.03 }
  #else
    box { <-0.03, 1.77, -0.03>, <0.03, 1.83, 0.03> }
  #end
  pigment { rgbt 1 } hollow no_image
  interior { light { brightness 806 lumens samples Samples } }
  rotate y * Rotation
}
#if (Case > 0)
union {
  #switch (Case)
  #case (1)
    box { <-1, 0.05995, -1>, <0, 0.06005, 1> pigment { rgb 0 } no_image }
  #break
  #case (2)
    cylinder { <-0.004, 0.06, -0.05>, <-0.004, 0.06, 0.05>, 0.00045 pigment { rgb 0 } no_image }
    cylinder { <0.004, 0.06, -0.05>, <0.004, 0.06, 0.05>, 0.0012 pigment { rgb 0 } no_image }
  #break
  #case (3)
    box { <-1, 0.05995, -1>, <-0.0005, 0.06005, 1> pigment { rgb 0 } no_image }
    box { <0.0005, 0.05995, -1>, <1, 0.06005, 1> pigment { rgb 0 } no_image }
  #break
  #case (4)
    cylinder { <0, 0.06, -0.05>, <0, 0.06, 0.05>, 0.0002 pigment { rgb 0 } no_image }
  #break
  #end
  rotate y * Rotation
}
#end
