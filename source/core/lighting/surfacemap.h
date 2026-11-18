#ifndef POVRAY_CORE_LIGHTING_SURFACEMAP_H
#define POVRAY_CORE_LIGHTING_SURFACEMAP_H

#include "core/configcore.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include "base/messenger.h"
#include "core/bounding/boundingbox.h"
#include "core/lighting/bounceorders.h"
#include "core/math/intersectiondepth.h"
#include "core/math/randomsequence.h"
#include "core/render/ray.h"
#include "core/scene/object.h"
#include "core/scene/tracethreaddata.h"
#include "core/shape/isosurface.h"
#include "core/shape/mesh.h"

namespace pov
{
namespace surface_map
{

constexpr double kPi = 3.14159265358979323846;
// Lines sharing a direction: the existing SIMD box width, so the map does not depend on the machine.
constexpr unsigned kBundle = FLAT_BBOX_WIDTH;

inline double Radical(std::uint64_t n, unsigned base)
{
    double result = 0.0, place = 1.0 / base;
    while (n) { result += (n % base) * place; n /= base; place /= base; }
    return result;
}

inline double Wrap(double u) { return u - std::floor(u); }

// The concentric map of the unit square onto the unit disc.
inline void Concentric(double u, double v, double& x, double& y)
{
    u = 2.0 * u - 1.0;
    v = 2.0 * v - 1.0;
    if (u == 0.0 && v == 0.0) { x = y = 0.0; return; }
    double r, phi;
    if (std::fabs(u) > std::fabs(v)) { r = u; phi = kPi / 4.0 * v / u; }
    else { r = v; phi = kPi / 2.0 - kPi / 4.0 * u / v; }
    x = r * std::cos(phi);
    y = r * std::sin(phi);
}

// Any frame will do; the axis the normal leans least on is never parallel to it.
inline void TangentFrame(const Vector3d& normal, Vector3d& t1, Vector3d& t2)
{
    Vector3d helper(0.0);
    const int a = std::fabs(normal[X]) <= std::fabs(normal[Y]) ? (std::fabs(normal[X]) <= std::fabs(normal[Z]) ? X : Z)
                                                                : (std::fabs(normal[Y]) <= std::fabs(normal[Z]) ? Y : Z);
    helper[a] = 1.0;
    t1 = cross(helper, normal).normalized();
    t2 = cross(normal, t1);
}

// The unit normal at a root, facing as the solid's outside.
inline bool OrientedNormal(TraceThreadData* thread, Intersection& hit, Vector3d& normal)
{
    hit.Object->Normal(normal, &hit, thread);
    Vector3d face;
    if (hit.Object->FaceNormal(face, &hit))
    {
        if (dot(face, normal) < 0.0) face.invert();
        normal = face;
    }
    if (Test_Flag(hit.Object, INVERTED_FLAG)) normal.invert();
    const double length2 = normal.lengthSqr();
    if (!IsFiniteIntersectionDepth(length2) || !(length2 > 0.0)) return false;
    normal /= std::sqrt(length2);
    return true;
}

using Coords = std::array<std::int64_t, 3>;

/// One point a line found: where, relative to its region, facing which way, and how to find its root again.
struct Record final
{
    float at[3];               // position less the region's low corner
    float normal[3];           // facing the solid's outside
    std::uint32_t line;        // in its chunk
    std::uint32_t leaf;        // the object's leaf it lies on
    std::uint8_t ordinal;      // its leaf's k-th root on that line
    std::uint8_t joint;        // another leaf's root at the same depth, to the line's rounding
    std::uint16_t unused;
};
static_assert(sizeof(Record) == 36, "a record is 36 bytes");

/// What a read found: points; nothing in the region at that level; or a level finer than coordinates resolve.
enum class Found { kPoints, kNothing, kPrecision };

struct Stats;

/// Points on one object's surface, found by isotropic lines through (surface box ∩ lattice cube) at a level's density. The
/// frame is the object's own bound: origin at its low corner, unit its largest side, so results scale with the object. A
/// chunk of lines is built the first time a reader asks for it, by every thread that asks at once, and never changes after.
class Map final
{
public:
    /// Lines [first, first + count) of a region, those a level adds to the coarser ones; immutable once published.
    class Chunk final
    {
    public:
        std::vector<Record> records;
        ~Chunk() { delete failure.load(); }
        /// How many lines the chunk fired.
        std::uint64_t Lines() const { return count; }
        /// Takes another chunk's lines, for a reader that keeps a subset of its records.
        void SameLinesAs(const Chunk& other) { level = other.level; first = other.first; count = other.count; }
    private:
        friend class Map;
        friend struct Stats;
        struct Slot final { std::vector<Record> records; std::size_t traced = 0; };
        std::uint64_t Line(std::size_t i) const { return first + records[i].line; }
        int level = 0;
        std::uint64_t first = 0, count = 0;
        std::size_t traced = 0, units = 0;
        std::atomic<std::size_t> next{0}, done{0};
        std::vector<Slot> slots;                   // released once assembled
        std::atomic<bool> published{false};
        std::atomic<std::string*> failure{nullptr}; // the first failing bundle's message
        std::atomic<Chunk*> finer{nullptr};
    };

    /// One surface box meeting a lattice cube: the lines' sphere is the one round (box ∩ cube).
    class Region final
    {
    public:
        /// The whole surface box's sides.
        const Vector3d& BoxSide() const { return side; }
        /// The object piece whose surface box this is.
        std::uint32_t Piece() const { return piece; }
        ~Region() { for (Chunk* c = first.load(); c != nullptr;) { Chunk* next = c->finer.load(); delete c; c = next; } }
    private:
        friend class Map;
        friend struct Stats;
        std::uint32_t piece = 0;
        const Region* previous = nullptr;          // earlier region of this piece, in the same cube
        Vector3d lo, hi, cubeLo, cubeHi, side;     // box (widened by its rounding) ∩ cube, the cube, the box's sides
        double crofton = 0.0;                      // 2 pi r^2 / E^2 of the sphere round (box ∩ cube)
        std::uint64_t key = 0;
        int base = 0;                              // the finest level with a single line
        std::atomic<Chunk*> first{nullptr};
    };

    /// A region's chunks up to the level read, and the area each of their points stands for there.
    struct Part final
    {
        const Region* region = nullptr;
        double weight = 0.0;
        std::vector<const Chunk*> chunks;
    };
    struct Points final
    {
        Found found = Found::kNothing;
        std::vector<Part> parts;                   // one per surface box meeting the cube, in the object's order
        std::size_t Count() const
        {
            std::size_t n = 0;
            for (const Part& p : parts) for (const Chunk* c : p.chunks) n += c->records.size();
            return n;
        }
    };

    explicit Map(ObjectPtr object) : object(object)
    {
        Vector3d hi;
        Widen(object->BBox, origin, hi);
        unit = std::max({hi[X] - origin[X], hi[Y] - origin[Y], hi[Z] - origin[Z]});
        for (std::uint32_t leaf = 0; leaf < object->Surface_Leaves(); ++leaf) leaves.push_back(object->Surface_Leaf(leaf));
        for (auto& child : root.children) child.store(nullptr, std::memory_order_relaxed);
        Fill(root);
    }
    ~Map() { Free(root); }

    /// Whether an object has the finite surface a map is made of.
    static bool Bounded(ConstObjectPtr o)
    {
        double extent = 0.0;
        for (int a = 0; a < 3; ++a) extent = std::max(extent, double(o->BBox.size[a]));
        return o->BBox.finite && std::isfinite(extent) && extent > 0.0;
    }

    ConstObjectPtr Object() const { return object; }
    /// The frame: lattice cube (c, k) is [origin + Unit 2^-c k, origin + Unit 2^-c (k + 1)), half-open.
    const Vector3d& Origin() const { return origin; }
    double Unit() const { return unit; }
    /// A cube's side at level c, and the line spacing at level l: each level halves it.
    double Spacing(int level) const { return std::ldexp(unit, -level); }
    /// The coarsest level whose spacing is at most `spacing`.
    int LevelFor(double spacing) const
    {
        int exponent;
        const double m = std::frexp(unit / spacing, &exponent);   // unit / spacing = m 2^exponent, m in [1/2, 1)
        return m == 0.5 ? exponent - 1 : exponent;
    }
    /// Whether coordinates and the surface's own accuracy resolve level `level`'s spacing in cube (c, k).
    bool Resolves(int c, const Coords& k, int level) const
    {
        double extent = 0.0;
        for (int a = 0; a < 3; ++a)
            for (std::int64_t corner : {k[a], k[a] + 1}) extent = std::max(extent, std::fabs(origin[a] + Spacing(c) * double(corner)));
        return Spacing(level) > std::max(object->SurfaceTolerance(), std::numeric_limits<double>::epsilon() * (extent + Spacing(c)));
    }
    /// Where the object's surface can be, as the object reports it.
    void Boxes(std::vector<SurfaceBox>& out) const { Surface_Boxes(object, origin, origin + Vector3d(unit), out); }
    /// The leaf a record lies on.
    ObjectPtr Leaf(std::uint32_t leaf) const { return leaves[leaf]; }
    /// Bytes the map holds: its nodes, regions and published chunks.
    std::size_t Bytes() const { return bytes.load(std::memory_order_relaxed); }

    /// The points of cube (c, k) at level `level`, built here or joined while other threads build them: a reader's jobs may
    /// each read a cube to share its work.
    Points Read(int c, Coords k, int level, TraceThreadData* td)
    {
        Points out;
        if (!Canonical(c, k)) return out;
        if (!Resolves(c, k, level)) return Points{Found::kPrecision, {}};
        Node* node = Find(c, k);
        out.parts.resize(node->regions.size());
        std::vector<std::pair<std::size_t, Chunk*>> chunks;
        for (std::size_t j = 0; j < node->regions.size(); ++j)
        {
            Region& r = *node->regions[j];
            const int read = std::max(level, r.base);
            // A spacing coordinates or the surface's own accuracy do not resolve, or more lines than a record can number: no read.
            if (!(Lines(r, read) <= std::numeric_limits<std::uint32_t>::max())) return Points{Found::kPrecision, {}};
            out.parts[j].region = &r;
            out.parts[j].weight = r.crofton * unit * unit / Lines(r, read);
            for (int l = r.base; l <= read; ++l) chunks.emplace_back(j, Chain(r, l));
        }
        for (const auto& chunk : chunks) Work(*node, chunk.first, *chunk.second, td);
        for (const auto& chunk : chunks) { Wait(*chunk.second); out.parts[chunk.first].chunks.push_back(chunk.second); }
        out.found = out.Count() > 0 ? Found::kPoints : Found::kNothing;
        return out;
    }

    static Vector3d Position(const Part& part, const Record& r) { return part.region->lo + Vector3d(r.at[X], r.at[Y], r.at[Z]); }
    static Vector3d Normal(const Record& r) { return Vector3d(r.normal[X], r.normal[Y], r.normal[Z]); }
    /// A record's id, from integers only: its region's key, its line, leaf and ordinal.
    static std::uint64_t Id(const Part& part, const Chunk& chunk, std::size_t i)
    {
        const Record& r = chunk.records[i];
        return BounceOrderMix(BounceOrderMix(BounceOrderMix(part.region->key ^ chunk.Line(i)) ^ r.leaf) ^ r.ordinal);
    }

    /// Each record's root again, in record order: its line fired once more over the cube's segment widened by the line's
    /// rounding, and of its leaf's roots there the one of its ordinal at its place, else the nearest; on `target`, a copy of
    /// the map's object moved by `move`, when given. A root not found again is warned of once and left with no object, so
    /// its reader drops that one point.
    void Hits(const Part& part, TraceThreadData* td, std::vector<Intersection>& out, ObjectPtr target = nullptr,
              const TRANSFORM* move = nullptr) const
    {
        Line line;
        for (const Chunk* chunk : part.chunks)
            for (std::size_t i = 0; i < chunk->records.size(); ++i)
            {
                const Record& rec = chunk->records[i];
                if (i == 0 || chunk->Line(i) != chunk->Line(i - 1)) Fire(*part.region, chunk->Line(i), td, line, true, target, move);
                Vector3d at = Position(part, rec);
                if (move != nullptr) MTransPoint(at, at, move);
                const Intersection* best = nullptr;
                double nearest = HUGE_VAL;
                for (const Line::Root& root : line.roots)
                {
                    if (root.piece != part.region->piece || root.leaf != rec.leaf) continue;
                    const double d = (root.hit.IPoint - at).length();
                    if (std::uint8_t(root.ordinal) == rec.ordinal && d <= line.slack) { best = &root.hit; break; }
                    if (d < nearest) { nearest = d; best = &root.hit; }
                }
                out.push_back(best != nullptr ? *best : Intersection());
                if (best == nullptr) Warn(td, kLost, "lost a root on firing its line again", object);
            }
    }

private:
    friend struct Stats;
    struct Node final
    {
        int level = 0;
        Coords at{};
        std::vector<std::unique_ptr<Region>> regions;
        std::array<std::atomic<Node*>, 8> children;
    };

    // What one line found along its cube: every root of the object by depth, with its piece (a portal's window has none:
    // max), its leaf and its leaf's ordinal there, and how far rounding can move a position on the line.
    struct Line final
    {
        struct Root final { Intersection hit; std::uint32_t piece, leaf, ordinal; };
        std::vector<Root> roots;
        Vector3d direction, start;
        double slack = 0.0;
    };

    enum Drop : unsigned { kLost = 1, kNoNormal = 2 };

    // A single-precision box widened by its own rounding.
    static void Widen(const BoundingBox& box, Vector3d& lo, Vector3d& hi)
    {
        for (int a = 0; a < 3; ++a)
        {
            const double l = box.lowerLeft[a], h = l + box.size[a];
            const double margin = std::numeric_limits<float>::epsilon() * (std::fabs(l) + std::fabs(h));
            lo[a] = l - margin;
            hi[a] = h + margin;
        }
    }

    // Lines a region's level l fires: its disc's area over the spacing squared (each root then stands for exactly that).
    double Lines(const Region& r, int level) const { return std::ceil(std::ldexp(r.crofton, 2 * level)); }

    // The regions of a node: each surface box that meets its half-open cube, clipped to it.
    void Fill(Node& node) const
    {
        Vector3d lo, hi;
        for (int a = 0; a < 3; ++a) { lo[a] = origin[a] + Spacing(node.level) * double(node.at[a]); hi[a] = lo[a] + Spacing(node.level); }
        std::vector<SurfaceBox> boxes;
        Surface_Boxes(object, lo, hi, boxes);
        std::unordered_map<std::uint32_t, std::pair<std::uint32_t, const Region*>> pieces;
        for (const SurfaceBox& box : boxes)
        {
            std::unique_ptr<Region> r(new Region);
            r->piece = box.piece;
            Vector3d boxLo, boxHi;
            Widen(box.box, boxLo, boxHi);
            bool meets = true;
            for (int a = 0; a < 3; ++a)
            {
                r->lo[a] = std::max(boxLo[a], lo[a]);
                r->hi[a] = std::min(boxHi[a], hi[a]);
                meets = meets && (r->lo[a] <= r->hi[a]) && (boxLo[a] < hi[a]);
            }
            if (!meets) continue;
            r->cubeLo = lo;
            r->cubeHi = hi;
            r->side = boxHi - boxLo;
            // Isotropic lines cross a surface at a mean |cos| of a half (Cauchy-Crofton): a root stands for 2 pi r^2 / lines.
            const double radius = (r->hi - r->lo).length() * 0.5;
            r->crofton = 2.0 * kPi * (radius / unit) * (radius / unit);
            if (!(r->crofton > 0.0)) continue;
            if (!std::isfinite(r->crofton)) throw std::runtime_error("Surface map region has non-finite measure.");
            // The region's key: integers only, so the lines are the same at any scale.
            auto& piece = pieces[r->piece];
            const std::uint32_t ordinal = piece.first++;
            r->previous = piece.second;
            piece.second = r.get();
            r->key = BounceOrderMix(BounceOrderMix(BounceOrderMix(std::uint64_t(r->piece)) ^ ordinal) ^ std::uint64_t(std::int64_t(node.level)));
            for (std::int64_t a : node.at) r->key = BounceOrderMix(r->key ^ std::uint64_t(a));
            for (r->base = int(std::floor(-std::log2(r->crofton) / 2.0)); Lines(*r, r->base) > 1.0; --r->base) {}
            while (Lines(*r, r->base + 1) <= 1.0) ++r->base;
            node.regions.push_back(std::move(r));
        }
        bytes.fetch_add(sizeof(Node) + node.regions.size() * sizeof(Region), std::memory_order_relaxed);
    }

    // Every cube of a coarser level than the frame's holds the whole frame: it reads as the frame's own cube. Subsurface
    // reads such cubes when the material's reach is wider than the object.
    static bool Canonical(int& c, Coords& k)
    {
        if (c < 0)
        {
            if (k[X] != 0 || k[Y] != 0 || k[Z] != 0) return false;
            c = 0;
        }
        for (int a = 0; a < 3; ++a)
            if (k[a] < 0 || (c < std::numeric_limits<std::int64_t>::digits && k[a] >> c != 0)) return false;
        return c < std::numeric_limits<std::int64_t>::digits;
    }

    // The node of cube (c, k), made on the way down by whichever thread gets there first.
    Node* Find(int c, const Coords& k)
    {
        Node* node = &root;
        while (node->level < c)
        {
            const int shift = c - node->level - 1;
            const int o = int((k[X] >> shift) & 1) | int(((k[Y] >> shift) & 1) << 1) | int(((k[Z] >> shift) & 1) << 2);
            Node* child = node->children[o].load(std::memory_order_acquire);
            if (child == nullptr)
            {
                std::unique_ptr<Node> made(new Node);
                made->level = node->level + 1;
                for (int a = 0; a < 3; ++a) made->at[a] = k[a] >> shift;
                for (auto& grand : made->children) grand.store(nullptr, std::memory_order_relaxed);
                Fill(*made);
                if (node->children[o].compare_exchange_strong(child, made.get(), std::memory_order_acq_rel, std::memory_order_acquire))
                    child = made.release();
                else
                    bytes.fetch_sub(sizeof(Node) + made->regions.size() * sizeof(Region), std::memory_order_relaxed);
            }
            node = child;
        }
        return node;
    }

    // The chunk of a region at a level, its coarser ones made first; made by whichever thread gets there first.
    Chunk* Chain(Region& r, int level) const
    {
        std::atomic<Chunk*>* slot = &r.first;
        std::uint64_t first = 0;
        for (int l = r.base; ; ++l)
        {
            Chunk* c = slot->load(std::memory_order_acquire);
            if (c == nullptr)
            {
                std::unique_ptr<Chunk> made(new Chunk);
                made->level = l;
                made->first = first;
                made->count = std::uint64_t(Lines(r, l)) - first;
                assert(first + made->count <= std::numeric_limits<std::uint32_t>::max());   // a record's line is 32 bits
                made->units = std::size_t((made->count + kBundle - 1) / kBundle);
                made->slots.resize(made->units);
                made->published.store(made->units == 0, std::memory_order_relaxed);
                if (slot->compare_exchange_strong(c, made.get(), std::memory_order_acq_rel, std::memory_order_acquire))
                    c = made.release();
            }
            if (l == level) return c;
            first = c->first + c->count;
            slot = &c->finer;
        }
    }

    // Takes bundles of a chunk until none are left; whoever finishes the last one assembles and publishes it.
    void Work(const Node& node, std::size_t j, Chunk& chunk, TraceThreadData* td) const
    {
        for (std::size_t unit; (unit = chunk.next.fetch_add(1, std::memory_order_relaxed)) < chunk.units;)
        {
            try
            {
                Trace(node, j, chunk, unit, td);
                if (chunk.done.fetch_add(1, std::memory_order_acq_rel) + 1 == chunk.units) Publish(chunk);
            }
            catch (const std::exception& e)
            {
                std::unique_ptr<std::string> message(new std::string(e.what()));
                std::string* none = nullptr;
                if (chunk.failure.compare_exchange_strong(none, message.get(), std::memory_order_acq_rel)) message.release();
                throw;
            }
        }
    }

    // Only bundles already in flight remain: at most one bundle's trace per other thread.
    static void Wait(const Chunk& chunk)
    {
        while (!chunk.published.load(std::memory_order_acquire))
        {
            if (const std::string* message = chunk.failure.load(std::memory_order_acquire)) throw std::runtime_error(*message);
            std::this_thread::yield();
        }
    }

    // The slots in unit order, so which thread traced which bundle cannot change the result.
    void Publish(Chunk& chunk) const
    {
        std::size_t total = 0;
        for (const auto& slot : chunk.slots) total += slot.records.size();
        chunk.records.reserve(total);
        for (const auto& slot : chunk.slots)
        {
            chunk.traced += slot.traced;
            chunk.records.insert(chunk.records.end(), slot.records.begin(), slot.records.end());
        }
        std::vector<Chunk::Slot>().swap(chunk.slots);
        bytes.fetch_add(sizeof(Chunk) + chunk.records.capacity() * sizeof(Record), std::memory_order_relaxed);
        chunk.published.store(true, std::memory_order_release);
    }

    // Line `index` of a region: its direction from its bundle, its place on the disc round the region, both from one
    // low-discrepancy sequence scrambled by the region's key. False when it misses the region. Otherwise its roots along the
    // cube (`again`: widened by the line's rounding), on `target` moved by `move` when given, the line moved with it.
    bool Fire(const Region& r, std::uint64_t index, TraceThreadData* td, Line& line, bool again = false,
              ObjectPtr target = nullptr, const TRANSFORM* move = nullptr) const
    {
        line.roots.clear();
        const std::uint64_t bundle = index / kBundle;
        // Halton bases 2 and 3 for the direction, equal-area over the hemisphere (lines are undirected); 5 and 7 for the disc.
        const double z = Wrap(Radical(bundle + 1, 2) + Draw(r.key, kDrawEmitter, 0));
        const double phi = 2.0 * kPi * Wrap(Radical(bundle + 1, 3) + Draw(r.key, kDrawEmitter, 1));
        const double s = std::sqrt(std::max(0.0, 1.0 - z * z));
        const Vector3d direction(s * std::cos(phi), s * std::sin(phi), z);
        double x, y;
        Concentric(Wrap(Radical(index + 1, 5) + Draw(r.key, kDrawEmitter, 2)), Wrap(Radical(index + 1, 7) + Draw(r.key, kDrawEmitter, 3)), x, y);
        Vector3d t1, t2;
        TangentFrame(direction, t1, t2);
        // The disc covers the region's sphere, so a ray starting a radius back starts outside it.
        const double radius = (r.hi - r.lo).length() * 0.5;
        const Vector3d origin = (r.lo + r.hi) * 0.5 + (t1 * x + t2 * y - direction) * radius;
        double nearDepth = 0.0, farDepth = HUGE_VAL, cubeNear = 0.0, cubeFar = HUGE_VAL;
        for (int a = 0; a < 3; ++a)
        {
            if (direction[a] == 0.0)
            {
                if (origin[a] < r.lo[a] || origin[a] > r.hi[a]) return false;
                continue;
            }
            const double l = (r.lo[a] - origin[a]) / direction[a], h = (r.hi[a] - origin[a]) / direction[a];
            nearDepth = std::max(nearDepth, std::min(l, h));
            farDepth = std::min(farDepth, std::max(l, h));
            const double cl = (r.cubeLo[a] - origin[a]) / direction[a], ch = (r.cubeHi[a] - origin[a]) / direction[a];
            cubeNear = std::max(cubeNear, std::min(cl, ch));
            cubeFar = std::min(cubeFar, std::max(cl, ch));
        }
        if (!(nearDepth <= farDepth)) return false;
        line.direction = direction;
        line.start = origin + direction * cubeNear;
        // A moved copy is traced along the same line moved with it, its depths stretched alike.
        Vector3d from = origin, along = direction;
        double stretch = 1.0;
        if (move != nullptr)
        {
            MTransPoint(from, origin, move);
            MTransDirection(along, direction, move);
            stretch = along.length();
            along /= stretch;
        }
        const double first = cubeNear * stretch, last = cubeFar * stretch;
        // How far rounding moves a position on the line, in the target's space: a record keeps it to single precision of
        // its cube's side, a coordinate to double precision of its magnitude.
        line.slack = std::numeric_limits<float>::epsilon() * (r.cubeHi[X] - r.cubeLo[X]) * stretch +
                     std::numeric_limits<double>::epsilon() * (std::max({std::fabs(from[X]), std::fabs(from[Y]), std::fabs(from[Z])}) + last);
        const double widen = again ? line.slack : 0.0;
        struct Chord final : RayRootQuery
        {
            double nearDepth, farDepth;
            Chord(double n, double f) : nearDepth(n), farDepth(f) {}
            bool Admit(ConstObjectPtr, double depth) const override { return IsFiniteIntersectionDepth(depth); }
            bool AllRoots() const override { return true; }
            // the line starts outside the cube, so no root needs a scene-unit cut against hitting its own start
            bool PreciseBoundary() const override { return true; }
            bool Segment(double& n, double& f) const override { n = nearDepth; f = farDepth; return true; }
        } query(first - widen, last + widen);
        if (target == nullptr) target = object;
        TraceTicket ticket(1, 0.0);
        Ray ray(ticket, from, along);
        IStack stack(td->stackPool);
        const RayRootQueryDisabled restore(ray);
        ray.SetRootQuery(&query);
        const IsoShadowWindow exactHits(td, 0.0, 0.0);
        ray.SetFlags(Ray::OtherRay);
        target->All_Intersections(ray, stack, td);
        for (; !stack->empty(); stack->pop())
        {
            const Intersection& hit = stack->top();
            if (hit.Object == nullptr || hit.Depth < query.nearDepth || hit.Depth > query.farDepth) continue;
            line.roots.push_back(Line::Root{hit, 0, 0, 0});
        }
        std::stable_sort(line.roots.begin(), line.roots.end(), [](const Line::Root& a, const Line::Root& b) { return a.hit.Depth < b.hit.Depth; });
        for (Line::Root& root : line.roots)
        {
            if (!Surface_Part(target, root.hit, root.piece, root.leaf)) root.piece = std::numeric_limits<std::uint32_t>::max();
            // counted from the cube's own face, so a root the widening adds before it moves no ordinal
            for (const Line::Root* e = line.roots.data(); e != &root; ++e)
                root.ordinal += (e->leaf == root.leaf && e->piece == root.piece && e->hit.Depth >= first);
        }
        return true;
    }

    // A root is a region's when its widened box ∩ cube holds it and no earlier box of its piece in the cube does: each
    // point of a piece is counted once, however many boxes the piece has.
    static bool Owns(const Node& node, std::size_t j, const Vector3d& p)
    {
        const auto holds = [&p](const Region& r)
        {
            for (int a = 0; a < 3; ++a)
                if (!(p[a] >= r.lo[a] && p[a] <= r.hi[a] && p[a] < r.cubeHi[a])) return false;
            return true;
        };
        if (!holds(*node.regions[j])) return false;
        for (const Region* earlier = node.regions[j]->previous; earlier != nullptr; earlier = earlier->previous)
            if (holds(*earlier)) return false;
        return true;
    }

    // One bundle of a chunk's lines: each root the region owns, as a record.
    void Trace(const Node& node, std::size_t j, Chunk& chunk, std::size_t unit, TraceThreadData* td) const
    {
        const Region& r = *node.regions[j];
        Chunk::Slot& slot = chunk.slots[unit];
        Line line;
        std::vector<std::pair<ConstObjectPtr, bool>> meshes;
        const std::uint64_t end = std::min(chunk.count, std::uint64_t(unit + 1) * kBundle);
        for (std::uint64_t i = std::uint64_t(unit) * kBundle; i < end; ++i)
        {
            if (!Fire(r, chunk.first + i, td, line)) continue;
            ++slot.traced;
            meshes.clear();
            const auto& roots = line.roots;
            for (std::size_t k = 0; k < roots.size(); ++k)
            {
                Intersection& hit = line.roots[k].hit;
                // A mesh faces by its winding: its crossings along the line alternate from whether the line enters the cube inside it.
                const Mesh* mesh = dynamic_cast<const Mesh*>(hit.Object);
                auto known = meshes.end();
                if (mesh != nullptr)
                {
                    known = std::find_if(meshes.begin(), meshes.end(), [&](const std::pair<ConstObjectPtr, bool>& m) { return m.first == mesh; });
                    if (known == meshes.end())
                    {
                        const PreciseInsideScope precise(td);
                        known = meshes.insert(meshes.end(), std::make_pair(ConstObjectPtr(mesh), mesh->Inside(line.start, td)));
                    }
                }
                const bool inside = (known != meshes.end()) && known->second;
                if (known != meshes.end()) known->second = !known->second;
                if (roots[k].piece != r.piece || !Owns(node, j, hit.IPoint)) continue;
                Vector3d n;
                if (!OrientedNormal(td, hit, n)) { Warn(td, kNoNormal, "found no normal", hit.Object); continue; }
                if (mesh != nullptr && (dot(n, line.direction) > 0.0) == !inside) n.invert();
                // Joints come from every root on the line, before any is left to another region (k - 1 wraps past the end at 0).
                const auto meets = [&](std::size_t e) { return e < roots.size() && roots[e].hit.Object != hit.Object && std::fabs(roots[e].hit.Depth - hit.Depth) <= line.slack; };
                Record rec;
                const Vector3d at = hit.IPoint - r.lo;
                for (int a = 0; a < 3; ++a) { rec.at[a] = float(at[a]); rec.normal[a] = float(n[a]); }
                rec.line = std::uint32_t(i);
                rec.leaf = roots[k].leaf;
                rec.ordinal = std::uint8_t(roots[k].ordinal);
                rec.joint = std::uint8_t(meets(k - 1) || meets(k + 1));
                rec.unused = 0;
                slot.records.push_back(rec);
            }
        }
    }

    // Once per map and kind: the point is dropped, and the render goes on.
    void Warn(TraceThreadData* td, Drop kind, const char* what, ConstObjectPtr on) const
    {
        if (warned.fetch_or(kind, std::memory_order_relaxed) & kind) return;
        const std::string text = std::string("Surface map ") + what + " on the " + ShapeName(on) + (on->source ? " at " + *on->source : std::string()) +
                                 "; that point is dropped.";
        if (td->mediaMessages != nullptr) td->mediaMessages->Warning(kWarningGeneral, "%s", text.c_str());
        else std::fprintf(stderr, "%s\n", text.c_str());
    }

    static void Free(Node& node)
    {
        for (auto& child : node.children)
            if (Node* next = child.load(std::memory_order_relaxed)) { Free(*next); delete next; }
    }

    ObjectPtr object;
    Vector3d origin;
    double unit = 0.0;
    std::vector<ObjectPtr> leaves;
    mutable std::atomic<std::size_t> bytes{0};
    mutable std::atomic<unsigned> warned{0};
    Node root;
};

/// One object's view of a map: its own map, or its lineage's, traced through the lineage's first live copy and moved onto
/// this one by M (the copy's transforms since the lineage began, after the first's undone). Reads are in the map's frame.
class Instance final
{
public:
    Instance() = default;
    Instance(Map* map, ObjectPtr object, const TRANSFORM& m) : map(map), object(object), m(m)
    {
        moved = std::memcmp(&m.matrix, &Identity().matrix, sizeof(m.matrix)) != 0;
        // the area a unit square of the map's frame becomes, and the longest and shortest a unit length becomes
        const auto a = [&m](int i, int j) { return m.matrix[j][i]; };
        det = std::fabs(a(0, 0) * (a(1, 1) * a(2, 2) - a(1, 2) * a(2, 1)) - a(0, 1) * (a(1, 0) * a(2, 2) - a(1, 2) * a(2, 0)) +
                        a(0, 2) * (a(1, 0) * a(2, 1) - a(1, 1) * a(2, 0)));
        // the shortest is one over the longest under the inverse, accurate however unequal the axes
        if (moved)
        {
            squeeze = -1.0;
            stretch = Longest(m.matrix, &squeeze);
            if (squeeze < 0.0) squeeze = 1.0 / Longest(m.inverse, nullptr);
        }
    }
    explicit operator bool() const { return map != nullptr; }
    Map* operator->() const { return map; }
    Map& operator*() const { return *map; }
    bool Moved() const { return moved; }
    /// The longest and shortest a unit length of the map's frame becomes on this copy.
    double Stretch() const { return stretch; }
    double Squeeze() const { return squeeze; }
    Vector3d ToMap(const Vector3d& p) const { Vector3d q = p; if (moved) MInvTransPoint(q, p, &m); return q; }
    Vector3d Position(const Map::Part& part, const Record& r) const
    {
        Vector3d p = Map::Position(part, r);
        if (moved) MTransPoint(p, p, &m);
        return p;
    }
    Vector3d Normal(const Record& r) const
    {
        Vector3d n = Map::Normal(r);
        if (moved) { MTransNormal(n, n, &m); n.normalize(); }
        return n;
    }
    /// The area a record stands for on this copy: its region's share, times the copy's area change at its normal.
    double Weight(const Map::Part& part, const Record& r) const
    {
        if (!moved) return part.weight;
        Vector3d n = Map::Normal(r);
        MTransNormal(n, n, &m);
        return part.weight * det * n.length();
    }
    /// As Map::Hits: a root not found again has no object.
    void Hits(const Map::Part& part, TraceThreadData* td, std::vector<Intersection>& out) const
    {
        map->Hits(part, td, out, object, moved ? &m : nullptr);
    }

private:
    static const TRANSFORM& Identity()
    {
        static const TRANSFORM identity = [] { TRANSFORM t; MIdentity(t.matrix); MIdentity(t.inverse); return t; }();
        return identity;
    }
    // The longest a unit length becomes under M: the root of the largest eigenvalue of M Mᵀ, in closed form (the cubic's
    // largest root, free of cancellation); when the largest element off its diagonal is within rounding of it, its diagonal,
    // whose least element's root is then the shortest.
    static double Longest(const MATRIX m, double* shortest)
    {
        double g[3][3] = {};
        for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) for (int k = 0; k < 3; ++k) g[i][j] += m[i][k] * m[j][k];
        int p = 0, q = 1;
        for (int i = 0; i < 3; ++i) for (int j = i + 1; j < 3; ++j) if (std::fabs(g[i][j]) > std::fabs(g[p][q])) { p = i; q = j; }
        if (std::fabs(g[p][q]) <= std::numeric_limits<double>::epsilon() * (std::fabs(g[p][p]) + std::fabs(g[q][q])))
        {
            if (shortest != nullptr) *shortest = std::sqrt(std::min({g[0][0], g[1][1], g[2][2]}));
            return std::sqrt(std::max({g[0][0], g[1][1], g[2][2]}));
        }
        const double mean = (g[0][0] + g[1][1] + g[2][2]) / 3.0;
        double b[3][3], sum = 0.0;
        for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) { b[i][j] = g[i][j] - (i == j ? mean : 0.0); sum += b[i][j] * b[i][j]; }
        const double scale = std::sqrt(sum / 6.0);
        const double cosine = (b[0][0] * (b[1][1] * b[2][2] - b[1][2] * b[2][1]) - b[0][1] * (b[1][0] * b[2][2] - b[1][2] * b[2][0]) +
                               b[0][2] * (b[1][0] * b[2][1] - b[1][1] * b[2][0])) / (2.0 * scale * scale * scale);
        return std::sqrt(mean + 2.0 * scale * std::cos(std::acos(std::max(-1.0, std::min(1.0, cosine))) / 3.0));
    }
    Map* map = nullptr;
    ObjectPtr object = nullptr;
    TRANSFORM m;
    bool moved = false;
    double det = 1.0, stretch = 1.0, squeeze = 1.0;
};

/// The render's maps: one per object or lineage, made on its first ask and held in its own slot, with no lock.
class Maps final
{
public:
    ~Maps()
    {
        for (Entry* e = head.load(); e != nullptr;)
        {
            Entry* next = e->next;
            e->slot->store(nullptr);
            delete e;
            e = next;
        }
    }

    /// An object's map: a copy of a declared object shares its lineage's, traced through the lineage's first live copy when
    /// the map is made, which stays the map's object for the render.
    Instance Of(ObjectPtr object)
    {
        const std::shared_ptr<SurfaceLineage> lineage = std::atomic_load(&object->surfaceCopy.lineage);
        std::atomic<void*>* slot = lineage ? &lineage->map : &object->surfaceMap.map;
        void* known = slot->load(std::memory_order_acquire);
        if (known == nullptr)
        {
            std::unique_ptr<Entry> made(new Entry{slot, std::unique_ptr<Map>(new Map(lineage ? First_Surface_Copy(*lineage) : object)), nullptr});
            if (slot->compare_exchange_strong(known, made->map.get(), std::memory_order_acq_rel, std::memory_order_acquire))
            {
                known = made->map.get();
                Entry* e = made.release();
                e->next = head.load(std::memory_order_relaxed);
                while (!head.compare_exchange_weak(e->next, e, std::memory_order_release, std::memory_order_relaxed)) {}
            }
        }
        Map* map = static_cast<Map*>(known);
        TRANSFORM m;
        MIdentity(m.matrix);
        MIdentity(m.inverse);
        if (map->Object() != object)
        {
            const TRANSFORM& traced = map->Object()->surfaceCopy.since;
            std::memcpy(m.matrix, traced.inverse, sizeof(m.matrix));
            std::memcpy(m.inverse, traced.matrix, sizeof(m.inverse));
            Compose_Transforms(&m, &object->surfaceCopy.since);
        }
        return Instance(map, object, m);
    }

private:
    friend struct Stats;
    struct Entry final { std::atomic<void*>* slot; std::unique_ptr<Map> map; Entry* next; };
    std::atomic<Entry*> head{nullptr};
};

} // namespace surface_map
} // namespace pov

#endif
