#version 3.8;
#ifndef (Method) #declare Method = 2; #end
#ifndef (PhotonQuality) #declare PhotonQuality = 4; #end
#ifndef (PhotonCount) #declare PhotonCount = 524288; #end
#ifndef (Layered) #declare Layered = 1; #end
#ifndef (Nearby) #declare Nearby = 1; #end
global_settings {
    assumed_gamma 1 max_trace_level 8
    photons {
        method Method
        #if (Method=2) quality PhotonQuality #else count PhotonCount #end
    }
}
camera { location <0, 5, -6> look_at <0, 0, 0> right x*image_width/image_height angle 42 }
light_source { <-2, 6, -1> rgb 8 photon_only
    fade_power 2 fade_distance sqrt(0.5)
    photons { reflection on refraction on }
}
plane { y, 0 pigment { rgb 1 } finish { ambient 0 diffuse 1 } }
#if (Nearby)
box { <0.2, 0.004, -3>, <3, 0.005, 3>
    pigment { rgb <0.2, 0.6, 1> } finish { ambient 0 diffuse 1 }
}
#end
#if (Layered)
box { <-3, 0.01, -3>, <-0.2, 0.011, 3>
    pigment { rgbt <1, 0.7, 0.4, 0.5> } finish { ambient 0 diffuse 0.5 }
    photons { pass_through }
}
#end
#macro Lens(Center, Radius)
sphere { Center, Radius
    pigment { rgbt 1 }
    finish { ambient 0 diffuse 0 reflection { 0, 1 fresnel } conserve_energy }
    interior { ior 1.5 }
    photons { target reflection on refraction on }
}
#end
Lens(<-0.9, 1.0, 0>, 0.8)
Lens(<1.1, 0.28, -0.4>, 0.22)
