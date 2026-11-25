// Region scheduling under Max_Cache_Memory: a wall of translucent method 2 objects across the whole frame, each with a
// large cloud, so the clouds together are many times a small budget and every row of the image meets a row of them.
// Columns and Rows set the wall, Cells each object's domain_cells.
#version 3.8;
#ifndef (Columns) #declare Columns = 8; #end
#ifndef (Rows) #declare Rows = 5; #end
#ifndef (Cells) #declare Cells = 4000; #end
global_settings {
  assumed_gamma 1
  mm_per_unit 10
  subsurface { samples 12, 4 method 2 }
}
camera { location <0, 0, -10> look_at 0 right x * image_width / image_height angle 46 }
light_source { <-4, 6, -8> rgb 1.3 }
light_source { <5, -2, -6> rgb 0.4 }
plane { z, 1.2 pigment { rgb 0.55 } }
#declare Skin = material {
  texture { pigment { rgb <0.85, 0.5, 0.35> } finish { ambient 0 diffuse 0.25 specular 0.2 subsurface { translucency <2, 1.3, 0.9> } } }
  interior { ior 1.4 }
}
#for (I, 0, Columns - 1)
  #for (J, 0, Rows - 1)
    superellipsoid { <0.35, 0.35> scale <0.42, 0.4, 0.4>
      translate <(I - (Columns - 1) / 2) * 1.0, (J - (Rows - 1) / 2) * 0.95, 0>
      material { Skin } subsurface { domain_cells Cells } }
  #end
#end
