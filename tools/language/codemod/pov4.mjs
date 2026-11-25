// SPDX-License-Identifier: AGPL-3.0-or-later
// Rewrites in place that keep 4.0 scenes current: the spellings 4.0 has replaced, edited where the 4.0 parse finds them.
import { createRequire } from 'node:module';
import { join } from 'node:path';
import { root } from '../classic.mjs';
import { isPhotonMemory } from './convert.mjs';

const grammarDir = join(root, 'libraries/tree-sitter-pov4');
const require = createRequire(join(grammarDir, 'package.json'));
const Parser = require('tree-sitter');
const parser = new Parser();
parser.setLanguage(require(grammarDir));

// The text with each replaced spelling rewritten, and where each was.
export function rewritePov4(text) {
  const tree = parser.parse((index) => (index < text.length ? text.slice(index, index + 65536) : null));
  const edits = [];
  const walk = (node) => {
    if (isPhotonMemory(node)) edits.push({ node, to: 'photon_memory' });
    for (const child of node.children) walk(child);
  };
  walk(tree.rootNode);
  let out = text;
  for (const { node, to } of [...edits].reverse()) out = out.slice(0, node.startIndex) + to + out.slice(node.endIndex);
  return { text: out, changes: edits.map(({ node, to }) => ({ line: node.startPosition.row + 1, column: node.startPosition.column + 1, from: node.text, to })) };
}
