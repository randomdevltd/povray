// SPDX-License-Identifier: AGPL-3.0-or-later

const PREC = {
  lambda: -1, colour: 1, conditional: 2, or: 3, and: 4, equality: 5, relational: 6,
  additive: 7, multiplicative: 8, unary: 9, postfix: 10, block: 12,
};

const commaSep1 = (rule) => seq(rule, repeat(seq(',', rule)));

const COMPARISONS = [['||', PREC.or], ['&&', PREC.and], ['==', PREC.equality], ['!=', PREC.equality],
  ['<', PREC.relational], ['<=', PREC.relational], ['>', PREC.relational], ['>=', PREC.relational]];

// One copy of the operator rules per context: full, block items (a bare `<` starts a vector), vector components.
function expressionFamily(top, suffix, comparisons) {
  const name = (rule) => (suffix ? `_${rule}_${suffix}` : rule);
  const ref = ($, rule) => (suffix ? alias($[name(rule)], $[rule]) : $[rule]);
  const rules = {
    [top]: ($) => choice(ref($, 'conditional_expression'), ref($, 'binary_expression'),
      ...(comparisons.length ? [ref($, 'comparison_expression')] : []), ref($, 'unary_expression'),
      ref($, 'lambda'), $.colour_expression, $._postfix, $.array),
    [name('conditional_expression')]: ($) => prec.right(PREC.conditional, seq(field('condition', $[top]), '?',
      field('consequence', $[top]), ':', field('alternative', $[top]))),
    [name('binary_expression')]: ($) => choice(...[
      ['+', PREC.additive], ['-', PREC.additive], ['*', PREC.multiplicative], ['/', PREC.multiplicative],
    ].map(([op, p]) => prec.left(p, seq(field('left', $[top]), field('operator', op), field('right', $[top]))))),
    [name('unary_expression')]: ($) => prec(PREC.unary, seq(field('operator', choice('-', '+', '!')),
      field('operand', $[top]))),
    [name('lambda')]: ($) => prec.right(PREC.lambda, seq(field('parameters', choice($.parameters, $.identifier)), '=>',
      field('body', choice($.body, $[top])))),
  };
  if (comparisons.length) {
    rules[name('comparison_expression')] = ($) => choice(...comparisons.map(([op, p]) => prec.left(p,
      seq(field('left', $[top]), field('operator', op), field('right', $[top])))));
  }
  return rules;
}

module.exports = grammar({
  name: 'pov4',
  word: ($) => $.identifier,
  externals: ($) => [$.keyword, $.builtin, $.colour_operator, $.color_operator, $.colour_channel, $.tag_keyword, $.field_keyword],
  extras: ($) => [/\s/, $.comment],
  conflicts: ($) => [[$.parameter, $._primary], [$.body, $.dictionary], [$._item, $.dictionary], [$.channel],
    [$._expression, $._item_expression], [$.lambda, $._lambda_item]],

  rules: {
    source_file: ($) => repeat($._item),
    // Contextual photon quality prefixes use identifier/call/arithmetic trees; lowering extracts their operand.
    _item: ($) => choice($._statement, $.keyword, $.spread, $._item_expression, ',', ';'),

    _statement: ($) => choice($.let_statement, $.global_statement, $.assignment, $.function_definition,
      $.if_statement, $.while_statement, $.for_statement, $.break_statement, $.continue_statement,
      $.return_statement, $.include_statement, $.input_statement),
    _value: ($) => seq(field('value', $._expression), repeat(field('value', $.block))),
    let_statement: ($) => seq('let', field('name', $.identifier), optional(seq('=', $._value)), ';'),
    global_statement: ($) => seq('global', field('name', $.identifier), '=', $._value, ';'),
    assignment: ($) => seq(field('target', choice($.identifier, $.index_expression, $.member_expression)),
      '=', $._value, ';'),
    function_definition: ($) => seq('fn', field('name', $.identifier), field('parameters', $.parameters),
      field('body', $.body)),
    parameters: ($) => seq('(', optional(seq(commaSep1($.parameter), optional(','))), ')'),
    parameter: ($) => seq(field('name', $.identifier), optional(seq('=', field('default', $._expression)))),
    body: ($) => prec.dynamic(1, seq('{', repeat($._item), '}')),
    if_statement: ($) => prec.right(seq('if', field('condition', $.parenthesized_expression),
      field('consequence', $.body), optional(seq('else', field('alternative', choice($.body, $.if_statement)))))),
    while_statement: ($) => seq('while', field('condition', $.parenthesized_expression), field('body', $.body)),
    for_statement: ($) => seq('for', '(', field('variable', $.identifier), choice(
      seq('=', field('start', $._expression), 'to', field('end', $._expression),
        optional(seq('step', field('step', $._expression)))),
      seq('in', field('iterable', $._expression))), ')', field('body', $.body)),
    break_statement: (_) => seq('break', ';'),
    continue_statement: (_) => seq('continue', ';'),
    return_statement: ($) => seq('return', optional(field('value', $._expression)), ';'),
    include_statement: ($) => seq('include', field('file', $._expression), ';'),
    input_statement: ($) => seq('input', field('name', $.identifier),
      optional(seq('=', field('default', $._expression))), ';'),

    ...expressionFamily('_expression', '', COMPARISONS),
    ...expressionFamily('_item_expression', 'item', COMPARISONS.filter(([op]) => op !== '<')),
    ...expressionFamily('_component', 'component', []),
    _postfix: ($) => choice($.call_expression, $.index_expression, $.member_expression, $._primary),
    _primary: ($) => choice($.number, $.string, $.identifier, $.builtin, $.true, $.false, $.null, $.vector,
      $.dictionary, $.parenthesized_expression, $.block, $.function_block, $.tag_filter),

    colour_expression: ($) => prec.right(PREC.colour, seq(choice(
      seq(field('operator', $.colour_operator), field('value', $._component)),
      seq(field('operator', $.color_operator), optional(field('value', $._component))),
      $.channel), repeat($.channel))),
    channel: ($) => choice(prec.dynamic(1, seq(field('name', $.colour_channel), field('value', $._component))),
      field('name', $.colour_channel)),

    call_expression: ($) => prec(PREC.postfix, seq(field('function', $._postfix), field('arguments', $.arguments))),
    arguments: ($) => seq('(', optional(seq(commaSep1(choice($._expression, $.spread)), optional(','))), ')'),
    index_expression: ($) => prec(PREC.postfix, seq(field('object', $._postfix), token.immediate('['),
      field('index', $._expression), ']')),
    member_expression: ($) => prec(PREC.postfix, seq(field('object', $._postfix), token.immediate('.'),
      field('member', $.identifier))),

    vector: ($) => seq('<', $._component, ',', commaSep1($._component), '>'),
    array: ($) => seq('[', repeat($._item), ']'),
    dictionary: ($) => seq('{', optional(seq(commaSep1(choice($.pair, $.spread)), optional(','))), '}'),
    pair: ($) => seq(choice(field('key', choice($.identifier, $.string)), seq('[', field('computed', $._expression), ']')),
      ':', field('value', $._expression)),
    spread: ($) => seq('...', $._component),
    parenthesized_expression: ($) => seq('(', $._expression, ')'),
    block: ($) => prec(PREC.block, seq(field('name', choice($.keyword, alias('to', $.keyword), alias($.field_keyword, $.keyword))),
      '{', repeat($._item), '}')),
    function_block: ($) => seq('function', optional(choice(field('parameters', $.function_parameters),
      seq(field('width', $._size), ',', field('height', $._size)))),
      '{', repeat(choice($.keyword, $._expression, ',')), '}'),
    _size: ($) => choice($.number, $.identifier),
    tag_filter: ($) => seq(field('name', alias($.tag_keyword, $.keyword)), '{',
      optional(field('filter', $._tag_expression)), '}'),
    _tag_expression: ($) => choice($.string, $.identifier, alias($._tag_call, $.call_expression),
      alias($._tag_member, $.member_expression), alias($._tag_index, $.index_expression),
      alias(choice('any', 'none'), $.constant),
      alias($._tag_parenthesized, $.parenthesized_expression), alias($._tag_not, $.unary_expression),
      alias($._tag_binary, $.binary_expression)),
    _tag_parenthesized: ($) => seq('(', $._tag_expression, ')'),
    _tag_call: ($) => seq(field('function', choice($.identifier, $.builtin)), field('arguments', $.arguments)),
    _tag_member: ($) => seq(field('object', $.identifier), token.immediate('.'), field('member', $.identifier)),
    _tag_index: ($) => seq(field('object', $.identifier), token.immediate('['), field('index', $._expression), ']'),
    _tag_not: ($) => prec(3, seq(field('operator', '!'), field('operand', $._tag_expression))),
    _tag_binary: ($) => choice(
      prec.left(2, seq(field('left', $._tag_expression), field('operator', '&'), field('right', $._tag_expression))),
      prec.left(1, seq(field('left', $._tag_expression), field('operator', '|'), field('right', $._tag_expression)))),
    function_parameters: ($) => prec(PREC.block, seq('(', optional(commaSep1(choice($.identifier, $.builtin))), ')')),

    true: (_) => 'true',
    false: (_) => 'false',
    null: (_) => 'null',
    identifier: (_) => /[A-Za-z_][A-Za-z0-9_]*/,
    number: (_) => /(\d+\.?\d*|\.\d+)([eE][+-]?\d+)?/,
    string: (_) => /"([^"\\\n]|\\.)*"/,
    comment: (_) => token(choice(/\/\/[^\n]*/, /\/\*[^*]*\*+([^/*][^*]*\*+)*\//)),
  },
});
