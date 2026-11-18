// bounce_passes.pov: two facing walls, each a surface bounce light with Passes passes and a budget of Cells points, lit
// by a lamp between them, over a floor; the whole scene is scaled by Scale. Mirror 1 makes the left wall a mirror,
// Highlight 1 gives it highlights only, Radiosity 1 turns radiosity on, Lumens L states both walls' brightness in lumens,
// Pigment 1 or Emission 1 gives the left wall's bounce block an input of its own (a parse error).
#version 3.7;
#ifndef (Passes) #declare Passes = 1; #end
#ifndef (Scale) #declare Scale = 1; #end
#ifndef (Mirror) #declare Mirror = 0; #end
#ifndef (Highlight) #declare Highlight = 0; #end
#ifndef (Radiosity) #declare Radiosity = 0; #end
#ifndef (Lumens) #declare Lumens = 0; #end
#ifndef (Pigment) #declare Pigment = 0; #end
#ifndef (Emission) #declare Emission = 0; #end
#ifndef (Mixed) #declare Mixed = 0; #end
#ifndef (MixedSSLT) #declare MixedSSLT = 0; #end
#ifndef (VolumePasses) #declare VolumePasses = Passes; #end
#ifndef (VolumeGain) #declare VolumeGain = 1; #end
#ifndef (Cells) #declare Cells = 256; #end
global_settings { assumed_gamma 1
  #if (MixedSSLT) subsurface { method 2 samples 4, 2 } #end
  #if (Radiosity) radiosity { count 20 nearest_count 2 error_bound 1 recursion_limit 1 } #end
}
camera { location <0, 1.2, -4> * Scale look_at <0, 0.6, 0> * Scale angle 60 }
background { rgb 0 }
// no fade: a point light lights a scaled scene alike
light_source { <-0.2, 1.4, -0.6> * Scale, rgb 2 }
#macro Wall(Side, Left)
  box { <-0.05, 0, -1>, <0.05, 1.5, 1>
    pigment { rgb <0.9, 0.7, 0.5> }
    finish { ambient 0 emission 0
      #if (Left & Mirror) diffuse 0 reflection 1
      #elseif (Left & Highlight) diffuse 0 phong 1 specular 1
      #else diffuse 0.8 #end
      #if (MixedSSLT) subsurface { translucency 0.15 } #end
    }
    #if (MixedSSLT) interior { ior 1.3 } #end
    light {
      #if (Left & Pigment) pigment { rgb 1 } #end
      #if (Left & Emission) emission #end
      samples 4 domain_cells Cells
      #if (Lumens > 0) brightness Lumens lumens #end
      bounce { passes Passes }
    }
    translate Side * x
    scale Scale
  }
#end
Wall(-1, 1)
Wall(1, 0)
box { <-3, -0.1, -3>, <3, 0, 3> pigment { rgb 0.8 } finish { ambient 0 diffuse 0.8 } scale Scale }

#if (Mixed)
  sphere { <0, 0.8, 0>, 0.55 pigment { rgbt 1 } hollow
    interior {
      media { method 4 domain_cells 125 scattering { 1, rgb 0.8 } }
      light { brightness VolumeGain samples 8 self 0 bounce { passes VolumePasses } }
    }
    scale Scale
  }
#end
