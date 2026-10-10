// token_cache.sh: a lexing error after a loop, reported at its own line.
#declare K = 0;
#while (K < 2) #declare K = K + 1; #end
#declare T = "unterminated
