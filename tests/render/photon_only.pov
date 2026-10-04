#version 3.8;
#ifndef (Lamp) #declare Lamp = 1; #end
#ifndef (PhotonOnly) #declare PhotonOnly = 1; #end
#ifndef (Photons) #declare Photons = 0; #end
#ifndef (MixedLights) #declare MixedLights = 0; #end

global_settings {
    assumed_gamma 1
    max_trace_level 10
    ambient_light rgb 0
    #if (Photons) photons { count 40000 gather 20, 100 } #end
}
camera { location <0, 2, -4> look_at <0, 0.2, 0> angle 55 }
background { rgb 0 }
plane { y, 0 pigment { rgb 1 } finish { ambient 0 diffuse 1 } }
sphere {
    <0, 0.75, 0>, 0.5
    pigment { rgbf 1 }
    finish { ambient 0 diffuse 0 }
    interior { ior 1.5 }
    #if (Photons) photons { target refraction on reflection off collect off } #end
}
#if (Lamp)
light_source {
    <0, 4, 0>, rgb 3
    spotlight point_at <0, 0, 0> radius 20 falloff 25
    #if (PhotonOnly) photon_only on #end
    #if (Photons) photons { refraction on reflection off } #end
}
#end
#if (MixedLights)
light_source { <-2, 4, -3>, rgb 0.3 }
#end
