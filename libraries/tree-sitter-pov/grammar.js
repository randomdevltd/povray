// SPDX-License-Identifier: AGPL-3.0-or-later

const words = require('../../tools/language/keywords.json');

const CONSTANTS = ['clock', 'clock_on', 'false', 'no', 'now', 'off', 'on', 'pi', 't', 'tau', 'true', 'u', 'v',
  'version', 'x', 'y', 'yes', 'z'];
const STRING_FUNCTIONS = ['chr', 'concat', 'datetime', 'str', 'strlwr', 'strupr', 'substr', 'vstr'];
const FUNCTIONS = words
  .filter((k) => (k.category === 'float' || k.category === 'vector') && !CONSTANTS.includes(k.word))
  .map((k) => k.word)
  .concat(STRING_FUNCTIONS, ['internal', 'sqr']);
const COLOUR_CHANNELS = words.filter((k) => k.category === 'colour').map((k) => k.word);
const TAG_FILTERS = ['filter_tags', 'front_filter_tags', 'back_filter_tags'];
const VERSIONED = [...TAG_FILTERS, 'tags', 'any', 'none'];
const BINARY_TYPES = ['sint8', 'uint8', 'sint16be', 'sint16le', 'uint16be', 'uint16le', 'sint32be', 'sint32le'];
const SPECIAL = ['array', 'dictionary', 'function', 'texture', ...TAG_FILTERS];
const KEYWORDS = words.map((k) => k.word).filter((w) =>
  !CONSTANTS.includes(w) && !FUNCTIONS.includes(w) && !SPECIAL.includes(w));

const PREC = {
  conditional: 1, logical: 2, relational: 3, additive: 4, multiplicative: 5, unary: 6, postfix: 7, call: 8,
  directive_item: 2,
};

const hash = (word) => alias(token(prec(1, seq('#', /[ \t]*/, word))), '#' + word);
const paren = ($, rule) => seq('(', rule, ')');

module.exports = grammar({
  name: 'pov',
  word: $ => $.identifier,
  extras: $ => [/\s/, $.line_comment, $.block_comment],
  externals: $ => [$.block_comment],
  supertypes: $ => [$._directive],
  reserved: { global: _ => words.map((k) => k.word).filter((w) => !VERSIONED.includes(w)), raw: _ => [] },

  rules: {
    source_file: $ => repeat($._item),

    _item: $ => choice(
      prec(PREC.directive_item, $._directive),
      $._value,
      $.keyword,
      alias($.builtin_function, $.keyword),
      alias('texture', $.keyword),
      $.block,
      $.tag_filter,
      $.function_block,
      $.array_expression,
      $.dictionary_expression,
      $.bracket_group,
      ',',
      ';',
    ),

    keyword: _ => choice(...KEYWORDS),

    block: $ => choice(
      prec(1, seq(
        field('name', choice($.keyword, $.identifier, alias($.builtin_function, $.keyword))), $._block_body,
      )),
      $._texture_block,
    ),
    _texture_block: $ => seq(field('name', alias('texture', $.keyword)), $._block_body),
    layered_texture: $ => prec(2, seq(alias($._texture_block, $.block), repeat1(alias($._texture_block, $.block)))),
    _block_body: $ => seq('{', repeat($._item), '}'),
    bracket_group: $ => seq('[', repeat($._item), ']'),

    _directive: $ => choice(
      $.declare_directive, $.macro_directive, $.if_directive, $.while_directive, $.for_directive,
      $.switch_directive, $.break_directive, $.include_directive, $.version_directive, $.default_directive,
      $.undef_directive, $.message_directive, $.fopen_directive, $.fclose_directive, $.read_directive,
      $.write_directive, $.breakpoint_directive,
    ),

    declare_directive: $ => prec.right(seq(
      field('kind', choice(hash('declare'), hash('local'))),
      repeat($._declare_modifier),
      field('target', $._lvalue),
      '=',
      field('value', choice($._rvalue, $.layered_texture)),
      optional(';'),
    )),
    _declare_modifier: $ => prec.right(choice('optional', seq('deprecated', optional('once'), optional($.string)))),
    _lvalue: $ => choice($._lvalue_path, $.tuple_target),
    _lvalue_path: $ => choice(
      $.identifier,
      alias($._lvalue_index, $.index_expression),
      alias($._lvalue_member, $.member_expression),
    ),
    _lvalue_index: $ => seq(
      field('object', choice($._lvalue_path, $._scope)), token.immediate('['), field('index', $._expression), ']',
    ),
    _lvalue_member: $ => seq(field('object', choice($._lvalue_path, $._scope)), '.', field('member', $._raw_identifier)),
    tuple_target: $ => choice(
      seq('(', commaSlots(seq(repeat($._declare_modifier), $._lvalue)), ')'),
      seq('<', commaSlots($._lvalue), '>'),
      seq('{', commaSlots($._lvalue), '}'),
    ),

    macro_directive: $ => seq(
      hash('macro'),
      field('name', $.identifier),
      field('parameters', $.parameter_list),
      repeat(field('body', $._item)),
      hash('end'),
    ),
    parameter_list: $ => seq('(', optional(seq($.parameter, repeat(seq(',', $.parameter)))), ')'),
    parameter: $ => prec(1, seq(
      optional('optional'), field('name', choice($.identifier, alias($.constant, $.identifier))),
    )),

    if_directive: $ => seq(
      field('kind', choice(hash('if'), hash('ifdef'), hash('ifndef'))),
      field('condition', $.condition),
      repeat(field('consequence', $._item)),
      repeat(field('alternative', $.elseif_clause)),
      optional(field('alternative', $.else_clause)),
      hash('end'),
    ),
    condition: $ => paren($, $._expression),
    elseif_clause: $ => seq(hash('elseif'), field('condition', $.condition), repeat(field('body', $._item))),
    else_clause: $ => seq(hash('else'), repeat(field('body', $._item))),

    while_directive: $ => seq(
      hash('while'), field('condition', $.condition), repeat(field('body', $._item)), hash('end'),
    ),
    for_directive: $ => seq(
      hash('for'), '(',
      field('variable', $.identifier), ',',
      field('start', $._expression), ',',
      field('end', $._expression),
      optional(seq(',', field('step', $._expression))),
      ')',
      repeat(field('body', $._item)),
      hash('end'),
    ),

    switch_directive: $ => seq(
      hash('switch'), field('value', $.condition),
      repeat(choice($.case_clause, $.range_clause, $.else_clause)),
      hash('end'),
    ),
    case_clause: $ => seq(hash('case'), field('value', $.condition), repeat(field('body', $._item))),
    range_clause: $ => seq(
      hash('range'), '(', field('low', $._expression), ',', field('high', $._expression), ')',
      repeat(field('body', $._item)),
    ),
    break_directive: _ => hash('break'),
    breakpoint_directive: _ => hash('breakpoint'),

    include_directive: $ => seq(hash('include'), field('file', $._value)),
    version_directive: $ => prec.right(seq(hash('version'), field('version', $._value), optional(';'))),
    default_directive: $ => seq(hash('default'), $._block_body),
    undef_directive: $ => seq(hash('undef'), field('name', $.identifier)),
    message_directive: $ => seq(
      field('kind', choice(hash('debug'), hash('warning'), hash('error'), hash('render'), hash('statistics'))),
      field('message', $._value),
    ),
    fopen_directive: $ => seq(
      hash('fopen'), field('handle', $.identifier), field('file', $._value),
      field('mode', choice('read', 'write', 'append')),
    ),
    fclose_directive: $ => seq(hash('fclose'), field('handle', $.identifier)),
    read_directive: $ => seq(
      hash('read'), '(', field('handle', $.identifier), repeat(choice(',', field('target', $._lvalue))), ')',
    ),
    write_directive: $ => seq(
      hash('write'), '(', field('handle', $.identifier),
      repeat(choice(',', alias(choice(...BINARY_TYPES), $.keyword), field('value', $._value))), ')',
    ),

    _rvalue: $ => choice(
      $._value, $.colour_expression, $.block, $.function_block, $.array_expression, $.dictionary_expression,
    ),
    _argument: $ => choice(
      $._expression, $.colour_expression, $.block, $.function_block, $.array_expression, $.dictionary_expression,
    ),

    _expression: $ => choice($._value, alias($._logic_expression, $.binary_expression), $.conditional_expression),
    _logic_expression: $ => choice(
      prec.left(PREC.relational, seq(
        field('left', $._expression),
        field('operator', choice('<', '<=', '=', '!=', '>=', '>')),
        field('right', $._expression),
      )),
      prec.left(PREC.logical, seq(
        field('left', $._expression), field('operator', choice('&', '|')), field('right', $._expression),
      )),
    ),
    conditional_expression: $ => prec.right(PREC.conditional, seq(
      field('condition', $._expression), '?', field('consequence', $._expression), ':',
      field('alternative', $._expression),
    )),

    _value: $ => choice($.binary_expression, $.unary_expression, $._primary),
    binary_expression: $ => choice(
      prec.left(PREC.additive, seq(
        field('left', $._value), field('operator', choice('+', '-')), field('right', $._value),
      )),
      prec.left(PREC.multiplicative, seq(
        field('left', $._value), field('operator', choice('*', '/')), field('right', $._value),
      )),
    ),
    unary_expression: $ => prec(PREC.unary, seq(field('operator', choice('-', '+', '!')), field('operand', $._value))),

    _primary: $ => choice(
      $.number, $.string, $.identifier, $.constant, $.vector, $.parenthesized_expression, $.tuple,
      $.call_expression, $.index_expression, alias($._spaced_index, $.index_expression), $.member_expression,
      prec(-1, $.if_directive),
    ),
    constant: _ => choice(...CONSTANTS),
    builtin_function: _ => choice(...FUNCTIONS),
    parenthesized_expression: $ => paren($, $._expression),
    tuple: $ => seq('(', $._expression, repeat1(seq(',', $._expression)), ')'),
    vector: $ => seq('<', $._component, repeat(seq(optional(','), $._component)), optional(','), '>'),
    _component: $ => choice($._value, $.block, $.function_block),
    call_expression: $ => prec(PREC.call, choice(
      seq(field('function', $.identifier), field('arguments', alias($.macro_arguments, $.argument_list))),
      seq(field('function', $.builtin_function), field('arguments', $.argument_list)),
    )),
    argument_list: $ => seq('(', commaSlots($._argument), ')'),
    macro_arguments: $ => seq('(', repeat(choice($._rvalue, ',')), ')'),
    index_expression: $ => prec(PREC.postfix, seq(
      field('object', choice($.identifier, $._scope, $.call_expression, $.index_expression, $.member_expression)),
      token.immediate('['), field('index', $._expression), ']',
    )),
    _spaced_index: $ => prec(-1, seq(
      field('object', choice($.identifier, $.call_expression, $.index_expression, $.member_expression)),
      '[', field('index', $._expression), ']',
    )),
    member_expression: $ => prec(PREC.postfix, seq(
      field('object', choice($._primary, $._scope)), '.',
      field('member', $._raw_identifier),
    )),

    colour_expression: $ => prec.right(choice(
      seq(choice('color', 'colour'), choice($._value, $.colour_component), repeat($.colour_component)),
      seq($.colour_component, repeat($.colour_component)),
    )),
    colour_component: $ => prec.right(seq(field('channel', choice(...COLOUR_CHANNELS)), field('value', $._value))),

    _scope: $ => alias(choice('local', 'global'), $.identifier),
    _raw_identifier: $ => reserved('raw', $.identifier),

    function_block: $ => seq(
      'function',
      optional(choice(
        field('parameters', $.parameter_list), seq(field('width', $._value), ',', field('height', $._value)),
      )),
      '{', optional(field('body', choice($._expression, $.block))), '}',
    ),

    array_expression: $ => prec.right(seq(
      'array', optional('mixed'), repeat(seq('[', optional(field('size', $._expression)), ']')),
      optional(field('initializer', $.array_initializer)),
    )),
    array_initializer: $ => seq('{', repeat(choice($._rvalue, $.array_initializer, $._directive, ',')), '}'),

    dictionary_expression: $ => seq('dictionary', '{', repeat(choice($.dictionary_entry, $._directive, ',')), '}'),
    dictionary_entry: $ => seq(
      choice(seq('.', field('key', $._raw_identifier)), seq('[', field('key', $._expression), ']')), ':',
      field('value', $._rvalue),
    ),

    tag_filter: $ => seq(
      field('name', alias(choice(...TAG_FILTERS), $.keyword)), '{', optional(field('filter', $._tag_expression)), '}',
    ),
    _tag_expression: $ => choice(
      $.string, $.identifier, alias(choice('any', 'none'), $.constant),
      alias($._tag_parenthesized, $.parenthesized_expression),
      alias($._tag_not, $.unary_expression),
      alias($._tag_binary, $.binary_expression),
    ),
    _tag_parenthesized: $ => seq('(', $._tag_expression, ')'),
    _tag_not: $ => prec(3, seq(field('operator', '!'), field('operand', $._tag_expression))),
    _tag_binary: $ => choice(
      prec.left(2, seq(field('left', $._tag_expression), field('operator', '&'), field('right', $._tag_expression))),
      prec.left(1, seq(field('left', $._tag_expression), field('operator', '|'), field('right', $._tag_expression))),
    ),

    identifier: _ => /[A-Za-z_][A-Za-z0-9_]*/,
    number: _ => /\d+\.?\d*([eE][+-]?\d+)?|\.\d+([eE][+-]?\d+)?/,
    string: _ => /"(?:[^"\\]|\\[\s\S])*"/,
    line_comment: _ => token(seq('//', /[^\n]*/)),
  },
});

function commaSlots(rule) {
  return seq(optional(rule), repeat(seq(',', optional(rule))));
}
