#version 3.7;
#ifndef (Photons) #declare Photons = 1; #end
#ifndef (PhotonFile) #declare PhotonFile = 0; #end
#ifndef (Bounce) #declare Bounce = 1; #end
global_settings {
  assumed_gamma 1 max_trace_level 8
  #if (Photons) photons { method 2 quality 0.25 media 16
    #if (PhotonFile = 1) save_file "bounce_self.photons" #end
    #if (PhotonFile = 2) load_file "bounce_self.photons" #end
  } #end
}
camera { orthographic location <0, 0, -8> look_at 0 right 3*x up 2*y }
background { rgb 0 }
box { <-1, -0.7, -0.5>, <1, 0.7, 0.5> hollow pigment { rgbt 1 }
  interior {
    media { method 4 scattering { 1, rgb 0.5 } absorption rgb 0.05 }
    light {
      pigment { rgb <1, 0.6, 0.3> } brightness 1 samples 4
      photons { reflection on refraction on }
      #if (Bounce) bounce { passes 1 self off } #end
    }
  }
}
box { <-2, -1, 1>, <2, 1, 1.01>
  pigment { rgb 0 } finish { ambient 0 diffuse 0 reflection 1 }
  photons { target reflection on collect off }
}
