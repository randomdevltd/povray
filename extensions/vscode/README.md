# POV-Ray for VS Code

Syntax highlighting, block-aware completion and on-save diagnostics for POV-Ray scene
files — both classic `.pov`/`.inc` and 4.0 `.pov4`/`.inc4`.

## Features

- **Highlighting** for the shared reserved-word classes: statements, block keywords,
  block items, built-ins, colour operators and channels, numbers, strings and comments.
  The TextMate grammar is generated from the same word tables the parsers use.
- **Completion** driven by the scene-language block tables: inside `finish { … }` you
  are offered `phong`, `ambient`, `reflection { … }` and every other legal item of that
  block, including nested sub-blocks (`reflection` inside `finish` offers `fresnel`).
  Outside blocks you get block keywords, snippets, and identifiers declared in the
  document (`let`, `fn`, classic `#declare`/`#local`). After a `.` you get vector and
  colour components.
- **Diagnostics on save**: if `povray.executablePath` is set, saving a scene runs the
  renderer as a syntax check (16×16, no display) and turns every `file:line:column`
  message into an entry in the Problems panel — including the 4.0 static checker's
  property-name and typo diagnostics.

## Settings

| Setting | Effect |
|---|---|
| `povray.executablePath` | povray binary used for on-save diagnostics; empty disables them |
| `povray.includePath` | library search paths (`+L`) passed to the renderer |
| `povray.declare` | `Declare=name=value` entries passed to the renderer |

Notes: columns in diagnostics are byte-based and clamped to the line, and a check run
is killed after 30 seconds. Messages from include files are attached to the include
file when it can be resolved next to the document.

## Packaging and development

The grammar and block tables are generated; never edit them directly:

    node tools/language/vscode.mjs
    node --test extensions/vscode/test/pure.test.mjs extensions/vscode/test/generate.test.mjs

Package a `.vsix` from the repository root:

    cd extensions/vscode && npx @vscode/vsce package

and install it with *Extensions: Install from VSIX…*.
