#macro Swap(A, B)
  #local T = A;
  #declare A = B;
  #declare B = T;
#end
#macro Leaf() sphere { 0, Size } #end
#macro Tree()
  #local Size = 2;
  Leaf()
#end
#declare P = 1;
#declare Q = 2;
Swap(P, Q)
#declare R = P;
#declare Layered = texture { pigment { rgb 1 } } texture { pigment { rgbt 1 } }
#switch (P)
  #case (1)
    #declare S = 1;
  #case (2)
    #declare S = 2;
  #break
#end
#declare Spliced = 3 * #if (P) 1 + 2 #else 4 #end;
