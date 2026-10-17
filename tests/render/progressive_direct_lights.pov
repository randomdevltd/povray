#version 3.8;
#ifndef (Method) #declare Method = 2; #end
#ifndef (Source) #declare Source = 0; #end
#ifndef (Disabled) #declare Disabled = 1; #end
#ifndef (Power) #declare Power = 0.25; #end
#ifndef (Fade) #declare Fade = 0; #end
#ifndef (Lens) #declare Lens = 0; #end
global_settings {
    assumed_gamma 1
    photons { method Method #if (Method = 2) quality (Lens ? 4 : 0.25) #else count 16 #end }
}
camera { orthographic location <0, 1, -3> look_at 0 right 0.001*x up 0.001*y }
plane { y, 0 pigment { rgb 1 } finish { ambient 0 diffuse 1 } }
light_source {
    <0, 2, -3> rgb Power
    #if (Source = 1) fade_power 2 fade_distance 1 #end
    #if (Source = 2) area_light 0.2*x, 0.2*z, 2, 2 adaptive 0 circular orient #end
    #if (Source = 3) projected_through { sphere { <0, 1, -1.5>, 0.5 } } #end
    #if (Source = 4) shadowless #end
    #if (Lens) photon_only on #end
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
#if (Lens)
    sphere { <0, 1, -1.5>, 0.5 pigment { rgbf 1 } finish { ambient 0 diffuse 0 } interior { ior 1 }
             photons { target refraction on reflection off } }
#end
