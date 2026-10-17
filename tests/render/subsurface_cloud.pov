// Subsurface method 2 on one translucent object; each Case forces one reason the point cloud cannot serve it.
// 0 closed box; 1 tiny spacing, 1 MB cap; 2 plane, wide spacing; 3 light group on part; 4 thin wall; 5 one cell over cap.
#version 4.0;
#ifndef (Case) #declare Case = 0; #end
global_settings {
  assumed_gamma 1
  mm_per_unit 10
  subsurface { samples 12, 4 method 2 spacing (Case = 1 ? 0.02 : Case = 2 ? 50 : Case = 5 ? 0.0001 : 1) #if ((Case = 1) | (Case = 5)) memory 1 #end }
}

camera { location <0, 1.5, -4> look_at <0, 0.4, 0> right x * image_width / image_height angle 45 }
#declare Sun = light_source { <-2, 5, -3> rgb 1.3 }
light_source { Sun }

#declare Skin = material {
  texture { pigment { rgb <0.8, 0.4, 0.25> } finish { ambient 0 diffuse 0.2 subsurface { translucency (Case = 1 ? <0.6, 0.4, 0.3> : <3, 2, 1.5>) } } }
  interior { ior 1.4 }
}

#switch (Case)
  #case (2)
    plane { y, 0 material { Skin } }
    #break
  #case (3)
    merge {
      box { <-0.6, 0, -0.6>, <0, 1, 0.6> }
      light_group { light_source { Sun translate x } box { <0, 0, -0.6>, <0.6, 1, 0.6> } }
      material { Skin }
    }
    #break
  #case (4)
    box { <-0.6, 0, 0>, <0.6, 1, 0.00001> material { Skin } }
    #break
  #else
    box { <-0.6, 0, -0.6>, <0.6, 1, 0.6> material { Skin } }
#end
