#version Version;
// Declare=Backdrop=0 background only, 1 plane facing the camera (its solid side holds the camera), 2 the plane flipped,
// 3 the facing plane declared after the media.
#ifndef (Backdrop) #declare Backdrop = 1; #end
#ifndef (Hollow) #declare Hollow = 1; #end
#ifndef (Nested) #declare Nested = 0; #end
#ifndef (Blend) #declare Blend = 0; #end
#ifndef (DefaultBlend) #declare DefaultBlend = 0; #end
#ifndef (Varying) #declare Varying = 0; #end
#ifndef (Sibling) #declare Sibling = 0; #end
#ifndef (Csg) #declare Csg = 0; #end
#ifndef (Lit) #declare Lit = 0; #end
#ifndef (InnerFirst) #declare InnerFirst = 0; #end
#ifndef (TwoMedia) #declare TwoMedia = 0; #end

global_settings { assumed_gamma 1 }
#if (DefaultBlend) #default { interior { media_blend replace } } #end
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

// Blend=0 add, 1 replace, 2 subtract, 3 multiply, 4 subtract without media, 5 no media, 6 two half subtracts; Sibling, InnerFirst,
// Csg (intersection with a plane), Lit (backdrop lit through the boxes) and TwoMedia (two outer media, 0.75 subtracted) vary it.
#macro Inner(Absorption)
  #if (Csg) intersection { plane { y, 100 } #end
  box {
    <-2, -2, -0.5>, <2, 2, #if (Sibling) 1.5 #else 0.5 #end>
  #if (Csg) } #end
    pigment { rgbt 1 } #if (Hollow) hollow #end
    #switch (Blend)
      #case (1) interior { media_blend replace Medium(1.0) } #break
      #case (2) interior { media_blend subtract Medium(Absorption) } #break
      #case (6) interior { media_blend subtract Medium(Absorption) } #break
      #case (3) interior { media_blend multiply media { density { rgb 0.4 } } } #break
      #case (4) interior { media_blend subtract } #break
      #case (5) #break
      #else interior { Medium(1.0) }
    #end
  }
#end
#macro Subtracting()
  #local Amount = (TwoMedia ? 0.75 : 1.0);
  #if (Blend = 6)
    object { Inner(Amount / 2) scale <1, 1, 0.75> translate -z * 0.125 }
    object { Inner(Amount / 2) scale <1, 1, 0.75> translate z * 0.125 }
  #else
    Inner(Amount)
  #end
#end

#if (Nested)
  #if (InnerFirst) Subtracting() #end
  box {
    <-4, -4, -1>, <4, 4, 1> pigment { rgbt 1 } #if (Hollow) hollow #end
    interior { #if (TwoMedia) Medium(0.5) Medium(0.5) #else Medium(0.5) #end }
  }
  #if (!InnerFirst) Subtracting() #end
#else
  box {
    <-3, -3, -1>, <3, 3, 1> pigment { rgbt 1 }
    #if (Hollow) hollow #end
    interior { Medium(0.5) }
  }
#end
#if (Backdrop = 3) plane { z, 6 pigment { rgb 1 } finish { Backdrop_Finish } } #end
