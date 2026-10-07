// SPDX-License-Identifier: AGPL-3.0-or-later

module.exports = grammar({
  name: 'pov_tags',
  extras: $ => [/\s/, $.comment],
  rules: {
    source_file: $ => choice($._expression, $.filter_tags, $.tags),
    filter_tags: $ => seq('filter_tags', '{', $._expression, '}'),
    tags: $ => seq('tags', '{', optional(seq($.string, repeat(seq(',', $.string)), optional(','))), '}'),
    _expression: $ => choice($.string, $.any, $.none, $.not_expression, $.and_expression, $.or_expression, $.parenthesized_expression),
    not_expression: $ => prec.right(3, seq('!', $._expression)),
    and_expression: $ => prec.left(2, seq($._expression, '&', $._expression)),
    or_expression: $ => prec.left(1, seq($._expression, '|', $._expression)),
    parenthesized_expression: $ => seq('(', $._expression, ')'),
    any: _ => 'any',
    none: _ => 'none',
    string: _ => /"(?:\\["\\nrtbf]|[^"\\\x00-\x1f])*"/,
    comment: _ => token(choice(/\/\/[^\n]*/, /\/\*[^*]*\*+([^/*][^*]*\*+)*\//)),
  },
});
