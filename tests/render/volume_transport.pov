// A fixed-flux source, separate from its absorbing medium, above a white receiver.
#version 3.8;
#ifndef (Case) #declare Case = 1; #end
#ifndef (PhotonGate) #declare PhotonGate = 0; #end
#ifndef (PhotonRun) #declare PhotonRun = 1; #end
#ifndef (PhotonQuality) #declare PhotonQuality = 4; #end
#ifndef (Draws) #declare Draws = 512; #end
#ifndef (SceneScale) #declare SceneScale = 1; #end
#ifndef (Sigma) #declare Sigma = 0; #end
#ifndef (Gain) #declare Gain = 1; #end
#ifndef (Flux) #declare Flux = 10; #end
#ifndef (Height) #declare Height = 0; #end
#ifndef (Gap) #declare Gap = 0; #end
#ifndef (Probe) #declare Probe = 0; #end
#declare Frame = 2;
#declare CameraHeight = 8;
#if (Case = 5) #declare Frame = 0.02; #end
#if (Case = 6 | Case = 7) #declare CameraHeight = 0.25; #end
global_settings {
  assumed_gamma 1
  #if (PhotonGate & PhotonRun) photons { method 2 quality PhotonQuality } #end
}
camera {
  orthographic location <0, CameraHeight, 0> * SceneScale sky z look_at 0
  right x * Frame * SceneScale up z * Frame * SceneScale
}
plane { y, Probe * SceneScale pigment { rgb 1 } finish { ambient 0 emission 0 diffuse 1 } no_shadow
  #if (PhotonGate) photons { pass_through off } #end
}
#if (Case = 6) fog { distance 1 / Sigma color rgb 0 } #end
#if (Case = 7)
  box { <-100, -0.1, -100>, <100, 100, 100> pigment { rgbt 1 } hollow
    interior { media { mix replace method 3 absorption Sigma } } }
#end
#declare Source = union {
  #switch (Case)
    #case (8)
      box { <-0.5, 1, -0.5>, <0, 2, 0.5> }
    #break
    #case (9)
      box { <-0.5, 1, -0.5>, <0.5, 2, 0.5> clipped_by { plane { x, 0 } } }
    #break
    #case (2) #case (3)
      box { <-0.73, 1.2, -0.3>, <-0.22, 1.8, 0.3> }
      box { <0.31, 1.2, -0.3>, <0.81, 1.8, 0.3> }
    #break
    #case (5)
      box { <-0.5, 0, -0.5>, <0.5, 1, 0.5> }
    #break
    #else
      box { <-0.5, 1, -0.4>, <0.5, 2, 0.4> }
  #end
  pigment { rgbt 1 } hollow no_image
  interior {
    #if (Sigma > 0 & Case != 6 & Case != 7) media { method 3 absorption Sigma / SceneScale } #end
    #if (Case = 7) media { mix replace method 3 absorption Sigma } #end
    light {
      #if (Case = 4)
        pigment { rgb <1, 0, 0> }
      #else
        #if (Case = 2 | Case = 3)
          pigment {
            gradient x color_map { [0 rgb <1, 0.2, 0.05>] [1 rgb <0.05, 0.4, 1>] }
            scale 2 translate -x
          }
        #end
      #end
      #if (Case = 5 | Case = 8 | Case = 9) brightness 0.1 nits #else brightness Flux * Gain lumens #end
      samples Draws
      #if (PhotonGate & PhotonRun) photons { refraction on } #end
    }
  }
  translate y * (Height + Gap)
  scale SceneScale
}
object { Source }
#if (Case = 3)
  box { <-0.13, 0.49, -2>, <0.13, 0.51, 2> pigment { rgb 0 } no_image }
#end

#if (PhotonGate)
  box {
    <-0.75 * Frame, 0.45, -0.75 * Frame> * SceneScale,
    <0.75 * Frame, 0.55, 0.75 * Frame> * SceneScale
    pigment { rgbt 1 } finish { ambient 0 emission 0 diffuse 0 }
    interior { ior 1 }
    photons { target 1 refraction on collect off }
  }
#end
