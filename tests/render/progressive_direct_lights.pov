#version 3.8;
#ifndef (Method) #declare Method = 2; #end
#ifndef (Source) #declare Source = 0; #end
#ifndef (Disabled) #declare Disabled = 1; #end
#ifndef (Power) #declare Power = 0.25; #end
#ifndef (Fade) #declare Fade = 0; #end
#ifndef (Lens) #declare Lens = 0; #end
#ifndef (Blocked) #declare Blocked = 0; #end
#ifndef (Distance) #declare Distance = sqrt(3.25); #end  // from the light to the lens centre
#ifndef (Media) #declare Media = 0; #end                 // a scattering cube round the viewed point, no floor
#ifndef (Glass) #declare Glass = 0; #end                 // the lens is glass, viewed from above over its whole caustic
#ifndef (Bend) #declare Bend = 0; #end                   // the lens is a rod whose index field bends the beam (2: no field)
#ifndef (PhotonQuality) #declare PhotonQuality = (Lens ? 4 : 0.25); #end
#declare Toward = vnormalize(<0, 2, -3>);
#declare Gap = sqrt(3.25);
#declare Reach = Gap + Distance;
#switch (Fade)
    #case (1) #declare Boost = (1 + Reach / 2) / 2; #break
    #case (2) #declare Boost = Reach * Reach; #break
    #case (3) #declare Boost = (1 + pow(Reach / 1.5, 3)) / 2; #break
    #case (4) #declare Boost = (1 + Reach * Reach) / 2; #break
    #case (5) #declare Boost = (1 + Reach) / 2; #break
    #else #declare Boost = 1;
#end
global_settings {
    assumed_gamma 1
    photons { method Method #if (Method = 2) quality PhotonQuality #else count (Glass ? 400000 : 16) #end
              #if (Blocked) max_trace_level 1 #end #if (Media) media 32 #end }
}
#if (Glass)
    camera { orthographic location <0, 5, 0> look_at 0 right 3*x up 3*y }
#elseif (Bend)
    camera { orthographic location <0, 1, 3> look_at 0 right 0.001*x up 0.001*y }
#else
    camera { orthographic location <0, 1, -3> look_at 0 right 0.001*x up 0.001*y }
#end
#if (Media)
    box { -0.6, 0.6 hollow pigment { rgbt 1 } finish { ambient 0 diffuse 0 }
          interior { media { intervals 1 samples 8 scattering { 1, 1 } } } photons { pass_through } }
#else
    plane { y, 0 pigment { rgb 1 } finish { ambient 0 diffuse 1 } }
#end
light_source {
    Toward * Reach rgb Power * Boost
    #if (Source = 1) fade_power 2 fade_distance 1 #end
    #if (Source = 2) area_light 0.2*x, 0.2*z, 2, 2 adaptive 0 circular orient #end
    #if (Source = 3) projected_through { sphere { <0, 1, -1.5>, 0.5 } } #end
    #if (Source = 4) shadowless #end
    #if (Lens & !Blocked) photon_only on #end
    #switch (Fade)
        #case (1) fade_power 1 fade_distance 2 #break
        #case (2) fade_power 2 #break
        #case (3) fade_power 3 fade_distance 1.5 #break
        #case (4) parallel point_at 0 fade_power 2 fade_distance 1 #break
        #case (5) cylinder point_at 0 radius 2 falloff 3 fade_power 1 fade_distance 1 #break
    #end
    photons { refraction 1-Disabled reflection 1-Disabled }
}
// an ior 1 target on the line to the viewed point: its photons alone light that point
#if (Lens & Bend)
    #declare Across = vnormalize(vcross(x, Toward));
    cylinder { -z, z, 0.5 pigment { rgbf 1 } finish { ambient 0 diffuse 0 }
               interior { ior 1 #if (Bend = 1) refraction { strength 0.1 field { function { max(0, 1 - z*z) * (1 + x) / 2 } } detail auto } #end }
               photons { target refraction on reflection off }
               matrix <1, 0, 0, Across.x, Across.y, Across.z, Toward.x, Toward.y, Toward.z, 0, 0, 0> translate Toward * (Gap + 0.5) }
#elseif (Lens)
    sphere { Toward * Gap, 0.5 pigment { rgbf 1 } finish { ambient 0 diffuse 0 } interior { ior (Glass ? 1.5 : 1) }
             photons { target refraction on reflection off } #if (Glass) no_image #end }
#end
