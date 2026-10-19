// Photon receivers that end, split or crowd under one dappled patch of photon-only light; render 128 by 96, -a. Case picks one;
// Twin=1 swaps in an unbroken surface with the same top (the reference); Mask=1 renders the receiver white (the pixels to compare).
#version 3.8;
#ifndef (Method) #declare Method = 2; #end
#ifndef (Q) #declare Q = 1; #end
#ifndef (Count) #declare Count = 2000000; #end
#ifndef (Case) #declare Case = 1; #end
#ifndef (Twin) #declare Twin = 0; #end
#ifndef (Mask) #declare Mask = 0; #end
#ifndef (Lens) #declare Lens = 0; #end   // 0 bumpy glass slab (dappled patch), 1 glass ball (focused caustic)
#ifndef (M) #declare M = 64; #end
#ifndef (Kind) #declare Kind = 0; #end
#ifndef (Gap) #declare Gap = 0.004; #end
#ifndef (Lamp) #declare Lamp = (Lens = 1 ? 1 : 25); #end
global_settings { assumed_gamma 1 max_trace_level 10
  photons { #if (Method = 2) method 2 quality Q #else count Count #end } }
#if (Case = 11)
camera { orthographic location <0.3, 10, 0> look_at <0.3, 0, 0> right 1 * x up 0.75 * z }
light_source { <-40, 8, 0> rgb Lamp * 26 fade_power 2 fade_distance 0.70710678 photon_only on photons { refraction on reflection off } }
#else
camera { orthographic location <0, 10, 0> look_at <0, 0, 0> right 4 * x up 3 * z }
light_source { <0, 8, 0> rgb Lamp fade_power 2 fade_distance 0.70710678 photon_only on photons { refraction on reflection off } }
#end
#if (!Mask)
#if (Case = 11)
box { <-7, 1, -1.5>, <-4.4, 1.4, 1.5> pigment { rgbf 1 } interior { ior 1.5 } normal { bumps 0.4 scale 0.3 } photons { target } no_shadow no_image }
#elseif (Lens = 1)
sphere { <0.2, 1.5, 0.1>, 1 pigment { rgbf 1 } interior { ior 1.5 } photons { target } no_shadow no_image }
#else
box { <-1.6, 1, -1.2>, <1.6, 1.4, 1.2> pigment { rgbf 1 } interior { ior 1.5 } normal { bumps 0.4 scale 0.3 } photons { target } no_shadow no_image }
#end
#end
#declare A = <1, 0.35, 0.3>; #declare B = <0.3, 0.45, 1>;
#if (Mask)
#declare T = texture { pigment { rgb 1 } finish { emission 1 diffuse 0 } }
#declare TA = T; #declare TB = T;
#else
#declare T = texture { pigment { rgb 1 } finish { ambient 0 diffuse 1 } }
#declare TA = texture { pigment { rgb A } finish { ambient 0 diffuse 1 } }
#declare TB = texture { pigment { rgb B } finish { ambient 0 diffuse 1 } }
#end
#declare Floor = box { <-3, -0.1, -2.5>, <3, 0, 2.5> }
#if (Twin & Case != 5 & Case != 7 & Case != 10 & Case != 11)
  object { Floor texture { T } }
#else
#switch (Case)
#case (1)  // a floor ending inside the lit patch: two straight edges and a corner
  box { <-0.5, -0.1, -1.5>, <2, 0, 0.6> texture { T } }
#break
#case (2)  // strips narrower and wider than the support
  #declare W = array[6] { 0.02, 0.04, 0.08, 0.16, 0.32, 0.64 }
  #declare X0 = -1.9;
  #for (I, 0, 5) box { <X0, -0.1, -1.5>, <X0 + W[I], 0, 1.5> texture { T } } #declare X0 = X0 + W[I] + 0.12 + 0.1 * I; #end
#break
#case (3)  // tiled floor: Kind 0 boxes in a union, 1 triangle pairs in a union, 2 separate boxes
  #declare DX = 4 / M; #declare DZ = 3 / M;
  #switch (Kind)
  #case (0) union { #for (I, 0, M - 1) #for (J, 0, M - 1) box { <-2 + I * DX, -0.1, -1.5 + J * DZ>, <-2 + (I + 1) * DX, 0, -1.5 + (J + 1) * DZ> } #end #end texture { T } } #break
  #case (1) union { #for (I, 0, M - 1) #for (J, 0, M - 1)
      triangle { <-2 + I * DX, 0, -1.5 + J * DZ>, <-2 + (I + 1) * DX, 0, -1.5 + J * DZ>, <-2 + (I + 1) * DX, 0, -1.5 + (J + 1) * DZ> }
      triangle { <-2 + I * DX, 0, -1.5 + J * DZ>, <-2 + (I + 1) * DX, 0, -1.5 + (J + 1) * DZ>, <-2 + I * DX, 0, -1.5 + (J + 1) * DZ> }
      #end #end texture { T } } #break
  #case (2) #for (I, 0, M - 1) #for (J, 0, M - 1) box { <-2 + I * DX, -0.1, -1.5 + J * DZ>, <-2 + (I + 1) * DX, 0, -1.5 + (J + 1) * DZ> texture { T } } #end #end #break
  #end
#break
#case (4)  // 400 flat-topped pebbles (discs), separate objects
  #declare R = seed(3);
  #for (I, 1, 400) #declare P = <4 * rand(R) - 2, 0, 3 * rand(R) - 1.5>; #declare S = 0.03 + 0.07 * rand(R);
    cylinder { P - y * 0.05, P, S texture { T } } #end
#break
#case (5)  // 400 round pebbles, separate objects (no twin: reference is method 2 at high quality)
  #declare R = seed(3);
  #for (I, 1, 400) #declare P = <4 * rand(R) - 2, 0, 3 * rand(R) - 1.5>; #declare S = 0.04 + 0.06 * rand(R);
    sphere { P + y * S * 0.5, S scale <1, 0.6, 1> texture { T } } #end
#break
#case (6)  // glyph tops
  text { ttf "timrom.ttf" "MWM" 0.1, 0 rotate x * 90 scale 1.6 translate <-2, 0, -0.5> texture { T } }
  text { ttf "timrom.ttf" "ilt" 0.1, 0 rotate x * 90 scale 0.8 translate <-1.2, 0, 0.6> texture { T } }
#break
#case (7)  // a sphere cap split into 8 by 6 pieces, separate objects; twin is the whole sphere
  #if (Twin) sphere { <0, -3, 0>, 3 texture { T } }
  #else
  #for (I, 0, 7) #for (J, 0, 5)
    intersection { sphere { <0, -3, 0>, 3 } box { <-2 + I * 0.5, -1, -1.5 + J * 0.5>, <-1.5 + I * 0.5, 1, -1 + J * 0.5> } texture { T } }
  #end #end
  #end
#break
#case (8)  // a mesh with holes
  #declare R = seed(7);
  mesh { #for (I, 0, 39) #for (J, 0, 29) #if (rand(R) > 0.3 | Twin)
    triangle { <-2 + I * 0.1, 0, -1.5 + J * 0.1>, <-1.9 + I * 0.1, 0, -1.5 + J * 0.1>, <-1.9 + I * 0.1, 0, -1.4 + J * 0.1> }
    triangle { <-2 + I * 0.1, 0, -1.5 + J * 0.1>, <-1.9 + I * 0.1, 0, -1.4 + J * 0.1>, <-2 + I * 0.1, 0, -1.4 + J * 0.1> }
    #end #end #end texture { T } }
#break
#case (9)  // CSG: a floor with round holes, and a plane clipped by an intersection
  difference { box { <-2, -0.1, -1.5>, <0.3, 0, 1.5> }
    #declare R = seed(11);
    #for (I, 1, 14) #declare P = <-1.9 + 2.1 * rand(R), 0, -1.4 + 2.8 * rand(R)>; cylinder { P - y, P + y, 0.03 + 0.25 * rand(R) } #end
    texture { T } }
  intersection { plane { y, 0 } box { <-0.8, -1, -0.8>, <0.8, 1, 0.8> rotate y * 30 translate x * 1.2 } texture { T } }
#break
#case (10)  // two textures meeting at x = 0; twin is one box with the same two pigments
  #if (Twin)
    box { <-3, -0.1, -2.5>, <3, 0, 2.5> texture { pigment { object { box { <0, -1, -2>, <3, 1, 2> } rgb #if (Mask) 1 #else A #end rgb #if (Mask) 1 #else B #end } } finish { #if (Mask) emission 1 diffuse 0 #else ambient 0 diffuse 1 #end } } }
  #else
    box { <-2, -0.1, -1.5>, <0, 0, 1.5> texture { TA } }
    box { <0, -0.1, -1.5>, <2, 0, 1.5> texture { TB } }
  #end
#break
#case (11)  // a thin plate over a wider one, Gap apart; a grazing lamp shadows a strip of the lower plate
  box { <-2, -0.01, -1.5>, <0.3, 0, 1.5> texture { T } }
  #if (!Twin) box { <-2, -0.1, -1.5>, <2, -Gap, 1.5> texture { T } } #end
#break
#end
#end
