// lit_by_tags cost: a grid of spheres on a floor under eight tagged lights, with area shadows.
// Query 0 has no lit_by_tags (the base build renders it too), 1 a query on every object that selects every light,
// 2 a query that selects half the lights, so it renders a different image.
#version 4.0;
#ifndef (Query) #declare Query = 0; #end
#ifndef (Count) #declare Count = 8; #end
global_settings { assumed_gamma 1 }
background { rgb 0.02 }
camera { location <0, 9, -14> look_at <0, 0, 0> right x * image_width / image_height angle 50 }
#for (I, 0, 7)
  #if (mod(I, 2) = 0) #local Parity = "even"; #else #local Parity = "odd"; #end
  light_source {
    <10 * cos(I * pi / 4), 6 + mod(I, 3), 10 * sin(I * pi / 4)> rgb 0.25
    area_light x, z, 3, 3 adaptive 1
    tags { concat("lamp-", str(I, 0, 0)), Parity }
  }
#end
union {
  plane { y, 0 pigment { checker rgb 0.8 rgb 0.5 } finish { diffuse 0.8 specular 0.2 } }
  #for (I, 0, Count - 1)
    #for (J, 0, Count - 1)
      sphere { <(I - (Count - 1) / 2) * 1.4, 0.5, (J - (Count - 1) / 2) * 1.4>, 0.5
        pigment { rgb <0.3 + 0.7 * I / Count, 0.5, 0.3 + 0.7 * J / Count> } finish { diffuse 0.7 phong 0.4 } }
    #end
  #end
  #switch (Query)
    #case (1) lit_by_tags { any | none } #break
    #case (2) lit_by_tags { "even" } #break
  #end
}
