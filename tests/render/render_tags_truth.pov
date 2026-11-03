#version 4.0;
#ifdef (LegacySymbols)
  #version 3.7;
  #declare any = "missing";
  #declare none = "missing";
  #version 4.0;
#end
#ifndef (Expr) #declare Expr = 0; #end
#ifndef (Expected) #declare Expected = -1; #end
#ifndef (Late) #declare Late = 0; #end
#ifndef (Combined) #declare Combined = 0; #end
global_settings { assumed_gamma 1 }
background { rgb 0 }
#macro Boxes(All, X)
  #for (I, 0, 3)
    #if (All | (Expected < 0 | mod(int(Expected/pow(2, I)), 2)))
      box {
        <-3.8 + 2*I + X, -0.8, 0>, <-2.2 + 2*I + X, 0.8, 0.1>
        pigment { rgb <0.2 + 0.2*I, 0.8 - 0.15*I, 0.5> }
        finish { emission 1 diffuse 0 }
        #switch (I)
          #case (1) tags { "a", "alpha", "a" } #break
          #case (2) tags { "b" } #break
          #case (3) tags { "b", "a" } #break
        #end
      }
    #end
  #end
#end
#declare ExprCamera = camera {
  orthographic location <0, 0, -5> look_at 0 right 8*x up 2*y
  tags { "camera", "camera" }
  #if (!Late)
    #switch (Expr)
      #case (1) filter_tags { any } #break
      #case (2) filter_tags { none } #break
      #case (3) filter_tags { "a" } #break
      #case (4) filter_tags { "a" & "b" } #break
      #case (5) filter_tags { "a" | "b" } #break
      #case (6) filter_tags { "a" | "b" & !"a" } #break
      #case (7) filter_tags { !("a" | "b") } #break
      #case (8) filter_tags { "a*" & !"b" } #break
      #case (9) filter_tags { any & none } #break
      #case (10) filter_tags { any | none } #break
      #case (11) filter_tags { ("a" | "b") & !"a" } #break
      #case (12) filter_tags { "a**" & !"b" } #break
      #case (13) filter_tags { "*b*" } #break
      #case (14) filter_tags { "A" } #break
      #case (15) filter_tags { !none } #break
    #end
  #end
}
#declare PlainCamera = camera { orthographic location <0, 0, -5> look_at 0 right 8*x up 2*y }
#if (Combined)
  // One 128x32 frame of two 64x32 screen views: the left half is the expression's own selection
  // over all four boxes, the right half the boxes the expected mask places, seen without a filter.
  camera { PlainCamera tags { "camera" } }
  box {
    <-4, -1, 0>, <0, 1, 0.01>
    pigment { screen { camera { ExprCamera translate 100*x } } scale <4, 2, 1> translate <-4, -1, 0> }
    finish { emission 1 diffuse 0 }
    tags { "camera" }
  }
  box {
    <0, -1, 0>, <4, 1, 0.01>
    pigment { screen { camera { PlainCamera translate 200*x } } scale <4, 2, 1> translate <0, -1, 0> }
    finish { emission 1 diffuse 0 }
    tags { "camera" }
  }
  Boxes(1, 100)
  Boxes(0, 200)
#else
  camera { ExprCamera }
  Boxes(0, 0)
#end
#if (Late) global_settings { filter_tags { "a" } } #end
