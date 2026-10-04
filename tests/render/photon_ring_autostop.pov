#version 3.8;

global_settings {
  assumed_gamma 1
  photons { spacing 0.04 autostop 0 jitter 0 gather 20, 100 }
}

camera {
  location <0, 5, -8>
  look_at <0, 0, 0>
  right x*image_width/image_height
  angle 60
}

light_source {
  <0, 10, 0>
  color rgb 1
  parallel point_at <0, 0, 0>
  photons { refraction on reflection off }
}

plane {
  y, -1
  pigment { color rgb 0.8 }
  finish { diffuse 0.8 }
}

blob {
  threshold 0.6
  #for (I, 0, 31)
    #local A = 2*pi*I/32;
    sphere { <cos(A), 0, sin(A)>, 0.3, 1 }
    sphere { <3*cos(A), 0, 3*sin(A)>, 0.3, 1 }
  #end
  texture { pigment { color rgbf <1, 1, 1, 1> } finish { diffuse 0 } }
  interior { ior 1.5 }
  photons { target 1 refraction on reflection off collect off }
}
