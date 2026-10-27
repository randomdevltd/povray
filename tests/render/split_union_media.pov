#version Version;
// An unclipped union of two boxes sharing its interior medium; Whole=1 sets split_union off for the reference,
// Lit=1 makes the medium a light and Constant=1 drops its density, so only the light names the union.
#ifndef (Whole) #declare Whole = 0; #end
#ifndef (Lit) #declare Lit = 0; #end
#ifndef (Constant) #declare Constant = 0; #end
// Nest=1 places the union inside a union with a third box, Nest=2 inside a light_group with a light.
#ifndef (Nest) #declare Nest = 0; #end

global_settings { assumed_gamma 1 }
camera { location <0, 1.5, -6> look_at <0, 0.6, 0> }
light_source { <-5, 8, -6> rgb 1 }
background { rgb 0.2 }
plane { y, 0 pigment { rgb 0.6 } }

#declare Held = union {
  box { <-2.5, 0.1, -1>, <-0.3, 1.6, 1> }
  box { <0.3, 0.1, -1>, <2.5, 1.6, 1> }
  #if (Whole) split_union off #end
  hollow
  pigment { rgbt 1 }
  interior {
    media {
      #if (Version >= 4.0) resolution 0.1 #end
      absorption 0.6
      emission 0.4
      #if (!Constant) density { bozo scale 0.4 } #end
    }
    #if (Lit) light { brightness 1.5 samples 4 } #end
  }
}

#switch (Nest)
  #case (0) object { Held } #break
  #case (1) union { object { Held } box { <-0.2, 0.1, 1.5>, <0.2, 0.5, 1.9> } pigment { rgb <0.8, 0.3, 0.2> } } #break
  #case (2) light_group { object { Held } light_source { <0, 4, -3> rgb 0.5 } global_lights on } #break
#end
