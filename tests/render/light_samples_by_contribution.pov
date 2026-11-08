// finish { light_samples M by_contribution }: a clear filtering slab and a mirror over a white floor, lit by a surface
// light and a classic area light. Mode 0 states nothing, 1 light_samples 1, 2 light_samples 1 by_contribution, 4
// light_samples 1 full in the default finish, 3 an unknown word after light_samples. Version is the scene's #version
// (by_contribution is the default from 4.0). OptOut 1 gives every finish its own light_samples 1 full. Slab 0 and
// Mirror 0 leave the floor alone; Classic 0 drops the area light, Panel 0 the surface light; Layers 2 lays a clear top
// layer over the floor; Floor 0 drops the floor; Depth is max_trace_level; Jitter and Adaptive set the area light's.
#version version;
#ifndef (Version) #declare Version = 3.8; #end
#version Version;
#ifndef (Mode) #declare Mode = 0; #end
#ifndef (OptOut) #declare OptOut = 0; #end
#ifndef (Floor) #declare Floor = 1; #end
#ifndef (Slab) #declare Slab = 1; #end
#ifndef (Mirror) #declare Mirror = 1; #end
#ifndef (Classic) #declare Classic = 1; #end
#ifndef (Panel) #declare Panel = 1; #end
#ifndef (Layers) #declare Layers = 1; #end
#ifndef (Jitter) #declare Jitter = 1; #end
#ifndef (Adaptive) #declare Adaptive = 1; #end
#ifndef (Samples) #declare Samples = 16; #end
#ifndef (Depth) #declare Depth = 5; #end
global_settings { assumed_gamma 1 max_trace_level Depth }
#switch (Mode)
  #case (1) #default { finish { light_samples 1 } } #break
  #case (2) #default { finish { light_samples 1 by_contribution } } #break
  #case (3) #default { finish { light_samples 1 sometimes } } #break
  #case (4) #default { finish { light_samples 1 full } } #break
#end
#macro Own() #if (OptOut) light_samples 1 full #end #end
camera { location <0, 2.5, -4> look_at <0, 0.3, 0> angle 40 }
#if (Floor)
  box { <-3, -0.1, -3>, <3, 0, 3>
    texture { pigment { rgb 0.8 } finish { ambient 0 diffuse 0.9 Own() } }
    #if (Layers = 2) texture { pigment { rgbt <1, 1, 1, 0.9> } finish { ambient 0 diffuse 0.5 Own() } } #end
  }
#end
#if (Slab)
  box { <-0.8, 0.3, -0.8>, <0.8, 0.4, 0.8> rotate y * 20
    pigment { rgbf <0.6, 0.9, 1, 0.9> } finish { ambient 0 diffuse 0.3 reflection { 0.05 } Own() } interior { ior 1.3 }
  }
#end
#if (Mirror)
  box { <-2, 0, 1.5>, <2, 1.5, 1.6> pigment { rgb 0.5 } finish { ambient 0 diffuse 0.5 reflection { 0.3 } Own() } }
#end
#if (Panel)
  box { <-0.6, 2.98, -0.6>, <0.6, 3, 0.6>
    pigment { rgb 1 } finish { ambient 0 diffuse 0 emission 1 }
    light { brightness 4 nits samples Samples }
  }
#end
#if (Classic)
  light_source { <2, 3, -2>, rgb 0.3 area_light x, z, 9, 9 adaptive Adaptive #if (Jitter) jitter #end }
#end
