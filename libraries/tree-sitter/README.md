# Tree-sitter C runtime

Vendored from Tree-sitter v0.25.10, commit
`da6fe9beb4f7f67beb75914ca8e0d48ae48d6406`:
https://github.com/tree-sitter/tree-sitter/tree/v0.25.10

The native runtime (`lib/src`, excluding the unused WebAssembly libc) and public
C API (`lib/include`) are unmodified. Build only `lib/src/lib.c`, which includes
the individual implementation files. WebAssembly execution is disabled.

Runtime files retain upstream whitespace for byte-for-byte comparison during
upgrades. The scoped `.gitattributes` rule disables end-of-line and end-of-file
whitespace warnings only for this vendored implementation; generated grammar and
POV-Ray integration files follow the repository's normal whitespace checks.

Tree-sitter is MIT licensed; see `LICENSE`. Its bundled ICU Unicode headers have
their own notice in `lib/src/unicode/LICENSE`; `lib/src/portable/endian.h` retains
its public-domain notice. Keep these notices in source distributions.

The generated tag grammar uses the matching 0.25.10 CLI and language ABI 15.
Ordinary POV-Ray builds require only a C11 compiler and the existing C++ compiler,
without Node.js, Rust, network access, or grammar generation.
