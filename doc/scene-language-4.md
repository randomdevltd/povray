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
  error if there is none. `A[I] = E;` and `D.K = E;` assign into a container.
- `global X = E;` binds `X` at file scope from anywhere. This is what classic `#declare` inside a
  macro does.
- `let X;` declares `X` as `null`.

An `.inc4` include runs in the including file's scope (as classic includes do).

## Syntax

### Lexical

- Comments `// ...` and `/* ... */` (not nested).
- Numbers as classic: `1`, `.5`, `1e-3`. Strings as classic, same escapes.
- Identifiers as classic; they may not be a reserved word. Reserved words are the classic reserved
  words plus `let`, `fn`, `global`, `return`, `null`, `in`, `to`, `step`. (`if`, `else`, `for`,
  `while`, `include`, `true`, `false` are already classic reserved words.)
- `;` after an item is allowed and ignored. `let`, assignment, `global`, `include` and `return`
  statements end with `;`.

### Statements

```pov
include "colors.inc4";             // 4.0 include: runs in this scope
include "textures.inc";            // classic include: see "Mixing languages"

let X = 1;   X = X + 1;   global Seed = seed(42);
fn F(A, B = 2) { A * B }

if (X > 1) { ... } else if (X < 0) { ... } else { ... }
while (X < 10) { ... }
for (I = 0 to 10) { ... }          // inclusive, step 1 (classic #for semantics exactly)
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

Changes from classic expressions: equality is `==` (classic `=`), logical operators are `&&` / `||`
(classic `&` / `|`). Everything else keeps classic meaning, including float→vector promotion,
`.x/.y/.z/.t/.red/.green/.blue/.filter/.transmit` members, `x`, `y`, `z`, `t`, `u`, `v`, `pi`,
`clock` and every classic built-in function.

Literals:

```pov
<1, 2, 3>                         // vector (2–5 components) as classic
rgb <1, 0.5, 0>                   // colour expressions as classic
[1, 2, 3]  [...A, 4]              // array; spreads
[for (I = 0 to 9) { I * I }]       // comprehension: the array is an output
{ A: 1, "b c": 2, ...D }          // dictionary; string or identifier keys; spreads
null  true  false
```

An array literal is an output, so `if`/`for`/`let` work inside it, and items are separated by commas
or juxtaposition. In a block body a bracket group is emitted as-is, so classic map entries keep
working: `color_map { [0 color Red] [1 color Blue] }`.

The conditional operator replaces the mid-expression `#if` splice:

```pov
// classic:  color rgb <1, #if (Hot) 0.2 #else 0.8 #end, 0>
color rgb <1, Hot ? 0.2 : 0.8, 0>
```

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

The grammar does not know each block's legal contents; the renderer's object builders check that, as
they do for classic scenes.

A value used as an item in a block body is emitted by kind: a number, vector, colour or string is
emitted as itself; an object becomes `object { V }`; a texture inside `texture { }` is `V`, and so on.

### Render-time functions

`function { ... }` and `function(x, y, z) { ... }` blocks keep the classic render-time function
language (they compile to the function VM). Inside them, names bound by `let` to numbers are
substituted as constants and names bound to render-time functions are called, as in classic.
A 4.0 lambda is construct-time only and cannot be called from a render-time function.

### Built-ins added by 4.0

`len(A)`, `range(A, B[, S])` (inclusive, classic `#for` stepping), `map(A, F)`, `filter(A, F)`,
`push(A, V)` (returns a new array), `keys(D)`, `defined(Name)`, `debug(S)`, `warning(S)`,
`error(S)`, `array(N[, Fill])`.

## Mixing languages

A `.pov4` file may `include` a classic file. The classic file is parsed by the classic parser into
the same scene; its declarations become visible to the 4.0 program by name, and its macros can be
called from 4.0 code (the call is handed to the classic parser at that point). This lets 4.0 scenes
use the standard include library unchanged. A classic file cannot include a 4.0 file.

## Codemod

`tools/language/codemod` converts a well-formed classic file to 4.0:

| Classic | 4.0 |
|---|---|
| `#declare X = E;` / `#local X = E;` | `let X = E;` (or `X = E;` / `global X = E;` by scope) |
| `#macro M(A, B) ... #end` | `fn M(A, B) { ... }` |
| `#if (C) ... #elseif (D) ... #else ... #end` | `if (C) { } else if (D) { } else { }` |
| `#ifdef (X)` / `#ifndef (X)` | `if (defined(X))` / `if (!defined(X))` |
| `#while (C) ... #end` | `while (C) { }` |
| `#for (I, A, B, S) ... #end` | `for (I = A to B step S) { }` |
| `#switch` / `#case` / `#range` / `#break` | `if` chain on a temporary |
| `#include "f.inc"` | `include "f.inc4";` (converted) or `include "f.inc";` (classic) |
| `#debug` / `#warning` / `#error` | `debug(...)` / `warning(...)` / `error(...)` |
| `#default { ... }` | `default { ... }` |
| `#version N;` | removed (the extension decides) |
| `array[N] { a, b }` | `[a, b]`; `array[N]` alone becomes `array(N)` |
| `dictionary { ["k"]: v, .k2: w }` | `{ k: v, k2: w }` |
| `=` / `&` / `\|` in expressions | `==` / `&&` / `\|\|` |
| `#if` inside an expression | `C ? A : B` |
| identifiers that are 4.0 reserved words | renamed with a trailing `_` |

A file is **well-formed** when every directive sits at a statement or block-item boundary, or inside
an expression where it is a complete conditional (convertible to `?:`). Files that splice partial
blocks (`#if (A) pigment { #else texture { pigment { #end ...`), build tokens by macro, or use
`#fopen`/`#read`/`#write` are reported with their location and reason, not converted.

## Classic front end

The classic parser keeps its object builders and expression evaluator. Its token source changes:
each file is lexed once into a compact token array (reserved words resolved, identifier names
interned, numbers converted). `#while`, `#for`, macro calls and macro returns move an index into
that array instead of seeking the file and lexing it again.

## Evaluation corpus

`tools/language/eval` converts every scene under `distribution/scenes` to 4.0 and renders three
images per scene:

| Image | What |
|---|---|
| A | the classic scene as shipped |
| B | the converted 4.0 scene, run with the classic scene's language version |
| C | the converted 4.0 scene, run as 4.0 |

A≠B is a conversion or evaluator defect, unless the converted scene is plainly right and the
original was not (flagged for review, not assumed). B≠C is the effect of 4.0 defaults on that scene;
it is reported for review, since a different image is often the better one.
