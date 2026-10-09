// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef POVRAY_CORE_TAGFILTER_H
#define POVRAY_CORE_TAGFILTER_H

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace pov
{

using PreparedSetId = std::size_t;
struct TagFilterNode;

struct TagFilter final
{
    bool specified = false;
    std::shared_ptr<const TagFilterNode> root;
    std::string expression;

    bool operator==(const TagFilter& other) const
    {
        return specified == other.specified && expression == other.expression;
    }

    bool operator<(const TagFilter& other) const
    {
        return specified != other.specified ? specified < other.specified : expression < other.expression;
    }
};

/// The tags of a union that was split into its children, which each child must still satisfy.
struct TagScope final
{
    std::vector<std::string> tags;
    std::shared_ptr<const TagScope> enclosing;
};

TagFilter ParseTagFilter(const std::string& expression);
bool MatchesTags(const std::vector<std::string>& sortedUniqueTags, const TagFilter& filter);
void MergeTags(std::vector<std::string>& sortedUniqueTags, const std::vector<std::string>& added);

}

#endif
