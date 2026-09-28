#version 3.7;
#ifndef (Method) #declare Method = 2; #end
#ifndef (SurfaceOnly) #declare SurfaceOnly = 0; #end
#ifndef (Samples) #declare Samples = 256; #end
#ifndef (Spacing) #declare Spacing = 0.35; #end
#ifndef (PhotonCount) #declare PhotonCount = 200000; #end
#ifndef (GatherMin) #declare GatherMin = 0; #end
#ifndef (GatherMax) #declare GatherMax = 20000; #end
#ifndef (GatherRadius) #declare GatherRadius = 0.025; #end
#ifndef (Dispersion) #declare Dispersion = 1.08; #end
#ifndef (Bands) #declare Bands = 21; #end
#ifndef (Mfp) #declare Mfp = 0.08; #end
#ifndef (Skin) #declare Skin = 0; #end
#ifndef (Flesh) #declare Flesh = 0.85; #end
#ifndef (Thickness) #declare Thickness = 1; #end
#ifndef (Thin) #declare Thin = 0; #end
#ifndef (Receiver) #declare Receiver = 0; #end
#ifndef (Nearby) #declare Nearby = 0; #end
#ifndef (Power) #declare Power = 6; #end
#ifndef (Photons) #declare Photons = 1; #end
#ifndef (Fill) #declare Fill = 1; #end
#ifndef (Backdrop) #declare Backdrop = 0.04; #end
#ifndef (MapFile) #declare MapFile = "subsurface-prism.ph"; #end
global_settings {
  assumed_gamma 1 max_trace_level 16 mm_per_unit 1
  subsurface { method Method samples Samples, 0 spacing Spacing }
  #if (Photons)
    photons { count PhotonCount gather GatherMin, GatherMax radius GatherRadius autostop 0
      #ifdef (SaveMap) save_file MapFile #end
      #ifdef (LoadMap) load_file MapFile #end
    }
  #end
}
background { rgb Backdrop }
camera { orthographic location <(Receiver = 1 ? -7 : 7), 7, -11> look_at <0, 1.3, 0> right x * 10 up y * 6.25 }
#declare ReceiverFinish = finish {
  ambient 0 diffuse 0.8 specular 0
  #if (!SurfaceOnly)
    subsurface { translucency Mfp
      #if (Skin) colour rgb Flesh thickness Thickness #end
    }
  #end
}
#declare ReceiverTexture = texture {
  pigment { #if (Skin) rgb <0.35, 0.55, 0.8> #else rgb 0.85 #end }
  finish { ReceiverFinish }
}
#if (Receiver = 1)
  sphere { <2, 0.8, 0>, 0.8 texture { ReceiverTexture } interior { ior 1.4 } photons { collect on } }
  box { <-4, -1, -2>, <5, 0, 2> pigment { rgb 0.3 } finish { ambient 0 diffuse 0.8 } photons { collect off } }
#else
  box { <-4, (Thin ? -0.012 : -1), -2>, <5, 0, 2> texture { ReceiverTexture } interior { ior 1.4 } photons { collect on } }
#end
#if (Nearby)
  box { <-4, 0.015, 1.5>, <5, 0.027, 1.8>
    pigment { rgb 0.85 } finish { ambient 0 diffuse 0.8 } photons { collect on }
  }
#end
#ifndef (HidePrism)
  prism { linear_sweep linear_spline -0.65, 0.65, 4
    <-0.6, -0.346410>, <0.6, -0.346410>, <0, 0.692820>, <-0.6, -0.346410>
    pigment { rgbt 1 }
    finish { ambient 0 diffuse 0 specular 0.3 roughness 0.005 reflection { 0, 1 fresnel on } conserve_energy }
    interior { ior 1.5 dispersion Dispersion dispersion_samples Bands }
    rotate x * -90 rotate z * -18.6 translate <-2, 3, 0>
    photons { target refraction on reflection off collect off }
  }
#end
#ifndef (NoLights)
  light_source { <-20, 3, 0> rgb Power parallel point_at <-2, 3, 0>
    projected_through { box { <-3.51, 2.965, -0.55>, <-3.49, 3.035, 0.55> } }
    photons { refraction on reflection off }
  }
#end
#if (Fill)
  light_source { <-4, 8, -7> rgb 0.25 photons { refraction off reflection off } }
#end
