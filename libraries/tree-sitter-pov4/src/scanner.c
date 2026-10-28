// SPDX-License-Identifier: AGPL-3.0-or-later
// Classifies reserved words with one lookup instead of one lexer token per word.

#include "tree_sitter/parser.h"

#include <stdlib.h>
#include <string.h>

#include "words.h"

enum TokenType { KEYWORD, BUILTIN, COLOUR_OPERATOR, COLOR_OPERATOR, COLOUR_CHANNEL, TAG_KEYWORD, FIELD_KEYWORD };

static int compare(const void *key, const void *entry) { return strcmp((const char *)key, *(const char *const *)entry); }

static bool contains(const char *word, const char *const *list, size_t count)
{
    return bsearch(word, list, count, sizeof(*list), compare) != NULL;
}

#define CONTAINS(word, list) contains(word, list, sizeof(list) / sizeof(list[0]))

static bool is_word_char(int32_t c, bool first)
{
    return (c == '_') || ((c >= 'a') && (c <= 'z')) || ((c >= 'A') && (c <= 'Z')) || (!first && (c >= '0') && (c <= '9'));
}

// Skips what the classic tokenizer skips between tagged and its brace: white space and comments.
static void skip_space_and_comments(TSLexer *lexer)
{
    while (true)
    {
        while ((lexer->lookahead == ' ') || (lexer->lookahead == '\t') || (lexer->lookahead == '\n') ||
               (lexer->lookahead == '\r') || (lexer->lookahead == '\f') || (lexer->lookahead == '\v'))
            lexer->advance(lexer, false);
        if (lexer->lookahead != '/')
            return;
        lexer->advance(lexer, false);
        if (lexer->lookahead == '/')
        {
            while (!lexer->eof(lexer) && (lexer->lookahead != '\n'))
                lexer->advance(lexer, false);
        }
        else if (lexer->lookahead == '*')
        {
            lexer->advance(lexer, false);
            int32_t previous = 0;
            while (!lexer->eof(lexer) && !((previous == '*') && (lexer->lookahead == '/')))
            {
                previous = lexer->lookahead;
                lexer->advance(lexer, false);
            }
            lexer->advance(lexer, false);
        }
        else
            return;
    }
}

void *tree_sitter_pov4_external_scanner_create(void) { return NULL; }
void tree_sitter_pov4_external_scanner_destroy(void *payload) { (void)payload; }
unsigned tree_sitter_pov4_external_scanner_serialize(void *payload, char *buffer) { (void)payload; (void)buffer; return 0; }
void tree_sitter_pov4_external_scanner_deserialize(void *payload, const char *buffer, unsigned length)
{
    (void)payload; (void)buffer; (void)length;
}

bool tree_sitter_pov4_external_scanner_scan(void *payload, TSLexer *lexer, const bool *valid)
{
    (void)payload;
    if (!valid[KEYWORD] && !valid[BUILTIN] && !valid[COLOUR_OPERATOR] && !valid[COLOR_OPERATOR] && !valid[COLOUR_CHANNEL] &&
        !valid[TAG_KEYWORD] && !valid[FIELD_KEYWORD])
        return false;
    while ((lexer->lookahead == ' ') || (lexer->lookahead == '\t') || (lexer->lookahead == '\n') ||
           (lexer->lookahead == '\r') || (lexer->lookahead == '\f') || (lexer->lookahead == '\v'))
        lexer->advance(lexer, true);
    if (!is_word_char(lexer->lookahead, true))
        return false;
    char word[32];
    size_t length = 0;
    while (is_word_char(lexer->lookahead, false))
    {
        if (length + 1 >= sizeof(word))
            return false;
        word[length++] = (char)lexer->lookahead;
        lexer->advance(lexer, false);
    }
    word[length] = '\0';
    bool call = (lexer->lookahead == '(');
    if (valid[TAG_KEYWORD] && (!strcmp(word, "filter_tags") || !strcmp(word, "front_filter_tags") ||
                               !strcmp(word, "back_filter_tags") || !strcmp(word, "tagged")))
    {
        // tagged is contextual: a keyword only when a block follows
        lexer->mark_end(lexer);
        skip_space_and_comments(lexer);
        lexer->result_symbol = TAG_KEYWORD;
        return strcmp(word, "tagged") || (lexer->lookahead == '{');
    }
    if (valid[KEYWORD] && (!strcmp(word, "output") || !strcmp(word, "light")))
    {
        // output and light are contextual: a block name only when a block follows
        lexer->mark_end(lexer);
        skip_space_and_comments(lexer);
        lexer->result_symbol = KEYWORD;
        return lexer->lookahead == '{';
    }
    if (valid[FIELD_KEYWORD] && !strcmp(word, "field"))
    {
        lexer->mark_end(lexer);
        skip_space_and_comments(lexer);
        lexer->result_symbol = FIELD_KEYWORD;
        return lexer->lookahead == '{';
    }
    if (valid[BUILTIN] && call && CONTAINS(word, BUILTINS))
        lexer->result_symbol = BUILTIN;
    else if (valid[COLOUR_CHANNEL] && CONTAINS(word, CHANNELS))
        lexer->result_symbol = COLOUR_CHANNEL;
    else if (valid[KEYWORD] && CONTAINS(word, KEYWORDS))
        lexer->result_symbol = KEYWORD;
    else if (valid[BUILTIN] && CONTAINS(word, BUILTINS))
        lexer->result_symbol = BUILTIN;
    else if (valid[COLOUR_OPERATOR] && CONTAINS(word, COLOURS))
        lexer->result_symbol = COLOUR_OPERATOR;
    else if (valid[COLOR_OPERATOR] && CONTAINS(word, COLORS))
        lexer->result_symbol = COLOR_OPERATOR;
    else
        return false;
    return true;
}
