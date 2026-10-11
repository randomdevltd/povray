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
