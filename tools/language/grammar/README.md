# Language grammar artifacts

Machine-readable and human-readable descriptions of the 4.0 scene language, all generated:

| artifact | generator | source of truth |
|---|---|---|
| `pov4.json` | `extract.mjs` | `libraries/tree-sitter-pov4/src/grammar.json` (committed tree-sitter output) |
| `blocks.json` | `blocks.mjs` | the classic parser's `EXPECT`/`CASE` loops in `source/parser/*.cpp` |
| `../../source/parser/pov4schema.h` | `blocks.mjs` | `blocks.json` |
| `../../../doc/language-4-grammar.md` | `render.mjs` | `pov4.json` + `blocks.json` |

`pov4.json` normalizes the tree-sitter grammar into a documented format (`format:
pov4-grammar/1`) with node kinds `seq`, `choice`, `repeat`, `repeat1`, `opt`, `prec`,
`alias`, `field`, `token` and leaves `str`, `re`, `sym`, `blank`, plus semantic groups
(statements, expressions, lexical rules) and the scanner word classes.

`blocks.json` lists, per block keyword (`finish`, `texture`, `camera`, the object
keywords, ...), the reserved words that may appear as items, and the items of nested
sub-blocks such as `reflection { ... }` inside `finish`. Each entry records the parser
functions it was mined from. `pov4schema.h` compiles the same table into the renderer,
where the `.pov4` static checker consumes it. `--inspect` prints every mined function.

Regenerate everything with `node extract.mjs && node blocks.mjs && node render.mjs`
(run from this directory; `node --test test/` checks completeness, reserved-word
validity, known-content anchors and regeneration stability). `keywords.json` comes from
`tools/language/keywords.mjs` as before; these generators consume it, never edit it.
