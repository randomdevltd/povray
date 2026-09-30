#version 3.7;
#ifndef (SurfaceOnly) #declare SurfaceOnly = 0; #end
#ifndef (CSG) #declare CSG = 0; #end
#ifndef (Method) #declare Method = 2; #end
#ifndef (Samples) #declare Samples = 128; #end
#ifndef (Single) #declare Single = 0; #end
#ifndef (Spacing) #declare Spacing = 0.7; #end
#ifndef (Photons) #declare Photons = 1; #end
#ifndef (Collect) #declare Collect = 1; #end
#ifndef (PhotonCount) #declare PhotonCount = 30000; #end
#ifndef (GatherMin) #declare GatherMin = 0; #end
#ifndef (GatherMax) #declare GatherMax = 20000; #end
#ifndef (GatherRadius) #declare GatherRadius = 0.12; #end
#ifndef (Skin) #declare Skin = 0; #end
#ifndef (Thickness) #declare Thickness = 1; #end
#ifndef (Glow) #declare Glow = 0; #end
#ifndef (Thin) #declare Thin = 0; #end
#ifndef (Nearby) #declare Nearby = 0; #end
#ifndef (ExtraLights) #declare ExtraLights = 0; #end
#ifndef (Area) #declare Area = 0; #end
#ifndef (RefractPhotons) #declare RefractPhotons = 1; #end
#ifndef (ReflectPhotons) #declare ReflectPhotons = 0; #end
#ifndef (LightPhotons) #declare LightPhotons = 1; #end
#ifndef (Radiosity) #declare Radiosity = 0; #end
#ifndef (Shift) #declare Shift = 0; #end
#ifndef (Mfp) #declare Mfp = 0.28; #end
#ifndef (Depth) #declare Depth = 0; #end
#ifndef (Spread) #declare Spread = 0; #end
#ifndef (Power) #declare Power = 0.08; #end
global_settings {
  assumed_gamma 1 mm_per_unit 1 max_trace_level 12
  subsurface { method Method samples Samples, Single spacing Spacing }
  #if (Photons)
    photons { count PhotonCount gather GatherMin, GatherMax radius GatherRadius autostop 0
      #ifdef (LoadMap) load_file "subsurface-photons.ph" #end
      #ifdef (SaveMap) save_file "subsurface-photons.ph" #end
    }
  #end
  #if (Radiosity)
    radiosity { count 20 nearest_count 5 error_bound 0.8 pretrace_start 0.08 pretrace_end 0.04 subsurface on }
  #end
}
camera { orthographic location <0, 7, -9> look_at <0, 0, 0> right x * 6 up y * 4.5 }
background { rgb 0 }
#declare ReceiverFinish = finish {
  diffuse 0.8 ambient 0 specular 0
  #if (!SurfaceOnly) subsurface { translucency Mfp
    #if (Depth > 0)
      pigment { bozo scale 0.1 color_map { [0 rgb <0.8, 0.5, 0.4>] [1 rgb <0.95, 0.75, 0.6>] } }
      volume_sampling { depth Depth #if (Spread > 0) spread Spread #end }
    #end
    #if (Skin) colour rgb <0.85, 0.65, 0.45> thickness Thickness #end
    #if (Glow) emission rgb <0.015, 0.02, 0.03> #end
  } #end
}
#if (CSG) merge { #end
box { <-2.8, -2 + 1.96 * Thin, -2.8>, <0.8, 0, 2.8>
  pigment { #if (Skin = 2) checker rgb <0.2, 0.4, 0.6> rgb <0.6, 0.4, 0.2> scale 0.18
            #elseif (Skin) rgb <0.25, 0.4, 0.6> #else rgb <0.85, 0.65, 0.45> #end }
  finish { ReceiverFinish } interior { ior 1.4 }
  photons { collect Collect }
}
#if (CSG)
  box { <3.1, -2, -2.8>, <3.2, 0, 2.8> pigment { rgb 0 } finish { ReceiverFinish } }
  interior { ior 1.4 }
} #end
box { <0.9, -0.1, -2.8>, <2.8, 0, 2.8> pigment { rgb <0.85, 0.65, 0.45> } finish { ambient 0 diffuse 0.8 } }
#if (Nearby)
  box { <-2.8, 0.006, -2.8>, <0.8, 0.012, 2.8> pigment { rgb 0.8 } finish { ambient 0 diffuse 0.8 }
        photons { collect on } }
#end
#ifndef (HideLens)
sphere { <Shift, 2.1, 0>, 0.9 pigment { rgbt 1 } finish { ambient 0 diffuse 0 reflection 0 }
         interior { ior 1.5 }
         photons { target refraction RefractPhotons reflection ReflectPhotons collect off } }
#end
#ifndef (NoLights)
  light_source { <Shift, 6, -0.2> rgb Power spotlight point_at <Shift, 0, 0> radius 8 falloff 10 tightness 0
    #if (Area) area_light x * 0.12, z * 0.12, 3, 3 adaptive 0 #end
    photons { refraction LightPhotons reflection LightPhotons #if (Area) area_light #end }
  }
#end
#for (Index, 1, ExtraLights)
  light_source { <Index, 4, -6> rgb 0 photons { refraction off reflection off } }
#end
