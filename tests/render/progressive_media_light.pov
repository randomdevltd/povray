#version 4.0;
#ifndef (Photons) #declare Photons = 1; #end
#ifndef (PhotonQuality) #declare PhotonQuality = 1; #end
#ifndef (PointLight) #declare PointLight = 0; #end
#ifndef (Emitters) #declare Emitters = 1; #end
#ifndef (Brightness) #declare Brightness = 1; #end
#ifndef (LampRadius) #declare LampRadius = 0.04; #end
#ifndef (LensIor) #declare LensIor = 1; #end
#ifndef (SourceRefraction) #declare SourceRefraction = 1; #end
#ifndef (Absorption) #declare Absorption = 0; #end
#ifndef (Heat) #declare Heat = 0; #end
#declare Power = <3, 2, 1>;
global_settings {
    assumed_gamma 1 atmospheric_ior 1 max_trace_level 12
    #if (Photons) photons { method 2 quality PhotonQuality } #end
}
camera { orthographic location <0, 8, 0> sky z look_at 0 right 4*x up 4*z }
plane { y, 0 pigment { rgb 1 } finish { ambient 0 diffuse 1 } }
sphere {
    <0, 2, 0>, 0.8 pigment { rgbt 1 } finish { ambient 0 diffuse 0 }
    interior { ior LensIor } photons { target refraction on reflection off collect off }
}
#if (PointLight)
    light_source { <0, 5, 0> rgb Power*Brightness fade_power 2 fade_distance 0
                   photons { refraction SourceRefraction reflection off } }
#else
    #for (Index, 0, Emitters-1)
        #declare Lamp = <(mod(Index, 8)-(min(Emitters, 8)-1)/2)*0.15,
                          5, (floor(Index/8)-(ceil(Emitters/8)-1)/2)*0.15>;
        sphere {
            0, LampRadius hollow no_image pigment { rgbt 1 }
            interior { ior 1
                media { method 4 emission Power/(Emitters*4*pi*pow(LampRadius, 3)/3)
                    absorption Absorption
                    light_source { samples 64 brightness Brightness
                                   photons { refraction SourceRefraction reflection off } }
                }
                #if (Heat)
                    media { method 3 refraction Heat density { spherical scale LampRadius } }
                #end
            }
            translate Lamp
        }
    #end
#end
