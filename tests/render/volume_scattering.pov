// A transparent source and a separate dilute isotropic scattering receiver.
#version 3.8;
#ifndef (Receiver) #declare Receiver = 0; #end
#ifndef (Radius) #declare Radius = 1; #end
#ifndef (Surface) #declare Surface = 0; #end
#ifndef (Native) #declare Native = 1; #end
#ifndef (SourceL) #declare SourceL = 100; #end
global_settings { assumed_gamma 1 }
#if (Surface)
  camera {
    orthographic location <0, 8, 0> sky z look_at 0
    right x * 0.2 * Radius up z * 0.2 * Radius
  }
  plane { y, 0 pigment { rgb 1 } finish { ambient 0 emission 0 diffuse 1 } no_shadow }
#else
  camera {
    orthographic location <Receiver, 0, -3> * Radius look_at x * Receiver * Radius
    right x * 0.002 * Radius up y * 0.002 * Radius
  }
  box {
    <Receiver - 0.001, -0.001, -0.02> * Radius,
    <Receiver + 0.001, 0.001, 0.02> * Radius
    pigment { rgbt 1 } hollow
    interior {
      media { method 4 intervals 1 samples 256, 256
        scattering { 1, rgb 0.01 / Radius extinction 1 } }
    }
  }
#end
sphere {
  0, #if (Native) Radius #else 1 #end
  #if (Surface = 2) clipped_by { plane { x, 0 } } #end
  pigment { rgbt 1 } hollow no_image
  interior { light { brightness SourceL nits samples 256 } }
  #if (!Native) scale Radius #end
  #if (Surface) translate y * 2 * Radius #end
}
