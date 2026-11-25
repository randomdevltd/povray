#ifndef POVRAY_CORE_LIGHTING_SURFACEREFINE_H
#define POVRAY_CORE_LIGHTING_SURFACEREFINE_H

#include "core/configcore.h"
#include "core/lighting/bounceorders.h"
#include "core/lighting/surfacebounce.h"
#include "core/lighting/surfacemap.h"
#include "core/render/trace.h"
#include "core/support/preparationprogress.h"
#include "core/support/preparedcache.h"
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
    std::vector<Intersection> hits;              // kept only for later passes; empty until Relocate when placement was read back
    std::vector<std::uint64_t> ids;
    /// For later passes: the cube read that found each point, so its hit can be found again.
    struct Source final
    {
        std::uint32_t piece = 0;
        int level = 0, read = 0;
        Coords at{};
        bool operator<(const Source& o) const
        {
            if (piece != o.piece) return piece < o.piece;
            if (level != o.level) return level < o.level;
            if (read != o.read) return read < o.read;
            return at < o.at;
        }
    };
    std::vector<Source> sources;
};

/// A shaded point, stored once: where it is in the map's frame (to give it to a child), where on this copy, its light.
struct RefinePoint final
{
    Vector3d frame, at, normal;
    double jacobian = 1.0;                   // this copy's area change at the point
    MathColour front, back;
    std::uint64_t id = 0;
    std::uint32_t cell = 0;                  // the cell whose read found it
    Intersection hit;                        // not saved: Relocate finds it again
    double Value() const { return (front + back).Greyscale(); }
};

/// A cell: one piece's surface in one lattice cube, read at line level `read`; its points are its own read's, then those
/// its ancestors read inside it.
struct RefineCell final
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
    std::uint32_t index = 0;                 // its place in the committed sequence; not saved, as the order gives it
    double Share() const { return points.empty() ? 0.0 : area / double(points.size()); }
};

inline void Serialize(CacheArchive& a, RefinePoint& p)
{
    Serialize(a, p.frame);
    Serialize(a, p.at);
    Serialize(a, p.normal);
    Serialize(a, p.jacobian);
    Serialize(a, p.front);
    Serialize(a, p.back);
    Serialize(a, p.id);
    Serialize(a, p.cell);
}

inline void Serialize(CacheArchive& a, RefineCell& c)
{
    Serialize(a, c.piece);
    Serialize(a, c.level);
    Serialize(a, c.read);
    Serialize(a, c.at);
    Serialize(a, c.area);
    Serialize(a, c.weight);
    Serialize(a, c.inclusion);
    Serialize(a, c.points);
    std::uint64_t own = c.own;
    Serialize(a, own);
    c.own = std::size_t(own);
    Serialize(a, c.rank);
    Serialize(a, c.power);
    Serialize(a, c.local);
    Serialize(a, c.ranked);
    Serialize(a, c.key);
    Serialize(a, c.split);
}

/// What placement committed since the last record: the points and cells added, which new cells joined the queue, which
/// cells were split, and the totals after; resuming from it gives the state the committed sequence had there.
struct RefineBatch final
{
    std::vector<RefinePoint> points;
    std::vector<RefineCell> cells;
    std::vector<std::uint8_t> queued;
    std::vector<std::uint64_t> split;
    std::uint64_t splits = 0, finished = 0, traced = 0, shaded = 0, missing = 0;
    std::vector<std::int32_t> starts;
    bool last = false;
};

inline void Serialize(CacheArchive& a, RefineBatch& b)
{
    Serialize(a, b.points);
    Serialize(a, b.cells);
    Serialize(a, b.queued);
    Serialize(a, b.split);
    Serialize(a, b.splits);
    Serialize(a, b.finished);
    Serialize(a, b.traced);
    Serialize(a, b.shaded);
    Serialize(a, b.missing);
    Serialize(a, b.starts);
    Serialize(a, b.last);
    if (a.Reading() && (b.queued.size() != b.cells.size()))
        throw CacheFormatError("a placement record's queue flags do not match its cells");
}

/// Splits committed between placement records, so a long placement saves its progress as it goes.
constexpr std::size_t kSplitsPerRecord = 16;

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
/// With a `cache`, the start and every kSplitsPerRecord committed splits are records of it, labelled `label` " start" and
/// `label` " splits", each a step the preparation can resume from: a placement read back in part goes on with the same
/// committed sequence. A placement read back keeps no hits; Relocate finds them again for later passes.
template<class Workers>
Result Place(Workers& workers, const Part& part, const std::vector<Trace*>& shaders, const std::function<void()>& cooperate,
             std::size_t units, PreparedCacheUnit* cache = nullptr, const std::string& label = "o1")
{
    Map& map = *part.map;
    const surface_map::Instance& view = part.map;
    if (cache != nullptr && !cache->Saving())
        cache = nullptr;

    using Point = RefinePoint;
    using Cell = RefineCell;
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
        workers.Uncounted(pending.size(), cooperate, [&](std::size_t index, std::size_t worker)
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
        });
        std::vector<std::pair<std::uint32_t, std::uint32_t>> located;
        for (std::size_t index = 0; index < pending.size(); ++index)
            for (std::size_t i = 0; i < pending[index].child.fresh.size(); ++i)
                located.emplace_back(std::uint32_t(index), std::uint32_t(i));
        workers.Uncounted(located.size(), cooperate, [&](std::size_t index, std::size_t worker)
        {
            workers[worker].Watch(part.owner);
            light(pending[located[index].first].child.fresh[located[index].second], worker);
        });
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
    std::vector<std::uint8_t> queuedOf;          // per committed cell: whether it joined the queue
    // A committed cell joins the queue while it can be split: its parent held points and the next level resolves in it.
    auto offer = [&](Cell* c)
    {
        const bool queued = c->ranked && map.Resolves(c->level, c->at, c->read + 1);
        queuedOf.resize(cells.size(), 0);
        queuedOf[cells.size() - 1] = queued;
        if (queued)
            queue.insert(Entry{c->rank, c->power, c->local, c->key, c});
        else
            ++finished;
    };
    auto entryOf = [](Cell* c) { return Entry{c->rank, c->power, c->local, c->key, c}; };
    // Records: what was committed since the last one, written by a build and applied by a resume.
    std::size_t markedPoints = 0, markedCells = 0, missing = 0;
    std::vector<int> starts;
    std::vector<std::uint64_t> splitSince;
    bool replayed = false;
    auto take = [&](bool last)
    {
        RefineBatch batch;
        batch.points.assign(store.begin() + std::ptrdiff_t(markedPoints), store.end());
        batch.cells.assign(cells.begin() + std::ptrdiff_t(markedCells), cells.end());
        queuedOf.resize(cells.size(), 0);
        batch.queued.assign(queuedOf.begin() + std::ptrdiff_t(markedCells), queuedOf.end());
        batch.split.swap(splitSince);
        batch.splits = splits;
        batch.finished = finished;
        batch.traced = traced;
        batch.shaded = shaded;
        batch.missing = missing;
        if (markedCells == 0)
            batch.starts.assign(starts.begin(), starts.end());
        batch.last = last;
        markedPoints = store.size();
        markedCells = cells.size();
        return batch;
    };
    auto apply = [&](RefineBatch& batch)
    {
        replayed = true;
        for (RefinePoint& point : batch.points) store.push_back(std::move(point));
        for (std::size_t i = 0; i < batch.cells.size(); ++i)
        {
            for (std::uint32_t k : batch.cells[i].points)
                if (k >= store.size()) throw CacheFormatError("a placement record's cell names a point not yet placed");
            batch.cells[i].index = std::uint32_t(cells.size());
            cells.push_back(std::move(batch.cells[i]));
            queuedOf.push_back(batch.queued[i]);
            if (batch.queued[i]) queue.insert(entryOf(&cells.back()));
        }
        for (std::uint64_t index : batch.split)
        {
            if (index >= cells.size() || queue.erase(entryOf(&cells[std::size_t(index)])) != 1)
                throw CacheFormatError("a placement record splits a cell that is not queued");
            cells[std::size_t(index)].split = true;
        }
        splits = std::size_t(batch.splits);
        finished = std::size_t(batch.finished);
        traced = std::size_t(batch.traced);
        shaded = std::size_t(batch.shaded);
        missing = std::size_t(batch.missing);
        if (markedCells == 0)
            starts.assign(batch.starts.begin(), batch.starts.end());
        markedPoints = store.size();
        markedCells = cells.size();
    };
    const std::string startLabel = label + " start", splitsLabel = label + " splits";
    auto record = [&](const std::string& what, RefineBatch& batch)
    {
        cache->Record(what.c_str(), []() {}, [&](CacheArchive& archive) { Serialize(archive, batch); });
    };
    // a record read back is applied as it is read, so a damaged one is reported as its cache's
    auto replay = [&](const std::string& what)
    {
        RefineBatch batch;
        cache->Record(what.c_str(), []() {}, [&](CacheArchive& archive)
        {
            Serialize(archive, batch);
            if (archive.Reading()) apply(batch);
        });
        return batch.last;
    };
    auto commit = [&](Child& child) -> Cell*
    {
        Cell c = std::move(child.cell);
        std::vector<std::uint32_t> points;
        for (Point& p : child.fresh)
        {
            p.cell = std::uint32_t(cells.size());
            points.push_back(std::uint32_t(store.size()));
            store.push_back(std::move(p));
        }
        shaded += child.fresh.size();
        points.insert(points.end(), c.points.begin(), c.points.end());
        c.points = std::move(points);
        c.index = std::uint32_t(cells.size());
        cells.push_back(std::move(c));
        return &cells.back();
    };

    // The start: selected pieces share the light's budget, searching their own cubes until a read finds points,
    // its lines reach the piece's quota, or coordinates stop resolving; retained records preserve the read's area.
    workers.Step("start");
    if (cache != nullptr && cache->Replays())
        replay(startLabel);
    else
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
        if (cache != nullptr)
        {
            RefineBatch batch = take(false);
            record(startLabel, batch);
        }
    }
    const std::size_t looked = shaded;
    if (cache != nullptr)
        cache->Checkpoint();
    // a resume reads the committed splits back, record by record, then goes on splitting where they end
    bool placed = false;
    while (cache != nullptr && !placed && cache->Replays())
        placed = replay(splitsLabel);
    progress();

    // The loop: the highest ranked cell is split and committed, its children ranked from their points and queued. The next
    // highest cells are split ahead, one per worker, and held until the sequence reaches them, so the committed sequence is
    // the one-at-a-time sequence on any thread count; held splits it never reaches are discarded.
    workers.Step("splits");
    std::map<const Cell*, Split> held;
    std::size_t discarded = 0, wasted = 0;
    bool full = placed;
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
            splitSince.push_back(ready->first->index);
            for (Child& child : ready->second.children) offer(commit(child));
            held.erase(ready);
            if (cache != nullptr && splits % kSplitsPerRecord == 0)
            {
                RefineBatch batch = take(false);
                record(splitsLabel, batch);
                cache->Checkpoint();
            }
        }
        progress();
    }
    if (cache != nullptr && !placed)
    {
        RefineBatch batch = take(true);
        record(splitsLabel, batch);
        cache->Checkpoint();
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
            if (part.keepHits)
            {
                const Cell& from = cells[p.cell];
                layout.sources.push_back(Layout::Source{from.piece, from.level, from.read, from.at});
                if (!replayed) layout.hits.push_back(p.hit);
            }
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

/// Finds again the hits of a placement read back, for passes that shade its points again: each cube read that found points
/// is read once more and its records' roots fired again, and each point takes the root of its own record.
template<class Workers>
void Relocate(Workers& workers, const Part& part, Layout& layout, const std::function<void()>& cooperate)
{
    const std::size_t count = layout.points.size();
    if (layout.hits.size() == count)
        return;
    if (layout.sources.size() != count)
        throw std::logic_error("Surface bounce cannot find a placement's hits again without its reads");
    Map& map = *part.map;
    std::map<Layout::Source, std::vector<std::size_t>> reads;
    for (std::size_t i = 0; i < count; ++i)
        reads[layout.sources[i]].push_back(i);
    std::vector<std::pair<Layout::Source, std::vector<std::size_t>>> work(reads.begin(), reads.end());
    layout.hits.assign(count, Intersection());
    std::vector<std::uint8_t> found(count, 0);
    workers.Uncounted(work.size(), cooperate, [&](std::size_t index, std::size_t worker)
    {
        workers[worker].Watch(part.owner);
        const Layout::Source& source = work[index].first;
        TraceThreadData* const td = workers.Thread(worker);
        const Map::Points points = map.Read(source.level, source.at, source.read, td);
        for (const Map::Part& p : points.parts)
        {
            if (p.region->Piece() != source.piece) continue;
            std::vector<Intersection> hits;
            part.map.Hits(p, td, hits);
            std::size_t n = 0;
            for (const Map::Chunk* chunk : p.chunks)
                for (std::size_t r = 0; r < chunk->records.size(); ++r, ++n)
                {
                    const std::uint64_t id = Map::Id(p, *chunk, r);
                    for (std::size_t i : work[index].second)
                        if (layout.ids[i] == id)
                        {
                            layout.hits[i] = hits[n];
                            found[i] = 1;
                        }
                }
        }
    });
    for (std::size_t i = 0; i < count; ++i)
        if (!found[i] || layout.hits[i].Object == nullptr)
            throw std::runtime_error("Surface bounce could not find a saved placement's point again on " +
                                     std::string(part.owner->source ? *part.owner->source : "an unnamed object") +
                                     "; delete its saved caches to rebuild them");
}

} // namespace surface_refine
} // namespace pov

#endif
