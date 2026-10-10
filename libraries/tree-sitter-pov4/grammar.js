// SPDX-License-Identifier: AGPL-3.0-or-later

const PREC = {
  lambda: -1, colour: 1, conditional: 2, or: 3, and: 4, equality: 5, relational: 6,
  additive: 7, multiplicative: 8, unary: 9, postfix: 10, block: 12,
};

const commaSep1 = (rule) => seq(rule, repeat(seq(',', rule)));

module.exports = grammar({
  name: 'pov4',
  word: ($) => $.identifier,
  externals: ($) => [$.keyword, $.builtin, $.colour_operator, $.color_operator, $.colour_channel],
  extras: ($) => [/\s/, $.comment],
  conflicts: ($) => [[$.parameter, $._primary], [$.body, $.dictionary], [$._item, $.dictionary], [$.channel], [$._item, $.pair]],

  rules: {
    source_file: ($) => repeat($._item),
    _item: ($) => choice($._statement, $.keyword, $.spread, $._expression, ',', ';'),

    _statement: ($) => choice($.let_statement, $.global_statement, $.assignment, $.function_definition,
      $.if_statement, $.while_statement, $.for_statement, $.break_statement, $.continue_statement,
      $.return_statement, $.include_statement),
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

    _expression: ($) => choice($.conditional_expression, $.binary_expression, $.unary_expression,
      $.lambda, $.colour_expression, $._postfix, $.array),
    _comparable: ($) => choice($._expression, $.comparison_expression),
    _postfix: ($) => choice($.call_expression, $.index_expression, $.member_expression, $._primary),
    _primary: ($) => choice($.number, $.string, $.identifier, $.builtin, $.true, $.false, $.null, $.vector,
      $.dictionary, $.parenthesized_expression, $.block, $.function_block),

    conditional_expression: ($) => prec.right(PREC.conditional, seq(field('condition', $._expression), '?',
      field('consequence', $._expression), ':', field('alternative', $._expression))),
    binary_expression: ($) => choice(...[
      ['+', PREC.additive], ['-', PREC.additive], ['*', PREC.multiplicative], ['/', PREC.multiplicative],
    ].map(([op, p]) => prec.left(p, seq(field('left', $._expression), field('operator', op),
      field('right', $._expression))))),
    comparison_expression: ($) => choice(...[
      ['||', PREC.or], ['&&', PREC.and], ['==', PREC.equality], ['!=', PREC.equality],
      ['<', PREC.relational], ['<=', PREC.relational], ['>', PREC.relational], ['>=', PREC.relational],
    ].map(([op, p]) => prec.left(p, seq(field('left', $._comparable), field('operator', op),
      field('right', $._comparable))))),
    unary_expression: ($) => prec(PREC.unary, seq(field('operator', choice('-', '+', '!')),
      field('operand', $._expression))),
    lambda: ($) => prec.right(PREC.lambda, seq(field('parameters', choice($.parameters, $.identifier)), '=>',
      field('body', choice($.body, $._expression)))),
    colour_expression: ($) => prec.right(PREC.colour, seq(choice(
      seq(field('operator', $.colour_operator), field('value', $._expression)),
      seq(field('operator', $.color_operator), optional(field('value', $._expression))),
      $.channel), repeat($.channel))),
    channel: ($) => choice(prec.dynamic(1, seq(field('name', $.colour_channel), field('value', $._expression))),
      field('name', $.colour_channel)),

    call_expression: ($) => prec(PREC.postfix, seq(field('function', $._postfix), field('arguments', $.arguments))),
    arguments: ($) => seq('(', optional(seq(commaSep1(choice($._expression, $.spread)), optional(','))), ')'),
    index_expression: ($) => prec(PREC.postfix, seq(field('object', $._postfix), token.immediate('['),
      field('index', $._expression), ']')),
    member_expression: ($) => prec(PREC.postfix, seq(field('object', $._postfix), token.immediate('.'),
      field('member', $.identifier))),

    vector: ($) => seq('<', $._expression, ',', commaSep1($._expression), '>'),
    array: ($) => seq('[', repeat($._item), ']'),
    dictionary: ($) => seq('{', optional(seq(commaSep1(choice($.pair, $.spread)), optional(','))), '}'),
    pair: ($) => seq(choice(field('key', choice($.identifier, $.string)), seq('[', field('computed', $._expression), ']')),
      ':', field('value', $._expression)),
    spread: ($) => seq('...', $._expression),
    parenthesized_expression: ($) => seq('(', $._comparable, ')'),
    block: ($) => prec(PREC.block, seq(field('name', choice($.keyword, alias('to', $.keyword))),
      '{', repeat($._item), '}')),
    function_block: ($) => seq('function', optional(choice(field('parameters', $.function_parameters),
      seq(field('width', $._size), ',', field('height', $._size)))),
      '{', repeat(choice($.keyword, $._comparable, ',')), '}'),
    _size: ($) => choice($.number, $.identifier),
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
