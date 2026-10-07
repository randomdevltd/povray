// SPDX-License-Identifier: AGPL-3.0-or-later

#include "core/scene/tagfilter.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>
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
    if (!tree || ts_node_has_error(ts_tree_root_node(tree.get())))
        throw std::runtime_error("Invalid tag filter: expected quoted pattern, any, none, !, &, |, or parentheses");
    TagFilter result;
    result.specified = true;
    result.root = BuildExpression(ts_tree_root_node(tree.get()), expression, result.expression, 0);
    return result;
}

bool MatchesTags(const std::vector<std::string>& sortedUniqueTags, const TagFilter& filter)
{
    return !filter.specified || (filter.root && Evaluate(*filter.root, sortedUniqueTags));
}

}
