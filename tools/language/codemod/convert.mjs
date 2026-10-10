// SPDX-License-Identifier: AGPL-3.0-or-later
// Classic to 4.0 conversion: CST-driven text edits plus the scope analysis that decides let / X = / global X =.
import { existsSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { basename, dirname, isAbsolute, join, normalize, resolve } from 'node:path';
import { at, contentItems, isExpressionIf, parse, root, scan } from '../classic.mjs';

const NEW_RESERVED = new Set(['let', 'fn', 'return', 'null', 'in', 'step', 'continue']);
const RESERVED = new Set([...JSON.parse(readFileSync(join(root, 'tools/language/keywords.json'), 'utf8'))
  .map((k) => k.word), ...NEW_RESERVED]);
const OPERATORS = { '=': '==', '&': '&&', '|': '||' };
const PRECEDENCE_4 = { '||': 3, '&&': 4, '==': 5, '!=': 5, '<': 6, '<=': 6, '>': 6, '>=': 6, '+': 7, '-': 7, '*': 8, '/': 8 };
const PRECEDENCE_CLASSIC = { '|': 2, '&': 2, '<': 3, '<=': 3, '=': 3, '!=': 3, '>=': 3, '>': 3, '+': 4, '-': 4, '*': 5, '/': 5 };
const SINGLE_VALUES = new Set(['number', 'string', 'identifier', 'constant', 'vector', 'parenthesized_expression',
  'index_expression', 'member_expression', 'binary_expression', 'unary_expression']);
const COMMENTS = ['line_comment', 'block_comment'];

const body = (node) => node.childrenForFieldName('body');
const items = (nodes) => nodes.filter((n) => !COMMENTS.includes(n.type));
const token = (node, type) => node.children.find((c) => c.type === type);
const lastToken = (node, type) => node.children.findLast((c) => c.type === type);
const same = (a, b) => !!a && !!b && a.startIndex === b.startIndex && a.endIndex === b.endIndex && a.type === b.type;
const hasAncestor = (node, type) => { for (let p = node.parent; p; p = p.parent) if (p.type === type) return true; return false; };
const operator = (node) => node.childForFieldName('operator').type;
const isComparison = (node) => node.type === 'conditional_expression' ||
  (node.type === 'binary_expression' && PRECEDENCE_CLASSIC[operator(node)] <= 3);
const isMacroCall = (node) => node.type === 'call_expression' && node.childForFieldName('function').type === 'identifier';

function lvalueRoot(node) {
  while (node.type === 'index_expression' || node.type === 'member_expression') node = node.childForFieldName('object');
  return node.type === 'identifier' ? node : null;
}

// Whether a macro's expansion starts with a block or item, which classic reads as a whole argument.
function yieldsItems(info, macros, seen = new Set()) {
  if (!info || seen.has(info)) return false;
  seen.add(info);
  const first = contentItems(body(info.node))[0];
  if (['block', 'keyword', 'bracket_group'].includes(first?.type)) return true;
  return !!first && isMacroCall(first) && yieldsItems(macros.get(first.childForFieldName('function').text), macros, seen);
}

function argumentNodes(list) {
  const slots = [[]];
  for (const child of list.children.slice(1, -1)) {
    if (child.type === ',') slots.push([]);
    else if (child.isNamed && !COMMENTS.includes(child.type)) slots.at(-1).push(child);
  }
  return slots;
}

// Per-file facts the conversion of any file in the same closure needs: macros, their scope use, includes.
function analyse(unit) {
  const macros = new Map();
  const macroAt = new Map();
  const includes = [];
  const declared = new Map();
  const reads = new Map();
  const topReads = new Set();
  const topLocals = new Set();
  const globals = new Set();
  const identifiers = new Set();
  const note = (map, name, node) => { if (!map.has(name)) map.set(name, []); map.get(name).push(node); };

  const walk = (node, macro, bound) => {
    if (node.type === 'identifier') identifiers.add(node.text);
    switch (node.type) {
      case 'macro_directive': {
        const params = node.childForFieldName('parameters').namedChildren.filter((p) => p.type === 'parameter')
          .map((p) => p.childForFieldName('name')?.text);
        const info = { name: node.childForFieldName('name').text, node, params, locals: new Set(), assigned: new Set(),
          calls: new Set(), reads: [], unit };
        macros.set(info.name, info);
        macroAt.set(node.startIndex, info);
        for (const child of node.namedChildren) if (child.type !== 'parameter_list') walk(child, info, new Set());
        for (const p of params) identifiers.add(p);
        return;
      }
      case 'declare_directive': {
        const target = node.childForFieldName('target');
        const rootId = lvalueRoot(target);
        const isLocal = node.childForFieldName('kind').type === '#local';
        if (rootId) {
          identifiers.add(rootId.text);
          note(declared, rootId.text, node);
          const param = macro ? macro.params.indexOf(rootId.text) : -1;
          if (param >= 0) macro.assigned.add(param);
          else if (macro && isLocal && same(rootId, target)) macro.locals.add(rootId.text);
          else if (!macro && isLocal && same(rootId, target)) topLocals.add(rootId.text);
          else if (!isLocal && same(rootId, target)) globals.add(rootId.text);
        }
        if (target.type !== 'identifier') for (const c of target.namedChildren) if (!same(c, rootId)) walk(c, macro, bound);
        walk(node.childForFieldName('value'), macro, bound);
        return;
      }
      case 'for_directive':
        identifiers.add(node.childForFieldName('variable').text);
        if (macro) macro.locals.add(node.childForFieldName('variable').text);
        for (const c of node.namedChildren) if (!same(c, node.childForFieldName('variable'))) walk(c, macro, bound);
        return;
      case 'include_directive': {
        const file = node.childForFieldName('file');
        if (file.type === 'string') includes.push({ node, name: JSON.parse(file.text.replace(/\\(?!["\\])/g, '\\\\')) });
        return;
      }
      case 'function_block': {
        const params = node.childForFieldName('parameters');
        const inner = new Set(bound);
        for (const p of params?.namedChildren ?? []) inner.add(p.childForFieldName('name')?.text);
        if (!params) for (const p of ['x', 'y', 'z']) inner.add(p);
        for (const c of node.namedChildren) if (!same(c, params)) walk(c, macro, inner);
        return;
      }
      case 'call_expression': {
        const fn = node.childForFieldName('function');
        if (fn.type === 'identifier') {
          identifiers.add(fn.text);
          if (macro) macro.calls.add(fn.text);
          note(reads, fn.text, fn);
        }
        walk(node.childForFieldName('arguments'), macro, bound);
        return;
      }
      case 'member_expression':
        walk(node.childForFieldName('object'), macro, bound);
        return;
      case 'dictionary_entry':
        walk(node.childForFieldName('value'), macro, bound);
        if (node.childForFieldName('key').type !== 'identifier') walk(node.childForFieldName('key'), macro, bound);
        return;
      case 'block':
        for (const c of node.namedChildren) if (!same(c, node.childForFieldName('name'))) walk(c, macro, bound);
        return;
      case 'undef_directive':
        note(reads, node.childForFieldName('name').text, node.childForFieldName('name'));
        return;
      case 'fopen_directive': case 'fclose_directive': case 'tag_filter':
        return;
      case 'identifier':
        if (bound.has(node.text)) return;
        note(reads, node.text, node);
        if (macro) macro.reads.push(node);
        else topReads.add(node.text);
        return;
      default:
        for (const c of node.namedChildren) walk(c, macro, bound);
    }
  };
  for (const child of unit.tree.rootNode.namedChildren) walk(child, null, new Set());
  return { macros, macroAt, includes, declared, reads, topReads, topLocals, globals, identifiers };
}

export class Program {
  constructor({ includePath = [join(root, 'distribution/include')], includes = 'classic' } = {}) {
    this.includePath = includePath;
    this.mode = includes;
    this.units = new Map();
    this.names = new Map();
  }

  load(path) {
    path = resolve(path);
    if (!this.units.has(path)) {
      const text = readFileSync(path, 'latin1');
      const tree = parse(text);
      const unit = { path, text, tree, wf: scan(tree, text) };
      unit.info = analyse(unit);
      this.units.set(path, unit);
    }
    return this.units.get(path);
  }

  resolveInclude(name, sceneDir) {
    for (const dir of [sceneDir, ...this.includePath]) {
      const candidate = resolve(dir, name);
      if (existsSync(candidate)) return candidate;
    }
    return null;
  }

  // The scene and every file it includes, includes first; `names` maps each include to its .inc4 name, kept under
  // the output folder and unique within the closure.
  closure(scene, folder = dirname(resolve(scene))) {
    const scenePath = resolve(scene);
    const order = [];
    if (!this.names.has(folder)) this.names.set(folder, new Map());
    const names = this.names.get(folder);
    const seen = new Set();
    const visit = (path) => {
      if (seen.has(path)) return;
      seen.add(path);
      const unit = this.load(path);
      for (const inc of unit.info.includes) {
        inc.path = this.resolveInclude(inc.name, dirname(scenePath));
        if (!inc.path) continue;
        if (!names.has(inc.path) && inc.path !== scenePath) {
          const plain = normalize(inc.name);
          const kept = isAbsolute(plain) || plain.startsWith('..') ? basename(plain) : plain;
          let name = kept.replace(/(\.[A-Za-z0-9]*)?$/, '.inc4');
          for (let i = 2; [...names.values()].includes(name); i++) name = kept.replace(/(\.[A-Za-z0-9]*)?$/, `-${i}.inc4`);
          names.set(inc.path, name);
        }
        visit(inc.path);
      }
      order.push(unit);
    };
    visit(scenePath);
    return { units: order, names };
  }

  // Converts a scene (and in `convert` mode its includes); returns one result per file.
  convertScene(scene, { folder, entryIsInclude = !/\.pov$/i.test(scene) } = {}) {
    const { units, names } = this.closure(scene, folder);
    const macros = new Map();
    for (const unit of units) for (const [name, info] of unit.info.macros) macros.set(name, info);
    const callers = new Map([...macros.keys()].map((name) => [name, new Set()]));
    for (const info of macros.values()) for (const callee of info.calls) callers.get(callee)?.add(info);
    const transitive = new Map();
    const callersOf = (name) => {
      if (!transitive.has(name)) {
        const found = new Set();
        const stack = [...(callers.get(name) ?? [])];
        while (stack.length) {
          const info = stack.pop();
          if (found.has(info)) continue;
          found.add(info);
          stack.push(...(callers.get(info.name) ?? []));
        }
        transitive.set(name, found);
      }
      return transitive.get(name);
    };
    const sceneUnit = units.at(-1);
    const classicReads = new Set();
    if (this.mode === 'classic')
      for (const unit of units.slice(0, -1))
        for (const name of [...unit.info.topReads, ...[...unit.info.macros.values()].flatMap((m) =>
          m.reads.map((r) => r.text).filter((n) => !m.params.includes(n) && !m.locals.has(n)))])
          if (!unit.info.declared.has(name)) classicReads.add(name);
    const classicCalls = new Set();
    if (this.mode === 'classic')
      for (const unit of units.slice(0, -1))
        for (const [name, nodes] of unit.info.reads) if (nodes.some((n) => n.parent.type === 'call_expression')) classicCalls.add(name);
    const taken = new Set(units.flatMap((u) => [...u.info.identifiers]));
    const renames = new Map();
    for (const word of NEW_RESERVED) {
      if (!taken.has(word)) continue;
      let name = `${word}_`;
      while (taken.has(name)) name += '_';
      taken.add(name);
      renames.set(word, name);
    }
    const bindings = (unit) => new Set([...unit.info.globals, ...(unit === sceneUnit ? unit.info.topLocals : [])]);
    const context = { macros, callersOf, converted: new Set(), mode: this.mode, classicReads, classicCalls, taken, renames, names };
    const results = [];
    for (const unit of this.mode === 'convert' ? units : [sceneUnit]) {
      const others = new Set(units.filter((u) => u !== unit).flatMap((u) => [...bindings(u)]));
      const result = convertUnit(unit, { ...context, others, isInclude: unit !== sceneUnit || entryIsInclude });
      if (result.refusals.length === 0) context.converted.add(unit.path);
      results.push({ ...result, name: names.get(unit.path) });
    }
    return results;
  }
}

// Writes converted files: the scene to `outScene`, converted includes beside it under their closure names.
export function writeConversion(results, scene, outScene) {
  return results.map((result) => {
    const output = result.path === resolve(scene) ? outScene : join(dirname(outScene), result.name);
    if (result.text !== null) {
      mkdirSync(dirname(output), { recursive: true });
      writeFileSync(output, result.text, 'latin1');
    }
    return { ...result, output: result.text !== null ? output : null };
  });
}

function convertUnit(unit, context) {
  const { text, tree } = unit;
  const eol = text.includes('\r\n') ? '\r\n' : '\n';
  const edits = [];
  const refusals = unit.wf.reasons.map((r) => ({ ...r }));
  const notes = [];
  const edit = (start, end, str, inner = false) => edits.push({ start, end, str, inner, seq: edits.length });
  const replace = (node, str) => edit(node.startIndex, node.endIndex, str);
  const insert = (index, str) => edit(index, index, str);
  const wrap = (node) => { edit(node.startIndex, node.startIndex, '(', true); edit(node.endIndex, node.endIndex, ')', true); };
  const refuse = (node, reason, detail) => refusals.push({ reason, ...at(node), ...(detail ? { detail } : {}) });
  const remark = (node, kind, detail) => notes.push({ note: kind, ...at(node), ...(detail ? { detail } : {}) });
  const render = (node) => applyEdits(text, edits, node.startIndex, node.endIndex);
  const nextIsSemicolon = (node) => node.nextSibling?.type === ';';
  const renamed = (name) => context.renames.get(name) ?? name;
  const commentsIn = (node, start, end) => node.descendantsOfType(COMMENTS)
    .filter((c) => c.startIndex >= start && c.endIndex <= end);
  const keepComments = (node, start, end, line) => commentsIn(node, start, end)
    .map((c) => c.text + (c.type === 'line_comment' || line ? eol : ' ')).join('');

  const spaceBefore = (index) => { while (index > 0 && ' \t'.includes(text[index - 1])) index--; return index; };
  const spaceAfter = (index) => { while (index < text.length && ' \t\r'.includes(text[index])) index++; return index; };
  const lineStart = (index) => { const ws = spaceBefore(index); return ws === 0 || text[ws - 1] === '\n' ? ws : null; };
  const remove = (node, end = node.endIndex) => {
    const start = spaceBefore(node.startIndex);
    const after = spaceAfter(end);
    const atLineStart = start === 0 || text[start - 1] === '\n';
    if (atLineStart && (after === text.length || text[after] === '\n')) edit(start, Math.min(after + 1, text.length), '');
    else if (atLineStart) edit(node.startIndex, after, '');
    else edit(start, end, '');
  };

  const temporary = (base) => {
    let name = base;
    for (let i = 2; context.taken.has(name); i++) name = `${base}${i}`;
    context.taken.add(name);
    return name;
  };

  const versionSaves = new Set();
  for (const [name, decls] of unit.info.declared) {
    const isSave = (d) => d.childForFieldName('value')?.text === 'version';
    const otherReads = (unit.info.reads.get(name) ?? []).filter((n) => n.parent.type !== 'version_directive');
    if (decls.every(isSave) && otherReads.length === 0) versionSaves.add(name);
  }

  // Scope state: the current macro (null at file level) and a stack of statement lists with the names bound in each.
  let macro = null;
  let scopes = [new Set()];
  const bound = (name) => scopes.some((s) => s.has(name));
  const bind = (name) => scopes.at(-1).add(name);
  const inList = (nodes, fn) => { scopes.push(new Set()); try { fn ? fn(nodes) : nodes.forEach(visit); } finally { scopes.pop(); } };

  const visitChildren = (node) => { for (const c of node.children) visit(c); };

  function condition(cond, kind, expression) {
    const inner = items(cond.namedChildren)[0];
    const keep = !expression || (inner && isComparison(inner));
    const open = { '#if': '', '#elseif': '', '#ifdef': 'defined(', '#ifndef': '!defined(' }[kind];
    replace(token(cond, '('), (keep ? '(' : '') + open);
    replace(lastToken(cond, ')'), (open ? ')' : '') + (keep ? ')' : ''));
    if (inner?.type === 'member_expression' && ['local', 'global'].includes(inner.childForFieldName('object').text))
      refuse(inner, 'scope-prefix', `${inner.text} has no 4.0 form`);
    visitChildren(cond);
  }

  function ifStatement(node) {
    const kind = node.childForFieldName('kind');
    replace(kind, 'if');
    condition(node.childForFieldName('condition'), kind.type, false);
    insert(node.childForFieldName('condition').endIndex, ' {');
    inList(node.childrenForFieldName('consequence'));
    for (const clause of node.childrenForFieldName('alternative')) {
      if (clause.type === 'elseif_clause') {
        replace(clause.child(0), '} else if');
        condition(clause.childForFieldName('condition'), '#elseif', false);
        insert(clause.childForFieldName('condition').endIndex, ' {');
      } else {
        replace(clause.child(0), '} else {');
      }
      inList(body(clause));
    }
    replace(lastToken(node, '#end'), '}');
  }

  const isSingleValueIf = (node) => node.childrenForFieldName('alternative').some((c) => c.type === 'else_clause') &&
    [node.childrenForFieldName('consequence'), ...node.childrenForFieldName('alternative').map(body)]
      .every((b) => items(b).length === 1 && SINGLE_VALUES.has(items(b)[0].type) && items(b)[0].type !== 'identifier');

  // Where a value-position #if sits: the classic operator around it, if any, and on which side.
  function spliceContext(node) {
    const parent = node.parent;
    if (parent.type === 'binary_expression')
      return { precedence: PRECEDENCE_CLASSIC[operator(parent)], right: !same(parent.childForFieldName('left'), node) };
    if (parent.type === 'unary_expression') return { precedence: 6, right: true };
    if (parent.type === 'member_expression' || parent.type === 'index_expression') return { precedence: 7, right: false };
    return null;
  }

  function ifExpression(node, splice = spliceContext(node)) {
    const nested = ['if_directive', 'elseif_clause', 'else_clause'].includes(node.parent.type);
    const parens = !!spliceContext(node) || nested;
    const kind = node.childForFieldName('kind');
    const cond = node.childForFieldName('condition');
    const branches = [items(node.childrenForFieldName('consequence')),
      ...node.childrenForFieldName('alternative').map((c) => items(body(c)))];
    for (const [value] of branches) {
      const binds = value.type === 'binary_expression' ? PRECEDENCE_CLASSIC[operator(value)]
        : value.type === 'conditional_expression' ? 1 : 9;
      if (splice && (splice.right ? binds <= splice.precedence : binds < splice.precedence))
        refuse(value, 'splice-precedence', `${value.text} would regroup with the operator around the #if`);
    }
    edit(kind.startIndex, cond.startIndex, parens ? '(' : '');
    condition(cond, kind.type, true);
    let previous = branches[0];
    const values = (list) => list.forEach((v) => (v.type === 'if_directive' ? ifExpression(v, splice) : visit(v)));
    edit(cond.endIndex, previous[0].startIndex, ' ? ' + keepComments(node, cond.endIndex, previous[0].startIndex));
    values(previous);
    for (const clause of node.childrenForFieldName('alternative')) {
      const list = items(body(clause));
      if (clause.type === 'elseif_clause') {
        const c = clause.childForFieldName('condition');
        edit(previous.at(-1).endIndex, c.startIndex, ' : ' + keepComments(node, previous.at(-1).endIndex, c.startIndex));
        condition(c, '#elseif', true);
        edit(c.endIndex, list[0].startIndex, ' ? ' + keepComments(clause, c.endIndex, list[0].startIndex));
      } else {
        edit(previous.at(-1).endIndex, list[0].startIndex, ' : ' + keepComments(node, previous.at(-1).endIndex, list[0].startIndex));
      }
      values(list);
      previous = list;
    }
    const tail = keepComments(node, previous.at(-1).endIndex, node.endIndex, false);
    edit(previous.at(-1).endIndex, node.endIndex, (tail ? ' ' + tail : '') + (parens ? ')' : ''));
  }

  function declare(node) {
    const kind = node.childForFieldName('kind');
    const target = node.childForFieldName('target');
    const value = node.childForFieldName('value');
    const isLocal = kind.type === '#local';
    if (node.children.some((c) => c.type === 'optional')) refuse(node, 'optional-declare', 'optional has no 4.0 form');
    if (node.children.some((c) => c.type === 'deprecated')) remark(node, 'deprecated-dropped');
    if (target.type === 'tuple_target') refuse(target, 'tuple', 'tuple declarations have no 4.0 form');
    if (value.type === 'layered_texture') refuse(value, 'layered-texture', 'a declared layered texture has no 4.0 value form');
    let head = '';
    if (target.type === 'identifier') {
      const name = target.text;
      if (versionSaves.has(name)) { remove(node); return; }
      if (!macro) {
        head = bound(name) ? '' : 'let ';
        bind(name);
        if (isLocal && context.isInclude && context.mode === 'convert' && context.others.has(name))
          refuse(node, 'include-local-shadow', `the include's #local ${name} would replace the including file's ${name}`);
        else if (isLocal && context.isInclude) remark(node, 'include-local', `${name} stays visible to the including file`);
        if (context.classicReads.has(name)) remark(node, 'read-by-classic-include', `${name} is read by a classic include`);
      } else if (macro.params.includes(name) || bound(name)) {
        head = '';
      } else if (isLocal) {
        head = 'let ';
        bind(name);
      } else if (macro.locals.has(name)) {
        refuse(node, 'declare-maybe-local', `${name} is #local in ${macro.name} on some paths only`);
      } else {
        head = 'global ';
        for (const caller of context.callersOf(macro.name))
          if (caller.locals.has(name) || caller.params.includes(name)) {
            refuse(node, 'dynamic-scope', `#declare ${name} in ${macro.name} assigns a local of calling macro ${caller.name}`);
            break;
          }
      }
    }
    edit(kind.startIndex, target.startIndex, head);
    visit(target);
    visit(value);
    if (!node.children.some((c) => c.type === ';')) insert(value.endIndex, ';');
  }

  function macroDefinition(node) {
    const info = unit.info.macroAt.get(node.startIndex);
    const params = node.childForFieldName('parameters');
    if (context.classicCalls.has(info.name))
      refuse(node, 'called-by-classic-include', `a classic include calls ${info.name}, which would become a 4.0 function`);
    for (const p of params.namedChildren) if (p.child(0).type === 'optional') refuse(p, 'optional-parameter', `${p.text} has no 4.0 form`);
    replace(node.child(0), 'fn');
    visit(node.childForFieldName('name'));
    for (const p of params.namedChildren) if (p.type === 'parameter') visit(p.childForFieldName('name'));
    insert(params.endIndex, ' {');
    const outer = [macro, scopes];
    macro = info;
    scopes = [new Set(info.params)];
    for (const read of info.reads) {
      const name = read.text;
      if (info.params.includes(name) || info.locals.has(name) || context.macros.has(name)) continue;
      for (const caller of context.callersOf(info.name))
        if (caller !== info && (caller.locals.has(name) || caller.params.includes(name))) {
          refuse(read, 'dynamic-scope', `${name} in ${info.name} reads a local of calling macro ${caller.name}`);
          break;
        }
    }
    try { body(node).forEach(visit); } finally { [macro, scopes] = outer; }
    replace(lastToken(node, '#end'), '}');
  }

  function forLoop(node) {
    const variable = node.childForFieldName('variable');
    const start = node.childForFieldName('start');
    const end = node.childForFieldName('end');
    const step = node.childForFieldName('step');
    replace(node.child(0), 'for');
    edit(variable.endIndex, start.startIndex, ' = ');
    edit(start.endIndex, end.startIndex, ' to ');
    if (step) edit(end.endIndex, step.startIndex, ' step ');
    insert(token(node, ')').endIndex, ' {');
    [variable, start, end, step].forEach((n) => n && visit(n));
    inList(body(node), (nodes) => { bind(variable.text); nodes.forEach(visit); });
    replace(lastToken(node, '#end'), '}');
  }

  function switchStatement(node) {
    const value = items(node.childForFieldName('value').namedChildren)[0];
    visit(value);
    const simple = ['identifier', 'member_expression', 'index_expression'].includes(value.type) &&
      !value.descendantsOfType('call_expression').length;
    const start = lineStart(node.startIndex);
    const indent = start === null ? null : text.slice(start, node.startIndex);
    const name = simple ? render(value) : temporary('Switch_Value');
    const preamble = simple ? '' : `let ${name} = ${isComparison(value) ? `(${render(value)})` : render(value)};`;
    const clauses = node.namedChildren.filter((c) => c.type.endsWith('_clause'));
    if (!clauses.length) {
      if (simple) remove(node); else replace(node, preamble);
      return;
    }
    const operand = (n) => (n.type === 'conditional_expression' || (n.type === 'binary_expression' &&
      PRECEDENCE_4[OPERATORS[operator(n)] ?? operator(n)] <= PRECEDENCE_4['==']) ? `(${render(n)})` : render(n));
    const term = (n) => { visit(n); return operand(n); };
    const separator = indent === null ? ' ' : eol + indent;
    let group = [];
    let first = true;
    clauses.forEach((clause, i) => {
      const content = items(body(clause));
      const last = content.at(-1);
      const hasBreak = last?.type === 'break_directive';
      const isLast = i === clauses.length - 1;
      for (const b of clause.descendantsOfType('break_directive'))
        if (!same(b, last) && !enclosingLoop(b, clause)) refuse(b, 'break-placement', '#break before the end of a #case');
      if (clause.type === 'case_clause') {
        group.push({ clause, test: `${name} == ${term(items(clause.childForFieldName('value').namedChildren)[0])}` });
      } else if (clause.type === 'range_clause') {
        const bound = (n) => { visit(n); return n.type === 'conditional_expression' || (n.type === 'binary_expression' &&
          PRECEDENCE_4[OPERATORS[operator(n)] ?? operator(n)] <= PRECEDENCE_4['<=']) ? `(${render(n)})` : render(n); };
        const low = bound(clause.childForFieldName('low'));
        group.push({ clause, test: `${name} >= ${low} && ${name} <= ${bound(clause.childForFieldName('high'))}` });
      } else {
        group.push({ clause, test: null });
      }
      if (content.length === 0 && !isLast && clause.type !== 'else_clause') return;
      if (!hasBreak && content.length && !isLast) refuse(clause, 'switch-fallthrough', 'a #case runs on into the next one');
      const isElse = group.some((g) => g.test === null);
      if (isElse && !isLast) refuse(clause, 'switch-fallthrough', '#else is not the last clause');
      if (isElse && first) refuse(clause, 'switch-fallthrough', '#switch with only #else');
      const head = group.at(-1).clause;
      const headEnd = head.type === 'else_clause' ? head.child(0).endIndex
        : head.type === 'case_clause' ? head.childForFieldName('value').endIndex : token(head, ')').endIndex;
      const header = isElse ? '} else {' : `${first ? '' : '} else '}if (${group.map((g) => g.test).join(' || ')}) {`;
      const at = group[0].clause.startIndex;
      const from = first ? node.startIndex : indent !== null && lineStart(at) !== null ? lineStart(at) : at;
      const lead = first ? (preamble ? preamble + separator : '') : indent !== null && lineStart(at) !== null ? indent : '';
      const kept = commentsIn(node, from, headEnd).filter((c) => !hasAncestor(c, 'condition'))
        .map((c) => c.text + (c.type === 'line_comment' || indent !== null ? separator : ' ')).join('');
      edit(from, headEnd, lead + kept + header);
      inList(hasBreak ? content.slice(0, -1) : content);
      if (hasBreak) remove(last);
      first = false;
      group = [];
    });
    replace(lastToken(node, '#end'), '}');
  }

  function enclosingLoop(node, stop) {
    for (let p = node.parent; p && !same(p, stop); p = p.parent) {
      if (p.type === 'while_directive' || p.type === 'for_directive') return p;
      if (p.type === 'macro_directive' || p.type === 'switch_directive') return null;
    }
    return null;
  }

  function breakStatement(node) {
    for (let p = node.parent; p; p = p.parent) {
      if (p.type === 'while_directive' || p.type === 'for_directive') { replace(node, 'break;'); return; }
      if (p.type === 'macro_directive' || p.type === 'switch_directive') break;
    }
    refuse(node, 'break-placement', '#break outside a loop or #switch');
  }

  function arrayExpression(node) {
    const init = node.childForFieldName('initializer');
    const sizes = node.childrenForFieldName('size');
    sizes.forEach(visit);
    const count = (size) => (['binary_expression', 'conditional_expression'].includes(size.type) ? `(${render(size)})` : render(size));
    const built = (dims) => dims.reduceRight((inner, size) => `array(${render(size)}${inner ? `, ${inner}` : ''})`, '');
    if (init) {
      edit(node.startIndex, init.startIndex + 1, '[');
      const brackets = (list, depth) => {
        if (!same(list, init)) replace(list.child(0), '[');
        const elements = items(list.children.slice(1, -1)).filter((c) => c.type !== ',');
        for (const c of elements) (c.type === 'array_initializer' ? brackets(c, depth + 1) : visit(c));
        const size = sizes[depth];
        const fill = built(sizes.slice(depth + 1)) || 'null';
        let pad = '';
        if (size && elements.some((c) => c.type.endsWith('_directive'))) refuse(size, 'array-size', 'the element count is not known');
        else if (size?.type === 'number' && Number(size.text) > elements.length)
          pad = `, ${fill}`.repeat(Number(size.text) - elements.length).slice(elements.length ? 0 : 2);
        else if (size && size.type !== 'number')
          pad = `${elements.length ? ', ' : ''}...array(${elements.length ? `${count(size)} - ${elements.length}` : render(size)}${fill === 'null' ? '' : `, ${fill}`})`;
        const after = elements.at(-1)?.endIndex ?? list.child(0).endIndex;
        if (pad) insert(after, !elements.length && text[after] === ' ' ? ` ${pad}` : pad);
        replace(list.lastChild, ']');
      };
      brackets(init, 0);
      return;
    }
    replace(node, built(sizes) || '[]');
  }

  const plainKey = (name) => (/^[A-Za-z_][A-Za-z0-9_]*$/.test(name) && !RESERVED.has(name) ? name : JSON.stringify(name));

  function dictionaryExpression(node) {
    edit(node.startIndex, token(node, '{').startIndex, '');
    const children = node.children;
    separate(children.slice(children.findIndex((c) => c.type === '{') + 1, -1));
    for (const child of node.namedChildren) {
      if (child.type === 'dictionary_entry') {
        const key = child.childForFieldName('key');
        const bracketed = child.children[0].type === '[';
        let written;
        if (!bracketed) written = plainKey(key.text);
        else if (key.type === 'string') written = /^"[A-Za-z_][A-Za-z0-9_]*"$/.test(key.text) ? plainKey(key.text.slice(1, -1)) : key.text;
        else { visit(key); written = `[${render(key)}]`; }
        edit(child.startIndex, bracketed ? token(child, ']').endIndex : key.endIndex, written);
        visit(child.childForFieldName('value'));
      } else if (child.type.endsWith('_directive')) {
        refuse(child, 'directive-in-dictionary', 'a directive inside a dictionary literal');
      }
    }
  }

  function includeDirective(node) {
    const file = node.childForFieldName('file');
    replace(node.child(0), 'include');
    const inc = unit.info.includes.find((i) => i.node.startIndex === node.startIndex);
    if (context.mode === 'convert') {
      if (inc?.path && context.converted.has(inc.path)) replace(file, JSON.stringify(context.names.get(inc.path)));
      else { remark(node, 'classic-include', inc?.path ? `${inc.name} was not converted` : `${file.text} not found`); visit(file); }
    } else {
      visit(file);
    }
    if (!nextIsSemicolon(node)) insert(node.endIndex, ';');
  }

  function undefDirective(node) {
    const name = node.childForFieldName('name').text;
    let head = '';
    if (!macro) {
      head = bound(name) ? '' : 'let ';
      bind(name);
    } else if (!macro.params.includes(name) && !bound(name)) {
      if (macro.locals.has(name)) refuse(node, 'declare-maybe-local', `#undef ${name} in ${macro.name}: ${name} is #local on some paths only`);
      head = 'global ';
    }
    replace(node, `${head}${renamed(name)} = null;`);
  }

  function messageDirective(node) {
    const kind = node.childForFieldName('kind');
    const message = node.childForFieldName('message');
    edit(kind.startIndex, message.startIndex, `${kind.type.slice(1)}(`);
    visit(message);
    insert(message.endIndex, nextIsSemicolon(node) ? ')' : ');');
  }

  function callExpression(node) {
    const fn = node.childForFieldName('function');
    if (separate(node.childForFieldName('arguments').children.slice(1, -1)))
      refuse(node, 'empty-argument', `${fn.text}(...) leaves an argument out`);
    const info = fn.type === 'identifier' ? context.macros.get(fn.text) : null;
    if (info?.assigned.size) {
      const args = argumentNodes(node.childForFieldName('arguments'));
      for (const index of info.assigned) {
        const arg = args[index];
        const variable = arg?.length === 1 && !arg[0].descendantsOfType('call_expression').length && lvalueRoot(arg[0]);
        if (!variable) continue;
        const name = variable.text;
        const repeated = ['while_directive', 'for_directive', 'macro_directive'].some((t) => hasAncestor(node, t));
        const readLater = (unit.info.reads.get(name) ?? []).some((n) => n.startIndex >= node.endIndex) ||
          [...context.macros.values()].some((m) => !m.params.includes(name) && !m.locals.has(name) &&
            m.reads.some((r) => r.text === name));
        const detail = `${fn.text} assigns its parameter ${info.params[index]}, passed ${arg[0].text}`;
        if (repeated || readLater) refuse(arg[0], 'by-reference-argument', detail);
        else remark(arg[0], 'by-reference-unused', detail);
      }
    }
    if (fn.type === 'identifier') for (const arg of argumentNodes(node.childForFieldName('arguments')).flat()) splitItemOperand(arg);
    visitChildren(node);
  }

  // `M() -y` is two arguments in classic when M expands to an object: nothing applies an operator to it.
  function splitItemOperand(arg) {
    for (let n = arg; n.type === 'binary_expression' && ['+', '-'].includes(operator(n)); n = n.childForFieldName('left')) {
      const left = n.childForFieldName('left');
      if (isMacroCall(left) && yieldsItems(context.macros.get(left.childForFieldName('function').text), context.macros)) {
        insert(left.endIndex, ',');
        return;
      }
    }
  }

  function binaryExpression(node) {
    const op = OPERATORS[operator(node)] ?? operator(node);
    if (op !== operator(node)) replace(node.childForFieldName('operator'), op);
    for (const side of ['left', 'right']) {
      const child = node.childForFieldName(side);
      if (child.type !== 'binary_expression') continue;
      const inner = PRECEDENCE_4[OPERATORS[operator(child)] ?? operator(child)];
      if (inner < PRECEDENCE_4[op] || (side === 'right' && inner === PRECEDENCE_4[op])) wrap(child);
    }
    visitChildren(node);
  }

  // Inserts the commas classic lets a list omit; returns whether a slot is empty (leading, doubled or trailing comma).
  function separate(parts) {
    const list = parts.filter((p) => !COMMENTS.includes(p.type));
    let empty = false;
    list.forEach((a, i) => {
      const b = list[i + 1];
      if (a.type === ',' && (i === 0 || !b || b.type === ',')) empty = true;
      if (b && a.type !== ',' && b.type !== ',') insert(a.endIndex, ',');
    });
    return empty;
  }

  function vector(node) {
    const parts = node.children.slice(1, -1);
    separate(parts);
    if (items(parts).at(-1)?.type === ',') replace(items(parts).at(-1), '');
    visitChildren(node);
  }

  function visit(node) {
    switch (node.type) {
      case 'ERROR': return;
      case 'identifier':
        if (context.renames.has(node.text)) replace(node, renamed(node.text));
        return;
      case 'string':
        if (/[\r\n]/.test(node.text)) replace(node, node.text.replace(/\r?\n/g, '\\n'));
        return;
      case 'block_comment':
        if (node.text.slice(2, -2).includes('/*')) refuse(node, 'nested-comment', '4.0 comments do not nest');
        return;
      case 'binary_expression': binaryExpression(node); return;
      case 'member_expression': {
        const object = node.childForFieldName('object');
        const member = node.childForFieldName('member');
        if (object.type === 'identifier' && ['local', 'global'].includes(object.text))
          refuse(node, 'scope-prefix', `${node.text} has no 4.0 form`);
        if (NEW_RESERVED.has(member.text)) edit(object.endIndex, node.endIndex, `[${JSON.stringify(member.text)}]`);
        visit(object);
        return;
      }
      case 'tuple': refuse(node, 'tuple', 'tuples have no 4.0 form'); return;
      case 'tag_filter': return;
      case 'vector': vector(node); return;
      case 'call_expression': callExpression(node); return;
      case 'declare_directive': declare(node); return;
      case 'macro_directive': macroDefinition(node); return;
      case 'if_directive':
        if (isExpressionIf(node) || (['block', 'bracket_group'].includes(node.parent.type) && isSingleValueIf(node))) ifExpression(node);
        else ifStatement(node);
        return;
      case 'while_directive':
        replace(node.child(0), 'while');
        insert(node.childForFieldName('condition').endIndex, ' {');
        visit(node.childForFieldName('condition'));
        inList(body(node));
        replace(lastToken(node, '#end'), '}');
        return;
      case 'for_directive': forLoop(node); return;
      case 'switch_directive': switchStatement(node); return;
      case 'break_directive': breakStatement(node); return;
      case 'include_directive': includeDirective(node); return;
      case 'version_directive': remove(node); return;
      case 'default_directive': replace(node.child(0), 'default'); visitChildren(node); return;
      case 'undef_directive': undefDirective(node); return;
      case 'message_directive': messageDirective(node); return;
      case 'array_expression': arrayExpression(node); return;
      case 'dictionary_expression': dictionaryExpression(node); return;
      case 'block': case 'bracket_group': inList(node.children); return;
      default: visitChildren(node);
    }
  }

  if (!unit.wf.syntax) visitChildren(tree.rootNode);
  refusals.sort((a, b) => a.line - b.line || a.column - b.column);
  return { path: unit.path, refusals, notes, text: refusals.length ? null : applyEdits(text, edits, 0, text.length) };
}

// Applies the edits inside [start, end]; an edit inside a wider one is superseded by it, and an insertion at the
// range's edge belongs to the enclosing construct unless it is marked inner (parentheses around the range).
export function applyEdits(text, edits, start, end) {
  const whole = start === 0 && end === text.length;
  const inside = edits.filter((e) => e.start >= start && e.end <= end &&
    !(e.start === e.end && !whole && !e.inner && (e.start === start || e.start === end)));
  inside.sort((a, b) => a.start - b.start || (a.start === a.end ? -1 : 0) - (b.start === b.end ? -1 : 0) ||
    (b.end - b.start) - (a.end - a.start) || a.seq - b.seq);
  let out = '';
  let pos = start;
  for (const e of inside) {
    if (e.start < pos) {
      if (e.end <= pos) continue;
      throw new Error(`overlapping edits at ${e.start}`);
    }
    out += text.slice(pos, e.start) + e.str;
    pos = e.end;
  }
  return out + text.slice(pos, end);
}
