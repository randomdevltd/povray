#version 3.8;
#ifndef (Method) #declare Method = 2; #end
#ifndef (Source) #declare Source = 0; #end
#ifndef (Disabled) #declare Disabled = 1; #end
#ifndef (Power) #declare Power = 0.25; #end
global_settings {
    assumed_gamma 1
    photons { method Method #if (Method = 2) quality 0.25 #else count 16 #end }
}
camera { orthographic location <0, 1, -3> look_at 0 right 0.001*x up 0.001*y }
plane { y, 0 pigment { rgb 1 } finish { ambient 0 diffuse 1 } }
light_source {
    <0, 2, -3> rgb Power
    #if (Source = 1) fade_power 2 fade_distance 1 #end
    #if (Source = 2) area_light 0.2*x, 0.2*z, 2, 2 adaptive 0 circular orient #end
    #if (Source = 3) projected_through { sphere { <0, 1, -1.5>, 0.5 } } #end
    #if (Source = 4) shadowless #end
    photons { refraction 1-Disabled reflection 1-Disabled }
}
