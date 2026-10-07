# Render tag grammar

This is the minimal Tree-sitter grammar for POV-Ray tag declarations and Boolean
filters. It intentionally contains no full SDL scene grammar. `src/parser.c`,
`src/grammar.json`, `src/node-types.json`, and `src/tree_sitter/*` are generated
and checked in. The renderer parses only a filter expression, lowers its concrete
syntax tree to a small immutable expression tree, and releases the Tree-sitter
parser and concrete tree immediately. Copies share the immutable expression.

The grammar accepts a bare filter, `filter_tags { expression }`, or a standalone
`tags { "tag", "tag" }` declaration. The renderer's `ParseTagFilter` API accepts
only the first two. The existing SDL parser retains responsibility for object
placement, declarations, macro execution, and resolving SDL string expressions.

Atoms are quoted tag patterns, `any` (at least one tag), and `none` (no tags).
`*` matches zero or more bytes; all other pattern characters are literal and
matching is anchored and case sensitive. Operators bind in the order `!`, `&`,
`|`; parentheses override precedence. Strings support `\"`, `\\`, `\n`, `\r`,
`\t`, `\b`, and `\f`. Filters reject syntax recovery, missing nodes, and trailing
input. The renderer limits a filter to 1 MiB and expression tree depth to 256
to bound pathological input. Whitespace, comments and redundant parentheses are
removed from the cache key; logically equivalent algebraic rewrites are not
promised to share a key.

For grammar maintenance, use Node.js and the pinned Tree-sitter CLI 0.25.10:

```sh
npm ci
npm run generate
npm test
```

The runtime is pinned to the same release in `../tree-sitter`. Regenerate and
review the grammar artifacts together whenever upgrading either component.
Regular renderer builds compile the committed C artifacts directly.
