// Stacked see-through bricks under Max_Cache_Memory: method 2 subsurface blocks whose refracting interiors share faces,
// lit by a surface light, with a facets normal at two scales whose cells alternate from one lookup to the next. Their
// clouds hold many cells. The image must not depend on the thread count, the block order or what the budget spilled.
// Cells sets each brick's domain_cells, Rows the stack's height.
#version 3.8;
#ifndef (Cells) #declare Cells = 2000; #end
#ifndef (Rows) #declare Rows = 4; #end
global_settings {
  assumed_gamma 1
  mm_per_unit 10
  subsurface { samples 12, 4 method 2 }
}
camera { location <1.6, 2.4, -4.6> look_at <0, 0.55, 0.3> right x * image_width / image_height angle 42 }
light_source { <-2, 6, -3> rgb 1.5 }
box { <-1.4, 2.6, -0.6>, <-0.4, 2.7, 0.4>
  pigment { rgb 1 } finish { ambient 0 emission 1 diffuse 0 }
  light { pigment { rgb <1, 0.8, 0.6> } brightness 40 nits samples 8 }
}
plane { y, 0 pigment { rgb 0.6 } }
#declare Brick = material {
  texture {
    pigment { rgbf <0.9, 0.5, 0.75, 0.6> }
    normal { average normal_map { [3 facets coords 1.6 size 1 metric 2 scale 0.021] [1 facets coords 1.6 size 1 metric 2 scale 0.0105] } }
    finish { ambient 0 diffuse 0.3 specular 0.3 subsurface { translucency <1.5, 0.8, 1.2> } }
  }
  interior { ior 1.3 }
}
#for (I, 0, 5)
  #for (J, 0, Rows - 1)
    #for (K, 0, 1)
      box { <I * 0.4 - 1.2, J * 0.3, K * 0.4>, <I * 0.4 - 0.8, J * 0.3 + 0.3, K * 0.4 + 0.4> material { Brick } subsurface { domain_cells Cells } }
    #end
  #end
#end
