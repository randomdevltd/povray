#version 3.8;

global_settings {
  assumed_gamma 1
  photons { spacing 0.05 autostop 0 jitter 0 gather 20, 100 }
}

camera {
  location <0, 12, -30>
  look_at <0, 0, 0>
  right x*image_width/image_height
  angle 85
}

light_source {
  <-4, 12, -3>
  color rgb 1
  photons { refraction on reflection on }
}

plane {
  y, 0
  pigment { color rgb 0.9 }
  finish { diffuse 0.8 }
}

sphere {
  <0, 2, 0>, 2
  texture {
    pigment { color rgbf <1, 1, 1, 0.95> }
    finish { specular 0.1 }
  }
  interior { ior 1.5 }
  photons { target 1 refraction on reflection on collect off }
}
