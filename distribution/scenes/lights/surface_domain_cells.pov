// Thin translucent capsule with an emissive vein just below its skin.
#version 3.7;
#ifndef (Cells) #declare Cells = 64; #end
#ifndef (MediaCells) #declare MediaCells = 500000; #end
#ifndef (VolumeCells) #declare VolumeCells = 256; #end
#ifndef (UseDefault) #declare UseDefault = 0; #end
#ifndef (Export) #declare Export = 0; #end
#ifndef (Passes) #declare Passes = 1; #end
#ifndef (S) #declare S = 1; #end
#default { media { domain_cells MediaCells } }
// Cells=0 leaves the surface budget at the renderer's default.
#if (Cells > 0)
#default { interior { light { domain_cells VolumeCells } } light { domain_cells Cells } }
#end
global_settings { assumed_gamma 1 mm_per_unit 1/S subsurface { method 2 samples 4, 4 spacing 0.5 } }
camera { orthographic location <0,0,-3>*S look_at 0 right x*1.4*S up y*0.8*S }
background { rgb 0.01 }
sphere_sweep {
  cubic_spline 4,
  <-1.5,0,0>,0.12, <-0.5,0,0>,0.12, <0.5,0,0>,0.12, <1.5,0,0>,0.12
  pigment { rgb <0.8,0.5,0.3> }
  finish { ambient 0 diffuse 1 subsurface { translucency 0.3 } }
  interior { ior 1.4 }
  light { bounce { passes Passes } samples 4
    #if (Cells > 0 & !UseDefault) domain_cells Cells #end }
  scale S
}
cylinder {
  <-0.45,0,-0.105>, <0.45,0,-0.105>, 0.008
  hollow no_image pigment { rgbt 1 } finish { ambient 0 diffuse 0 }
  interior { light { brightness 2 samples 4 domain_cells VolumeCells attenuation { cutoff 0 } } }
  scale S
}
box { <-0.7,-0.3,-0.15>, <0.7,-0.28,0.3> pigment { rgb 1 } finish { ambient 0 diffuse 1 } scale S }
