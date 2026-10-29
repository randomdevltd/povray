// SPDX-License-Identifier: AGPL-3.0-or-later
const assert = require("node:assert");
const { test } = require("node:test");

const Parser = require("tree-sitter");

test("parses a 4.0 scene", () => {
  const parser = new Parser();
  parser.setLanguage(require("."));
  const tree = parser.parse("let R = 1;\nsphere { <0, 0, 0>, R translate -x }\n");
  assert.equal(tree.rootNode.hasError, false);
  assert.deepEqual(tree.rootNode.namedChildren.map((n) => n.type), ["let_statement", "block"]);
});


test("parses contextual photon quality expressions without reserving the name", () => {
  const parser = new Parser();
  parser.setLanguage(require("."));
  for (const expression of ["quality", "(quality * 2) / 2", "+quality", "-1", "quality(0.125)"]) {
    const tree = parser.parse(`let quality = 0.125; global_settings { photons { method 2 quality ${expression} } }`);
    assert.equal(tree.rootNode.hasError, false, expression);
    assert.equal(tree.rootNode.namedChild(0).childForFieldName("name").type, "identifier");
  }
  assert.equal(parser.parse("fn quality(Q) { Q } let Q = quality(1);").rootNode.hasError, false);
});


test("parses independent refraction without reserving the name detail", () => {
  const parser = new Parser();
  parser.setLanguage(require("."));
  const tree = parser.parse(`let detail = 0.06;
    interior { refraction { function { x + detail } /* spacing */ detail detail } refraction off }`);
  assert.equal(tree.rootNode.hasError, false);
});


test("parses light units after brightness while nits and lumens stay free names", () => {
  const parser = new Parser();
  parser.setLanguage(require("."));
  const tree = parser.parse(`let nits = 2; let lumens = scale_lumens(800, 0.5);
    sphere { 0,1 light { brightness 8 nits samples 4 } }
    sphere { 0,1 light { brightness lumens lumens bounce { passes 2 } } }`);
  assert.equal(tree.rootNode.hasError, false);
});


test("parses a bare media emission source selector beside brightness", () => {
  const parser = new Parser();
  parser.setLanguage(require("."));
  const tree = parser.parse(`sphere { 0, 1 interior {
    media { emission rgb <100, 40, 10> density { spherical } }
    light { emission brightness 12 lumens }
  } }`);
  assert.equal(tree.rootNode.hasError, false);
  assert.equal(tree.rootNode.descendantsOfType("keyword").filter((n) => n.text === "emission").length, 2);
});


test("parses surface bounce blocks while leaving bounce and passes free", () => {
  const parser = new Parser();
  parser.setLanguage(require("."));
  const tree = parser.parse(`let bounce = 2; let passes = 3;
    sphere { 0,1 light { bounce /* order */ { passes passes } } }
    sphere { 0,1 light { bounce } }`);
  assert.equal(tree.rootNode.hasError, false);
  assert.equal(tree.rootNode.namedChild(0).childForFieldName("name").type, "identifier");
  assert.equal(tree.rootNode.namedChild(1).childForFieldName("name").type, "identifier");
  assert.ok(tree.rootNode.descendantsOfType("block").some((n) => n.childForFieldName("name").text === "bounce"));
});
