#version 4.0;
#ifndef (Left) #declare Left = 1; #end
#ifndef (Right) #declare Right = 1; #end
#ifndef (LeftLamp) #declare LeftLamp = 1; #end
#ifndef (RightLamp) #declare RightLamp = 1; #end
#ifndef (Grouped) #declare Grouped = 1; #end
global_settings {
    assumed_gamma 1 ambient_light 0 max_trace_level 8
    photons { method 2 quality 0.25 media 16 }
}
camera { orthographic location <0, 1, -6> look_at <0, 1, 0> right 5*x up 2.5*y }
// each half holds a photon-only lamp over a lens focusing into a scattering box; the left half may form a light group
#macro Half(X, Lamp)
    #if (Lamp) light_source { <X, 5, 0> rgb 3 photon_only on photons { refraction on reflection off } } #end
    sphere { <X, 3, 0>, 0.4 pigment { rgbf 1 } finish { ambient 0 diffuse 0 }
             interior { ior 1.5 } photons { target refraction on reflection off collect off } }
    box { <X - 1, 0, -1>, <X + 1, 2, 1> hollow pigment { rgbt 1 } finish { ambient 0 diffuse 0 }
          interior { media { intervals 1 samples 4 scattering { 1, 0.4 } } } photons { pass_through collect off } }
#end
#if (Left)
    #if (Grouped) light_group { Half(-1.2, LeftLamp) global_lights off photons on } #else Half(-1.2, LeftLamp) #end
#end
#if (Right) Half(1.2, RightLamp) #end
