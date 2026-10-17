#version 4.0;
// Tag inheritance and tagged selections: Case picks a scene, Reference=1 writes what it must render as.
#ifndef (Case) #declare Case = 1; #end
#ifndef (Filter) #declare Filter = 0; #end
#ifndef (Reference) #declare Reference = 0; #end
global_settings { assumed_gamma 1 }
background { rgb 0 }
#declare Flat = finish { emission 1 diffuse 0 }
#declare Lit = finish { emission 0 ambient 0 diffuse 1 }
#macro At(I) <-5 + 2 * I, 0, 0>, 0.6 pigment { rgb <0.2 + 0.15 * I, 1 - 0.15 * I, 0.5> } finish { Flat } #end
#macro Shown(Mask, I) (mod(int(Mask / pow(2, I)), 2) = 1) #end
#declare View = camera { orthographic location <0, 0, -5> look_at 0 right 12 * x up 6 * y }
#switch (Case)
#case (1) // each way of writing tags, inside a union tagged "a", beside an untagged top-level object
  #declare Masks = array[7] { 63, 3, 1, 6, 56, 2, 7 };
  camera { View
    #if (!Reference)
      #switch (Filter)
        #case (1) filter_tags { "a" } #break
        #case (2) filter_tags { "a" & !"c" } #break
        #case (3) filter_tags { "c" } #break
        #case (4) filter_tags { none } #break
        #case (5) filter_tags { "a" & "c" } #break
        #case (6) filter_tags { any } #break
      #end
    #end
  }
  #if (Reference)
    #for (I, 0, 5) #if (Shown(Masks[Filter], I)) sphere { At(I) } #end #end
  #else
    union {
      sphere { At(0) }
      sphere { At(1) tags { "c", inherit } }
      sphere { At(2) tags { "c" } }
      sphere { At(3) tags { none } }
      sphere { At(4) tags { } }
      tags { "a" }
    }
    sphere { At(5) }
  #end
#break
#case (9) // a later tags block on the same object replaces earlier ones, list and inherit alike
  #declare Masks = array[8] { 31, 0, 1, 2, 20, 0, 8, 16 };
  camera { View
    #if (!Reference)
      #switch (Filter)
        #case (1) filter_tags { "d" } #break
        #case (2) filter_tags { "n" } #break
        #case (3) filter_tags { "b" } #break
        #case (4) filter_tags { "p" } #break
        #case (5) filter_tags { "a" | "x" } #break
        #case (6) filter_tags { none } #break
        #case (7) filter_tags { "e" } #break
      #end
    #end
  }
  #if (Reference)
    #for (I, 0, 4) #if (Shown(Masks[Filter], I)) sphere { At(I) } #end #end
  #else
    #declare D0 = sphere { At(0) tags { "d" } }
    #declare D3 = sphere { At(3) tags { "d", inherit } }
    #declare D4 = sphere { At(4) tags { "d" } }
    union {
      object { D0 tags { "n" } }
      sphere { At(1) tags { "a", inherit } tags { "b" } }
      sphere { At(2) tags { "x" } tags { inherit } }
      object { D3 tags { } }
      object { D4 tags { "e", inherit } }
      tags { "p" }
    }
  #end
#break
#case (2) // tagged sees neither the tree it is written in nor that tree's earlier children
  camera { View }
  sphere { At(0) tags { "s" } }
  union {
    sphere { At(1) tags { "s" } }
    #if (!Reference) object { tagged { "s" } translate y * 2 } #end
  }
  #if (Reference) sphere { At(0) translate y * 2 } #end
#break
#case (3) // a copy placed under a tagged parent inherits from it only if it inherits at all
  camera { View #if (!Reference) filter_tags { "b" & "p" } #end }
  #if (Reference)
    sphere { At(0) translate y * 2 }
  #else
    sphere { At(0) tags { "b", inherit } }
    sphere { At(1) tags { "b" } }
    #declare Picked = tagged { "b" };
    union { object { Picked translate y * 2 } tags { "p" } }
  #end
#break
#case (4) // a light group selected whole is copied with its lights, which light the copy
  camera { View }
  #declare Group = light_group {
    light_source { <-4, 3, -3> rgb <1, 0.5, 0.2> }
    sphere { <-4, 0, 0>, 1 pigment { rgb 1 } finish { Lit } }
    tags { "g" }
  }
  object { Group }
  #if (Reference) object { Group translate x * 6 } #else object { tagged { "g" } translate x * 6 } #end
#break
#case (5) // a selected part of a light group is copied as the group holding that part, lit only as there
  camera { View }
  light_source { <0, 0, -20> rgb 0.5 }
  light_group {
    light_source { <-4, 3, -3> rgb <1, 0.5, 0.2> tags { "part" } }
    sphere { <-4, 0, 0>, 1 pigment { rgb 1 } finish { Lit } tags { "part" } }
    box { <-5.5, -2.5, 1>, <-2.5, 2.5, 1.2> pigment { rgb 1 } finish { Lit } }
  }
  #if (Reference)
    light_group {
      light_source { <-4, 3, -3> rgb <1, 0.5, 0.2> }
      sphere { <-4, 0, 0>, 1 pigment { rgb 1 } finish { Lit } }
      translate x * 6
    }
  #else
    object { tagged { "part" } translate x * 6 }
  #end
#break
#case (6) // a declared selection expands once: placing it again places the same copies
  camera { View }
  sphere { At(0) tags { "s" } }
  #if (Reference)
    sphere { At(0) translate x * 2 }
    sphere { At(0) translate x * 4 }
  #else
    #declare Picked = tagged { "s" };
    object { Picked translate x * 2 }
    object { Picked translate x * 4 }
  #end
#break
#case (7) // trace and inside work on a declared selection, building its deferred mesh where they need it
  camera { View }
  isosurface_mesh { function { x * x + y * y + z * z - 1 } contained_by { box { -1.2, 1.2 } }
                    translate -4 * x pigment { rgb 1 } finish { Flat } tags { "s" } }
  #if (Reference)
    sphere { <2, 0, 0>, 1 pigment { rgb <0, 1, 0> } finish { Flat } }
  #else
    #declare Picked = tagged { "s" };
    #declare Normal = <0, 0, 0>;
    #declare Hit = trace(Picked, <-4, 0, -5>, z, Normal);
    #if (inside(Picked, <-4, 0, 0>) & (vlength(Normal) > 0) & (abs(Hit.z + 1) < 0.05))
      sphere { <2, 0, 0>, 1 pigment { rgb <0, 1, 0> } finish { Flat } }
    #end
  #end
#break
#case (8) // a far portal mouth answers to the tags its near mouth inherits from a union; Side 1 looks at the far mouth
  #ifndef (Side) #declare Side = 0; #end
  #declare Ahead = (Side = 0 ? 1 : -1);
  camera { orthographic location <20 * Side, 0, -5 * Ahead> look_at <20 * Side, 0, 0> right 6 * x up 3 * y
    #switch (Filter) #case (1) filter_tags { "u" } #break #case (2) filter_tags { "x" } #break #end
  }
  sphere { <0, 0, 3 * Ahead>, 2 pigment { rgb <1, 0.2, 0.2> } finish { Flat } tags { "u", "x" } }
  sphere { <20, 0, 3 * Ahead>, 2 pigment { rgb <0.2, 1, 0.2> } finish { Flat } tags { "u", "x" } }
  #if (Reference)
    portal { disc { 0, -z, 1 } to { translate 20 * x } tags { "u" } }
  #else
    union { portal { disc { 0, -z, 1 } to { translate 20 * x } } sphere { <0, 5, 0>, 0.1 } tags { "u" } }
  #end
#break
#end
