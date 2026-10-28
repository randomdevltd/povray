#version 3.8;
#ifndef (PhotonMethod) #declare PhotonMethod = 1; #end
#ifndef (Absorbing) #declare Absorbing = 1; #end
#ifndef (Varying) #declare Varying = 0; #end
#ifndef (InsideSource) #declare InsideSource = 0; #end
#ifndef (NativePointInside) #declare NativePointInside = 0; #end

global_settings {
    assumed_gamma 1 ambient_light rgb 0 max_trace_level 12
    photons {
        method PhotonMethod
        #if (PhotonMethod = 2) quality 2 #else count 30000 gather 40, 100 #end
        #if (NativePointInside) media 10, 1 #end
    }
}
camera { orthographic location <0, 0.5, -0.2> look_at 0 right 0.3*x up 0.3*y }
background { rgb 0 }
plane { y, 0 pigment { rgb 1 } finish { ambient 0 diffuse 1 } }
light_source {
    <0, (InsideSource ? 1.5 : 5), 0> rgb 0.5
    #if (!NativePointInside) parallel #end
    point_at 0 photon_only on
    photons { refraction on reflection off }
}
sphere {
    <0, (InsideSource ? 0.5 : 3), 0>, (InsideSource ? 0.4 : 0.8)
    pigment { rgbf 1 } finish { ambient 0 diffuse 0 }
    interior { ior 1 }
    photons { target refraction on reflection off collect off }
}
// Downward photons traverse the slab; camera rays stay below it.
box {
    <-2, 1, -2>, <2, 2, 2> hollow pigment { rgbt 1 } finish { ambient 0 diffuse 0 }
    interior {
        ior 1
        media {
            method 4 resolution 0.025 absorption Absorbing * <0.2, 0.4, 0.6>
            #if (NativePointInside) scattering { 1, rgb 0.01 extinction 0 } #end
            #if (Varying) density { function { 0.25 + 0.25*y } } #end
        }
    }
    photons { pass_through collect off }
}
