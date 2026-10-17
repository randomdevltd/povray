#version 3.8;
#ifndef (Method) #declare Method = 2; #end
#ifndef (Radiosity) #declare Radiosity = 1; #end
#ifndef (Caustic) #declare Caustic = 1; #end
#ifndef (PhotonQuality) #declare PhotonQuality = 0.25; #end
global_settings {
    assumed_gamma 1 ambient_light 0 max_trace_level 8
    photons { method Method #if (Method = 2) quality PhotonQuality #else count 200000 #end }
    #if (Radiosity)
        radiosity { pretrace_start 0.08 pretrace_end 0.04 count 200 nearest_count 10 error_bound 0.5 recursion_limit 1
                    low_error_factor 0.5 gray_threshold 0 brightness 1 }
    #end
}
camera { location <0, 1.2, -4> look_at <0, 0.9, 1> right x*image_width/image_height angle 50 }
// the lamp lights nothing directly and the wall collects no photons: radiosity alone carries the floor caustic to it
light_source { <0, 5, -1> rgb 30 photon_only on fade_power 2 fade_distance 0.7071067811865476
               photons { refraction Caustic reflection off } }
plane { y, 0 pigment { rgb 1 } finish { ambient 0 emission 0 diffuse 1 } }
plane { -z, -1 pigment { rgb 1 } finish { ambient 0 emission 0 diffuse 1 } photons { collect off } }
sphere { <0, 1.6, -0.2>, 0.5 pigment { rgbf 1 } finish { ambient 0 diffuse 0 }
         interior { ior 1.5 } photons { target refraction on reflection off collect off } }
