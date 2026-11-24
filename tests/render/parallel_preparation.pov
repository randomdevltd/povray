// parallel_preparation.pov: preparation that must not depend on the thread count. Case 0 surface bounce on Count
// patterned subsurface spheres under an area light, Cells points each; 1 Count interior volume lights of patterned
// emission, LightCells cells each; 2 both, with two surface light panels.
#version 3.8;
#ifndef (Case) #declare Case = 0; #end
#ifndef (Count) #declare Count = 4; #end
#ifndef (Cells) #declare Cells = 256; #end
#ifndef (LightCells) #declare LightCells = 4096; #end
#ifndef (Samples) #declare Samples = 4; #end
#ifndef (Area) #declare Area = 3; #end
global_settings { assumed_gamma 1 subsurface { method 2 samples 16, 4 } }
camera { location <0, 2.2, -6> look_at <0, 0.8, 0> angle 60 }
background { rgb 0 }
plane { y, 0 pigment { checker rgb 0.7, rgb 0.4 scale 0.5 } finish { ambient 0 diffuse 0.8 } }
#macro Place(I) translate <mod(I, 4) * 1.1 - 1.65, 0.45 + 0.95 * int(I / 4), mod(I * 7, 5) * 0.2> #end

#if (Case = 0 | Case = 2)
  light_source { <2, 5, -3>, rgb 3 area_light x * 0.6, z * 0.6, Area, Area }
  light_source { <-3, 2, -1>, rgb <0.6, 0.7, 1> }
  #for (I, 0, Count - 1)
    sphere { 0, 0.42
      pigment { granite colour_map { [0 rgb <0.9, 0.3, 0.2>] [1 rgb <0.2, 0.8, 0.9>] } scale 0.3 translate I }
      finish { ambient 0 emission 0 diffuse 0.8 subsurface { translucency 0.4 } }
      interior { ior 1.3 }
      light { bounce samples Samples domain_cells Cells }
      Place(I) #if (Case = 2) translate y * 1.9 #end
    }
  #end
#end

#if (Case = 2)
  #for (I, 0, 1)
    box { <-0.5, 3, -0.5>, <0.5, 3.02, 0.5>
      pigment { rgb 1 } finish { ambient 0 emission 1 diffuse 0 }
      light { pigment { rgb <1, 0.8 - 0.4 * I, 0.4 + 0.4 * I> } brightness 2 nits samples Samples }
      translate x * (I * 2 - 1)
    }
  #end
#end

#if (Case = 1 | Case = 2)
  #for (I, 0, Count - 1)
    sphere { 0, 0.4 pigment { rgbt 1 } hollow no_image
      interior {
        light {
          pigment { granite colour_map { [0 rgb 0] [1 rgb <2, 1, 0.5>] } scale 0.25 translate I }
          brightness 3 samples Samples domain_cells LightCells
        }
      }
      Place(I) #if (Case = 2) translate y * 1.9 #end
    }
  #end
#end
