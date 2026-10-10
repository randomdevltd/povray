// token_cache.sh: included once per loop pass; its macro is called after the include returns.
#ifndef (IncCount) #declare IncCount = 0; #end
#declare IncCount = IncCount + 1;
#macro Twice(X) (2 * X) #end
