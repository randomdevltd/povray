#version 3.8;

#ifndef(Photons)
  #declare Photons = 1;
#end
#ifndef(Spacing)
  #declare Spacing = 0.02;
#end
#ifndef(GatherMax)
  #declare GatherMax = 100;
#end

global_settings {
  assumed_gamma 1
  max_trace_level 8
  #if (Photons)
    photons { spacing Spacing autostop 0 jitter 0 gather 20, GatherMax }
  #end
}

camera {
  location <0, 2, -10>
  look_at <0, 0, 0>
  right x*image_width/image_height
  angle 65
}

light_source {
  <3, 16, -5>
  color rgb 1
  parallel point_at <0, 0, 0>
  #if (Photons)
    photons { refraction on reflection off }
  #end
}

plane {
  y, 0
  pigment { color rgb <0.85, 0.9, 0.8> }
  finish { diffuse 0.9 ambient 0 }
}

box {
  <-6, 3, -6>, <6, 4, 6>
  texture {
    pigment { color rgbf <1, 1, 1, 1> }
    normal { ripples 0.06 scale 0.45 }
    finish { diffuse 0 specular 0.15 ambient 0 }
  }
  interior { ior 1.33 }
  #if (Photons)
    photons { target 1 refraction on reflection off collect off }
  #end
}
