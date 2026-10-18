#!/bin/sh
# parser_token_count.sh <povray> <srcdir>: the #declare that ends on the parse's 3000001st token keeps its expression's value.
set -e
POVRAY=$1; SRCDIR=$2
for pad in 12 13 14; do
    "$POVRAY" +i"$SRCDIR/tests/render/parser_token_count.pov" +w8 +h8 -a -d -p -v -gp -f Declare=Pad=$pad > parser_token_count.log 2>&1 || { cat parser_token_count.log; echo "parser_token_count: Pad=$pad does not parse" >&2; exit 1; }
    tr -d '\n' < parser_token_count.log | grep -q 'wrong values: 0' && ! grep -q 'iteration' parser_token_count.log || { grep -e 'iteration' -e 'wrong values' parser_token_count.log; echo "parser_token_count: Pad=$pad declared a wrong value" >&2; exit 1; }
done
rm -f parser_token_count.log
echo "parser_token_count: every #declare kept its value across the 3000001st token"
