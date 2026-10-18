// Thin wedges in rows at four relative indices, the grey ones needing more layer tables than method 2 keeps, so that a
// thread drops and rebuilds the boundaries; the others share a reduced albedo of 1 in blue. Only k > 0 shows wedge k alone.
#version 4.0;
#ifndef (Only) #declare Only = 0; #end
global_settings {
  assumed_gamma 1.0
  mm_per_unit 10
  subsurface { samples 12, 4 method 2 }
}
camera { orthographic location <0, 3, 0> direction -y right x * 2.2 up z * 2.2 }
light_source { <-2, 6, -3> rgb 1.2 }
background { rgb 0 }
#declare Iors = array[4] { 1.3, 1.6, 1.45, 1.9 }
#declare Low = array[4] { <0.1, 0.05, 3>, <0.05, 0.05, 0.05>, <0.2, 0.08, 3>, <0.08, 0.08, 0.08> }
#declare High = array[4] { <0.95, 0.9, 3>, <0.95, 0.95, 0.95>, <0.9, 0.95, 3>, <0.9, 0.9, 0.9> }
#declare Centre = array[4] { 0.9, 0.2, -0.5, -0.85 }
#declare Half = array[4] { 0.12, 0.5, 0.12, 0.17 }
#for (K, 0, 3)
  intersection {
    box { <-1, -1, -Half[K]>, <1, 0, Half[K]> }
    plane { -y, 0.01 rotate z * -14 translate x * -1 }
    pigment { gradient <1, 0, 2> color_map { [0 rgb Low[K]] [1 rgb High[K]] } scale 2 + 4 * Half[K] translate <-1, 0, -Half[K]> }
    finish { ambient 0 diffuse 0.8 subsurface { translucency <2, 1.6, 1.2> } }
    interior { ior Iors[K] }
    translate z * Centre[K]
    #if ((Only != 0) & (Only != K + 1)) no_image #end
  }
#end
