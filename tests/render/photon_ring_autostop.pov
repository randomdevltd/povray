#version 3.8;

#ifndef(Spacing)
  #declare Spacing = 0.04;
#end
#ifndef(Spot)
  #declare Spot = 0;
#end
#ifndef(Cylinder)
  #declare Cylinder = 0;
#end
#ifndef(PointSource)
  #declare PointSource = 0;
#end
#ifndef(Autostop)
  #declare Autostop = 0;
#end
#ifndef(AreaSource)
  #declare AreaSource = 0;
#end
#ifndef(MultiTarget)
  #declare MultiTarget = 0;
#end
#ifndef(ManyTargets)
  #declare ManyTargets = 0;
#end

global_settings {
  assumed_gamma 1
  photons { spacing Spacing autostop Autostop jitter 0 gather 20, 100
    #ifdef(SaveMap) save_file "photon_ring_threads.ph" #end
  }
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
  #if (AreaSource)
    area_light <2, 0, 0>, <0, 0, 2>, 2, 2
  #elseif (Spot)
    spotlight point_at <0, 0, 0> radius 20 falloff 30
  #elseif (Cylinder)
    cylinder point_at <0, 0, 0> radius 3 falloff 5
  #elseif (!PointSource)
    parallel point_at <0, 0, 0>
  #end
  photons { refraction on reflection off #if (AreaSource) area_light #end }
}

plane {
  y, -1
  pigment { color rgb 0.8 }
  finish { diffuse 0.8 }
}

#macro PhotonTarget(Offset, Size)
  blob {
    threshold 0.6
    #for (I, 0, 31)
      #local A = 2*pi*I/32;
      sphere { Size*<cos(A), 0, sin(A)> + Offset, 0.3*Size, 1 }
      sphere { Size*<3*cos(A), 0, 3*sin(A)> + Offset, 0.3*Size, 1 }
    #end
    texture { pigment { color rgbf <1, 1, 1, 1> } finish { diffuse 0 } }
    interior { ior 1.5 }
    photons { target 1 refraction on reflection off collect off }
  }
#end

#if (ManyTargets)
  #for (Target, 0, 7)
    PhotonTarget(<(Target-3.5)*0.8, 0, 0>, 0.35)
  #end
#elseif (MultiTarget)
  PhotonTarget(<-2, 0, 0>, 1)
  PhotonTarget(<2, 0, 0>, 0.65)
#else
  PhotonTarget(<0, 0, 0>, 1)
#end
