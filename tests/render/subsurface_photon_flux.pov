#version 3.7;
#ifndef (Method) #declare Method = 2; #end
#ifndef (SurfaceOnly) #declare SurfaceOnly = 0; #end
#ifndef (Samples) #declare Samples = 256; #end
#ifndef (Spacing) #declare Spacing = 0.7; #end
#ifndef (Photons) #declare Photons = 1; #end
#ifndef (Collect) #declare Collect = 1; #end
#ifndef (GatherMax) #declare GatherMax = 4096; #end
#ifndef (GatherRadius) #declare GatherRadius = 0.25; #end
#ifndef (Eta) #declare Eta = 1.4; #end
#ifndef (Mm) #declare Mm = 1; #end
#ifndef (Mfp) #declare Mfp = 0.25; #end
#ifndef (Angle) #declare Angle = 0; #end
#ifndef (Direct) #declare Direct = 0; #end
#ifndef (Power) #declare Power = 1; #end
#ifndef (ExtraLights) #declare ExtraLights = 0; #end
#ifndef (Skin) #declare Skin = 0; #end
#ifndef (Thickness) #declare Thickness = 1; #end
#ifndef (Glow) #declare Glow = 0; #end
#ifndef (Thin) #declare Thin = 0; #end
#ifndef (Mixed) #declare Mixed = 0; #end
#ifndef (ChildEta) #declare ChildEta = Eta; #end
#ifndef (Extent) #declare Extent = 6; #end
#ifndef (Infinite) #declare Infinite = 0; #end
#ifndef (MeshMode) #declare MeshMode = 0; #end
#ifndef (Offset) #declare Offset = 0; #end
#ifndef (Layers) #declare Layers = 0; #end
#ifndef (Group) #declare Group = 0; #end
#ifndef (GlobalLights) #declare GlobalLights = 0; #end
global_settings {
  assumed_gamma 1 mm_per_unit Mm
  subsurface { method Method samples Samples, 0 spacing Spacing }
  #if (Photons) photons { load_file "subsurface-photons.ph" gather 0, GatherMax radius GatherRadius } #end
}
camera { orthographic location <0, 6, -0.001> look_at 0 right x * 4 up z * 3 }
background { rgb 0 }
#declare Surface = texture {
  pigment { #if (Skin = 2) checker rgb 0.25 rgb 1 scale 0.15 #elseif (Skin) rgb 0.25 #else rgb 0.8 #end }
  finish { ambient 0 diffuse 1 #if (!SurfaceOnly) subsurface { translucency Mfp
    #if (Skin) colour rgb 0.8 thickness Thickness #end
    #if (Glow) emission rgb 0.05 #end
  } #end }
}
#if (Group) light_group { #end
#if (Mixed) merge { #else union { #end
  #if (MeshMode)
    mesh { inside_vector <0.13, 1, 0.29>
      #for (Axis, 0, 2)
        #for (Sign, -1, 1, 2)
          #local N = (Axis = 0 ? x : (Axis = 1 ? y : z)) * Sign;
          #local U = (Axis = 0 ? y : (Axis = 1 ? z : x));
          #local V = (Axis = 0 ? z : (Axis = 1 ? x : y)) * Sign;
          #local A = N - U - V; #local B = N + U - V;
          #local C = N + U + V; #local D = N - U + V;
          #if (MeshMode = 2 & Axis = 1 & Sign = 1)
            #local S1 = <0.6, 0.8, 0.1>; #local S2 = <0.3, 0.8, -0.2>;
            #local S3 = <-0.1, 0.8, 0.3>; #local S4 = <0.1, 0.8, -0.1>;
            smooth_triangle { A, S1, B, S2, C, S3 }
            smooth_triangle { A, S1, C, S3, D, S4 }
          #else
            triangle { A, B, C } triangle { A, C, D }
          #end
        #end
      #end
      scale <Extent, 2 - 1.99 * Thin, Extent> translate -y * (2 - 1.99 * Thin)
  #elseif (Infinite)
    intersection { plane { y, 0 } plane { -y, 4 - 3.98 * Thin }
  #else
    box { <-Extent, -4 + 3.98 * Thin, -Extent>, <Extent, 0, Extent>
  #end
    texture { Surface } interior { ior ChildEta } photons { collect Collect }
    #if (Layers) texture { pigment { rgbt <0.8, 0.8, 0.8, 0.5> }
      finish { ambient 0 diffuse 1 subsurface { translucency Mfp } } } #end
  }
  #if (Mixed)
    box { <Extent + 0.01, -4, -Extent>, <Extent + 1, 0, Extent> texture { Surface } interior { ior Eta }
      photons { collect (Mixed = 2) } }
  #end
  #if (Mixed) interior { ior Eta } #end
  translate y * Offset
}
#if (Direct)
  light_source { <sin(radians(Angle)), cos(radians(Angle)), 0> * 1000 rgb Power parallel point_at 0 }
#end
#for (Index, 1, ExtraLights)
  light_source { <Index, 4, -6> rgb 0 }
#end
#if (Group) light_source { <0, 4, 0> rgb 0 } global_lights GlobalLights } #end
