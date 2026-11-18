#ifndef POVRAY_CORE_LIGHTING_SURFACEMAPSTATS_H
#define POVRAY_CORE_LIGHTING_SURFACEMAPSTATS_H

#include "core/lighting/surfacemap.h"
#include <map>

namespace pov
{
namespace surface_map
{

/// Chunks built and lines traced, by level.
using Levels = std::map<int, std::array<std::size_t, 2>>;

/// What maps built, for reports and tests: it reads their published chunks, and nothing reads it back.
struct Stats final
{
    template<class F>
    static void Walk(const Map::Node& node, const F& f)
    {
        for (const auto& r : node.regions)
            for (const Map::Chunk* c = r->first.load(std::memory_order_acquire); c != nullptr; c = c->finer.load(std::memory_order_acquire))
                if (c->published.load(std::memory_order_acquire)) f(*r, *c);
        for (const auto& child : node.children)
            if (const Map::Node* next = child.load(std::memory_order_acquire)) Walk(*next, f);
    }

    /// Chunks built, lines traced and points found over every chunk of a map, and their ids' digest.
    static void Totals(const Map& map, std::size_t& chunks, std::size_t& traced, std::size_t& found, std::uint64_t& digest,
                       Levels* levels)
    {
        chunks = traced = found = 0;
        digest = 0;
        Walk(map.root, [&](const Map::Region& r, const Map::Chunk& c)
        {
            ++chunks;
            traced += c.traced;
            found += c.records.size();
            if (levels) { ++(*levels)[c.level][0]; (*levels)[c.level][1] += c.traced; }
            Map::Part part;
            part.region = &r;
            for (std::size_t i = 0; i < c.records.size(); ++i) digest ^= BounceOrderMix(Map::Id(part, c, i));
        });
    }

    /// The same over every map, and how many maps there are.
    static void Totals(const Maps& maps, std::size_t& chunks, std::size_t& traced, std::size_t& found, std::uint64_t& digest,
                       Levels* levels, std::size_t* count)
    {
        chunks = traced = found = 0;
        digest = 0;
        if (count) *count = 0;
        for (const Maps::Entry* e = maps.head.load(std::memory_order_acquire); e != nullptr; e = e->next)
        {
            std::size_t c, t, f;
            std::uint64_t d;
            Totals(*e->map, c, t, f, d, levels);
            if (count) ++*count;
            chunks += c;
            traced += t;
            found += f;
            digest ^= d;
        }
    }
};

inline void Totals(const Map& map, std::size_t& chunks, std::size_t& traced, std::size_t& found, std::uint64_t& digest,
                   Levels* levels = nullptr)
{
    Stats::Totals(map, chunks, traced, found, digest, levels);
}

inline void Totals(const Maps& maps, std::size_t& chunks, std::size_t& traced, std::size_t& found, std::uint64_t& digest,
                   Levels* levels = nullptr, std::size_t* count = nullptr)
{
    Stats::Totals(maps, chunks, traced, found, digest, levels, count);
}

/// "level chunks/lines" for each level, finest last.
inline std::string LevelsText(const Levels& levels)
{
    std::string text;
    for (const auto& l : levels)
        text += " " + std::to_string(l.first) + ":" + std::to_string(l.second[0]) + "/" + std::to_string(l.second[1]);
    return text;
}

} // namespace surface_map
} // namespace pov

#endif
