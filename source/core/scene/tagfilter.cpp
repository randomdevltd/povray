// SPDX-License-Identifier: AGPL-3.0-or-later

#include "core/scene/tagfilter.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>

#include "tree_sitter/api.h"

extern "C" const TSLanguage* tree_sitter_pov_tags();

namespace pov
{

struct TagFilterNode final
{
    enum Kind { Pattern, Any, None, Not, And, Or };
    Kind kind;
    std::string pattern;
    std::shared_ptr<const TagFilterNode> left;
    std::shared_ptr<const TagFilterNode> right;
};

namespace
{

using NodePtr = std::shared_ptr<const TagFilterNode>;

TSNode ExpressionChild(TSNode node, uint32_t index)
{
    for (uint32_t i = 0; i < ts_node_named_child_count(node); ++i)
    {
        TSNode child = ts_node_named_child(node, i);
        if (std::strcmp(ts_node_type(child), "comment") != 0 && index-- == 0)
            return child;
    }
    throw std::runtime_error("Expected a tag filter expression");
}

std::string DecodeString(const std::string& text)
{
    std::string value;
    for (std::size_t i = 1; i + 1 < text.size(); ++i)
    {
        char c = text[i];
        if (c == '\\')
        {
            c = text[++i];
            switch (c)
            {
                case 'n': c = '\n'; break;
                case 'r': c = '\r'; break;
                case 't': c = '\t'; break;
                case 'b': c = '\b'; break;
                case 'f': c = '\f'; break;
            }
        }
        value += c;
    }
    return value;
}

std::string QuoteString(const std::string& value)
{
    std::string quoted = "\"";
    for (char c : value)
    {
        switch (c)
        {
            case '"': quoted += "\\\""; break;
            case '\\': quoted += "\\\\"; break;
            case '\n': quoted += "\\n"; break;
            case '\r': quoted += "\\r"; break;
            case '\t': quoted += "\\t"; break;
            case '\b': quoted += "\\b"; break;
            case '\f': quoted += "\\f"; break;
            default: quoted += c;
        }
    }
    return quoted + '"';
}

NodePtr BuildExpression(TSNode node, const std::string& source, std::string& canonical, unsigned depth)
{
    if (depth > 256)
        throw std::runtime_error("Tag filter expression exceeds 256 nesting levels");
    const char* type = ts_node_type(node);
    if (std::strcmp(type, "source_file") == 0 || std::strcmp(type, "filter_tags") == 0 ||
        std::strcmp(type, "parenthesized_expression") == 0)
        return BuildExpression(ExpressionChild(node, 0), source, canonical, depth + 1);

    auto result = std::make_shared<TagFilterNode>();
    if (std::strcmp(type, "string") == 0)
    {
        result->kind = TagFilterNode::Pattern;
        result->pattern = DecodeString(source.substr(ts_node_start_byte(node), ts_node_end_byte(node) - ts_node_start_byte(node)));
        canonical = QuoteString(result->pattern);
    }
    else if (std::strcmp(type, "any") == 0 || std::strcmp(type, "none") == 0)
    {
        result->kind = std::strcmp(type, "any") == 0 ? TagFilterNode::Any : TagFilterNode::None;
        canonical = type;
    }
    else if (std::strcmp(type, "not_expression") == 0)
    {
        result->kind = TagFilterNode::Not;
        result->left = BuildExpression(ExpressionChild(node, 0), source, canonical, depth + 1);
        canonical = "!(" + canonical + ")";
    }
    else if (std::strcmp(type, "and_expression") == 0 || std::strcmp(type, "or_expression") == 0)
    {
        result->kind = std::strcmp(type, "and_expression") == 0 ? TagFilterNode::And : TagFilterNode::Or;
        std::string right;
        result->left = BuildExpression(ExpressionChild(node, 0), source, canonical, depth + 1);
        result->right = BuildExpression(ExpressionChild(node, 1), source, right, depth + 1);
        canonical = "(" + canonical + (result->kind == TagFilterNode::And ? "&" : "|") + right + ")";
    }
    else
        throw std::runtime_error("Expected a tag filter expression");
    return result;
}

bool MatchPattern(const std::string& tag, const std::string& pattern)
{
    std::size_t t = 0, p = 0, star = std::string::npos, retry = 0;
    while (t < tag.size())
    {
        if (p < pattern.size() && pattern[p] == '*')
        {
            star = p++;
            retry = t;
        }
        else if (p < pattern.size() && pattern[p] == tag[t])
        {
            ++p;
            ++t;
        }
        else if (star != std::string::npos)
        {
            p = star + 1;
            t = ++retry;
        }
        else
            return false;
    }
    while (p < pattern.size() && pattern[p] == '*')
        ++p;
    return p == pattern.size();
}

bool Evaluate(const TagFilterNode& node, const std::vector<std::string>& tags)
{
    switch (node.kind)
    {
        case TagFilterNode::Any: return !tags.empty();
        case TagFilterNode::None: return tags.empty();
        case TagFilterNode::Not: return !Evaluate(*node.left, tags);
        case TagFilterNode::And: return Evaluate(*node.left, tags) && Evaluate(*node.right, tags);
        case TagFilterNode::Or: return Evaluate(*node.left, tags) || Evaluate(*node.right, tags);
        case TagFilterNode::Pattern:
            if (node.pattern.find('*') == std::string::npos)
                return std::binary_search(tags.begin(), tags.end(), node.pattern);
            for (const auto& tag : tags)
                if (MatchPattern(tag, node.pattern))
                    return true;
            return false;
    }
    return false;
}

std::string At(std::size_t at, const std::string& expression)
{
    return " at character " + std::to_string(at + 1) + " of '" + expression + "'";
}

// Scans an expression the grammar rejected, token by token, to name the first error and its 1-based character.
std::string DescribeError(TSNode root, const std::string& expression)
{
    static const char *const kOperand = "; expected a quoted pattern, any, none, ! or (";
    bool operand = true;
    unsigned depth = 0;
    std::size_t i = 0;
    while (i < expression.size())
    {
        const char c = expression[i];
        if (std::isspace(static_cast<unsigned char>(c)))
        {
            ++i;
            continue;
        }
        if (expression.compare(i, 2, "//") == 0)
        {
            i = expression.find('\n', i);
            continue;
        }
        if (expression.compare(i, 2, "/*") == 0)
        {
            const std::size_t close = expression.find("*/", i + 2);
            if (close == std::string::npos)
                return "unclosed comment" + At(i, expression);
            i = close + 2;
            continue;
        }
        const std::size_t start = i;
        std::string token(1, c);
        if (c == '"')
        {
            for (++i; (i < expression.size()) && (expression[i] != '"'); ++i)
            {
                if (static_cast<unsigned char>(expression[i]) < 0x20)
                    return "control character in a quoted pattern" + At(i, expression);
                if (expression[i] == '\\')
                {
                    if ((i + 1 >= expression.size()) || (std::strchr("\"\\nrtbf", expression[i + 1]) == nullptr))
                        return "unknown escape in a quoted pattern" + At(i, expression);
                    ++i;
                }
            }
            if (i >= expression.size())
                return "unclosed quote" + At(start, expression);
            token = expression.substr(start, ++i - start);
        }
        else if (std::isalnum(static_cast<unsigned char>(c)) || (c == '_'))
        {
            while ((i < expression.size()) && (std::isalnum(static_cast<unsigned char>(expression[i])) || (expression[i] == '_')))
                ++i;
            token = expression.substr(start, i - start);
        }
        else
            ++i;
        const bool word = (token[0] == '"') || (token == "any") || (token == "none");
        if (operand && word)
            operand = false;
        else if (operand && ((token == "!") || (token == "(")))
            depth += (token == "(");
        else if (!operand && ((token == "&") || (token == "|")))
            operand = true;
        else if (!operand && (token == ")") && (depth > 0))
            --depth;
        else
            return "unexpected '" + token + "'" + At(start, expression) + (operand ? kOperand : (depth > 0) ? "; expected &, | or )" : "; expected & or |");
    }
    if (operand || (depth > 0))
        return "expression ends early" + At(expression.size(), expression) + (operand ? kOperand : "; expected )");
    return "invalid expression" + At(ts_node_start_byte(root), expression);
}

}

TagFilter ParseTagFilter(const std::string& expression)
{
    if (expression.size() > 1024 * 1024)
        throw std::runtime_error("Tag filter expression exceeds 1 MiB");
    std::unique_ptr<TSParser, decltype(&ts_parser_delete)> parser(ts_parser_new(), ts_parser_delete);
    if (!parser || !ts_parser_set_language(parser.get(), tree_sitter_pov_tags()))
        throw std::runtime_error("Unable to initialize tag filter grammar");
    std::unique_ptr<TSTree, decltype(&ts_tree_delete)> tree(
        ts_parser_parse_string(parser.get(), nullptr, expression.data(), static_cast<uint32_t>(expression.size())), ts_tree_delete);
    if (!tree)
        throw std::runtime_error("Unable to parse tag filter");
    if (ts_node_has_error(ts_tree_root_node(tree.get())))
        throw std::runtime_error(DescribeError(ts_tree_root_node(tree.get()), expression));
    TagFilter result;
    result.specified = true;
    result.root = BuildExpression(ts_tree_root_node(tree.get()), expression, result.expression, 0);
    return result;
}

void MergeTags(std::vector<std::string>& tags, const std::vector<std::string>& added)
{
    if (added.empty() || std::includes(tags.begin(), tags.end(), added.begin(), added.end()))
        return;
    std::vector<std::string> merged;
    merged.reserve(tags.size() + added.size());
    std::set_union(tags.begin(), tags.end(), added.begin(), added.end(), std::back_inserter(merged));
    tags.swap(merged);
}

bool MatchesTags(const std::vector<std::string>& sortedUniqueTags, const TagFilter& filter)
{
    return !filter.specified || (filter.root && Evaluate(*filter.root, sortedUniqueTags));
}

}
