// bounce_shading.pov: a bounce wall lit by a point light, casting its light on a floor and a ball. Case 1 a checker
// wall; 2 a Fresnel wall with energy-conserving reflection; 3 two flush translucent slabs as one bounce union, lit from
// behind (Opaque 1 makes them plain diffuse, which must stay dark in front); 4 the same two slabs with no bounce.
// Crand gives the checker wall a crand finish.
#version 3.8;
#ifndef (Case) #declare Case = 1; #end
#ifndef (Cells) #declare Cells = 4096; #end
#ifndef (Samples) #declare Samples = 16; #end
#ifndef (Opaque) #declare Opaque = 0; #end
#ifndef (Crand) #declare Crand = 0; #end
global_settings { assumed_gamma 1 mm_per_unit 10 subsurface { method 2 samples 16, 4 } }
background { rgb 0 }

#if (Case = 1 | Case = 2)
  camera { location <0, 1.3, -3> look_at <0, 0.9, 1> angle 60 }
  light_source { <0.8, 1.6, 1.2>, rgb 2 fade_distance 1 fade_power 2 }
  box { <-2.5, -0.1, -3>, <2.5, 0, 2> pigment { rgb 0.7 } finish { ambient 0 diffuse 0.8 } }
  sphere { <-0.6, 0.35, 1.2>, 0.35 pigment { rgb 1 } finish { ambient 0 diffuse 0.8 } }
  box { <-2.5, 0, 2>, <2.5, 2.5, 2.1>
    #if (Case = 1)
      pigment { checker rgb <0.9, 0.15, 0.1>, rgb 0.9 scale 0.4 }
      finish { ambient 0 diffuse 0.8 crand Crand }
    #else
      pigment { rgb <0.9, 0.8, 0.6> }
      finish { ambient 0 diffuse 0.8 fresnel 1 specular 0.4 reflection { 0.05, 1 fresnel } conserve_energy }
      interior { ior 1.5 }
    #end
    light { bounce domain_cells Cells samples Samples }
  }
#else
  camera { orthographic location <0, 0, -5> look_at 0 right x * 4 up y * 2.5 }
  light_source { <1, 1, 30>, rgb 200 fade_distance 1 fade_power 2 }
  union {
    box { <-3, -2, 0>, <3, 2, 1> }
    box { <-3, -2, 1>, <3, 2, 2> }
    #if (Opaque)
      pigment { rgb 0.95 } finish { ambient 0 diffuse 0.9 }
    #else
      pigment { rgb 0.95 } finish { ambient 0 diffuse 0.9 subsurface { translucency 4 } } interior { ior 1.3 }
    #end
    #if (Case = 3) light { bounce domain_cells Cells samples Samples } #end
  }
#end
