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


test("parses independent refraction without reserving contextual field and detail names", () => {
  const parser = new Parser();
  parser.setLanguage(require("."));
  const tree = parser.parse(`let field = 2; let detail = 0.06;
    interior { refraction { strength -0.3 field /* pattern */ { function { x + field } } detail detail } }`);
  assert.equal(tree.rootNode.hasError, false);
});
