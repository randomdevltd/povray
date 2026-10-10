// token_cache.sh: loops, macros, includes and rewritten files, replayed from the token cache.
#version 3.7;
#include "token_cache.inc"
#declare Sum = 0;
#for (I, 1, 5)
  #declare J = 0;
  #while (J < 3)
    #declare Sum = Sum + Twice(I * J);
    #declare J = J + 1;
  #end
  #include "token_cache.inc"
#end
#for (I, 1, 2) #declare S = concat("a\tb", "é", chr(65)); #end
#macro Fact(N) #if (N <= 1) 1 #else N * Fact(N - 1) #end #end
#debug concat("token_cache sum ", str(Sum, 0, 0), " includes ", str(IncCount, 0, 0),
              " strlen ", str(strlen(S), 0, 0), " fact ", str(Fact(6), 0, 0), "\n")
#fopen F "token_cache_out.inc" write
#write (F, "#declare Written = 1;\n")
#fclose F
#include "token_cache_out.inc"
#fopen F "./token_cache_out.inc" write
#write (F, "#declare Written = 2;\n")
#fclose F
#include "token_cache_out.inc"
#include "token_cache_skip.inc"
#include "token_cache_float.inc"
#for (I, 1, 2) #declare Tri = Tri + Thrice(1); #end
#include "token_cache_long.inc"
#fopen F "token_cache_long.inc" write
#write (F, "// rewritten\n")
#fclose F
#for (I, 1, 2) #declare TailSum = TailSum + Tail(10) + Span(); #end
#debug concat("token_cache written ", str(Written, 0, 0), " tri ", str(Tri, 0, 0),
              " tail ", str(TailSum, 0, 0), "\n")
#ifdef (Bad) #include "token_cache_bad.inc" #end
#ifdef (BadFloat) #declare F = 1e; #end
#ifdef (Crlf) #include "token_cache_crlf.inc" #end
camera { location <0, 0, -3> look_at 0 }
sphere { 0, 1 pigment { rgb 1 } }
