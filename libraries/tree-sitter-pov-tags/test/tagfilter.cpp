// SPDX-License-Identifier: AGPL-3.0-or-later

#include "core/scene/tagfilter.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{

void Check(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

bool Match(const std::vector<std::string>& tags, const std::string& expression)
{
    return pov::MatchesTags(tags, pov::ParseTagFilter(expression));
}

void Invalid(const std::string& expression)
{
    try { pov::ParseTagFilter(expression); }
    catch (const std::runtime_error&) { return; }
    throw std::runtime_error("Accepted invalid filter: " + expression);
}

}

int main()
{
    try
    {
        Check(pov::MatchesTags({}, pov::TagFilter{}), "Omitted filter must include untagged objects");
        Check(pov::MatchesTags({"hero"}, pov::TagFilter{}), "Omitted filter must include tagged objects");
        Check(Match({}, "none") && !Match({}, "any"), "Untagged any/none semantics");
        Check(!Match({"hero"}, "none") && Match({"hero"}, "any"), "Tagged any/none semantics");
        Check(!Match({}, "\"*\"") && Match({""}, "\"*\""), "Wildcard requires a tag");
        Check(Match({"room:left", "visible"}, "\"room:*\" & !\"hidden\""), "Boolean wildcard filter");
        Check(Match({"hero"}, "\"hero\" | \"other\" & none"), "AND before OR");
        Check(!Match({"hero"}, "(\"hero\" | \"other\") & none"), "Parentheses override precedence");
        Check(Match({"alphabet"}, "\"a**b*t\"") && !Match({"alphabet"}, "\"a*b\""), "Anchored wildcard");
        Check(Match({"a?"}, "\"a?\"") && !Match({"ab"}, "\"a?\""), "Only star is special");
        Check(!Match({"Hero"}, "\"hero\""), "Tags are case sensitive");
        Check(Match({"quote\"backslash\\\n"}, "\"quote\\\"backslash\\\\\\n\""), "Escaped string");
        Check(pov::ParseTagFilter("filter_tags { /*a*/ (any) & !none }") ==
              pov::ParseTagFilter("any&!(none)"), "Canonical expression ignores comments, whitespace, parentheses");
        const auto original = pov::ParseTagFilter("any | none");
        const auto copy = original;
        Check(copy.root == original.root && Match({}, copy.expression), "Immutable expression sharing");
        for (const char* expression : {"", "filter_tags {}", "tags {\"hero\"}", "any any", "\"a\",\"b\"",
                                      "any && none", "any || none", "!", "(any", "any)", "bare", "\"bad\\q\""})
            Invalid(expression);
        Invalid(std::string(300, '!') + "any");
        Invalid(std::string(1024 * 1024 + 1, ' '));
        std::cout << "Tag-filter semantic checks passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
