// Caches under Max_Cache_Memory: rows of translucent method 2 blocks whose clouds hold many cells, a method 4 medium
// whose density grid and a parallel light's optical-depth grid are large, and Spheres small spheres that only fill
// the scene's bounding tree. Blocks sets how many blocks, in rows of eight, Cells their domain_cells, MediaCells the
// medium's.
#version 3.8;
#ifndef (Blocks) #declare Blocks = 8; #end
#ifndef (Cells) #declare Cells = 5000; #end
#ifndef (MediaCells) #declare MediaCells = 200000; #end
#ifndef (Spheres) #declare Spheres = 0; #end
global_settings {
  assumed_gamma 1
  mm_per_unit 10
  subsurface { samples 12, 4 method 2 }
}
camera { location <0, 2.2, -6> look_at <0, 0.4, 0> right x * image_width / image_height angle 50 }
light_source { <-2, 6, -3> rgb 1.2 }
light_source { <0, 10, 0> rgb 0.4 parallel point_at 0 media_attenuation on }
plane { y, 0 pigment { rgb 0.6 } }
#declare Skin = material {
  texture { pigment { rgb <0.8, 0.45, 0.3> } finish { ambient 0 diffuse 0.2 subsurface { translucency <2, 1.4, 1> } } }
  interior { ior 1.4 }
}
#for (I, 0, Blocks - 1)
  superellipsoid { <0.3, 0.3> scale <0.28, 0.5, 0.28> translate <(mod(I, 8) - 3.5) * 0.7, 0.5, div(I, 8) * 1.1>
    material { Skin } subsurface { domain_cells Cells } }
#end
#if (MediaCells > 0)
  box { <-2.5, 0.01, 0.6>, <2.5, 2, 2> pigment { rgbt 1 } hollow
    interior { media { method 4 domain_cells MediaCells scattering { 1, 0.6 } absorption 0.2
      density { granite scale 0.7 color_map { [0 rgb 0] [1 rgb 1.5] } } } } }
#end
#for (I, 1, Spheres)
  sphere { <mod(I, 200) * 0.03 - 3, 0.02, 3 + div(I, 200) * 0.03>, 0.01 pigment { rgb 1 } }
#end
