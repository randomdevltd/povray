#version 4.0;
#ifndef (ReceiverMethod) #declare ReceiverMethod = 4; #end
#ifndef (PhotonQuality) #declare PhotonQuality = 0.25; #end
#ifndef (Photons) #declare Photons = 1; #end
#ifndef (Heat) #declare Heat = 0.08; #end
#ifndef (MediaSteps) #declare MediaSteps = 16; #end
#ifndef (Grouped) #declare Grouped = 0; #end
#ifndef (Variation) #declare Variation = 0; #end
#ifndef (LampPower) #declare LampPower = 1; #end
global_settings {
    assumed_gamma 1 atmospheric_ior 1 max_trace_level 8 refraction_detail 0.1
    photons { method 2 quality PhotonQuality media MediaSteps }
}
camera { orthographic location <0, 2, -8> look_at <0, 2, 0> right 4*x up 4*y }
#if (Grouped) light_group { #end
sphere {
    <0, 5, 0>, 0.12 hollow no_image pigment { rgbt 1 }
    interior { ior off media { method 4 emission 100*LampPower
        light_source { samples 8 photons { refraction Photons reflection off } }
    } }
}
sphere {
    <0, 3.5, 0>, 0.65 hollow pigment { rgbt 1 } finish { ambient 0 diffuse 0 }
    interior { ior 1.4
        #if (Heat) media { method 3 refraction Heat density { spherical scale 0.65 translate 3.5*y } } #end
    }
    photons { target refraction on reflection off collect off }
}
box {
    <-2, 0, -1>, <2, 3, 1> hollow pigment { rgbt 1 }
    interior {
        media { method ReceiverMethod intervals 1 samples 2 scattering { 1, 0.3 } }
        media { method ReceiverMethod intervals 1 samples 2 absorption 0.01
            #if (ReceiverMethod = 4) resolution 0.08 #end
            density { function { 0.5+Variation*x } }
        }
    }
    photons { pass_through collect off }
}

#if (Grouped) photons on } #end
