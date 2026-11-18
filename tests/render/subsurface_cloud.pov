// Subsurface method 2 on one translucent object; each Case was once a reason the point cloud could not serve it.
// 0 closed box; 1 many points, 1 MB cap; 2 plane; 3 light group on part; 4 thin wall; 5 one cell over cap;
// 6 a thin lens of two large spheres with a single point to its budget: lit by its exit points.
// Cells sets the object's domain_cells (0: the default).
// Site 1 looks from another place, which a bounded object's cloud must not depend on.
#version 4.0;
#ifndef (Case) #declare Case = 0; #end
#ifndef (Site) #declare Site = 0; #end
#ifndef (Cells) #declare Cells = (Case = 1 ? 1000000 : Case = 5 ? 1000000000 : Case = 6 ? 1 : 0); #end
#macro Budget() #if (Cells > 0) subsurface { domain_cells Cells } #end #end
global_settings {
  assumed_gamma 1
  mm_per_unit 10
  subsurface { samples 12, 4 method 2 #if ((Case = 1) | (Case = 5)) memory 1 #end }
}

#if (Site = 1)
  camera { location <3, 2.5, 2.5> look_at <0, 0.4, 0> right x * image_width / image_height angle 30 }
#else
  camera { location <0, 1.5, -4> look_at <0, 0.4, 0> right x * image_width / image_height angle 45 }
#end
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
      Budget()
    }
    #break
  #case (4)
    box { <-0.6, 0, 0>, <0.6, 1, 0.00001> material { Skin } Budget() }
    #break
  #case (6)
    intersection { sphere { <0, 16.65, 0>, 16.68 } sphere { <0, -16.65, 0>, 16.68 } rotate -60 * x translate 0.4 * y material { Skin } Budget() }
    #break
  #else
    box { <-0.6, 0, -0.6>, <0.6, 1, 0.6> material { Skin } Budget() }
#end
