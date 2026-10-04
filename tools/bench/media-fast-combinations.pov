// Static media with layered densities, emission, absorption, scattering, two parallel lights and several containers.
#version 3.7;
#ifndef (Fast) #declare Fast = 0; #end
#ifndef (CellSize) #declare CellSize = 0.12; #end
#ifndef (Area) #declare Area = 0; #end
#ifndef (Samples) #declare Samples = 12; #end
#ifndef (Intervals) #declare Intervals = 3; #end
#ifndef (SecondarySamples) #declare SecondarySamples = 8; #end
#ifndef (SecondaryIntervals) #declare SecondaryIntervals = 2; #end
global_settings { assumed_gamma 1.0 max_trace_level 8 }
camera { location <0, 1.4, -7.5> look_at <0, 0.75, 0> angle 48 right x * 16 / 9 up y }
background { color rgb <0.015, 0.02, 0.035> }
light_source { <-40, 70, -20> color rgb <0.9, 0.85, 0.78> parallel point_at <0, 0, 0> media_attenuation on }
light_source { <35, 45, 20> color rgb <0.35, 0.45, 0.7> parallel point_at <0, 0, 0> media_attenuation on }
#if (Area)
light_source { <0, 5, -3> color rgb 0.5 area_light x * 0.7, z * 0.7, 3, 3 adaptive 1 media_attenuation on }
#end
plane { y, -0.3 pigment { color rgb <0.22, 0.24, 0.28> } finish { diffuse 0.8 } }

sphere { <-2.1, 0.75, 0>, 1.15 hollow pigment { color rgbt 1 }
  interior { media {
    #if (Fast) method 4 resolution CellSize #else method 3 #end
    scattering { 1, rgb <0.7, 0.8, 1.0> * 0.8 extinction 0.8 }
    absorption rgb <0.05, 0.12, 0.2>
    density { bozo turbulence 0.45 octaves 4 density_map { [0 rgb 0.02] [0.35 rgb 0.08] [0.75 rgb 1.4] [1 rgb 2.5] } scale 0.3 }
    density { gradient y color_map { [0 rgb 0.1] [0.4 rgb 1] [1 rgb 0.3] } scale 2 }
    intervals Intervals samples Samples
  } }
}

sphere { <0, 0.8, 0.2>, 1.1 hollow pigment { color rgbt 1 }
  interior { media {
    #if (Fast) method 4 resolution CellSize #else method 3 #end
    scattering { 2, rgb <0.7, 0.35, 0.15> * 0.7 extinction 0.65 }
    absorption rgb <0.12, 0.05, 0.02>
    emission rgb <0.28, 0.07, 0.015>
    density { crackle turbulence 0.18 octaves 3 density_map { [0 rgb 0] [0.3 rgb 0.05] [0.65 rgb 1] [1 rgb 2] } scale 0.24 }
    density { spherical color_map { [0 rgb 1] [0.9 rgb 0.4] [1 rgb 0] } scale 1.15 translate <0, 0.8, 0.2> }
    density { function { max(0, 1 - sqrt(x * x + z * z) / 1.5) } }
    intervals Intervals samples Samples
  } }
}

box { <1.25, 0, -0.65>, <3.05, 1.65, 0.65> hollow pigment { color rgbt 1 }
  interior { media {
    #if (Fast) method 4 resolution CellSize #else method 3 #end
    scattering { 1, rgb <0.25, 0.8, 0.55> * 0.7 extinction 0.9 }
    absorption rgb <0.08, 0.04, 0.1>
    density { wrinkles turbulence 0.2 octaves 4 color_map { [0 rgb 0.1] [0.4 rgb 0.2] [0.7 rgb 1.3] [1 rgb 2.0] } scale 0.2 }
    density { gradient x color_map { [0 rgb 0.15] [0.5 rgb 1] [1 rgb 0.4] } scale 1.8 translate x * 1.25 }
    intervals Intervals samples Samples
  }
  media {
    absorption rgb <0.025, 0.015, 0.035>
    density { gradient y color_map { [0 rgb 0.1] [1 rgb 0.65] } scale 1.65 }
    method 3 intervals SecondaryIntervals samples SecondarySamples
  } }
}
