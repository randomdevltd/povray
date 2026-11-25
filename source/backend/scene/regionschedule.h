//******************************************************************************
///
/// @file backend/scene/regionschedule.h
///
/// Orders the tiles of a progressive render's final passes so that a cache budget loads each cache section as few
/// times as it can: greedy working-set clusters over the sections each tile touched in the coarse passes.
///
/// @copyright
/// @parblock
///
/// Persistence of Vision Ray Tracer ('POV-Ray') version 3.8.
/// Copyright 1991-2019 Persistence of Vision Raytracer Pty. Ltd.
///
/// POV-Ray is free software: you can redistribute it and/or modify
/// it under the terms of the GNU Affero General Public License as
/// published by the Free Software Foundation, either version 3 of the
/// License, or (at your option) any later version.
///
/// POV-Ray is distributed in the hope that it will be useful,
/// but WITHOUT ANY WARRANTY; without even the implied warranty of
/// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
/// GNU Affero General Public License for more details.
///
/// You should have received a copy of the GNU Affero General Public License
/// along with this program.  If not, see <http://www.gnu.org/licenses/>.
///
/// ----------------------------------------------------------------------------
///
/// POV-Ray is based on the popular DKB raytracer version 2.12.
/// DKBTrace was originally written by David K. Buck.
/// DKBTrace Ver 2.0-2.12 were written by David K. Buck & Aaron A. Collins.
///
/// @endparblock
///
//******************************************************************************

#ifndef POVRAY_BACKEND_REGIONSCHEDULE_H
#define POVRAY_BACKEND_REGIONSCHEDULE_H

// C++ standard header files
#include <algorithm>
#include <cstdint>
#include <functional>
#include <list>
#include <queue>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace pov
{

/// A tile's distance along the Hilbert curve over an n x n grid of tiles, n a power of two.
inline std::uint64_t RegionHilbert(std::uint32_t n, std::uint32_t x, std::uint32_t y)
{
    std::uint64_t d = 0;
    for (std::uint32_t s = n / 2; s > 0; s /= 2)
    {
        const std::uint32_t rx = (x & s) ? 1 : 0, ry = (y & s) ? 1 : 0;
        d += std::uint64_t(s) * s * ((3 * rx) ^ ry);
        if (ry == 0)
        {
            if (rx == 1)
            {
                x = n - 1 - x;
                y = n - 1 - y;
            }
            std::swap(x, y);
        }
    }
    return d;
}

/// One tile of the image: where it lies on the Hilbert curve, and the cache sections its rays touched.
struct RegionTile final
{
    std::uint64_t hilbert = 0;
    std::vector<std::uint32_t> blocks; ///< sorted indices into the section sizes
};

/// The tile order the final passes follow, cut into clusters whose sections fit the capacity together.
struct RegionPlan final
{
    std::vector<std::uint32_t> order;                      ///< tile indices in render order
    std::vector<std::uint32_t> clusterStart;               ///< where each cluster starts in order
    std::vector<std::vector<std::uint32_t>> clusterBlocks; ///< each cluster's sections, sorted
    std::uint64_t unionBytes = 0;                          ///< every section touched, once
    std::uint64_t plannedBytes = 0;                        ///< what the clusters load, each keeping the one before resident
};

/// Greedy working-set schedule. Starting from the tile with the largest footprint, it takes the tile with the fewest bytes
/// not yet resident (the cluster being built and the one before it, or `resident` for the first), ties going to the
/// earlier tile on the Hilbert curve; a tile whose sections would take the cluster past `capacity` starts the next one.
/// Costs fall through an inverted index from section to tiles and are kept in a lazy heap; each new cluster costs the
/// remaining tiles afresh. O(T·F·log T + K·T·F) for T tiles of F sections in K clusters.
inline RegionPlan PlanRegions(const std::vector<RegionTile>& tiles, const std::vector<std::uint64_t>& bytes, std::uint64_t capacity,
                              const std::vector<char>& resident)
{
    RegionPlan plan;
    const std::uint32_t count = std::uint32_t(tiles.size());
    if (count == 0)
        return plan;
    std::vector<std::vector<std::uint32_t>> users(bytes.size());
    std::vector<std::uint64_t> footprint(count, 0);
    for (std::uint32_t t = 0; t < count; t++)
        for (std::uint32_t b : tiles[t].blocks)
        {
            users[b].push_back(t);
            footprint[t] += bytes[b];
        }
    for (size_t b = 0; b < bytes.size(); b++)
        if (!users[b].empty())
            plan.unionBytes += bytes[b];

    // a section is free to the cluster being built when it is in it, or was in the one before (resident at the start)
    std::int64_t cluster = 0;
    std::vector<std::int64_t> inCluster(bytes.size(), -1), inPrevious(bytes.size(), -1);
    for (size_t b = 0; (b < bytes.size()) && (b < resident.size()); b++)
        if (resident[b])
            inPrevious[b] = 0;
    auto isFree = [&](std::uint32_t b) { return (inCluster[b] == cluster) || (inPrevious[b] == cluster); };

    typedef std::tuple<std::uint64_t, std::uint64_t, std::uint32_t> Entry;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> heap;
    std::vector<std::uint64_t> cost(count, 0);
    std::vector<char> done(count, 0);
    auto rebuild = [&]()
    {
        std::vector<Entry> entries;
        entries.reserve(count);
        for (std::uint32_t t = 0; t < count; t++)
        {
            if (done[t])
                continue;
            std::uint64_t c = 0;
            for (std::uint32_t b : tiles[t].blocks)
                if (!isFree(b))
                    c += bytes[b];
            cost[t] = c;
            entries.emplace_back(c, tiles[t].hilbert, t);
        }
        heap = std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>>(std::greater<Entry>(), std::move(entries));
    };
    rebuild();

    std::uint32_t first = 0;
    for (std::uint32_t t = 1; t < count; t++)
        if ((footprint[t] > footprint[first]) || ((footprint[t] == footprint[first]) && (tiles[t].hilbert < tiles[first].hilbert)))
            first = t;
    bool forced = true;
    std::uint64_t held = 0;
    std::vector<std::uint32_t> current;
    plan.clusterStart.push_back(0);
    while (plan.order.size() < count)
    {
        std::uint32_t t;
        if (forced)
        {
            t = first;
            forced = false;
        }
        else
        {
            const Entry top = heap.top();
            heap.pop();
            t = std::get<2>(top);
            if (done[t] || (std::get<0>(top) != cost[t]))
                continue; // stale: a fresher entry for it is in the heap
        }
        std::uint64_t add = 0;
        for (std::uint32_t b : tiles[t].blocks)
            if (inCluster[b] != cluster)
                add += bytes[b];
        if ((held > 0) && (held + add > capacity))
        {
            // the cluster is full: its sections are what the next one starts with resident
            std::sort(current.begin(), current.end());
            for (std::uint32_t b : current)
                inPrevious[b] = cluster + 1;
            plan.clusterBlocks.push_back(std::move(current));
            current.clear();
            cluster++;
            held = 0;
            plan.clusterStart.push_back(std::uint32_t(plan.order.size()));
            rebuild();
            continue;
        }
        done[t] = 1;
        plan.order.push_back(t);
        for (std::uint32_t b : tiles[t].blocks)
        {
            if (inCluster[b] == cluster)
                continue;
            const bool wasFree = (inPrevious[b] == cluster);
            inCluster[b] = cluster;
            current.push_back(b);
            held += bytes[b];
            if (wasFree)
                continue;
            plan.plannedBytes += bytes[b];
            // every tile needing it is now that much cheaper
            for (std::uint32_t u : users[b])
                if (!done[u])
                {
                    cost[u] -= bytes[b];
                    heap.emplace(cost[u], tiles[u].hilbert, u);
                }
        }
    }
    std::sort(current.begin(), current.end());
    plan.clusterBlocks.push_back(std::move(current));
    return plan;
}

/// For each section, the clusters that use it, ascending: the known future that eviction ranks sections by.
inline std::vector<std::vector<std::uint32_t>> RegionUses(const RegionPlan& plan, size_t sections)
{
    std::vector<std::vector<std::uint32_t>> uses(sections);
    for (std::uint32_t k = 0; k < plan.clusterBlocks.size(); k++)
        for (std::uint32_t b : plan.clusterBlocks[k])
            uses[b].push_back(k);
    return uses;
}

/// Bytes a least-recently-used cache of `capacity` bytes loads running the tiles in this order `passes` times over; the
/// baseline the plan is reported against and tested with.
inline std::uint64_t RegionLruLoads(const std::vector<RegionTile>& tiles, const std::vector<std::uint64_t>& bytes,
                                    const std::vector<std::uint32_t>& order, std::uint64_t capacity, int passes)
{
    std::list<std::uint32_t> lru;
    std::unordered_map<std::uint32_t, std::list<std::uint32_t>::iterator> where;
    std::uint64_t held = 0, loaded = 0;
    for (int pass = 0; pass < passes; pass++)
        for (std::uint32_t t : order)
        {
            for (std::uint32_t b : tiles[t].blocks)
            {
                auto at = where.find(b);
                if (at != where.end())
                {
                    lru.splice(lru.begin(), lru, at->second);
                    continue;
                }
                loaded += bytes[b];
                held += bytes[b];
                lru.push_front(b);
                where[b] = lru.begin();
            }
            // the tile's own sections stay while it runs; the rest go oldest first
            while ((held > capacity) && (lru.size() > tiles[t].blocks.size()))
            {
                held -= bytes[lru.back()];
                where.erase(lru.back());
                lru.pop_back();
            }
        }
    return loaded;
}

}
// end of namespace pov

#endif // POVRAY_BACKEND_REGIONSCHEDULE_H
