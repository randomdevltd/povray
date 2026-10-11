# Scene language 4.0 grammar

Tree-sitter grammar for `.pov4` and `.inc4` files (see `doc/scene-language-4.md`).
`src/parser.c`, `src/grammar.json`, `src/node-types.json`, `src/words.h` and
`src/tree_sitter/*` are generated and checked in; `src/scanner.c` is written by
hand. The renderer parses a 4.0 file once with this grammar, lowers the concrete
syntax tree to its own compact tree and releases the Tree-sitter objects.

The grammar does not know which items a block accepts: `sphere { ... }` and
`pigment { ... }` are both a `keyword` followed by a body of items. Reserved
words come from `tools/language/keywords.json` and fall into four classes,
listed in `words.js`:

- `keyword`: words that never start a value (`translate`, `pigment`, `hollow`),
  so `translate -x` is a keyword followed by a negated vector;
- `builtin`: value words (`x`, `pi`, `sin`, `vrotate`, `filter`, ...);
- `colour_operator` (`rgb`, `rgbf`, ..., `srgbft`) and `color_operator`
  (`color`, `colour`), prefix operators that take the whole following
  expression, as in classic `rgb <1, 0, 0> * 0.5`;
- `colour_channel` (`red`, `green`, `blue`, `filter`, `transmit`, `gray`, `grey`, `alpha`),
  which take a value when one follows (`color red 1 green 0.5`); `filter` directly followed by
  `(` is the built-in instead;
- statement words (`let`, `fn`, `if`, `for`, `null`, ...), ordinary tokens.

The first four are recognised by `src/scanner.c` with one table lookup instead
of one lexer token per word, which keeps the generated parser small. A word is a
keyword only where the grammar accepts one; elsewhere it lexes as `identifier`,
which the renderer rejects as a name.

Comparisons and logical operators (`comparison_expression`) parse only inside parentheses
and render-time function bodies, as in classic; elsewhere `<` starts a vector, so juxtaposed
vectors are separate items and `>` always closes a vector. Vectors take any number (two or
more) of components. Indexing and member access need no space before `[` or `.`: `A[0]`, `V.x`. A
spaced `[` starts a new item, so map entries such as `[0 color Red] [1 color Blue]`
stay bracket groups. Lambda bodies in braces are always bodies, never
dictionaries.

For grammar maintenance, use Node.js and the pinned Tree-sitter CLI 0.25.10:

```sh
npm ci
npm run generate   # also regenerates src/words.h
npm test
```

The install scripts of `tree-sitter`, `tree-sitter-cli` and this package must be
allowed to run: they build the runtime, fetch the CLI binary and compile the Node
binding (`build/Release/tree_sitter_pov4_binding.node`, same `binding.gyp` and
`bindings/node` layout as `../tree-sitter-pov`). The editor language server in
`extensions/vscode/server` loads that binding when it exists and falls back to
scanner-based analysis without it.

The runtime is pinned to the same release in `../tree-sitter`. Regular renderer
builds compile the committed C sources directly.
