#version 3.8;
#ifndef (Method) #declare Method = 2; #end
#ifndef (PhotonQuality) #declare PhotonQuality = 1; #end
#ifndef (LampPower) #declare LampPower = 40; #end
#ifndef (PhotonOnly) #declare PhotonOnly = 0; #end
#ifndef (LampType) #declare LampType = 0; #end
#ifndef (Mist) #declare Mist = 0; #end
#ifndef (Dispersion) #declare Dispersion = 1; #end
#ifndef (Subsurface) #declare Subsurface = 0; #end
#ifndef (FadeDistance) #declare FadeDistance = 0; #end
#ifndef (PhotonCount) #declare PhotonCount = 262144; #end
#ifndef (ViewMode) #declare ViewMode = 0; #end
#ifndef (Thin) #declare Thin = 0; #end
#ifndef (Nearby) #declare Nearby = 0; #end
#ifndef (TestGlow) #declare TestGlow = 0; #end
#ifndef (SourceGlow) #declare SourceGlow = 0; #end
#ifndef (InactiveLights) #declare InactiveLights = 0; #end
#ifndef (OnlyVolume) #declare OnlyVolume = 0; #end
#ifndef (Curved) #declare Curved = 0; #end
global_settings {
    assumed_gamma 1
    max_trace_level 8
    #if (Subsurface) mm_per_unit 1 subsurface { method 1 samples 16, 0 } #end
    photons {
        #if (Method > 0) method Method #end
        #if (Method = 2) quality PhotonQuality #else count PhotonCount #end
        #if (Mist) media 32 #end
    }
}
camera {
    location <0, 5, -8>
    look_at <0, 0, 0>
    right x*image_width/image_height
    angle 40
}
light_source {
    <-2, 6, -1> color rgb LampPower photon_only PhotonOnly
    photons { reflection on refraction on }
    #switch (LampType)
        #case (1) area_light 0.25*x, 0.25*z, 3, 3 #break
        #case (2) parallel point_at 0 #break
        #case (3) spotlight point_at 0 radius 10 falloff 25 #break
        #case (4) cylinder parallel point_at 0 radius 1.5 falloff 2 #break
    #end
    #if (LampType != 2 & LampType != 4) fade_power 2 fade_distance FadeDistance #end
}
#for (Index, 1, InactiveLights)
    light_source { <Index, 5, -2> rgb 0 }
#end
#if (Subsurface)
    box { <-4, -1, -4>, <4, 0, 4> pigment { rgb 0.8 }
          finish { ambient 0 diffuse 0.8 subsurface { translucency 0.3 } } interior { ior 1.4 } }
#elseif (Thin)
    box { <-4, -0.001, -4>, <4, 0, 4> pigment { rgb 1 } finish { ambient 0 diffuse 1 } }
#else
    plane { y, 0 pigment { color rgb 1 } finish { ambient 0 diffuse 1-OnlyVolume } }
#end
#if (Nearby)
    box { <0, 0.003, -4>, <4, 0.004, 4> pigment { rgb <0, 0.5, 1> } finish { ambient 0 diffuse 1 } }
#end
#if (ViewMode = 1)
    box { <-10, -10, -4.02>, <10, 10, -4> pigment { rgbt 1 } finish { ambient 0 diffuse 0 } interior { ior 1.5 } }
#elseif (ViewMode = 2)
    plane { x, -2 pigment { rgb 0 } finish { ambient 0 diffuse 0 reflection 1 } }
#end
sphere {
    <0, 1.1, 0>, 1
    #if (Curved) hollow #end
    pigment { color rgbt 1 }
    finish { ambient 0 diffuse 0 reflection { 0, 1 fresnel } conserve_energy }
    interior { ior 1.5 dispersion Dispersion
        #if (Curved) media { method 3 refraction 0.1 density { spherical scale 1.2 translate 1.1*y } } #end
    }
    photons { target reflection on refraction on }
}
#if (Mist)
    box { <-3, 0.001, -3>, <3, 3, 3> hollow pigment { rgbt 1 }
          interior { media { method 3 intervals 1 samples 8 scattering { 1, 0.1 } absorption 0.02
                             density { bozo scale 0.6 } } }
          photons { pass_through collect off } }
#end
#if (TestGlow)
    sphere { <-2, 6, -1>, 0.2 hollow no_image pigment { rgbt 1 }
             interior { media { emission SourceGlow } }
             photons { pass_through collect off } }
#end
