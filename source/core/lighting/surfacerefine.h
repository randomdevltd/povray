#ifndef POVRAY_CORE_LIGHTING_SURFACEREFINE_H
#define POVRAY_CORE_LIGHTING_SURFACEREFINE_H

#include "core/configcore.h"
#include "core/lighting/bounceorders.h"
#include "core/lighting/surfacebounce.h"
#include "core/lighting/surfacemap.h"
#include "core/render/trace.h"
#include "core/support/preparationprogress.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace pov
{
namespace surface_refine
{

using surface_map::Coords;
using surface_map::Map;

/// One surface bounce light: its map, whose points are the light's points, and the points it may shade.
struct Part final
{
    ObjectPtr owner = nullptr;
    surface_map::Instance map;
    bool inside = false;                         // seen from inside too: an open surface, or one holding a light
    std::size_t budget = 0;
    unsigned directions = 0;                     // view directions each point's view-dependent light is averaged over
    bool keepHits = false;                       // later passes shade the same points again
    bool internal = false;                       // exports only the light of volume emitters it holds, carried out from inside
};

/// The light's points, each shaded once: where, facing which way, the area each stands for, and what each exports.
struct Layout final
{
    std::vector<Vector3d> points, normals;
    std::vector<double> spacings, areas;
    std::vector<MathColour> front, back;
    std::vector<Intersection> hits;              // kept only for later passes
    std::vector<std::uint64_t> ids;
};

/// The light's field over its points, and what placing them did; no points when its map found none.
struct Result final
{
    Layout layout;
    std::shared_ptr<const SurfaceNodeIndex> index;
    std::size_t points = 0;
    std::vector<std::size_t> levels;             // points per finest cube level, from the coarsest holding any
    std::string report;
};

/// The field of one pass: each point's front and back light over the placed points.
inline std::shared_ptr<const SurfaceShadedField> Field(const Result& result, const std::vector<MathColour>& fronts,
                                                       const std::vector<MathColour>& backs)
{
    const auto& source = result.index->Source();
    std::vector<MathColour> front(source.size()), back(source.size());
    std::vector<float> weights(source.size());
    MathColour integral;
    bool open = false;
    for (std::size_t t = 0; t < source.size(); ++t)
    {
        const double area = result.layout.areas[source[t]];
        front[t] = fronts[source[t]];
        back[t] = backs[source[t]];
        open = open || !back[t].IsZero();
        weights[t] = float((front[t] + back[t]).Greyscale() * area);
        integral += (front[t] + back[t]) * float(area);
    }
    if (!open) back.clear();
    return std::make_shared<const SurfaceShadedField>(result.index, std::move(front), std::move(back), std::move(weights), integral);
}

inline std::uint64_t Bits(double value)
{
    std::uint64_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

/// Places one light's points and shades each once. Selected pieces start as the lattice cubes of their own
/// box's size, read at the coarsest level, no coarser than the cubes, at which the piece shows a point. Then the single highest ranked cell is split,
/// again and again, until the next split would shade past the light's budget of points: its eight children are read one level finer, their
/// new points shaded, and the cell's own points kept, each in the child holding it. A child ranks by its parent's points,
/// which bound it, each standing for the area a point of the parent's read stands for: by its error in exported power,
/// how much they disagree with their mean plus their power's own counting error (power over the root of their count, the
/// count being Poisson); then by their power; then by the power of those in it. So no cell's own read decides whether it
/// is split. A leaf's area is its own read's estimate,
/// unbiased by Cauchy-Crofton whatever was split, and every point in it, its own or kept from above, takes an equal share.
/// The children of a cell holding no points are not split, nor is a cell where the map's coordinates no longer resolve
/// the next level, so the splitting ends with the budget, or with the surface found.
template<class Workers>
Result Place(Workers& workers, const Part& part, const std::vector<Trace*>& shaders, const std::function<void()>& cooperate,
             std::size_t units)
{
    Map& map = *part.map;
    const surface_map::Instance& view = part.map;

    // A shaded point, stored once: where it is in the map's frame (to give it to a child), where on this copy, its light.
    struct Point final
    {
        Vector3d frame, at, normal;
        double jacobian = 1.0;                   // this copy's area change at the point
        MathColour front, back;
        std::uint64_t id = 0;
        Intersection hit;
        double Value() const { return (front + back).Greyscale(); }
    };
    // A cell: one piece's surface in one lattice cube, read at line level `read`; its points are its own read's, then those
    // its ancestors read inside it.
    struct Cell final
    {
        std::uint32_t piece = 0;
        int level = 0, read = 0;
        Coords at{};
        double area = 0.0;                       // its own read's estimate of the piece's area in its cube
        double weight = 0.0;                     // the area a point of its read stands for, found or not
        double inclusion = 1.0;                  // inverse probability of selecting this piece
        std::vector<std::uint32_t> points;
        std::size_t own = 0;
        double rank = 0.0, power = 0.0, local = 0.0; // from its parent's points (local: those in it); a start cell's own
        bool ranked = false;                     // its parent held points
        std::uint64_t key = 0;
        bool split = false;
        double Share() const { return points.empty() ? 0.0 : area / double(points.size()); }
    };
    // A split's outcome before it is committed: its children, each with its own new points and the parent's it holds.
    struct Child final
    {
        Cell cell;
        std::vector<Point> fresh;
    };
    struct Split final
    {
        std::vector<Child> children;
        std::size_t traced = 0;
    };

    std::deque<Point> store;
    std::deque<Cell> cells;
    // Where each piece's surface can be: one box per piece.
    std::vector<SurfaceBox> boxes;
    map.Boxes(boxes);
    std::map<std::uint32_t, std::pair<Vector3d, Vector3d>> pieceBoxes;
    for (const SurfaceBox& box : boxes)
    {
        Vector3d lo, hi;
        Make_min_max_from_BBox(lo, hi, box.box);
        const auto known = pieceBoxes.emplace(box.piece, std::make_pair(lo, hi));
        for (int a = X; !known.second && a <= Z; ++a)
        {
            known.first->second.first[a] = std::min(known.first->second.first[a], lo[a]);
            known.first->second.second[a] = std::max(known.first->second.second[a], hi[a]);
        }
    }
    std::size_t shaded = 0, advanced = 0;
    auto progress = [&]()
    {
        const std::size_t target = part.budget ? std::min(units, units * shaded / part.budget) : units;
        if (target > advanced) { AdvancePreparation(workers.td->preparationProgress, target - advanced); advanced = target; }
    };
    auto keyOf = [](std::uint32_t piece, int level, const Coords& at)
    {
        std::uint64_t key = BounceOrderMix(BounceOrderMix(std::uint64_t(piece)) ^ std::uint64_t(std::int64_t(level)));
        for (std::int64_t a : at) key = BounceOrderMix(key ^ std::uint64_t(a));
        return key;
    };
    // The piece's part of a read; a piece has one surface box, so one part at most.
    auto partOf = [](const Map::Points& points, std::uint32_t piece) -> const Map::Part*
    {
        const Map::Part* found = nullptr;
        for (const Map::Part& p : points.parts)
            if (p.region->Piece() == piece)
            {
                if (found) throw std::logic_error("Surface bounce found two surface boxes of one piece in one cube");
                found = &p;
            }
        return found;
    };
    // The records of a piece's part found again, in record order, each with its hit and not yet shaded.
    auto locate = [&](const Map::Part& p, std::size_t worker, std::vector<Point>& out)
    {
        TraceThreadData* const td = workers.Thread(worker);
        std::vector<Intersection> hits;
        view.Hits(p, td, hits);
        std::size_t n = 0;
        for (const Map::Chunk* chunk : p.chunks)
            for (std::size_t i = 0; i < chunk->records.size(); ++i, ++n)
            {
                // a root the map could not find again (it has warned) stands for no area
                if (hits[n].Object == nullptr) continue;
                const surface_map::Record& r = chunk->records[i];
                Point point;
                point.frame = Map::Position(p, r);
                point.at = view.Position(p, r);
                point.normal = view.Normal(r);
                point.jacobian = view.Weight(p, r) / p.weight;
                point.id = Map::Id(p, *chunk, i);
                point.hit = hits[n];
                out.push_back(std::move(point));
            }
    };
    // A located point's light, front and (seen from inside too) back; a function of the point alone, on any worker.
    auto light = [&](Point& point, std::size_t worker)
    {
        Trace* const shader = shaders[worker];
        const ConstObjectPtr host = part.internal ? part.owner : nullptr;
        point.front = shader->BounceExport(point.hit, point.normal, point.id, part.directions, host);
        if (part.inside)
            point.back = shader->BounceExport(point.hit, -point.normal, BounceOrderMix(point.id), part.directions, host);
        if (!part.keepHits) point.hit = Intersection();
    };
    // The records of a piece's part, shaded once each.
    auto shade = [&](const Map::Part& p, std::size_t worker, std::vector<Point>& out)
    {
        const std::size_t first = out.size();
        locate(p, worker, out);
        for (std::size_t i = first; i < out.size(); ++i)
            light(out[i], worker);
    };
    auto lines = [](const Map::Part& p)
    {
        std::uint64_t count = 0;
        for (const Map::Chunk* chunk : p.chunks) count += chunk->Lines();
        return count;
    };
    // The mean value of a cell's points, which share its area equally.
    auto meanOf = [&](const Cell& c)
    {
        double sum = 0.0;
        for (std::uint32_t i : c.points) sum += store[i].Value();
        return c.points.empty() ? 0.0 : sum / double(c.points.size());
    };

    // Splits as flat work, child cubes then points, so one split keeps every worker busy; each still depends on its cell alone.
    struct Pending final
    {
        std::size_t split = 0;
        bool meets = false, found = false;
        std::size_t traced = 0;
        Child child;
    };
    auto splitAll = [&](const std::vector<const Cell*>& from, std::vector<Split>& out)
    {
        std::vector<Pending> pending;
        pending.reserve(8 * from.size());
        for (std::size_t s = 0; s < from.size(); ++s)
        {
            const Cell& parent = *from[s];
            const double side = map.Spacing(parent.level + 1);
            std::vector<std::vector<std::uint32_t>> inherited(8);
            for (std::uint32_t i : parent.points)
            {
                int o = 0;
                for (int a = X; a <= Z; ++a)
                {
                    const std::int64_t k = std::int64_t(std::floor((store[i].frame[a] - map.Origin()[a]) / side));
                    o |= int(std::min<std::int64_t>(1, std::max<std::int64_t>(0, k - 2 * parent.at[a]))) << a;
                }
                inherited[o].push_back(i);
            }
            const double mean = meanOf(parent), share = parent.weight;
            for (int o = 0; o < 8; ++o)
            {
                Pending next;
                next.split = s;
                Cell& c = next.child.cell;
                c.piece = parent.piece;
                c.inclusion = parent.inclusion;
                c.level = parent.level + 1;
                c.read = parent.read + 1;
                c.at = {2 * parent.at[X] + (o & 1), 2 * parent.at[Y] + ((o >> 1) & 1), 2 * parent.at[Z] + (o >> 2)};
                c.key = keyOf(c.piece, c.level, c.at);
                // the parent's points bound it, and those in it estimate it
                c.ranked = !parent.points.empty();
                for (std::uint32_t i : parent.points)
                {
                    c.rank += share * store[i].jacobian * std::fabs(store[i].Value() - mean);
                    c.power += share * store[i].jacobian * store[i].Value();
                }
                if (c.ranked) c.rank += c.power / std::sqrt(double(parent.points.size()));
                for (std::uint32_t i : inherited[o])
                    c.local += share * store[i].jacobian * store[i].Value();
                c.weight = parent.weight;
                // a cube the piece's box does not meet holds none of it, so it is not read
                const auto box = pieceBoxes.find(c.piece);
                next.meets = box != pieceBoxes.end();
                for (int a = X; next.meets && a <= Z; ++a)
                {
                    const double lo = map.Origin()[a] + map.Spacing(c.level) * double(c.at[a]), hi = lo + map.Spacing(c.level);
                    next.meets = lo <= box->second.second[a] && hi >= box->second.first[a];
                }
                if (!next.meets && inherited[o].empty())
                    continue;
                c.points = std::move(inherited[o]);
                pending.push_back(std::move(next));
            }
        }
        workers.For(pending.size(), cooperate, [&](std::size_t index, std::size_t worker)
        {
            workers[worker].Watch(part.owner);
            Pending& next = pending[index];
            if (!next.meets)
                return;
            Cell& c = next.child.cell;
            const Map::Points points = map.Read(c.level, c.at, c.read, workers.Thread(worker));
            if (const Map::Part* p = partOf(points, c.piece))
            {
                next.found = true;
                c.weight = p->weight * c.inclusion;
                next.traced = lines(*p);
                locate(*p, worker, next.child.fresh);
            }
        }, 0);
        std::vector<std::pair<std::uint32_t, std::uint32_t>> located;
        for (std::size_t index = 0; index < pending.size(); ++index)
            for (std::size_t i = 0; i < pending[index].child.fresh.size(); ++i)
                located.emplace_back(std::uint32_t(index), std::uint32_t(i));
        workers.For(located.size(), cooperate, [&](std::size_t index, std::size_t worker)
        {
            workers[worker].Watch(part.owner);
            light(pending[located[index].first].child.fresh[located[index].second], worker);
        }, 0);
        // a cube the piece's box does not meet holds none of it; one it meets is kept even when its read found none
        for (Pending& next : pending)
        {
            Cell& c = next.child.cell;
            for (const Point& point : next.child.fresh) c.area += point.jacobian * c.weight;
            c.own = next.child.fresh.size();
            out[next.split].traced += next.traced;
            if (next.found || !c.points.empty()) out[next.split].children.push_back(std::move(next.child));
        }
    };

    struct Entry final
    {
        double rank, power, local;
        std::uint64_t key;
        Cell* cell;
        // The most disagreeing first; then the most power; then the most of it inside; then the lower key, piece, level and
        // coordinates.
        bool operator<(const Entry& o) const
        {
            if (rank != o.rank) return rank > o.rank;
            if (power != o.power) return power > o.power;
            if (local != o.local) return local > o.local;
            if (key != o.key) return key < o.key;
            if (cell->piece != o.cell->piece) return cell->piece < o.cell->piece;
            if (cell->level != o.cell->level) return cell->level < o.cell->level;
            return cell->at < o.cell->at;
        }
    };
    std::set<Entry> queue;
    std::size_t splits = 0, finished = 0, traced = 0;
    // A committed cell joins the queue while it can be split: its parent held points and the next level resolves in it.
    auto offer = [&](Cell* c)
    {
        if (c->ranked && map.Resolves(c->level, c->at, c->read + 1))
            queue.insert(Entry{c->rank, c->power, c->local, c->key, c});
        else
            ++finished;
    };
    auto commit = [&](Child& child) -> Cell*
    {
        Cell c = std::move(child.cell);
        std::vector<std::uint32_t> points;
        for (Point& p : child.fresh) { points.push_back(std::uint32_t(store.size())); store.push_back(std::move(p)); }
        shaded += child.fresh.size();
        points.insert(points.end(), c.points.begin(), c.points.end());
        c.points = std::move(points);
        cells.push_back(std::move(c));
        return &cells.back();
    };

    // The start: selected pieces share the light's budget, searching their own cubes until a read finds points,
    // its lines reach the piece's quota, or coordinates stop resolving; retained records preserve the read's area.
    workers.Step("start");
    std::vector<int> starts;
    std::size_t missing = 0;
    {
        struct Piece final { std::uint32_t piece; int level; std::vector<Coords> cubes; std::size_t budget = 0; double inclusion = 1.0; };
        std::vector<Piece> pieces;
        for (const auto& box : pieceBoxes)
        {
            const Vector3d& lo = box.second.first;
            const Vector3d& hi = box.second.second;
            const double extent = std::max({hi[X] - lo[X], hi[Y] - lo[Y], hi[Z] - lo[Z]});
            // the finest cubes no smaller than the box: it meets at most two along each axis
            int level = extent > 0.0 ? map.LevelFor(extent) : 0;
            if (extent > 0.0 && map.Spacing(level) < extent) --level;
            level = std::max(level, 0);
            Coords low, high;
            const std::int64_t last = (level < 62) ? (std::int64_t(1) << level) - 1 : std::numeric_limits<std::int64_t>::max();
            for (int a = X; a <= Z; ++a)
            {
                const auto index = [&](double v)
                {
                    const double k = std::floor((v - map.Origin()[a]) / map.Spacing(level));
                    return std::int64_t(std::min(double(last), std::max(0.0, k)));
                };
                low[a] = index(lo[a]);
                high[a] = index(hi[a]);
            }
            Piece piece{box.first, level, {}};
            for (std::int64_t x = low[X]; x <= high[X]; ++x)
                for (std::int64_t y = low[Y]; y <= high[Y]; ++y)
                    for (std::int64_t z = low[Z]; z <= high[Z]; ++z)
                        piece.cubes.push_back({x, y, z});
            pieces.push_back(std::move(piece));
        }
        const std::size_t selected = std::min(part.budget, pieces.size());
        for (std::size_t i = 0; i < selected; ++i)
        {
            const std::size_t lo = i * pieces.size() / selected, hi = (i + 1) * pieces.size() / selected;
            const std::uint64_t key = keyOf(pieces[lo].piece, pieces[lo].level, pieces[lo].cubes.front());
            Piece& piece = pieces[lo + BounceOrderMix(key) % (hi - lo)];
            piece.budget = part.budget / selected + (i < part.budget % selected);
            piece.inclusion = double(hi - lo);
        }
        // One scrambled representative per stratum spreads seeds across pieces without favouring their ordering.
        struct Found final { int read = -1; std::vector<Child> children; };
        std::vector<Found> found(pieces.size());
        workers.For(pieces.size(), cooperate, [&](std::size_t index, std::size_t worker)
        {
            workers[worker].Watch(part.owner);
            const Piece& piece = pieces[index];
            if (piece.budget == 0) return;
            TraceThreadData* const td = workers.Thread(worker);
            for (int read = piece.level; ; ++read)
            {
                std::uint64_t probed = 0;
                bool resolves = false, any = false;
                std::vector<std::pair<Coords, const Map::Part*>> parts;
                std::vector<Map::Points> reads;
                reads.reserve(piece.cubes.size());
                for (const Coords& k : piece.cubes)
                {
                    reads.push_back(map.Read(piece.level, k, read, td));
                    resolves = resolves || reads.back().found != surface_map::Found::kPrecision;
                    if (const Map::Part* p = partOf(reads.back(), piece.piece))
                    {
                        probed += lines(*p);
                        std::size_t records = 0;
                        for (const Map::Chunk* chunk : p->chunks) records += chunk->records.size();
                        any = any || records > 0;
                        parts.emplace_back(k, p);
                    }
                }
                if (parts.empty()) return;
                if (any)
                {
                    found[index].read = read;
                    std::size_t records = 0;
                    double area = 0.0;
                    for (const auto& kp : parts)
                        for (const Map::Chunk* chunk : kp.second->chunks)
                            for (const auto& record : chunk->records)
                            {
                                ++records;
                                area += view.Weight(*kp.second, record);
                            }
                    const std::size_t retain = std::min(records, piece.budget);
                    std::vector<std::size_t> representatives;
                    const std::uint64_t key = keyOf(piece.piece, piece.level, piece.cubes.front());
                    if (records > retain)
                    {
                        const std::size_t offset = BounceOrderMix(key) % records;
                        for (std::size_t i = 0; i < retain; ++i)
                        {
                            const std::size_t lo = i * records / retain, hi = (i + 1) * records / retain;
                            representatives.push_back((offset + lo + BounceOrderMix(key ^ BounceOrderMix(i)) % (hi - lo)) % records);
                        }
                        std::sort(representatives.begin(), representatives.end());
                    }
                    std::size_t ordinal = 0, next = 0;
                    for (const auto& kp : parts)
                    {
                        Child child;
                        Cell& c = child.cell;
                        c.piece = piece.piece;
                        c.inclusion = piece.inclusion;
                        c.level = piece.level;
                        c.read = read;
                        c.at = kp.first;
                        c.key = keyOf(c.piece, c.level, c.at);
                        if (records <= retain)
                        {
                            c.weight = kp.second->weight;
                            shade(*kp.second, worker, child.fresh);
                            for (const Point& point : child.fresh) c.area += point.jacobian * c.weight;
                            c.own = child.fresh.size();
                            if (c.own > 0) found[index].children.push_back(std::move(child));
                            continue;
                        }
                        Map::Part subset = *kp.second;
                        subset.chunks.clear();
                        std::deque<Map::Chunk> chunks;
                        std::size_t count = 0, kept = 0;
                        for (const Map::Chunk* chunk : kp.second->chunks)
                        {
                            chunks.emplace_back();
                            Map::Chunk& copy = chunks.back();
                            copy.SameLinesAs(*chunk);
                            for (const auto& record : chunk->records)
                            {
                                ++count;
                                if (next < retain && ordinal == representatives[next])
                                {
                                    copy.records.push_back(record);
                                    ++kept;
                                    ++next;
                                }
                                ++ordinal;
                            }
                            subset.chunks.push_back(&copy);
                        }
                        if (kept == 0) continue;
                        subset.weight *= double(count) / double(kept);
                        c.weight = subset.weight;
                        shade(subset, worker, child.fresh);
                        for (const Point& point : child.fresh) c.area += point.jacobian * subset.weight;
                        c.own = child.fresh.size();
                        if (c.own > 0) found[index].children.push_back(std::move(child));
                    }
                    double retainedArea = 0.0;
                    for (const Child& child : found[index].children) retainedArea += child.cell.area;
                    if (retainedArea > 0.0)
                        for (Child& child : found[index].children)
                        {
                            child.cell.area *= piece.inclusion * area / retainedArea;
                            child.cell.weight *= piece.inclusion * area / retainedArea;
                        }
                    return;
                }
                if (!resolves || probed >= piece.budget) return;
            }
        }, 0);
        for (std::size_t i = 0; i < pieces.size(); ++i)
        {
            if (found[i].read < 0) { ++missing; continue; }
            starts.push_back(found[i].read - pieces[i].level);
            // a start cell has no parent: its own points rank it
            for (Child& child : found[i].children)
            {
                Cell* const c = commit(child);
                const double mean = meanOf(*c), share = c->weight;
                for (std::uint32_t k : c->points)
                {
                    c->rank += share * store[k].jacobian * std::fabs(store[k].Value() - mean);
                    c->power += share * store[k].jacobian * store[k].Value();
                }
                c->rank += c->power / std::sqrt(double(c->points.size()));
                c->local = c->power;
                c->ranked = true;
                offer(c);
            }
        }
    }
    const std::size_t looked = shaded;
    progress();

    // The loop: the highest ranked cell is split and committed, its children ranked from their points and queued. The next
    // highest cells are split ahead, one per worker, and held until the sequence reaches them, so the committed sequence is
    // the one-at-a-time sequence on any thread count; held splits it never reaches are discarded.
    workers.Step("splits");
    std::map<const Cell*, Split> held;
    std::size_t discarded = 0, wasted = 0;
    bool full = false;
    while (!full && shaded < part.budget && !queue.empty())
    {
        // Splits run ahead only while the ones before them may leave budget: a split reads its cube at four times its
        // density (a surface is two-dimensional, each level halves the spacing), so it is expected to shade about four
        // times its own read's points. This only schedules work; what is committed does not depend on it.
        std::vector<const Cell*> ahead;
        std::size_t expected = 0;
        for (auto it = queue.begin(); it != queue.end() && ahead.size() < workers.Count(); ++it)
        {
            if (!ahead.empty() && shaded + expected >= part.budget) break;
            expected += 4 * it->cell->own;
            if (held.find(it->cell) == held.end()) ahead.push_back(it->cell);
        }
        if (!ahead.empty())
        {
            std::vector<Split> done(ahead.size());
            splitAll(ahead, done);
            for (std::size_t i = 0; i < ahead.size(); ++i) held.emplace(ahead[i], std::move(done[i]));
        }
        while (shaded < part.budget && !queue.empty())
        {
            const auto top = queue.begin();
            const auto ready = held.find(top->cell);
            if (ready == held.end()) break;
            // the budget bounds the points: the split that would pass it is not taken, and placement ends there
            std::size_t fresh = 0;
            for (const Child& child : ready->second.children) fresh += child.fresh.size();
            if (shaded + fresh > part.budget)
            {
                full = true;
                break;
            }
            queue.erase(top);
            ++splits;
            traced += ready->second.traced;
            const_cast<Cell*>(ready->first)->split = true;
            for (Child& child : ready->second.children) offer(commit(child));
            held.erase(ready);
        }
        progress();
    }
    for (const auto& h : held)
    {
        ++discarded;
        for (const Child& child : h.second.children) wasted += child.fresh.size();
    }
    AdvancePreparation(workers.td->preparationProgress, units > advanced ? units - advanced : 0);

    // Every point lies in one leaf and takes an equal share of its area.
    std::vector<const Cell*> leaves;
    for (const Cell& c : cells)
        if (!c.split) leaves.push_back(&c);
    std::sort(leaves.begin(), leaves.end(), [](const Cell* a, const Cell* b)
    {
        if (a->level != b->level) return a->level < b->level;
        if (a->at != b->at) return a->at < b->at;
        return a->piece < b->piece;
    });
    Result result;
    Layout& layout = result.layout;
    double power = 0.0, area = 0.0;
    std::uint64_t digest = BounceOrderMix(part.budget);
    const int least = leaves.empty() ? 0 : leaves.front()->level;
    for (const Cell* c : leaves)
    {
        if (result.levels.size() <= std::size_t(c->level - least)) result.levels.resize(std::size_t(c->level - least) + 1, 0);
        result.levels[std::size_t(c->level - least)] += c->points.size();
        const double h = map.Spacing(c->read) * view.Stretch(), a = c->Share();
        for (std::uint32_t i : c->points)
        {
            const Point& p = store[i];
            layout.points.push_back(p.at);
            layout.normals.push_back(p.normal);
            layout.spacings.push_back(h);
            layout.areas.push_back(a);
            layout.front.push_back(p.front);
            layout.back.push_back(p.back);
            layout.ids.push_back(p.id);
            if (part.keepHits) layout.hits.push_back(p.hit);
            power += p.Value() * a;
            area += a;
            for (int ax = X; ax <= Z; ++ax) digest = BounceOrderMix(digest ^ Bits(p.at[ax]));
            digest = BounceOrderMix(digest ^ Bits(a) ^ Bits(p.Value()));
        }
    }
    int lowStart = 0, highStart = 0;
    if (!starts.empty())
    {
        lowStart = *std::min_element(starts.begin(), starts.end());
        highStart = *std::max_element(starts.begin(), starts.end());
    }
    char line[640];
    std::snprintf(line, sizeof(line),
                  "option B: %lu pieces, %lu found no point within the budget's count of lines; %lu points at the start "
                  "(read %d..%d levels finer than each piece's cubes); %lu splits, %lu cells finished; "
                  "%lu points of %lu, %lu shaded in splits held past the budget, discarded; %lu lines read in splits; "
                  "finest levels %d..%d; points stand for area %.6g, exported power %.6g; digest %016llx",
                  static_cast<unsigned long>(pieceBoxes.size()), static_cast<unsigned long>(missing), static_cast<unsigned long>(looked),
                  lowStart, highStart, static_cast<unsigned long>(splits), static_cast<unsigned long>(finished),
                  static_cast<unsigned long>(shaded), static_cast<unsigned long>(part.budget), static_cast<unsigned long>(wasted),
                  static_cast<unsigned long>(traced), least, least + int(result.levels.size()) - 1, area, power,
                  static_cast<unsigned long long>(digest));
    (void)discarded;
    result.report = line;
    result.points = layout.points.size();
    if (layout.points.empty())
        return result;
    result.index = std::make_shared<const SurfaceNodeIndex>(layout.points, layout.normals, layout.spacings);
    return result;
}

} // namespace surface_refine
} // namespace pov

#endif
