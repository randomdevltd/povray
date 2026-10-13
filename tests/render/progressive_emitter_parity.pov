#version 3.8;
#ifndef (Method) #declare Method = 2; #end
#ifndef (Source) #declare Source = 0; #end
#ifndef (Aperture) #declare Aperture = 0; #end
#ifndef (Blocker) #declare Blocker = 0; #end
#ifndef (Medium) #declare Medium = 0; #end
#ifndef (Power) #declare Power = 0.04; #end
#ifndef (PhotonQuality) #declare PhotonQuality = 0.25; #end
global_settings {
    assumed_gamma 1 max_trace_level 8
    photons { method Method #if (Method = 2) quality PhotonQuality #else count 65536 #end }
}
camera { orthographic location <0, 5, -6> look_at 0 right 5*x up 3*y }
plane { y, 0 pigment { rgb 1 } finish { ambient 0 diffuse 1 } }
sphere { y, 0.8 pigment { rgbt 1 } finish { ambient 0 diffuse 0 }
         interior { ior 1 } photons { target refraction on reflection off } }
light_source {
    <0, 6, 0> rgb Power photon_only
    fade_power 2 fade_distance sqrt(0.5)
    photons { refraction on reflection off area_light }
    #if (Source = 1 | Source = 2) area_light 0.7*x, 0.7*z, 5, 5 #end
    #if (Source = 2) circular orient #end
    #if (Source = 3) parallel point_at 0 fade_power 0 #end
    #switch (Aperture)
        #case (1) projected_through { box { <-100, 3, -100>, <100, 3.1, 100> } } #break
        #case (2) projected_through { box { <10, 3, -1>, <11, 3.1, 1> } } #break
        #case (3) projected_through { box { <0, 3, -100>, <100, 3.1, 100> } } #break
        #case (4) projected_through { union {
            box { <-100, 4, -100>, <100, 4.1, 100> }
            box { <-100, 3, -100>, <100, 3.1, 100> }
        } } #break
    #end
}
#if (Blocker)
    box { <-100, 3.5, -100>, <100, 3.6, 100> no_image
          pigment { rgb 0 } finish { ambient 0 diffuse 0 } }
#end
#if (Medium)
    box { <-100, -1, -100>, <100, 3.2, 100> hollow no_image pigment { rgbt 1 }
          interior { media { absorption 0.15 } }
          photons { pass_through collect off } }
#end
