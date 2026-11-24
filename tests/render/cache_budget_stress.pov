// Caches under a tiny Max_Cache_Memory at their worst: a translucent box with dense points whose large cells every
// shading point near it pins, rows of small boxes whose small cells spill and come back, a translucent plane seen through
// an anamorphic camera whose pixel spacing is far wider than the footprint texture filtering gives the rays, so its cells
// are lifted as far as they go and a gather's ring still spans several, and many small media boxes whose grids stay on the
// heap. Render with +TF. HotCells sets the box's domain_cells, Small and Lattices how many small boxes and media boxes.
#version 3.8;
#ifndef (HotCells) #declare HotCells = 20000; #end
#ifndef (Small) #declare Small = 12; #end
#ifndef (Lattices) #declare Lattices = 40; #end
global_settings { assumed_gamma 1 mm_per_unit 1 subsurface { samples 12, 4 method 2 } }
// 152 degrees across, a few hundredths of a degree high
camera { perspective location <0, 0, 0> direction z right x * 8 up y * 0.0002 }
light_source { <-20, 30, -20> rgb 1.2 }
light_source { <0, 100, 0> rgb 0.4 parallel point_at 0 media_attenuation on }
#macro Skin(Translucency)
  material { texture { pigment { rgb <0.8, 0.5, 0.35> } finish { ambient 0 diffuse 0.2 subsurface { translucency Translucency } } } interior { ior 1.4 } }
#end
box { <-1, -1, 2>, <1, 1, 4> Skin(2) subsurface { domain_cells HotCells } }
#for (I, 0, Small - 1)
  #local X = (I < Small / 2 ? -6 + I * 0.7 : 2 + (I - Small / 2) * 0.7);
  box { <X, -0.15, 5>, <X + 0.3, 0.15, 5.3> Skin(0.2) }
#end
#for (I, 0, Lattices - 1)
  #local X = (I < Lattices / 2 ? -14 + I * 0.25 : 9 + (I - Lattices / 2) * 0.25);
  box { <X, -0.1, 10>, <X + 0.2, 0.1, 10.2> pigment { rgbt 1 } hollow
    interior { media { method 4 domain_cells 3000 scattering { 1, 0.6 } absorption 0.2 density { granite scale 0.1 } } } }
#end
// unbounded, so its cells follow the pixel spacing
plane { -z, -60 Skin(0.002) }
