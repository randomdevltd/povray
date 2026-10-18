// The #declare that ends on the parse's 3000001st token must keep its expression's value; Pad shifts which one that is.
#version 3.7;
#ifndef (Pad) #declare Pad = 0; #end
#declare K = 1000;
#declare Bad = 0;
#for (P, 1, Pad) #end
#for (I, 1, 200000)
  #declare V = I + K;
  #if (V != I + K)
    #debug concat("iteration ", str(I, 0, 0), ": V = ", str(V, 0, 0), ", expected ", str(I + K, 0, 0), "\n")
    #declare Bad = Bad + 1;
  #end
#end
#debug concat("wrong values: ", str(Bad, 0, 0), "\n")
camera { location <0, 0, -3> look_at 0 }
light_source { <-5, 5, -5>, rgb 1 }
sphere { 0, 1 pigment { rgb 1 } }
