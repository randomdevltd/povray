# Scene language 4.0 (`.pov4`)

POV-Ray reads two scene languages:

- **Classic** (`.pov`, `.inc`, anything not listed below): the 3.x template language. Parsed by the
  existing parser, now fed from a lex-once token cache (see [Classic front end](#classic-front-end)).
- **4.0** (`.pov4`, `.inc4`): a new language with no `#` meta-language. The file extension is the
  opt-in; no marker inside the file is needed or accepted. A 4.0 file always runs with language
  version 4.0 semantics (the same defaults a classic `#version 4.0;` scene gets).

Object syntax is kept as close to classic as possible: `sphere { <0, 1, 0>, 1 texture { T } }` is
valid in both. What changes is everything around it: declarations, control flow, macros, includes.

Both languages have Tree-sitter grammars:

| Grammar | Directory | Used by |
|---|---|---|
| classic | `libraries/tree-sitter-pov` | codemod, editor tooling, splice inventory |
| 4.0 | `libraries/tree-sitter-pov4` | the renderer's 4.0 evaluator, codemod tests, editor tooling |

Generated C sources are committed; renderer builds do not run the generator.

## Execution model

A 4.0 file is a program that runs **once** to construct the scene. Running it emits *items* into an
*output*; what the output is depends on where the code runs:

| Code runs in | Its output is |
|---|---|
| the top level of a `.pov4` file (or an `.inc4` it includes) | the scene |
| a function body | the function's result (a *fragment*) |
| a block body, e.g. inside `union { ... }` | that block's contents |
| an array literal `[ ... ]` | the array's elements |

An **item** is anything that can appear in a classic block body: a keyword (`translate`, `rgb`,
`hollow`), a value (`<1, 0, 0>`, `2.5`, `"name"`), a block (`pigment { ... }`), or a bracket group
(`[0.5 color Red]`). Statements (`let`, `if`, `for`, `while`, `return`, `include`) emit nothing by
themselves; their bodies emit into the enclosing output.

So a statement **manifests** its result when its output is the scene, and a `let` never manifests:

```pov
let Ball = sphere { 0, 1 pigment { rgb <1, 0, 0> } };   // nothing in the scene yet
Ball                                                    // manifests one sphere
object { Ball translate x * 3 }                         // and a second one
```

At the top level only manifestable items are allowed: objects, `camera`, `light_source`, `background`,
`fog`, `sky_sphere`, `rainbow`, `media`, `global_settings`, `default`, `photons`, `radiosity`. A bare
value or modifier there is an error.

### Functions are components

A function body emits into the function's result; nothing reaches the scene unless the caller puts it
there. Where the call appears decides what happens:

```pov
fn Tree(H) {
  cylinder { 0, y * H, 0.1 pigment { rgb <0.4, 0.3, 0.2> } }
  sphere { y * H, H / 3 pigment { rgb <0.1, 0.5, 0.1> } }
}

Tree(2)                                   // statement at top level: both objects manifest
let T = Tree(3);                          // assignment: nothing manifests, T holds a fragment
union { ...T translate x * 4 }            // spread T's items into a union
union { Tree(1) translate -x * 4 }        // a call in a block body splices its items there
```

A **fragment** is the ordered list of items a call emitted. It behaves as:

- its single item, when it has exactly one and is used as a value (`let V = Perp(A);` where `Perp`
  emits one vector);
- its items spliced in place, when used in a block body, an array literal, or with `...`;
- an error anywhere else (e.g. arithmetic on a two-item fragment).

`return E;` ends the function with result `E`. It is an error to `return` a value after the function
has emitted items. A function that emits nothing and does not return yields `null`.

Lambdas are functions without names:

```pov
let Scale = (S) => S * 2;
let Bump = (P) => { let Q = P + y; Q * 0.5 };   // braces: a body, last items are the result
let Points = map(range(0, 9), (I) => <I, sin(I), 0>);
```

`fn Name(A, B = 1) { ... }` is shorthand for `let Name = (A, B = 1) => { ... };` and binds in the
current scope. Parameters may have defaults. Functions are closures; arguments are passed by value
(arrays and dictionaries are copied on write, so a callee never mutates its caller's array).

### Scope and assignment

- `let X = E;` binds `X` in the current **function** scope (the file is the outermost function).
  Re-running `let X` in the same scope rebinds it. `if`, `for` and `while` bodies do not open scopes.
- `X = E;` assigns to the nearest existing binding of `X` (local, enclosing function, file). It is an
  error if there is none. `A[I] = E;` and `D.K = E;` assign into a container; `V.x = E;` sets one
  component of a vector or colour (a colour channel is rounded to colour precision).
- `global X = E;` binds `X` at file scope from anywhere. This is what classic `#declare` inside a
  macro does.
- `let X;` declares `X` as `null`; `defined(X)` is false while `X` is `null`, so `X = null;`
  plays the part of classic `#undef X`.

An `.inc4` include runs in the including file's scope (as classic includes do).

## Syntax

This chapter is prose; `doc/language-4-grammar.md` is the generated EBNF reference and
`tools/language/grammar/pov4.json` plus `blocks.json` are the machine-readable grammars.

### Lexical

- Comments `// ...` and `/* ... */` (not nested).
- Numbers as classic: `1`, `.5`, `1e-3`. Strings as classic, same escapes.
- Identifiers as classic; they may not be a reserved word. Reserved words are the classic reserved
  words plus `let`, `fn`, `return`, `null`, `in`, `step`, `continue`. (`if`, `else`, `for`,
  `while`, `break`, `include`, `global`, `to`, `true`, `false` are already classic reserved words.)
  The 4.0 built-ins `len`, `map`, `push` and `keys` are not reserved: a name resolves to a 4.0
  binding first, then to a classic declaration of that name, then to the built-in. Names starting
  with `__pov4_` are reserved for the evaluator.
- `;` after an item is allowed and ignored. `let`, assignment, `global`, `include` and `return`
  statements end with `;`.
- Indexing and member access take no space before `[` or `.`: `A[I]`, `V.x`. A `[` after a space
  starts a new item, so `[0 color Red] [1 color Blue]` are two bracket groups.
- As in classic, an item that starts with `-`, `+`, `<` or `(` continues the expression before it
  (`x -y` is one expression, `translate -y` is a keyword and a value); separate juxtaposed values
  with a comma when that is not meant.

### Statements

```pov
include "colors.inc4";             // 4.0 include: runs in this scope
include "textures.inc";            // classic include: see "Mixing languages"

let X = 1;   X = X + 1;   global Seed = seed(42);
fn F(A, B = 2) { A * B }

if (X > 1) { ... } else if (X < 0) { ... } else { ... }
while (X < 10) { ... }
for (I = 0 to 10) { ... }          // inclusive, step 1 (classic #for semantics exactly; afterwards I is 11)
for (I = 0 to 1 step 0.1) { ... }
for (V in Points) { ... }          // arrays, dictionaries (keys), fragments
break;   continue;   return E;
```

Bodies always take braces. There is no `switch`; the codemod writes `if` chains.

### Expressions

| Precedence (high→low) | Operators |
|---|---|
| postfix | call `f(a, b)`, index `a[i]`, member `v.x` `d.key` |
| prefix | `-` `+` `!` |
| multiplicative | `*` `/` |
| additive | `+` `-` |
| relational | `<` `<=` `>` `>=` |
| equality | `==` `!=` |
| logical and | `&&` |
| logical or | `\|\|` |
| conditional | `C ? A : B` |
| lambda | `(params) => expr` / `(params) => { body }` / `P => expr` |

Changes from classic expressions: equality is `==` (classic `=`, with the same epsilon comparison:
`(0.1 + 0.2 == 0.3)` is 1), logical operators are `&&` / `||`
(classic `&` / `|`) and short-circuit when the left operand is a float that decides the result, and
`&&` binds tighter than `||`. As in classic, comparisons and logical operators (`<` ... `||`) need
parentheses except in conditions (`if (A < B)`, `while (...)`) and render-time function bodies:
block items, vector components, `let` values and arguments take only `+ - * /`, so juxtaposed
vectors (`box { <0, 0, 0> <1, 1, 1> }`) are two items and `>` always closes a vector. Write
`(I < 2) ? A : B`, `filter(L, (P) => (P > 0))`. Everything else keeps classic meaning, including float→vector promotion,
`.x/.y/.z/.t/.red/.green/.blue/.filter/.transmit` members, `x`, `y`, `z`, `t`, `u`, `v`, `pi`,
`clock` and every classic built-in function.

Literals:

```pov
<1, 2, 3>                         // vector: 2–5 components as a value; any number ≥2 as a block item (matrix <...>)
rgb <1, 0.5, 0>                   // colour expressions as classic
[1, 2, 3]  [...A, 4]              // array; spreads
[for (I = 0 to 9) { I * I }]       // comprehension: the array is an output
{ A: 1, "b c": 2, [K]: 3, ...D }  // dictionary; any word, string or computed [string] keys; spreads
null  true  false
```

Any word is a dictionary key, reserved or not (`{ step: 1 }`), and member access takes any word
(`D.step`); on vectors and colours the members are the classic component names.

An array literal is an output, so `if`/`for`/`let` work inside it, and items are separated by commas
or juxtaposition. Arrays have a fixed size once built: `A[I] = E` replaces an element, `push`
returns a longer copy. In a block body a bracket group is emitted as-is, so classic map entries keep
working: `color_map { [0 color Red] [1 color Blue] }`. An array whose elements are all arrays of the
same shape is, for `dimensions`, `dimension_size` and classic SDL, a classic multi-dimensional array:
`array(4, array(10))` has 2 dimensions and reaches a classic include as `array[4][10]`.

Colour expressions: `rgb`, `rgbf`, `rgbt`, `rgbft`, the `srgb` forms and `color`/`colour` are prefix
operators taking the whole following expression, as in classic (`rgb <1, 0, 0> * 0.5`); a colour
value is a 5-component vector rounded to the renderer's colour precision, exactly as a classic
colour identifier. Channel phrases follow classic `Parse_Colour`: `red`, `green`, `blue`, `filter`,
`transmit` (and `alpha`) each take a value and set one channel, after an optional `rgb...`/`color`
part, so `let C = color red 1 green 0.5 filter 0.1;` works in a `let` as in a body. A channel word
with no value is left as a keyword (`transmit all 0.5` in an image map). `filter` directly followed
by `(` is the built-in `filter(A, F)`. A `{` after `=>` always starts a lambda body; write
`=> ({ ... })` for a dictionary.

The conditional operator replaces the mid-expression `#if` splice:

```pov
// classic:  color rgb <1, #if (Hot) 0.2 #else 0.8 #end, 0>
color rgb <1, Hot ? 0.2 : 0.8, 0>
```

A binding may take a run of blocks: `let T = texture { ... } texture { ... };` binds a fragment of
both (a layered texture); `...T` or `T` in an object body emits both, as classic `texture { T }`
does. A classic include or a classic macro call sees the 4.0 file-scope bindings as classic
identifiers: before handing one to the classic parser the evaluator declares every file-scope
binding that changed since (floats, vectors, colours, strings, non-empty arrays, dictionaries,
layered textures, and objects and other handles by copy; functions, `null`, undeclarable blocks
and other fragments are not declared). A macro call inside a block body gets the declarations of
the bindings 4.0 changed since they were last declared, in the body just before it. From then until
the block reaches the classic parser, classic owns those values: a second call in the same block
sees what the first left, unless 4.0 assigns the binding again in between. After a classic include,
a top-level macro call, a macro call used as a value, or a block containing macro calls, floats,
vectors and strings the classic code changed are read back into their 4.0 bindings; other classic
changes are seen only through names that no 4.0 binding shadows.

### Blocks

`keyword { body }` is a block. Its body is an output, so it may contain statements:

```pov
union {
  for (I = 0 to 9) { sphere { x * I, 0.4 } }
  if (Shiny) { finish { phong 1 } }
  ...Extras
  translate y
}
```

A statement's emitted items land where it stands, so `scale if (Big) { 2 } else { 1 }` supplies the
operand of `scale`.

The grammar does not know each block's legal contents; the renderer's object builders check that, as
they do for classic scenes.

A value used as an item in a block body is emitted by kind: a number, vector, colour or string is
emitted as itself; an object becomes `object { V }`; a texture inside `texture { }` is `V`, and so on.
A value directly after the keyword of its own kind is emitted bare (`transform T`), and so is an
object written as the first item of an object block other than a CSG or `light_group`, which
classic reads as a copy to modify (`light_source { Lamp translate y }`); `pigment_pattern { P }`
takes a pigment as `pigment { }` does, and the word after `mix` is a keyword (`mix add`). Values spliced
from a spread or a call are separated by commas, so `sphere_sweep { linear_spline 4, ...Points }`
reads as written; an array must be spread (`...A`), not placed in a body whole. An uncalled
function word in a body is the keyword itself (`filter 0.5`, while `filter(A, F)` is the built-in).

### Render-time functions

`function { ... }` and `function(x, y, z) { ... }` blocks keep the classic render-time function
language (they compile to the function VM). Inside them, names bound by `let` to numbers are
substituted as constants and names bound to render-time functions are called, as in classic.
A 4.0 lambda is construct-time only and cannot be called from a render-time function.
Built-in constants (`x`, `pi`, `clock`, ...) are not callable, so `translate x (2)` is an error.
`trace(O, P, D, N)` sets the 4.0 variable `N` to the normal, binding it if needed.
The body uses 4.0 operators; it is lowered to the classic function language (`==` to `=`, `&&` to
`&`, `C ? A : B` to `select(-abs(C), A, B)`, `!C` to `select(-abs(C), 0, 1)`), with `let` floats
substituted as their values. The image-size form `function 300, 300 { pigment { ... } }` is kept
as well.

The fork's portal target block `portal { ... to { ... } }` is a block named `to`; `to` stays a
keyword of `for (I = A to B)` there.

### Built-ins added by 4.0

`len(A)`, `range(A, B[, S])` (inclusive, classic `#for` stepping), `map(A, F)`, `filter(A, F)`,
`push(A, V)` (returns a new array), `keys(D)`, `defined(Name)` (false when unbound or `null`), `debug(S)`,
`warning(S)`, `error(S)`, `array(N[, Fill])`.

## Renderer front end

The renderer picks the 4.0 front end by the input file's extension. It parses each `.pov4`/`.inc4`
file once with `libraries/tree-sitter-pov4`, lowers the tree to a compact internal tree, and runs
it. Plain values (null, floats, vectors, colours, strings, arrays, dictionaries, functions,
fragments) live in the evaluator; everything else is a *handle* to a classic identifier.

Objects and other blocks are still built by the classic parser. The evaluator lowers each
manifested item, and each block bound by `let` (`#declare __pov4_N = ...;`), to classic SDL with
every expression already evaluated, and hands it to the classic parser in order, in the same
parser, so classic defaults, `#version`-dependent behaviour and shared mesh data apply unchanged.
The scene starts with `#version 4.0;`. Arithmetic, vector and string built-ins run natively with
classic semantics; `seed`/`rand` call the classic generator; built-ins that need scene state
(`trace`, `inside`, `min_extent`, `str`, ...) and calls of classic functions, splines and macros
are evaluated by the classic parser (`#declare __pov4_N = <call>`, which needs no `;` there, so a
macro may end with directives after the object it makes) and the result read back.
`global_settings`, `background`, `default`, `photons`, `radiosity` and `interior_texture` cannot be
declared in classic SDL, so binding one with `let` keeps its text and manifests it where used.

Errors in 4.0 code are reported as `file:line:column: message`. Errors the classic parser finds in
lowered SDL point at the file and line of the 4.0 item they came from: lowered SDL is handed over
in batches of up to 1 MiB per file with a line map, so a loop does not split it. Numbers are written
in their shortest round-trip form, infinities as `1e400` and `-1e400`; a NaN is an error.

Limits: the evaluator runs on its own 256 MiB stack (Linux and Windows; elsewhere on the parser
thread's stack). Function calls nest at most 10,000 deep (classic allows about 100 nested macro
calls). Statements, expressions, blocks and nested arrays and dictionaries nest at most 100,000
levels in total, counting every active call and the parse of an `.inc4` included at that point.
`.inc4` includes nest at most 100 deep (each file is parsed once and reused). An array holds at most
256 MiB of values (4,194,304 elements on 64-bit builds), which bounds literals, comprehensions,
`range`, `array` and `push`. `int`, `div`, `chr`, `bitwise_*` and random streams reject values
outside the integer range, and `div` by zero is an error. A long-running 4.0 loop reports parse
progress and can be cancelled like a classic parse. A 5-component vector is printed with `rgbft`
only when it is a colour (from a colour expression or a classic colour identifier); arithmetic,
unary minus and component assignment keep it a colour, comparisons and `!` do not.

Testing aids (not language features):

| INI option | Switch | Effect |
|---|---|---|
| `Pov4_Lowered_File=f.pov` | `+GLf.pov` | write the classic SDL the scene was lowered to; rendering it gives the same image |
| `Pov4_Version=3.7` | `+ML3.7` | run a 4.0 scene under an older language version (image B of the evaluation corpus) |

## Mixing languages

A `.pov4` file may `include` a classic file. The classic file is parsed by the classic parser into
the same scene; its declarations become visible to the 4.0 program by name, and its macros can be
called from 4.0 code (the call is handed to the classic parser at that point: in place inside a
block body, as a scene item at the top level, through `#declare` where a value is needed). A classic
include in a block body is handed over in place too: `union { include "parts.inc"; }`. This lets 4.0 scenes
use the standard include library unchanged. A classic file cannot include a 4.0 file.

## Codemod

`tools/language/codemod/codemod.mjs <in>... -o <out-dir> [--includes classic|convert] [-L dir] [--json report]`
converts well-formed classic files to 4.0 with text edits on the classic tree: directives and operators are
rewritten, everything else (comments, layout, object bodies) is kept byte for byte. A `.pov` input becomes
`.pov4`; with `--includes convert` every include it reaches (scene folder, then `-L` dirs, then
`distribution/include`) becomes an `.inc4` beside it (an include named with `..` or an absolute path is written by its
file name, so nothing lands outside the output folder; names stay unique within each output folder), and includes
that are refused or not found stay classic.

| Classic | 4.0 |
|---|---|
| `#declare X = E;` / `#local X = E;` | `let X = E;`, `X = E;` or `global X = E;` (below) |
| `#declare A[I] = E;` / `D.K = E` | `A[I] = E;` / `D.K = E;` |
| `#macro M(A, B) ... #end` | `fn M(A, B) { ... }` |
| `#if (C) ... #elseif (D) ... #else ... #end` | `if (C) { } else if (D) { } else { }` |
| `#ifdef (X)` / `#ifndef (X)` | `if (defined(X))` / `if (!defined(X))` |
| `#while (C) ... #end` | `while (C) { }` |
| `#for (I, A, B, S) ... #end` | `for (I = A to B step S) { }` |
| `#break` in a loop | `break;` |
| `#switch` / `#case` / `#range` / `#break` / `#else` | `if` chain on the value, or on `let Switch_Value = V;` unless V is a plain name |
| `#include "f.inc"` | `include "f.inc4";` (converted) or `include "f.inc";` (classic) |
| `#debug` / `#warning` / `#error` | `debug(...);` / `warning(...);` / `error(...);` |
| `#default { ... }` | `default { ... }` |
| `#version N;` | removed, with a `#declare V = version;` used only to restore it |
| `#undef X` | `X = null;`, or `let X = null;` / `global X = null;` where 4.0 has no binding to assign |
| `array[N] { a, b }` | `[a, b]`, padded with `null` (or `array(M)` rows for `array[N][M]`) to a literal N or with `...array(N - 2)`; `array[N]` alone becomes `array(N)`, `array[N][M]` becomes `array(N, array(M))` |
| `dictionary { ["k"]: v, .k2: w, [K]: u }` | `{ k: v, k2: w, [K]: u }`; keys that are not plain names or are reserved stay strings |
| `D.step` where `step` is a 4.0 reserved word | `D["step"]` |
| `=` / `&` / `\|` in expressions | `==` / `&&` / `\|\|`, parenthesised where 4.0 precedence would regroup them: classic `&` and `\|` share one left-associative level, as do `=` and `<` |
| `#if` inside an expression | `C ? A : B` (a comparison C keeps its parentheses), parenthesised inside an operator; refused (`splice-precedence`) when a branch would regroup with the operator around it, as in `3 * #if (A) 1 + 2 ...` |
| a single-value `#if` among block items, `scale #if (A) 2 #else 3 #end` | `scale A ? 2 : 3` |
| `<1 2 3>`, `<1, 2,>`, `M(1 <2, 3>)`, `dictionary { ["a"]: 1 ["b"]: 2 }` | `<1, 2, 3>`, `<1, 2>`, `M(1, <2, 3>)`, `{ a: 1, b: 2 }`; an empty argument, `M(1,,3)`, is refused (`empty-argument`) |
| a string with a raw line break | the break written as `\n` |
| `deprecated` on a declaration | dropped |
| identifiers that are 4.0 reserved words | renamed with trailing `_`, as many as make the name unused in the scene and its includes |

Scope follows `Parse_Declare` and the symbol stack. At file level both directives bind in the file:
`let X` unless an earlier unconditional binding in the same or an enclosing statement list makes it `X =`.
In a macro, `#local X` is `let X` (or `X =` after a binding, or for a parameter); `#declare X` is `X =` for a
parameter or a dominating local and `global X =` otherwise. An include's top-level `#local` becomes `let` and is
noted (`include-local`): classic drops it at the end of the include, an `.inc4` leaves it in the includer's scope.
A file-level binding whose name a classic include reads is noted too (`read-by-classic-include`): the classic
include must see the 4.0 binding, as it saw the classic one.

A file is **well-formed** when every directive sits at a statement or block-item boundary, or inside an
expression where it is a complete conditional. Anything else is refused, with `file:line:column`, a reason and
the conversion of that file skipped:

| Reason | Why |
|---|---|
| reasons from `corpus.mjs` | syntax errors, partial macros, file I/O, splices, stray items, ambiguous indexes |
| `by-reference-argument` | a macro assigns a parameter the caller passed as a variable that is read again (classic writes it back; 4.0 passes by value) |
| `dynamic-scope` | a macro reads or `#declare`s a name that is local to a macro calling it (classic resolves names on the call stack) |
| `declare-maybe-local` | `#declare X` in a macro that makes `X` local on some paths only |
| `layered-texture` | `#declare T = texture { } texture { }` has no single 4.0 value |
| `tuple`, `optional-parameter`, `optional-declare`, `scope-prefix` | tuples, `optional`, `local.`/`global.` have no 4.0 form |
| `include-local-shadow` | with `--includes convert`, an include's top-level `#local X` where the scene or another include binds `X` |
| `called-by-classic-include` | a scene macro that a classic include calls back (a classic file cannot call a 4.0 function) |
| `splice-precedence` | see the `#if` row above |
| `array-size` | an array initializer with directives and a size, whose element count is not known |
| `switch-fallthrough`, `break-placement` | a `#case` that runs on into the next, a `#break` that is not the last item of a clause or outside a loop |
| `directive-in-dictionary`, `nested-comment` | no 4.0 form |

## Classic front end

The classic parser keeps its object builders and expression evaluator. Its token source changes:
each file is lexed once into a compact token array (reserved words resolved, identifier names
interned, numbers converted). `#while`, `#for`, macro calls and macro returns move an index into
that array instead of seeking the file and lexing it again.

## Evaluation corpus

`tools/language/eval/eval.mjs` converts every scene under `distribution/scenes` to 4.0 and renders three
images per scene at 160×120 without anti-aliasing (a scene's first animation frame is its `clock = 0` frame):

| Image | What |
|---|---|
| A | the classic scene as shipped |
| B | the converted 4.0 scene, run with the classic scene's language version (`--b-args`, `{version}` is its first `#version`; none passes nothing) |
| C | the converted 4.0 scene, run as 4.0 |

A≠B is a conversion or evaluator defect, unless the converted scene is plainly right and the
original was not (flagged for review, not assumed). B≠C is the effect of 4.0 defaults on that scene;
it is reported for review, since a different image is often the better one.

`eval all --out DIR --povray BIN` runs the three steps; `plan` (convert, list the renders), `render` (local, with
a time limit) and `report` also run alone, so another runner can do the renders in between. Pairs compare pixel
for pixel, alpha included; `noise-level` means no channel differs by more than 8 levels, under 0.5% of pixels by
more than 2, and the mean difference is under 0.5. Each scene is `identical`, `noise-level`, `different`,
`conversion-refused`, `eval-error`, `render-error` or `timeout` (`a-only` when B and C were not rendered);
`DIR/runs/<id>/index.html` shows the flagged ones with A, B, C and diff maps, and `DIR/index.html` lists the runs.

Each render's status records a hash of its inputs: the scene's folder (every file, recursively), the classic files
it includes, the converted files, the options and the binary (`--classic-id` for A, `--bc-id` or the
`--povray` file for B and C). Planning into an existing run drops statuses whose inputs
changed, `--force` also retries errors and timeouts, and `--only` limits which kinds render. A renders are cached
under `DIR/cache` by that hash plus `--classic-id` and reused by later runs, `--only B,C` ones included. Files a
scene reads from elsewhere (an image map in a library folder) are not in the hash.
