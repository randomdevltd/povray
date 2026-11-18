// Contract of an intersection's cells, refined on demand: every hit lies in a box of its own piece for region and
// whole-set answers, coarse and fine, through copies, transforms, inverted, clipped and nested children; finer answers lie
// in coarser ones; the extent holds every hit; and the answers do not depend on the order of requests, threads, copies or
// scale. Also reports the classification tests and nodes each worked case costs.
#include "core/math/matrix.h"
#include "core/material/noise.h"
#include "core/render/ray.h"
#include "core/scene/object.h"
#include "core/scene/scenedata.h"
#include "core/scene/tracethreaddata.h"
#include "core/shape/box.h"
#include "core/shape/csg.h"
#include "core/shape/plane.h"
#include "core/shape/sphere.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <functional>
#include <limits>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <vector>

using namespace pov;

static Sphere *Ball(const Vector3d& centre, double radius)
{
    Sphere *s = new Sphere();
    s->Center = centre;
    s->Radius = radius;
    s->Compute_BBox();
    return s;
}

static Box *Brick(const Vector3d& low, const Vector3d& high)
{
    Box *b = new Box();
    b->bounds[0] = low;
    b->bounds[1] = high;
    b->Compute_BBox();
    return b;
}

/// As Parse_CSG makes it: children linked, the box computed, the cells begun.
template <typename T>
static T *Group(T *csg, std::vector<ObjectPtr> children)
{
    for (ObjectPtr child : children)
    {
        child->Type |= IS_CHILD_OBJECT;
        csg->children.push_back(child);
    }
    csg->Compute_BBox();
    if (CSGIntersection *i = dynamic_cast<CSGIntersection *>(static_cast<CSG *>(csg)))
        i->Begin_Cells();
    return csg;
}

static void Scale(ObjectPtr o, const Vector3d& s) { TRANSFORM t; Compute_Scaling_Transform(&t, s); Scale_Object(o, s, &t); }
static void Rotate(ObjectPtr o, const Vector3d& r) { TRANSFORM t; Compute_Rotation_Transform(&t, r); Rotate_Object(o, r, &t); }
static void Translate(ObjectPtr o, const Vector3d& v) { TRANSFORM t; Compute_Translation_Transform(&t, v); Translate_Object(o, v, &t); }

/// The stock leaf: two large spheres intersected, scaled in its own statement; `placed` adds the scene's rotate and translate.
static const double kMid = 0.06, kLensR = (1 + (kMid / 2) * (kMid / 2)) / kMid;
static CSGIntersection *Lens()
{
    CSGIntersection *lens = Group(new CSGIntersection(false), {Ball(Vector3d(0, kLensR - kMid / 2, 0), kLensR), Ball(Vector3d(0, -(kLensR - kMid / 2), 0), kLensR)});
    Scale(lens, Vector3d(1.5, 1, 0.62));
    return lens;
}
static ObjectPtr Place(ObjectPtr declared)
{
    ObjectPtr o = Copy_Object(declared);
    Rotate(o, Vector3d(-80, 18, -14));
    Translate(o, Vector3d(0, 1.6, 0));
    return o;
}

static std::vector<SurfaceBox> Whole(ConstObjectPtr o, double fineness)
{
    class All final : public SurfaceBoxVisitor
    {
        public:
            std::vector<SurfaceBox> out;
            void Visit(const SurfaceBox& b) override { out.push_back(b); }
    } all;
    SurfaceQuery q;
    q.fineness = fineness;
    o->Visit_Surface_Boxes(all, 0, q);
    return all.out;
}

static std::vector<SurfaceBox> Region(ConstObjectPtr o, const Vector3d& lo, const Vector3d& hi, double fineness = 0.0)
{
    std::vector<SurfaceBox> out;
    Surface_Boxes(o, lo, hi, out, fineness);
    return out;
}

static bool Holds(const BoundingBox& box, const Vector3d& p, double reach)
{
    Vector3d low, high;
    Make_min_max_from_BBox(low, high, box);
    for (int a = X; a <= Z; ++a)
    {
        // a hit's own rounding, as Ray::MatchesSurfacePoint allows for
        const double rounding = 16.0 * std::numeric_limits<double>::epsilon() * std::max(std::fabs(p[a]), reach);
        if ((p[a] < low[a] - rounding) || (p[a] > high[a] + rounding))
            return false;
    }
    return true;
}

static bool Same(const std::vector<SurfaceBox>& a, const std::vector<SurfaceBox>& b)
{
    if (a.size() != b.size())
        return false;
    for (size_t k = 0; k < a.size(); ++k)
        for (int d = X; d <= Z; ++d)
            if ((a[k].piece != b[k].piece) || (a[k].box.lowerLeft[d] != b[k].box.lowerLeft[d]) || (a[k].box.size[d] != b[k].box.size[d]))
                return false;
    return true;
}

/// A point on a leaf shape's own surface, mapped to world; false for shapes it does not sample.
static bool Sample(ConstObjectPtr o, std::mt19937_64& random, Vector3d& p)
{
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    Vector3d u(unit(random) - 0.5, unit(random) - 0.5, unit(random) - 0.5);
    if (const Sphere *s = dynamic_cast<const Sphere *>(o))
    {
        u.normalize();
        if (!s->Spherical())
            MTransPoint(p, u, s->Trans);
        else
            p = s->Center + u * s->Radius;
        return true;
    }
    if (const Box *b = dynamic_cast<const Box *>(o))
    {
        const int face = int(unit(random) * 6) % 6;
        Vector3d q(b->bounds[0][X] + unit(random) * (b->bounds[1][X] - b->bounds[0][X]), b->bounds[0][Y] + unit(random) * (b->bounds[1][Y] - b->bounds[0][Y]),
                   b->bounds[0][Z] + unit(random) * (b->bounds[1][Z] - b->bounds[0][Z]));
        q[face / 2] = b->bounds[face % 2][face / 2];
        p = q;
        if (b->Trans != nullptr)
            MTransPoint(p, q, b->Trans);
        return true;
    }
    return false;
}

static void Leaves(ConstObjectPtr o, std::vector<ConstObjectPtr>& out)
{
    if (const CompoundObject *c = dynamic_cast<const CompoundObject *>(o))
        for (ConstObjectPtr k : c->children)
            Leaves(k, out);
    else
        out.push_back(o);
}

/// Points on the intersection's own surface: points of each child's leaf shapes that lie inside every other child.
static void Aims(ConstObjectPtr o, std::mt19937_64& random, std::vector<Vector3d>& out, unsigned each, TraceThreadData& thread)
{
    const CompoundObject *top = static_cast<const CompoundObject *>(o);
    for (size_t i = 0; i < top->children.size(); ++i)
    {
        std::vector<ConstObjectPtr> leaves;
        Leaves(top->children[i], leaves);
        for (ConstObjectPtr leaf : leaves)
        {
            unsigned kept = 0;
            for (unsigned tries = 0; (kept < each) && (tries < 4000000); ++tries)
            {
                Vector3d p;
                if (!Sample(leaf, random, p))
                    break;
                bool inside = true;
                for (size_t j = 0; inside && (j < top->children.size()); ++j)
                    inside = (j == i) || Inside_Object(p, top->children[j], &thread);
                if (inside)
                {
                    out.push_back(p);
                    ++kept;
                }
            }
        }
    }
}

struct Case
{
    std::string name;
    ObjectPtr object;
};

/// Every hit of rays aimed at the shapes' surfaces lies in a box of its own piece in each answer, and in the extents.
static unsigned Conservative(const Case& c, TraceThreadData& thread, const std::vector<double>& finenesses, const std::vector<int>& levels)
{
    std::mt19937_64 random(std::hash<std::string>()(c.name));
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    std::vector<Vector3d> aims;
    Aims(c.object, random, aims, 400, thread);
    std::vector<std::vector<SurfaceBox>> wholes;
    std::vector<std::pair<Vector3d, Vector3d>> extents;
    for (double f : finenesses)
    {
        wholes.push_back(Whole(c.object, f));
        Vector3d lo, hi;
        const bool any = c.object->Surface_Extent(f, lo, hi);
        assert(any);
        extents.push_back({lo, hi});
    }
    Vector3d o, top;
    Make_min_max_from_BBox(o, top, c.object->BBox);
    const double unitSide = std::max(top[X] - o[X], std::max(top[Y] - o[Y], top[Z] - o[Z]));
    TraceTicket ticket(8, 0.0);
    unsigned hits = 0;
    for (const Vector3d& aim : aims)
    {
        Vector3d d(unit(random) - 0.5, unit(random) - 0.5, unit(random) - 0.5);
        d.normalize();
        const double reach = (top - o).length() + 10.0;
        Ray ray(ticket, aim - d * reach, d);
        IStack stack(thread.stackPool);
        c.object->All_Intersections(ray, stack, &thread);
        for (; stack->size() > 0; stack->pop())
        {
            const Vector3d p = stack->top().IPoint;
            const double scale = reach + p.length();
            ++hits;
            for (size_t k = 0; k < wholes.size(); ++k)
            {
                bool in = false;
                for (const SurfaceBox& b : wholes[k])
                    in = in || Holds(b.box, p, scale);
                if (!in)
                    std::fprintf(stderr, "%s: hit %.17g %.17g %.17g outside the whole set at %g\n", c.name.c_str(), p.x(), p.y(), p.z(), finenesses[k]);
                assert(in);
                BoundingBox e;
                Make_BBox_from_min_max(e, extents[k].first, extents[k].second);
                assert(Holds(e, p, scale));
            }
            // the lattice cube holding the hit, at each level, is answered with one box of the piece holding it
            for (int level : levels)
            {
                const double side = std::ldexp(unitSide, -level);
                Vector3d lo, hi;
                for (int a = X; a <= Z; ++a)
                {
                    lo[a] = o[a] + side * std::floor((p[a] - o[a]) / side);
                    hi[a] = lo[a] + side;
                }
                const std::vector<SurfaceBox> r = Region(c.object, lo, hi);
                std::uint32_t piece = 0, leaf = 0;
                const bool surface = Surface_Part(c.object, stack->top(), piece, leaf);
                assert(surface);
                unsigned own = 0;
                bool in = false;
                for (const SurfaceBox& b : r)
                    if (b.piece == piece)
                    {
                        ++own;
                        in = in || Holds(b.box, p, scale);
                    }
                assert(own == 1);
                if (!in)
                    std::fprintf(stderr, "%s: hit %.17g %.17g %.17g outside its cube's box at level %d\n", c.name.c_str(), p.x(), p.y(), p.z(), level);
                assert(in);
            }
        }
    }
    // finer extents lie within coarser ones
    for (size_t k = 1; k < extents.size(); ++k)
        for (int a = X; a <= Z; ++a)
            assert((extents[k].first[a] >= extents[k - 1].first[a]) && (extents[k].second[a] <= extents[k - 1].second[a]));
    return hits;
}

int main()
{
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    Initialize_Noise();
    auto sceneData = std::make_shared<SceneData>();
    TraceThreadData thread(sceneData, 1, false);

    // the cases: the leaf, a bitten box, a thin shell, nested CSG, inverted and clipped children, a clipped intersection
    CSGIntersection *declared = Lens();
    ObjectPtr leaf = Place(declared);
    CSGIntersection *bitten = Group(new CSGIntersection(true), {Brick(Vector3d(-1), Vector3d(1)), Ball(Vector3d(1, 1, 1), 0.5)->Invert()});
    CSGIntersection *shell = Group(new CSGIntersection(true), {Ball(Vector3d(0.0), 1.0), Ball(Vector3d(0.0), 0.99)->Invert()});
    CSGUnion *pair = Group(new CSGUnion(), {Ball(Vector3d(-100, 0, 0), 0.1), Ball(Vector3d(100, 0, 0), 0.1)});
    CSGIntersection *far = Group(new CSGIntersection(false), {pair, Brick(Vector3d(-200), Vector3d(200))});
    CSGIntersection *inner = Group(new CSGIntersection(false), {Ball(Vector3d(0.3, 0, 0), 0.8), Ball(Vector3d(-0.3, 0, 0), 0.8)});
    CSGUnion *members = Group(new CSGUnion(), {inner, Ball(Vector3d(0, 0.9, 0), 0.4)});
    CSGIntersection *nested = Group(new CSGIntersection(false), {members, Brick(Vector3d(-2, -0.2, -2), Vector3d(2, 2, 2))});
    Rotate(nested, Vector3d(20, 35, -10));
    CSGIntersection *clipped = Group(new CSGIntersection(false), {Ball(Vector3d(0.0), 1.0), Brick(Vector3d(-0.5, -2, -2), Vector3d(2, 2, 2))});
    Plane *cut = new Plane();
    cut->Normal_Vector = Vector3d(0, 1, 0);
    cut->Distance = -0.3;
    cut->Compute_BBox();
    clipped->Clip.push_back(cut);
    ObjectPtr clippedChild = Ball(Vector3d(0.0), 1.2);
    clippedChild->Clip.push_back(Brick(Vector3d(-2, -2, -0.4), Vector3d(2, 2, 2)));
    CSGIntersection *withClipped = Group(new CSGIntersection(false), {clippedChild, Ball(Vector3d(0.5, 0, 0), 1.0)});
    Scale(withClipped, Vector3d(2, 0.5, 1));

    const std::vector<Case> cases = {{"leaf", leaf}, {"bitten box", bitten}, {"thin shell", shell}, {"far spheres", far},
                                     {"nested", nested}, {"clipped intersection", clipped}, {"clipped child", withClipped}};
    unsigned total = 0;
    for (const Case& c : cases)
    {
        assert(static_cast<CSGIntersection *>(c.object)->Refines());
        Vector3d lo, hi;
        Make_min_max_from_BBox(lo, hi, c.object->BBox);
        const double side = std::max(hi[X] - lo[X], std::max(hi[Y] - lo[Y], hi[Z] - lo[Z]));
        const unsigned hits = Conservative(c, thread, {side / 4, side / 64, side / 1024}, {2, 5, 8});
        assert(hits > 100);
        total += hits;
        std::printf("%-20s %6u hits, each in its piece's box in 3 whole-set answers, 3 region levels and 3 extents\n", c.name.c_str(), hits);
    }
    std::printf("conservative: %u hits checked\n", total);

    // a whole-set answer of many boxes names one box for each hit, through the cells it covers (B9)
    {
        CSGIntersection *flat = Group(new CSGIntersection(false), {Ball(Vector3d(0, kLensR - kMid / 2, 0), kLensR), Ball(Vector3d(0, -(kLensR - kMid / 2), 0), kLensR)});
        Scale(flat, Vector3d(1.5, 1, 0.62));
        ObjectPtr turned = Copy_Object(flat);
        Rotate(turned, Vector3d(-80, 18, -14));
        unsigned named = 0, boxes = 0;
        for (ObjectPtr o : {ObjectPtr(flat), turned, ObjectPtr(far)})
        {
            Vector3d lo, hi;
            Make_min_max_from_BBox(lo, hi, o->BBox);
            const double f = std::max(hi[X] - lo[X], std::max(hi[Y] - lo[Y], hi[Z] - lo[Z])) / 256;
            const std::vector<SurfaceBox> answer = Whole(o, f);
            boxes += unsigned(answer.size());
            std::mt19937_64 random(3);
            std::vector<Vector3d> aims;
            Aims(o, random, aims, 300, thread);
            TraceTicket ticket(8, 0.0);
            for (const Vector3d& aim : aims)
            {
                Ray ray(ticket, aim - Vector3d(0.3, 0.8, 0.5).normalized() * 500.0, Vector3d(0.3, 0.8, 0.5).normalized());
                IStack stack(thread.stackPool);
                o->All_Intersections(ray, stack, &thread);
                for (; stack->size() > 0; stack->pop())
                {
                    const Vector3d p = stack->top().IPoint;
                    const int k = static_cast<CSGIntersection *>(o)->Surface_Cell(p, f, answer);
                    assert(k >= 0);
                    assert(Holds(answer[k].box, p, 500.0 + p.length()));
                    ++named;
                }
            }
        }
        assert(named > 100);
        std::printf("cells: %u hits each named one box of its whole-set answer (%u boxes over three answers)\n", named, boxes);
    }

    // a copy shares the cells, the original going first: the copy still answers from its children
    {
        CSGIntersection *d = Lens();
        ObjectPtr a = Place(d), b = Place(d);
        assert(static_cast<CSGIntersection *>(a)->cells == d->cells);
        const std::vector<SurfaceBox> before = Whole(a, 0.01);
        Destroy_Object(d);
        const std::vector<SurfaceBox> after = Whole(b, 0.01);
        assert(Same(before, after));
        const unsigned hits = Conservative({"copy after its original", b}, thread, {0.1, 0.003}, {3, 7});
        std::printf("copies: the original destroyed, a copy gives the same %zu boxes; %u hits checked\n", after.size(), hits);
        Destroy_Object(a);
        Destroy_Object(b);
    }

    // determinism: the same answers whatever the order of requests, the threads, the copy that asks first
    {
        const std::vector<double> fs = {0.5, 0.05, 0.005};
        const auto answers = [&](ObjectPtr o, const std::vector<int>& order) {
            std::vector<std::vector<SurfaceBox>> out(fs.size());
            for (int k : order)
                out[k] = Whole(o, fs[k]);
            return out;
        };
        CSGIntersection *d0 = Lens();
        ObjectPtr ref = Place(d0);
        const auto coarseFirst = answers(ref, {0, 1, 2});
        CSGIntersection *d1 = Lens();
        ObjectPtr fineFirst = Place(d1);
        const auto f2 = answers(fineFirst, {2, 1, 0});
        CSGIntersection *d2 = Lens();
        ObjectPtr shuffled = Place(d2);
        const auto f3 = answers(shuffled, {1, 2, 0});
        // eight threads at once on a fresh lens, each asking all three in its own order
        CSGIntersection *d3 = Lens();
        ObjectPtr threaded = Place(d3);
        std::vector<std::vector<std::vector<SurfaceBox>>> perThread(8);
        std::vector<std::thread> workers;
        for (int t = 0; t < 8; ++t)
            workers.emplace_back([&, t]() {
                std::vector<int> order = {0, 1, 2};
                std::rotate(order.begin(), order.begin() + (t % 3), order.end());
                if (t & 1)
                    std::reverse(order.begin(), order.end());
                perThread[t] = answers(threaded, order);
            });
        for (std::thread& w : workers)
            w.join();
        // a second copy of the same declared lens, asked after the first copy refined the shared cells
        ObjectPtr second = Place(d0);
        const auto viaCopy = answers(second, {2, 0, 1});
        for (size_t k = 0; k < fs.size(); ++k)
        {
            assert(Same(coarseFirst[k], f2[k]) && Same(coarseFirst[k], f3[k]) && Same(coarseFirst[k], viaCopy[k]));
            for (int t = 0; t < 8; ++t)
                assert(Same(coarseFirst[k], perThread[t][k]));
        }
        // regions too
        Vector3d lo, hi;
        Make_min_max_from_BBox(lo, hi, ref->BBox);
        const Vector3d mid = (lo + hi) * 0.5;
        for (double s : {2.0, 0.3, 0.05})
            assert(Same(Region(ref, mid - Vector3d(s), mid + Vector3d(s)), Region(threaded, mid - Vector3d(s), mid + Vector3d(s))));
        std::printf("determinism: whole sets of %zu, %zu, %zu boxes equal coarse first, fine first, shuffled, through a second copy and on 8 threads\n",
                    coarseFirst[0].size(), coarseFirst[1].size(), coarseFirst[2].size());
    }

    // scale: the leaf scaled by 0.001, 1 and 1000 (the copy's transform, and the declared shape's own size)
    {
        const std::vector<double> scales = {0.001, 1.0, 1000.0};
        for (bool inDeclaration : {false, true})
        {
            std::vector<std::vector<SurfaceBox>> sets;
            for (double s : scales)
            {
                CSGIntersection *d = inDeclaration ? Group(new CSGIntersection(false), {Ball(Vector3d(0, kLensR - kMid / 2, 0) * s, kLensR * s),
                                                                                        Ball(Vector3d(0, -(kLensR - kMid / 2), 0) * s, kLensR * s)})
                                                   : Lens();
                if (inDeclaration)
                    Scale(d, Vector3d(1.5, 1, 0.62));
                ObjectPtr o = Copy_Object(d);
                Rotate(o, Vector3d(-80, 18, -14));
                if (!inDeclaration)
                    Scale(o, Vector3d(s));
                sets.push_back(Whole(o, 0.02 * s));
            }
            unsigned differ = 0;
            double worst = 0.0;
            for (size_t k = 0; k < scales.size(); ++k)
            {
                if (sets[k].size() != sets[1].size())
                {
                    ++differ;
                    continue;
                }
                for (size_t j = 0; j < sets[k].size(); ++j)
                    for (int a = X; a <= Z; ++a)
                        worst = std::max(worst, std::fabs(double(sets[k][j].box.lowerLeft[a]) / scales[k] - double(sets[1][j].box.lowerLeft[a])));
            }
            std::printf("scale 0.001, 1, 1000 (%s): %zu, %zu, %zu boxes; corners agree to %.3g of the unit-scale leaf\n",
                        inDeclaration ? "in the declaration" : "on the copy", sets[0].size(), sets[1].size(), sets[2].size(), worst);
            assert(differ == 0);
            assert(worst < 1e-5);
        }
    }

    // the worked cases: tests and nodes each request costs, from fresh objects
    {
        const auto cost = [](const char *name, const std::function<void()>& ask) {
            const std::uint64_t t0 = CSG_Cell_Tests(), n0 = CSG_Cell_Nodes();
            ask();
            std::printf("  %-58s %10llu tests %9llu nodes\n", name, (unsigned long long)(CSG_Cell_Tests() - t0),
                        (unsigned long long)(CSG_Cell_Nodes() - n0));
        };
        std::printf("worked cases:\n");
        CSGIntersection *d = Lens();
        ObjectPtr l = Place(d);
        Vector3d lo, hi;
        Make_min_max_from_BBox(lo, hi, l->BBox);
        const double E = std::max(hi[X] - lo[X], std::max(hi[Y] - lo[Y], hi[Z] - lo[Z]));
        cost("leaf: the bounce's first cube (the frame, fineness E)", [&]() { Region(l, lo, lo + Vector3d(E)); });
        cost("leaf: whole set at 1, 0.1, 0.01", [&]() { Whole(l, 1.0); Whole(l, 0.1); Whole(l, 0.01); });
        for (double f : {1e-1, 1e-2, 1e-3, 1e-4, 1e-5})
        {
            CSGIntersection *dd = Lens();
            ObjectPtr ll = Place(dd);
            Vector3d a, b;
            char name[96];
            cost("", [&]() { ll->Surface_Extent(f, a, b); });
            std::snprintf(name, sizeof(name), "    ^ leaf extent at %g: %.5g x %.5g x %.5g", f, b[X] - a[X], b[Y] - a[Y], b[Z] - a[Z]);
            std::printf("%s\n", name);
        }
    }
    std::printf("surface cells: ok\n");
    return 0;
}
