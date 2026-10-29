#ifndef POVRAY_CORE_LIGHTING_SURFACEREFINE_H
#define POVRAY_CORE_LIGHTING_SURFACEREFINE_H

#include "core/configcore.h"
#include "core/lighting/bounceincident.h"
#include "core/lighting/bounceorders.h"
#include "core/lighting/subsurfaceboundary.h"
#include "core/lighting/surfacebounce.h"
#include "core/math/randomsequence.h"
#include "core/scene/scenedata.h"
#include "core/support/preparationprogress.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

// A debug build may set a level that every node is split to, ignoring the budget, to render references.
#ifndef POV_SURFACE_BOUNCE_UNIFORM
#define POV_SURFACE_BOUNCE_UNIFORM 0
#endif

namespace pov
{
namespace surface_refine
{

// Every length below is a ratio of the baseline spacing, which follows the scene's total bounce area.
constexpr std::size_t kBaseline = 4096;      // level 0 nodes over the whole scene's bounce area
constexpr unsigned kLevels = 6;              // the finest spacing is the baseline's over 2^kLevels
constexpr double kTolerance = 0.1;           // relative error of local irradiance that splits a node
constexpr unsigned kFirstDraws = 4;          // draws a node starts with, the node itself first
constexpr unsigned kTrusted = 8;             // fewest draws whose spread is trusted as the node's error
constexpr unsigned kMostNodeDraws = 64;      // draws a node may grow to while its verdict or value is uncertain
constexpr double kNeededDraws = 4.0;         // most draws over those its own variance needs to resolve the tolerance
constexpr unsigned kChildDraws = 16;         // surface draws about a split node that may become its children
constexpr unsigned kAreaDraws = 64;          // surface draws about a split node that share its area among it and them
constexpr double kCoverStep = 1.0e-6;        // offset off a surface, over its part's radius, at which cover is tested
constexpr double kPacking = 0.68;            // nodes per squared spacing of a maximal Poisson disc set
constexpr double kFacing = 0.5;              // least cosine between normals of one sheet
constexpr std::size_t kProbe = 256;          // first area draws of a part
constexpr std::size_t kDrawsPerSeed = 8;     // area draws per expected level 0 node
constexpr std::size_t kMostDraws = std::size_t(1) << 22;
constexpr std::size_t kChunk = 65536;

/// The opaque solids that may cover a part's surface, its owner among them when opaque: a point covered on both sides
/// lies buried between solids, where no light reaches.
struct Cover final
{
    std::vector<ObjectPtr> solids;
    double step = 0.0;
    bool Buried(const Vector3d& point, const Vector3d& normal, TraceThreadData* td) const
    {
        return Inside(point + normal * step, td) && Inside(point - normal * step, td);
    }
    bool Inside(const Vector3d& point, TraceThreadData* td) const
    {
        for (ObjectPtr solid : solids)
            if (Inside_BBox(point, solid->BBox) && Inside_Object(point, solid, td)) return true;
        return false;
    }
};

/// One bounce target's surface, over every order: its area draws, level 0 nodes and their areas.
struct Part final
{
    ObjectPtr owner = nullptr;
    std::shared_ptr<const SurfaceGeometrySampler> sampler;
    std::vector<double> classes;
    Cover cover;
    double area = 0.0;
    std::size_t draws = 0;
    std::vector<SurfaceGeometrySample> pool;
    std::vector<std::size_t> seeds;
    std::vector<double> seedAreas;
    std::size_t NodeBytes(bool subsurface) const
    {
        return sizeof(Vector3d) + 4 * sizeof(float) + 1 + 2 * classes.size() * sizeof(MathColour) + sizeof(float) +
               (subsurface ? sizeof(MathColour) + sizeof(float) : 0);
    }
};

struct Node final
{
    Intersection hit;
    Vector3d normal;
    double area = 0.0;
    unsigned level = 0;
    std::uint32_t part = 0;
    std::uint64_t key = 0;
    unsigned draws = 0, samples = 0;
    std::vector<MathColour> sums;
    MathColour entry;
    double eta = 1.0;
    double sum = 0.0, square = 0.0, near = 0.0, nearWeight = 0.0;
    double half[2][2][3] = {};
    void Reset()
    {
        draws = samples = 0;
        sums.assign(sums.size(), MathColour());
        entry = MathColour();
        eta = 1.0;
        sum = square = near = nearWeight = 0.0;
        std::fill(&half[0][0][0], &half[0][0][0] + 12, 0.0);
    }
    double Mean() const { return samples ? sum / samples : 0.0; }
    double Error() const
    {
        if (samples < 2) return HUGE_VAL;
        const double mean = Mean();
        return std::sqrt(std::max(0.0, (square - samples * mean * mean) / (samples - 1)) / samples);
    }
};

/// What one order's refinement leaves for each part: its field, and its nodes for the subsurface bridge.
struct Result final
{
    std::shared_ptr<const SurfaceNodeIndex> index;
    std::shared_ptr<const SurfaceBounceRadiance> diffuse;
    std::vector<SurfaceBounceNode> nodes;
    std::vector<MathColour> entry;
    std::vector<double> etas, areas;
};

// Standard errors an error must also exceed: the two-sided 99.9% Student t quantile for the estimate's degrees of freedom,
// strict because every node of a scene is tested.
inline double Confidence(double freedom)
{
    static const double table[] = {636.62, 31.599, 12.924, 8.610, 6.869, 5.959, 5.408, 5.041, 4.781, 4.587};
    if (!(freedom >= 1.0)) return HUGE_VAL;
    if (freedom < 10.5) return table[int(freedom + 0.5) - 1];
    const double z = 3.290527;
    return z + (z * z * z + z) / (4.0 * freedom) + (5.0 * std::pow(z, 5) + 16.0 * z * z * z + 3.0 * z) / (96.0 * freedom * freedom);
}

inline double Radical(std::uint64_t n, unsigned base)
{
    double result = 0.0, place = 1.0 / base;
    while (n) { result += (n % base) * place; n /= base; place /= base; }
    return result;
}

// Draw `index` of a node's rotated Halton sequence on the unit disc, by the concentric map.
inline void DiscPoint(std::uint64_t key, std::uint64_t index, double& x, double& y)
{
    double u = Radical(index + 1, 2) + Draw(key, kDrawEmitter, 0), v = Radical(index + 1, 3) + Draw(key, kDrawEmitter, 1);
    u = 2.0 * (u - std::floor(u)) - 1.0;
    v = 2.0 * (v - std::floor(v)) - 1.0;
    if (u == 0.0 && v == 0.0) { x = y = 0.0; return; }
    double r, phi;
    if (std::fabs(u) > std::fabs(v)) { r = u; phi = M_PI / 4.0 * v / u; }
    else { r = v; phi = M_PI / 2.0 - M_PI / 4.0 * u / v; }
    x = r * std::cos(phi);
    y = r * std::sin(phi);
}

// The rotation taking unit `from` onto unit `to`, applied to `v`.
inline Vector3d Rotate(const Vector3d& v, const Vector3d& from, const Vector3d& to)
{
    const double c = dot(from, to);
    const Vector3d k = cross(from, to);
    return v * c + cross(k, v) + k * (dot(k, v) / (1.0 + c));
}

/// Spatial hash of a part's nodes at one spacing, for the Poisson disc test among one sheet.
class Grid final
{
public:
    explicit Grid(double cell) : cell(cell) {}
    void Insert(const Vector3d& point, std::size_t node) { cells[Key(point, 0, 0, 0)].push_back(node); }
    template<class At>
    bool Near(const Vector3d& point, const Vector3d& normal, double radius, const At& at) const
    {
        for (int dx = -1; dx <= 1; ++dx)
            for (int dy = -1; dy <= 1; ++dy)
                for (int dz = -1; dz <= 1; ++dz)
                {
                    const auto found = cells.find(Key(point, dx, dy, dz));
                    if (found == cells.end()) continue;
                    for (std::size_t node : found->second)
                    {
                        Vector3d p, n;
                        at(node, p, n);
                        if ((p - point).lengthSqr() < radius * radius && dot(n, normal) > kFacing) return true;
                    }
                }
        return false;
    }
private:
    std::uint64_t Key(const Vector3d& point, int dx, int dy, int dz) const
    {
        std::uint64_t key = 0;
        const int offset[3] = {dx, dy, dz};
        for (int a = 0; a < 3; ++a)
            key = BounceOrderMix(key ^ std::uint64_t(std::int64_t(std::floor(point[a] / cell)) + offset[a]));
        return key;
    }
    double cell;
    std::unordered_map<std::uint64_t, std::vector<std::size_t>> cells;
};

/// Draws each part's surface by its area, sets the scene's baseline spacing from the total, and picks level 0 nodes as a
/// Poisson disc set of the draws, each sheet apart; returns the baseline spacing.
template<class Workers>
double Seed(std::vector<Part>& parts, Workers& workers, std::size_t baseline, const std::function<void()>& cooperate)
{
    auto drawChunk = [&](Part& part, std::size_t first, std::size_t last, std::uint64_t partKey)
    {
        std::vector<SurfaceGeometrySample> chunk(last - first);
        std::vector<char> hit(last - first, 0);
        workers.For(last - first, cooperate, [&](std::size_t i, std::size_t worker)
        {
            TraceTicket ticket(1, 0.0);
            Ray ray(ticket);
            const std::uint64_t key = DeriveKey(partKey, kDrawEmitter, first + i);
            TraceThreadData* const td = workers.Thread(worker);
            hit[i] = part.sampler->SampleArea(ray, td, Radical(first + i + 1, 2), Draw(key, kDrawEmitter, 0),
                                              Draw(key, kDrawEmitter, 1), Draw(key, kDrawEmitter, 2), chunk[i]) &&
                     !part.cover.Buried(chunk[i].intersection.IPoint, chunk[i].normal, td);
        }, 0);
        for (std::size_t i = 0; i < chunk.size(); ++i)
            if (hit[i])
            {
                part.area += chunk[i].areaWeight;
                part.pool.push_back(std::move(chunk[i]));
            }
        part.draws = last;
    };
    auto partKey = [](std::size_t p) { return BounceOrderSeed(UINT64_C(0x7365656473757266), p, 0, 0); };
    for (std::size_t p = 0; p < parts.size(); ++p)
    {
        Part& part = parts[p];
        drawChunk(part, 0, kProbe, partKey(p));
        for (std::size_t last = kChunk; part.pool.empty() && last <= kChunk * 4; last += kChunk)
            drawChunk(part, part.draws, last, partKey(p));
        if (part.pool.empty())
            throw std::runtime_error("Surface bounce preparation found no surface samples");
    }
    // The spacing follows the scene's area as every draw so far estimates it; parts draw on until each has enough per node.
    double spacing = 0.0;
    for (bool more = true; more;)
    {
        double total = 0.0;
        for (const Part& part : parts) total += part.area / part.draws;
        spacing = std::sqrt(kPacking * total / double(baseline));
        more = false;
        for (std::size_t p = 0; p < parts.size(); ++p)
        {
            Part& part = parts[p];
            const double expected = std::max(1.0, kPacking * part.area / part.draws / (spacing * spacing));
            const double rate = double(part.pool.size()) / part.draws;
            const std::size_t wanted = std::min<std::size_t>(kMostDraws, std::size_t(std::ceil(kDrawsPerSeed * expected / rate)));
            more = more || part.draws < wanted;
            while (part.draws < wanted)
                drawChunk(part, part.draws, std::min(wanted, part.draws + kChunk), partKey(p));
        }
    }
    for (std::size_t p = 0; p < parts.size(); ++p)
    {
        Part& part = parts[p];
        Grid grid(spacing);
        auto at = [&](std::size_t i, Vector3d& point, Vector3d& normal)
        {
            point = part.pool[part.seeds[i]].intersection.IPoint;
            normal = part.pool[part.seeds[i]].normal;
        };
        for (std::size_t i = 0; i < part.pool.size(); ++i)
        {
            const SurfaceGeometrySample& draw = part.pool[i];
            if (grid.Near(draw.intersection.IPoint, draw.normal, spacing, at)) continue;
            grid.Insert(draw.intersection.IPoint, part.seeds.size());
            part.seeds.push_back(i);
        }
        std::vector<Vector3d> points, normals;
        for (std::size_t seed : part.seeds)
        {
            points.push_back(part.pool[seed].intersection.IPoint);
            normals.push_back(part.pool[seed].normal);
        }
        const SurfaceNodeIndex index(points, normals, std::vector<double>(points.size(), spacing));
        part.seedAreas.assign(part.seeds.size(), 0.0);
        std::vector<std::size_t> owner(part.pool.size());
        workers.For(part.pool.size(), cooperate, [&](std::size_t i, std::size_t)
        {
            owner[i] = index.Source()[index.Nearest(part.pool[i].intersection.IPoint, part.pool[i].normal)];
        }, 0);
        for (std::size_t i = 0; i < part.pool.size(); ++i)
            part.seedAreas[owner[i]] += part.pool[i].areaWeight / part.draws;
        part.area /= part.draws;
    }
    return spacing;
}

/// Refines one order's surface bounce fields over every part at once.
template<class Workers>
class Order final
{
public:
    Order(Workers& workers, std::vector<Part>& parts, const std::vector<std::size_t>& active,
          const std::vector<std::vector<LightSource*>>& sources, unsigned order, double baseline,
          std::size_t allowance, const std::vector<Trace*>& entries, const std::function<void()>& cooperate, std::size_t units) :
        workers(workers), parts(parts), active(active), sources(sources), order(order), baseline(baseline),
        allowance(allowance), subsurface(!entries.empty()), entries(entries), cooperate(cooperate), units(units)
    {
    }

    std::vector<Result> Run(const std::string& step)
    {
        for (std::size_t a = 0; a < active.size(); ++a)
        {
            const Part& part = parts[active[a]];
            for (std::size_t s = 0; s < part.seeds.size(); ++s)
            {
                Node node;
                node.hit = part.pool[part.seeds[s]].intersection;
                node.normal = part.pool[part.seeds[s]].normal;
                node.area = part.seedAreas[s];
                node.part = std::uint32_t(a);
                node.key = BounceOrderSeed(UINT64_C(0x6e6f646573757266), active[a], s, 0);
                node.sums.assign(2 * part.classes.size(), MathColour());
                bytes += part.NodeBytes(subsurface);
                nodes.push_back(std::move(node));
            }
        }
        std::vector<std::size_t> fresh(nodes.size());
        std::iota(fresh.begin(), fresh.end(), std::size_t(0));
        std::size_t remaining = units;
        for (level = 0;; ++level)
        {
            workers.Step(step + " l" + std::to_string(level) + " n" + std::to_string(nodes.size()));
            const std::size_t share = level == kLevels ? remaining : remaining / 2;
            remaining -= share;
            Measure(fresh, share);
            Index();
            std::vector<Verdict> verdicts(nodes.size());
            for (;;)
            {
                std::vector<std::size_t> again;
                workers.For(fresh.size(), cooperate, [&](std::size_t i, std::size_t)
                {
                    verdicts[fresh[i]] = Decide(fresh[i]);
                }, 0);
                for (std::size_t i : fresh)
                    if (verdicts[i].resample) again.push_back(i);
                if (again.empty()) break;
                Measure(again, 0, true);
            }
            if (level == kLevels) break;
            std::vector<std::size_t> split;
            for (std::size_t i : fresh)
                if (verdicts[i].split)
                {
                    split.push_back(i);
                    for (int c = 0; c < 3; ++c) reasons[c] += (verdicts[i].why >> c) & 1;
                }
            fresh = Split(split, verdicts);
            if (fresh.empty())
            {
                AdvancePreparation(workers.td->preparationProgress, remaining);
                break;
            }
        }
        return Finish();
    }

    std::size_t NodeCount() const { return nodes.size(); }
    std::vector<std::size_t> LevelCounts() const
    {
        std::vector<std::size_t> counts(level + 1, 0);
        for (const Node& node : nodes) ++counts[std::min(node.level, level)];
        return counts;
    }
    std::size_t Bytes() const { return bytes; }
    unsigned Levels() const { return level + 1; }
    bool Bound() const { return bound; }
    /// How many splits each criterion asked for: between nodes, within a node, close hits.
    const std::size_t* Reasons() const { return reasons; }

private:
    struct Verdict final { bool split = false, resample = false; double priority = 0.0; unsigned why = 0; };

    double Spacing(unsigned l) const { return std::ldexp(baseline, -int(l)); }

    void Measure(const std::vector<std::size_t>& which, std::size_t progress, bool more = false)
    {
        workers.For(which.size(), cooperate, [&](std::size_t i, std::size_t worker)
        {
            Node& node = nodes[which[i]];
            Sample(node, more ? std::min(2 * node.draws, kMostNodeDraws) : kFirstDraws, worker);
        }, progress);
    }

    // Draw 0 is the node itself, the others points of its disc on its surface; a draw that finds none, or a buried one,
    // is left out.
    void Sample(Node& node, unsigned last, std::size_t worker)
    {
        const Part& part = parts[active[node.part]];
        TraceThreadData* const td = workers.Thread(worker);
        const double radius = 0.5 * Spacing(node.level);
        const unsigned first = node.draws;
        std::vector<BounceIncidentBeam> batch;
        for (unsigned j = first; j < last; ++j)
        {
            double x = 0.0, y = 0.0;
            SurfaceGeometrySample origin;
            if (j > 0)
            {
                DiscPoint(node.key, j - 1, x, y);
                TraceTicket ticket(1, 0.0);
                Ray ray(ticket);
                if (!part.sampler->SampleNear(node.hit.IPoint, node.normal, radius, x, y, ray, td, origin) ||
                    part.cover.Buried(origin.intersection.IPoint, origin.normal, td))
                    continue;
            }
            const Intersection& at = j > 0 ? origin.intersection : node.hit;
            const Vector3d& normal = j > 0 ? origin.normal : node.normal;
            auto beams = CollectBounceIncident(workers[worker].trace, at.IPoint, sources[node.part], 1,
                                               BounceOrderSeed(node.key, order, j, 0), &at, &normal);
            double scalar = 0.0;
            for (auto& beam : beams)
            {
                beam.direction = Rotate(beam.direction, normal, node.normal);
                const double cosine = -dot(node.normal, beam.direction), magnitude = std::fabs(cosine);
                for (std::size_t c = 0; c < part.classes.size(); ++c)
                    node.sums[2 * c + (cosine < 0.0 ? 1 : 0)] += beam.angularWeight * std::pow(magnitude, part.classes[c]);
                const double g = beam.angularWeight.Greyscale() * magnitude;
                scalar += g;
                node.nearWeight += g;
                node.near += g / beam.distance;
                if (subsurface) batch.push_back(beam);
            }
            node.sum += scalar;
            node.square += scalar * scalar;
            ++node.samples;
            const double coordinate[2] = {x, y};
            for (int a = 0; j > 0 && a < 2; ++a)
            {
                double* h = node.half[a][coordinate[a] < 0.0 ? 0 : 1];
                h[0] += 1.0;
                h[1] += scalar;
                h[2] += scalar * scalar;
            }
        }
        node.draws = last;
        if (subsurface && first < last)
        {
            node.entry += entries[worker]->SurfaceSubsurfaceEntry(SurfaceBounceNode{node.hit, node.normal, 0.0}, batch, 0, node.eta);
            node.eta = SubsurfaceBoundary::Quantise(node.eta);
        }
    }

    void Index()
    {
        indices.assign(active.size(), nullptr);
        members.assign(active.size(), {});
        position.assign(nodes.size(), 0);
        for (std::size_t i = 0; i < nodes.size(); ++i) members[nodes[i].part].push_back(i);
        for (std::size_t p = 0; p < active.size(); ++p)
        {
            std::vector<Vector3d> points, normals;
            std::vector<double> spacings;
            for (std::size_t i : members[p])
            {
                points.push_back(nodes[i].hit.IPoint);
                normals.push_back(nodes[i].normal);
                spacings.push_back(Spacing(nodes[i].level));
            }
            indices[p] = std::make_shared<const SurfaceNodeIndex>(points, normals, spacings);
            const auto& source = indices[p]->Source();
            for (std::size_t t = 0; t < source.size(); ++t) position[members[p][source[t]]] = t;
        }
    }

    std::size_t At(std::uint32_t part, std::size_t tree) const { return members[part][indices[part]->Source()[tree]]; }

    Verdict Decide(std::size_t i) const
    {
        const Node& node = nodes[i];
        const SurfaceNodeIndex& index = *indices[node.part];
        const double mean = node.Mean(), error = node.Error(), spacing = Spacing(node.level);
        SurfaceNodeIndex::Weights weights;
        index.Interpolate(node.hit.IPoint, node.normal, weights, position[i]);
        double predicted = weights.count ? 0.0 : mean, predictedError2 = 0.0;
        Vector3d t1, t2;
        SurfaceGeometrySampler::TangentFrame(node.normal, t1, t2);
        bool unknown = !std::isfinite(error);
        double freedom = node.samples - 1.0;
        double low = HUGE_VAL, high = -HUGE_VAL;
        double xx = 0.0, xy = 0.0, yy = 0.0, xd = 0.0, yd = 0.0, sx = 0.0, sy = 0.0, mxx = 0.0, mxy = 0.0, myy = 0.0;
        for (unsigned k = 0; k < weights.count; ++k)
        {
            const Node& other = nodes[At(node.part, weights.node[k])];
            const double w = weights.kernel[k], value = other.Mean(), e = other.Error(), linear = weights.weight[k];
            predicted += linear * value;
            if (linear != 0.0)
            {
                predictedError2 += linear * linear * e * e;
                freedom = std::min(freedom, other.samples - 1.0);
            }
            if (w > 0.0)
            {
                low = std::min(low, value);
                high = std::max(high, value);
            }
            const Vector3d delta = other.hit.IPoint - node.hit.IPoint;
            const double dx = dot(delta, t1), dy = dot(delta, t2), dd = value - mean;
            xx += w * dx * dx; xy += w * dx * dy; yy += w * dy * dy; xd += w * dx * dd; yd += w * dy * dd;
            sx += w * dx; sy += w * dy;
            if (std::isfinite(e)) { mxx += w * w * e * e * dx * dx; mxy += w * w * e * e * dx * dy; myy += w * w * e * e * dy * dy; }
            else unknown = true;
        }
        if (low <= high) predicted = std::max(low, std::min(high, predicted));
        Verdict verdict;
        const double reference = std::max(mean, predicted);
        if (!(reference > 0.0) && !POV_SURFACE_BOUNCE_UNIFORM) return verdict;
        const double between = std::fabs(mean - predicted), betweenError = std::sqrt(error * error + predictedError2);
        const double determinant = xx * yy - xy * xy;
        // The neighbours' least squares gradient and its variance, from their errors and the node's own; neighbours along
        // one line leave it unknown, and the within test then has nothing to compare against.
        double gradient[2] = {0.0, 0.0}, gradientError2[2] = {0.0, 0.0};
        const bool spanned = determinant > 1.0e-9 * (xx + yy) * (xx + yy);
        if (spanned)
        {
            gradient[0] = (yy * xd - xy * yd) / determinant;
            gradient[1] = (xx * yd - xy * xd) / determinant;
            const double inverse[2][2] = {{yy / determinant, -xy / determinant}, {-xy / determinant, xx / determinant}};
            const double m[2][2] = {{mxx, mxy}, {mxy, myy}}, shared[2] = {inverse[0][0] * sx + inverse[0][1] * sy,
                                                                         inverse[1][0] * sx + inverse[1][1] * sy};
            for (int a = 0; a < 2; ++a)
                gradientError2[a] = inverse[a][0] * (m[0][0] * inverse[0][a] + m[0][1] * inverse[1][a]) +
                                    inverse[a][1] * (m[1][0] * inverse[0][a] + m[1][1] * inverse[1][a]) +
                                    (unknown ? 0.0 : error * error * shared[a] * shared[a]);
            if (unknown) gradientError2[0] = gradientError2[1] = HUGE_VAL;
        }
        // Half discs of radius spacing / 2 have centroids 4r / 3pi either side.
        const double separation = 4.0 * spacing / (3.0 * M_PI);
        double within = 0.0, withinError = HUGE_VAL, withinFreedom = 0.0;
        for (int a = 0; spanned && a < 2; ++a)
        {
            const double* lo = node.half[a][0];
            const double* hi = node.half[a][1];
            if (!(lo[0] > 0.0 && hi[0] > 0.0)) continue;
            const double residual = std::fabs(hi[1] / hi[0] - lo[1] / lo[0] - gradient[a] * separation);
            const double e2 = HalfError2(lo) + HalfError2(hi) + separation * separation * gradientError2[a];
            const double freedom = std::min(lo[0], hi[0]) - 1.0;
            if (residual - Confidence(freedom) * std::sqrt(e2) > within - Confidence(withinFreedom) * withinError)
            {
                within = residual;
                withinError = std::sqrt(e2);
                withinFreedom = freedom;
            }
        }
        const bool betweenLarge = between > kTolerance * reference, betweenSure = between > Confidence(freedom) * betweenError;
        const bool withinLarge = within > kTolerance * reference, withinSure = within > Confidence(withinFreedom) * withinError;
        const bool close = node.near > 0.0 && 2.0 * node.nearWeight / node.near < spacing;
        verdict.split = (betweenLarge && betweenSure) || (withinLarge && withinSure) || close;
        verdict.why = (betweenLarge && betweenSure ? 1 : 0) | (withinLarge && withinSure ? 2 : 0) | (close ? 4 : 0);
        const bool known = node.samples >= kTrusted && error <= kTolerance * reference;
        // The draws that resolve a difference of the tolerance, at the variance seen so far, bound how far a node grows.
        const double resolve = Confidence(HUGE_VAL) * error / (kTolerance * reference);
        const double needed = resolve * resolve * node.samples;
        const bool grow = node.draws < kMostNodeDraws && !(node.draws >= kNeededDraws * needed && node.samples >= kTrusted);
#if POV_SURFACE_BOUNCE_UNIFORM
        verdict.split = node.level < POV_SURFACE_BOUNCE_UNIFORM;
        verdict.resample = !verdict.split && grow && !known;
        verdict.priority = 1.0;
        return verdict;
#endif
        // Draws grow while a verdict is in doubt or the node's own value is not yet known to the tolerance.
        verdict.resample = !verdict.split && grow &&
                           ((betweenLarge && !betweenSure) || (withinLarge && !withinSure) || !known);
        verdict.priority = std::max({betweenLarge && betweenSure ? between : 0.0, withinLarge && withinSure ? within : 0.0,
                                     close ? mean : 0.0}) * node.area;
        return verdict;
    }

    static double HalfError2(const double* h)
    {
        if (h[0] < 2.0) return HUGE_VAL;
        const double mean = h[1] / h[0];
        return std::max(0.0, (h[2] - h[0] * mean * mean) / (h[0] - 1.0)) / h[0];
    }

    // Children of the split nodes, drawn about each within its own cell, kept as a Poisson disc set at half its spacing,
    // the most error per byte first while the budget lasts; each parent's area goes to it and its children by draws.
    std::vector<std::size_t> Split(std::vector<std::size_t> split, const std::vector<Verdict>& verdicts)
    {
        std::sort(split.begin(), split.end(), [&](std::size_t a, std::size_t b)
        {
            const double pa = verdicts[a].priority / parts[active[nodes[a].part]].NodeBytes(subsurface);
            const double pb = verdicts[b].priority / parts[active[nodes[b].part]].NodeBytes(subsurface);
            return pa > pb || (pa == pb && a < b);
        });
        std::vector<std::vector<SurfaceGeometrySample>> draws(split.size());
        std::vector<std::size_t> candidates(split.size(), 0);
        workers.For(split.size(), cooperate, [&](std::size_t k, std::size_t worker)
        {
            const Node& node = nodes[split[k]];
            const Part& part = parts[active[node.part]];
            TraceThreadData* const td = workers.Thread(worker);
            const std::uint64_t key = DeriveKey(node.key, kDrawEmitter, UINT64_C(0x6368696c64));
            for (unsigned j = 0; j < kAreaDraws; ++j)
            {
                if (j == kChildDraws) candidates[k] = draws[k].size();
                double x, y;
                DiscPoint(key, j, x, y);
                TraceTicket ticket(1, 0.0);
                Ray ray(ticket);
                SurfaceGeometrySample draw;
                if (!part.sampler->SampleNear(node.hit.IPoint, node.normal, Spacing(node.level), x, y, ray, td, draw) ||
                    part.cover.Buried(draw.intersection.IPoint, draw.normal, td))
                    continue;
                if (indices[node.part]->Nearest(draw.intersection.IPoint, draw.normal) == position[split[k]])
                    draws[k].push_back(std::move(draw));
            }
        }, 0);
        std::vector<Grid> grids;
        const double radius = Spacing(level + 1);
        for (std::size_t p = 0; p < active.size(); ++p)
        {
            grids.emplace_back(radius);
            for (std::size_t i : members[p]) grids.back().Insert(nodes[i].hit.IPoint, i);
        }
        auto at = [&](std::size_t i, Vector3d& point, Vector3d& normal) { point = nodes[i].hit.IPoint; normal = nodes[i].normal; };
        std::vector<std::size_t> children;
        for (std::size_t k = 0; k < split.size(); ++k)
        {
            const std::size_t parent = split[k];
            const std::uint32_t partIndex = nodes[parent].part;
            const Part& part = parts[active[partIndex]];
            const std::size_t first = nodes.size();
            for (std::size_t j = 0; j < candidates[k]; ++j)
            {
                const SurfaceGeometrySample& draw = draws[k][j];
                if (grids[partIndex].Near(draw.intersection.IPoint, draw.normal, radius, at)) continue;
                if (bytes + part.NodeBytes(subsurface) > allowance && !POV_SURFACE_BOUNCE_UNIFORM)
                {
                    bound = true;
                    break;
                }
                Node child;
                child.hit = draw.intersection;
                child.normal = draw.normal;
                child.level = level + 1;
                child.part = partIndex;
                child.key = BounceOrderMix(nodes[parent].key ^ BounceOrderMix(j + 1));
                child.sums.assign(2 * part.classes.size(), MathColour());
                bytes += part.NodeBytes(subsurface);
                grids[partIndex].Insert(child.hit.IPoint, nodes.size());
                nodes.push_back(std::move(child));
                children.push_back(nodes.size() - 1);
            }
            if (nodes.size() > first)
            {
                std::vector<double> counts(nodes.size() - first + 1, 0.0);
                for (const SurfaceGeometrySample& draw : draws[k])
                {
                    std::size_t best = 0;
                    double distance = (nodes[parent].hit.IPoint - draw.intersection.IPoint).lengthSqr();
                    for (std::size_t c = first; c < nodes.size(); ++c)
                    {
                        const double d = (nodes[c].hit.IPoint - draw.intersection.IPoint).lengthSqr();
                        if (d < distance) { distance = d; best = c - first + 1; }
                    }
                    counts[best] += 1.0;
                }
                const double total = std::accumulate(counts.begin(), counts.end(), 0.0), area = nodes[parent].area;
                nodes[parent].area = area * counts[0] / total;
                for (std::size_t c = first; c < nodes.size(); ++c) nodes[c].area = area * counts[c - first + 1] / total;
            }
            // The parent stays as a node of the finer level, measured afresh over its smaller disc.
            Node& kept = nodes[parent];
            kept.level = level + 1;
            kept.key = BounceOrderMix(kept.key ^ UINT64_C(0x706172656e74));
            kept.Reset();
            children.push_back(parent);
            if (bound) break;
        }
        return children;
    }

    std::vector<Result> Finish()
    {
        std::vector<Result> results(active.size());
        for (std::size_t p = 0; p < active.size(); ++p)
        {
            const Part& part = parts[active[p]];
            Result& result = results[p];
            result.index = indices[p];
            const auto& source = result.index->Source();
            const std::size_t count = source.size(), stride = 2 * part.classes.size();
            std::vector<MathColour> irradiance(count * stride);
            std::vector<float> power(count);
            std::vector<MathColour> exitance(count);
            workers.For(count, cooperate, [&](std::size_t t, std::size_t worker)
            {
                const Node& node = nodes[members[p][source[t]]];
                const double scale = 1.0 / node.samples;
                for (std::size_t j = 0; j < stride; ++j) irradiance[t * stride + j] = node.sums[j] * scale;
                exitance[t] = SurfaceIrradianceField::Exitance(node.hit, node.normal, part.classes, &irradiance[t * stride],
                                                               workers.Thread(worker));
                power[t] = float(exitance[t].Greyscale() * node.area);
            }, 0);
            MathColour integral;
            for (std::size_t t = 0; t < count; ++t) integral += exitance[t] * nodes[members[p][source[t]]].area;
            result.diffuse = std::make_shared<const SurfaceIrradianceField>(result.index, part.classes, std::move(irradiance),
                                                                            std::move(power), integral);
            if (!subsurface) continue;
            for (std::size_t i : members[p])
            {
                const Node& node = nodes[i];
                result.nodes.push_back(SurfaceBounceNode{node.hit, node.normal, node.area});
                result.entry.push_back(node.entry / float(node.samples));
                result.etas.push_back(node.eta);
                result.areas.push_back(node.area);
            }
        }
        return results;
    }

    Workers& workers;
    std::vector<Part>& parts;
    const std::vector<std::size_t>& active;
    const std::vector<std::vector<LightSource*>>& sources;
    unsigned order;
    double baseline;
    std::size_t allowance, bytes = 0, reasons[3] = {0, 0, 0};
    bool subsurface, bound = false;
    const std::vector<Trace*>& entries;
    const std::function<void()>& cooperate;
    std::size_t units;
    unsigned level = 0;
    std::vector<Node> nodes;
    std::vector<std::shared_ptr<const SurfaceNodeIndex>> indices;
    std::vector<std::vector<std::size_t>> members;
    std::vector<std::size_t> position;
};

} // namespace surface_refine
} // namespace pov

#endif
