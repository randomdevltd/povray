// SPDX-License-Identifier: AGPL-3.0-or-later
#include "tree_sitter/parser.h"

#include <wctype.h>

enum TokenType { BLOCK_COMMENT, TAGGED };

// Skips what the tokenizer skips between tagged and its brace: white space and comments, block comments nesting.
static void skip_space_and_comments(TSLexer *lexer)
{
    while (true)
    {
        while (iswspace(lexer->lookahead))
            lexer->advance(lexer, false);
        if (lexer->lookahead != '/')
            return;
        lexer->advance(lexer, false);
        if (lexer->lookahead == '/')
        {
            while (!lexer->eof(lexer) && (lexer->lookahead != '\n'))
                lexer->advance(lexer, false);
            continue;
        }
        if (lexer->lookahead != '*')
            return;
        lexer->advance(lexer, false);
        unsigned depth = 1;
        while ((depth > 0) && !lexer->eof(lexer))
        {
            int32_t c = lexer->lookahead;
            lexer->advance(lexer, false);
            if ((c == '*') && (lexer->lookahead == '/'))
            {
                lexer->advance(lexer, false);
                --depth;
            }
            else if ((c == '/') && (lexer->lookahead == '*'))
            {
                lexer->advance(lexer, false);
                ++depth;
            }
        }
    }
}

void *tree_sitter_pov_external_scanner_create(void) { return NULL; }
void tree_sitter_pov_external_scanner_destroy(void *payload) { (void)payload; }
unsigned tree_sitter_pov_external_scanner_serialize(void *payload, char *buffer)
{
    (void)payload;
    (void)buffer;
    return 0;
}
void tree_sitter_pov_external_scanner_deserialize(void *payload, const char *buffer, unsigned length)
{
    (void)payload; (void)buffer; (void)length;
}

bool tree_sitter_pov_external_scanner_scan(void *payload, TSLexer *lexer, const bool *valid_symbols)
{
    (void)payload;
    while (iswspace(lexer->lookahead))
        lexer->advance(lexer, true);
    // tagged is contextual: a keyword only when a block follows
    if (valid_symbols[TAGGED] && (lexer->lookahead == 't'))
    {
        static const char word[] = "tagged";
        for (const char *p = word; *p; ++p)
        {
            if (lexer->lookahead != *p)
                return false;
            lexer->advance(lexer, false);
        }
        if (iswalnum(lexer->lookahead) || (lexer->lookahead == '_'))
            return false;
        lexer->mark_end(lexer);
        skip_space_and_comments(lexer);
        lexer->result_symbol = TAGGED;
        return lexer->lookahead == '{';
    }
    if (!valid_symbols[BLOCK_COMMENT] || (lexer->lookahead != '/'))
        return false;
    lexer->advance(lexer, false);
    if (lexer->lookahead != '*')
        return false;
    lexer->advance(lexer, false);
    unsigned depth = 1;
    while (depth > 0)
    {
        if (lexer->eof(lexer))
            return false;
        int32_t c = lexer->lookahead;
        lexer->advance(lexer, false);
        if (c == '*' && lexer->lookahead == '/')
        {
            lexer->advance(lexer, false);
            --depth;
        }
        else if (c == '/' && lexer->lookahead == '*')
        {
            lexer->advance(lexer, false);
            ++depth;
        }
    }
    lexer->result_symbol = BLOCK_COMMENT;
    return true;
}
