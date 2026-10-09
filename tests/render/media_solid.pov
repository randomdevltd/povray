#version Version;
// Declare=Backdrop=0 background only, 1 plane facing the camera (its solid side holds the camera), 2 the plane flipped.
#ifndef (Backdrop) #declare Backdrop = 1; #end
#ifndef (Hollow) #declare Hollow = 1; #end
#ifndef (Nested) #declare Nested = 0; #end
#ifndef (Blend) #declare Blend = 0; #end
#ifndef (DefaultBlend) #declare DefaultBlend = 0; #end
#ifndef (Varying) #declare Varying = 0; #end
#ifndef (Sibling) #declare Sibling = 0; #end
#ifndef (Csg) #declare Csg = 0; #end
#ifndef (Lit) #declare Lit = 0; #end

global_settings { assumed_gamma 1 }
#if (DefaultBlend) #default { interior { media_blend inner } } #end
camera { orthographic location <0, 0, -20> look_at 0 right x * 16 up y * 12 }
background { rgb 1 }
#declare Backdrop_Finish = finish { #if (Lit) emission 0 diffuse 1 #else emission 1 diffuse 0 #end ambient 0 }
#if (Backdrop = 1) plane { z, 6 pigment { rgb 1 } finish { Backdrop_Finish } } #end
#if (Backdrop = 2) plane { -z, -6 pigment { rgb 1 } finish { Backdrop_Finish } } #end
#if (Lit) light_source { <0, 0, -100> rgb 1 parallel point_at 0 media_attenuation on } #end

#macro Medium(Absorption)
  media {
    absorption Absorption
    #if (Nested | Varying) method 4 density { #if (Varying) bozo scale 0.7 #else rgb 1 #end } #end
    #if (Varying) scattering { 1, 0.3 } #end
  }
#end

#if (Varying) light_source { <-10, 20, -15> rgb 1 } #end

// Declare=Blend=0 add, 1 inner, 2 subtract, 3 multiply, 4 subtract without media; Sibling=1 lets the inner box poke out.
// Csg=1 makes the inner box an intersection with an infinite plane; Lit=1 lights the backdrop through the boxes.
#if (Nested)
  box {
    <-4, -4, -1>, <4, 4, 1> pigment { rgbt 1 } hollow
    interior { Medium(0.5) }
  }
  #if (Csg) intersection { plane { y, 100 } #end
  box {
    <-2, -2, -0.5>, <2, 2, #if (Sibling) 1.5 #else 0.5 #end>
  #if (Csg) } #end
    pigment { rgbt 1 } hollow
    interior {
      #switch (Blend)
        #case (1) media_blend inner Medium(1.0) #break
        #case (2) media_blend subtract Medium(1.0) #break
        #case (3) media_blend multiply media { density { rgb 0.4 } } #break
        #case (4) media_blend subtract #break
        #else Medium(1.0)
      #end
    }
  }
#else
  box {
    <-3, -3, -1>, <3, 3, 1> pigment { rgbt 1 }
    #if (Hollow) hollow #end
    interior { Medium(0.5) }
  }
#end
