#ifdef (Quality)
  #declare Q = Quality;
#elseif (clock > 0.5 & clock < 1)
  #declare Q = 2;
#else
  #declare Q = 1;
#end
#ifndef (Seed) #declare Seed = seed(42); #end

#declare I = 0;
#while (I < 10 | I = 20)
  #if (I = 5) #break #end
  sphere { <I, 0, 0>, 0.4 }
  #declare I = I + 1;
#end

union {
  #for (J, 0, 1, 0.25)
    box { <J 0 0>, <J + 0.2, 1, 1,> }
  #end
  translate y * #if (Q > 1) 2 #else 1 #end
}

#switch (Q)
  #case (0)
  #case (1)
    #debug "low\n"
  #break
  #range (2, 3)
    #debug "mid\n"
  #break
  #else
    #debug "high\n"
#end

#switch (int(rand(Seed) * 3))
  #case (0) #declare Tint = 0; #break
  #case (1) #declare Tint = 1; #break
#end

#switch (Q = 1 | Q = 2)
  // grouped
  #case (1)
    #declare Hit = 1; // yes
  #break
#end
#switch (Q)
#end
#for (K, 1, 3) #end
#declare After = K;
#switch (Q)
  #range (Q <= 1, Q >= 1)
    #declare Hit = 2;
  #break
#end
