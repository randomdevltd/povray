# Classic scene language grammar

A Tree-sitter grammar for the classic (3.x) template language: `.pov`, `.inc` and `.mcr` files. It is used by
the 4.0 codemod, by editor tooling and by the splice inventory (`tools/language/corpus.mjs`); the renderer does
not compile it. `src/parser.c`, `src/grammar.json`, `src/node-types.json` and `src/tree_sitter/*` are generated
and checked in; `src/scanner.c` (nested block comments) is hand written.

The reserved words come from `tools/language/keywords.json` when the grammar is generated, so regenerate after
that list changes. `grammar.js` reads `../../tools/language/keywords.json`, so generate inside a full checkout;
a container must mount the repository root, not only this directory. Maintenance uses Node.js 24, a C/C++
toolchain and the pinned Tree-sitter CLI 0.25.10. From the repository root:

```sh
npm --prefix libraries/tree-sitter-pov ci          # also builds the Node binding
npm --prefix libraries/tree-sitter-pov run generate
npm --prefix libraries/tree-sitter-pov test        # corpus, highlight and tag tests, then the binding test
```

The install scripts of `tree-sitter`, `tree-sitter-cli` and this package must be allowed to run: they build the
runtime, fetch the CLI binary and compile the binding (`build/Release/tree_sitter_pov_binding.node`).

## Using it from Node

After `npm ci` here, a script elsewhere in the repository loads the runtime and the grammar through this
package's `node_modules`, as `tools/language/corpus.mjs` does:

```js
import { createRequire } from 'node:module';
const require = createRequire(new URL('../../libraries/tree-sitter-pov/package.json', import.meta.url));
const Parser = require('tree-sitter');
const parser = new Parser();
parser.setLanguage(require('.'));
const tree = parser.parse((i) => (i < text.length ? text.slice(i, i + 65536) : null));
```

Read files as `latin1` so arbitrary bytes survive. Pass a chunk callback, not one large string.

## Tree

A file, a block body, a bracket group and every directive body are sequences of *items*: `keyword` (a reserved
word that cannot start a value), values, `block` (`name { items }`, where the name is a keyword or, for newer
contextual syntax such as `skein`, an identifier), `bracket_group` (`[0.5 color Red]`), `function_block`,
`array_expression`, `dictionary_expression`, `tag_filter`, directives, `,` and `;`. The grammar does not know
which items each block accepts. Reserved words are never identifiers (`#declare pigment = 5;` is an error),
except the render-tag words (`tags`, `filter_tags`, `front_filter_tags`, `back_filter_tags`, `any`, `none`),
which classic reserves only from `#version 4.0`. Members after `.`, `local.`/`global.` scopes and function
parameters such as `u` and `x` accept the reserved words classic accepts there.

Directives are `declare_directive` (`kind` is `#declare` or `#local`; `target` is an identifier, index, member
or `tuple_target`; `value` may be a `layered_texture`), `macro_directive`, `if_directive` (`#if`, `#ifdef`,
`#ifndef` with `elseif_clause` / `else_clause`), `while_directive`, `for_directive`, `switch_directive`
(`case_clause`, `range_clause`, `else_clause`, `break_directive` as an item), `include_directive`,
`version_directive`, `default_directive`, `undef_directive`, `message_directive` (`#debug`, `#warning`,
`#error`, and the deprecated `#render` and `#statistics`), `fopen_directive`, `fclose_directive`,
`read_directive`, `write_directive` (values may carry a binary type such as `uint8`) and `breakpoint_directive`
(debug builds only).

Expressions follow the classic parser. Where classic reads a float, vector or declared value (block items,
vector components, `#declare` values, macro arguments, array elements) only `+ - * /`, unary `- + !` and
postfix forms apply, so `translate -x` is a keyword and a negation and `box { <0,0,0> <1,1,1> }` is two vectors.
Inside parentheses, built-in function arguments, conditions and `function { }` bodies the full grammar applies:
`?:`, then `& |`, then `< <= = != >= >`, all as `binary_expression` with an `operator` field. Vector components
may omit commas. Classic decides `A [i]` by the type of `A`; here `[` directly after an operand always
indexes, and a spaced `[` indexes where no item can follow (inside parentheses, vectors, arguments) but starts
a bracket group in a block body or after a statement-level `#declare` value, which `corpus.mjs` reports as
`ambiguous-index`. `colour_expression` (`rgb <..>`, `color red 1 green 0.5`) exists where a value is declared or
passed; in block bodies colour words are keywords followed by values. `#declare T = texture {..} texture {..}`
is one `layered_texture`.

An `if_directive` may also stand where a value is expected: `<1, #if (A) 0 #else 1 #end, 0>`. Its branches are
item sequences, so `corpus.mjs` decides whether each is a single value. Text whose directives split a block, a
vector or an argument list (`#if (A) union { #else merge { #end`, `Begin() ... }` after a macro that opens a
block, `#include` as a macro argument) produces `ERROR` or `MISSING` nodes.

Text meshes stay linear: a generated 25 MB `mesh2` (333k `vertex_vectors`, 431k `face_indices`) produces 7.9
million nodes, 3.6 million of them named, and parses in about 5 s with 1.7 GB resident in Node; 6.25 MB and
12.5 MB give 2.0 and 4.0 million nodes.

## Splice inventory

`node tools/language/corpus.mjs [--quiet] [--allow-errors] [--json FILE] [PATH...]` parses every `.pov`, `.inc`
and `.mcr` under `distribution/` and `tests/` (or the given paths, relative to the working directory). Each file
is `error` (syntax errors, with the first locations), `ill-formed` (parses, but has a reason below) or `clean`;
`--quiet` hides clean files. It exits 1 when any file has syntax errors unless `--allow-errors` is given, and
2 on bad arguments. With `--json` it writes:

```json
{
 "files": 633, "syntaxClean": 611, "wellFormed": 602,
 "directives": { "#if": { "top-level": 753, "block-item": 385, "macro-body": 227, "macro-fragment": 83,
                          "expression": 2, "partial": 172 } },
 "contexts": { "top-level": 12191, "block-item": 1440, "macro-body": 2206, "macro-fragment": 330,
               "expression": 5, "partial": 339 },
 "macros": { "statements": 292, "expression": 138, "items": 23, "directives": 34 },
 "notWellFormed": [ { "file": "distribution/include/shapes.inc",
                      "reasons": [ { "reason": "syntax-error", "line": 86, "column": 13, "count": 37 },
                                  { "reason": "partial-macro", "line": 360, "column": 1 } ] } ]
}
```

A directive's context is that of its nearest enclosing non-conditional, non-loop construct: `top-level`,
`block-item`, `macro-body`, `macro-fragment` (a macro whose body has keywords or bracket groups, so it is
spliced into a block), `expression` (a value position), or `partial` (the directive contains or sits in an
error). Macro kinds, looking through conditionals and loops in the body: `expression` (every path yields one
value), `items`, `statements` (blocks, macro calls, other values) and `directives` (nothing else). A file is
well-formed when it has no reasons. Reasons, each with a 1-based `line` and `column`:

| Reason | Meaning |
|---|---|
| `syntax-error` | first innermost `ERROR` or `MISSING` node; `count` is how many |
| `partial-macro` | a macro whose body is not a complete item sequence (it builds tokens for its caller) |
| `file-io` | `#fopen`, `#fclose`, `#read` or `#write` |
| `expression-if-without-else` | a value-position `#if` with no `#else` |
| `expression-if-not-single-value` | a value-position `#if` branch that is not exactly one value or block |
| `directive-in-expression` | another directive in a value position |
| `operator-splice` | a conditional or loop after a value whose first branch item is unary `+`/`-` |
| `ambiguous-index` | an identifier or index followed by a spaced bracket group |
| `stray-item` | a keyword or non-call value at statement level outside a macro, outside any span already in error |

## Queries

`queries/highlights.scm`, `locals.scm`, `tags.scm` (macros, function and block declarations, macro calls) and
`folds.scm`. `test/highlight` and `test/tags` hold their assertions.
