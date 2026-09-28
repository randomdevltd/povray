// Low optical-depth shadow rays through a smooth density, with the camera outside the media.
#version 3.8;
#ifndef (Samples) #declare Samples=89; #end
#ifndef (Method) #declare Method=3; #end
global_settings { assumed_gamma 1.0 }
camera { location <0,0.7,-8> look_at <0,0,0> right x*4/3 angle 45 }
light_source { <-20,40,10> rgb 1.2 }
light_source { <20,30,-10> rgb <0.5,0.6,0.8> }
plane { y,0 pigment { rgb 0.8 } finish { ambient 0 diffuse 1 } }
box { <-100,1,-100>,<100,10,100> hollow pigment { rgbt 1 }
 interior { media { absorption <0.002,0.0017,0.0011> method Method intervals 1 samples Samples jitter 0
  density { spherical scale 60 color_map { [0 rgb 1] [0.7 rgb 0.85] [0.93 rgb 0.25] [1 rgb 0.05] } }
 } }
}
