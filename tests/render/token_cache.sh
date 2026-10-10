#!/bin/sh
# token_cache.sh <povray> <srcdir>: replayed and streamed tokens parse as written; errors stay put.
set -e
POVRAY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1"); SRCDIR=$(cd "$2" && pwd)
WORK=$(mktemp -d); trap 'rm -rf "$WORK"' EXIT; cd "$WORK"
printf '#if (0)\n#declare F = 1e;\n\001 rejected by the lexer\n#end\n' > token_cache_skip.inc
printf '#macro Thrice(X) (3 * X) #end\n#declare Tri = 0;\n' >> token_cache_skip.inc
printf '#for (I, 1, 4) #declare Tri = Tri + Thrice(I); #end\n' >> token_cache_skip.inc
awk 'BEGIN { for (i = 0; i < 45000; i++) print "#declare Fill = 1;" }' > token_cache_long.inc
printf '#macro Tail(X) (X + 1) #end\n#declare TailSum = 0;\n' >> token_cache_long.inc
printf '#for (I, 1, 3) #declare TailSum = TailSum + Tail(I); #end\n' >> token_cache_long.inc
printf '#declare K = 0;\r\n#while (K < 2) #declare K = K + 1; #end\r\n' > token_cache_crlf.inc
printf '#declare L = 1;\r\n#declare M = Nowhere;\r\n' >> token_cache_crlf.inc
run() {
    "$POVRAY" +i"$SRCDIR/tests/render/token_cache.pov" +L"$SRCDIR/tests/render" \
        +w8 +h8 -d -p -v -gp -f "$@" > token_cache.log 2>&1
}
fail() { cat token_cache.log; echo "token_cache: $1" >&2; exit 1; }
says() { tr -d '\n' < token_cache.log | grep -q "$1"; }
run || fail "the scene does not parse"
says "token_cache sum 90 includes 6 strlen 5 fact 720" || fail "replayed tokens parsed differently"
says "token_cache written 2 tri 36 tail 31" || fail "rewritten or long files parsed differently"
! run Declare=Bad=1 || fail "an unterminated string parsed"
says "token_cache_bad.inc' line 4: Parse Error: Unterminated string" || fail "a lexing error moved"
! run Declare=BadFloat=1 || fail "1e parsed as a number"
! run Declare=Crlf=1 || fail "an undeclared identifier parsed"
says "token_cache_crlf.inc' line 4: Parse Error" || fail "a CRLF file's error moved"
echo "token_cache: loops, macros, rewritten, long and CRLF files parse as written; errors stay put"
