// Contract of Classify_Box: a box called outside holds no inside point (plain or precise) and no hit, a box called inside
// holds only inside points and no hit; for each shape with a rule of its own, inverted, transformed, through a frame
// map, clipped and in CSG.
#include "core/math/matrix.h"
#include "core/material/noise.h"
#include "core/render/ray.h"
#include "core/scene/object.h"
#include "core/scene/scenedata.h"
#include "core/scene/tracethreaddata.h"
#include "core/shape/box.h"
#include "core/shape/cone.h"
#include "core/shape/csg.h"
#include "core/shape/plane.h"
#include "core/shape/quadric.h"
#include "core/shape/sphere.h"
#include "core/shape/torus.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <vector>

using namespace pov;

struct Counts
{
    unsigned outside = 0, inside = 0, unknown = 0, points = 0, rays = 0;
};

static void Move(ObjectPtr o, int how)
{
    TRANSFORM t;
    if (how >= 1)
    {
        Compute_Scaling_Transform(&t, Vector3d(1.3, 0.7, 2.1));
        Scale_Object(o, Vector3d(1.3, 0.7, 2.1), &t);
        Compute_Translation_Transform(&t, Vector3d(0.4, -0.3, 0.2));
        Translate_Object(o, Vector3d(0.4, -0.3, 0.2), &t);
    }
    if (how >= 2)
    {
        Compute_Rotation_Transform(&t, Vector3d(-80, 18, -14));
        Rotate_Object(o, Vector3d(-80, 18, -14), &t);
    }
}

/// Classifies random boxes of the frame `toWorld` maps (null: world) and checks each definite answer by points and rays.
static void Check(const std::string& name, ObjectPtr object, const TRANSFORM *toWorld, TraceThreadData& thread, Counts& total)
{
    std::mt19937_64 random(std::hash<std::string>()(name));
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    TRANSFORM back;
    if (toWorld != nullptr)
        back = *toWorld;
    Counts c;
    TraceTicket ticket(8, 0.0);
    for (int b = 0; b < 3000; ++b)
    {
        const double size = std::pow(10.0, -3.0 + 3.5 * unit(random));
        Vector3d lo(-3 + 6 * unit(random), -3 + 6 * unit(random), -3 + 6 * unit(random));
        const Vector3d hi = lo + Vector3d(size * unit(random), size * unit(random), size * unit(random));
        const BoxClass k = Classify_Object(object, lo, hi, toWorld);
        if (k == BoxClass::Unknown)
        {
            ++c.unknown;
            continue;
        }
        (k == BoxClass::Outside) ? ++c.outside : ++c.inside;
        const auto world = [&](const Vector3d& p) { Vector3d w = p; if (toWorld != nullptr) MTransPoint(w, p, toWorld); return w; };
        for (int s = 0; s < 40; ++s)
        {
            // corners first, then points inside
            const Vector3d p = (s < 8) ? Vector3d((s & 1) ? hi.x() : lo.x(), (s & 2) ? hi.y() : lo.y(), (s & 4) ? hi.z() : lo.z())
                                       : Vector3d(lo.x() + unit(random) * (hi.x() - lo.x()), lo.y() + unit(random) * (hi.y() - lo.y()),
                                                  lo.z() + unit(random) * (hi.z() - lo.z()));
            const Vector3d w = world(p);
            for (bool precise : {false, true})
            {
                thread.preciseInside = precise;
                const bool in = Inside_Object(w, object, &thread);
                if (in != (k == BoxClass::Inside))
                    std::fprintf(stderr, "%s: box %s but point %.17g %.17g %.17g is %s (%s)\n", name.c_str(),
                                 k == BoxClass::Inside ? "inside" : "outside", w.x(), w.y(), w.z(), in ? "inside" : "outside",
                                 precise ? "precise" : "plain");
                assert(in == (k == BoxClass::Inside));
            }
            thread.preciseInside = false;
            ++c.points;
        }
        // lines through the box: no hit lies in it (a hit's own rounding aside)
        for (int r = 0; r < 6; ++r)
        {
            const Vector3d a(lo.x() + unit(random) * (hi.x() - lo.x()), lo.y() + unit(random) * (hi.y() - lo.y()), lo.z() + unit(random) * (hi.z() - lo.z()));
            Vector3d d(unit(random) - 0.5, unit(random) - 0.5, unit(random) - 0.5);
            d.normalize();
            const Vector3d from = world(a) - d * 50.0;
            Ray ray(ticket, from, d);
            IStack stack(thread.stackPool);
            object->All_Intersections(ray, stack, &thread);
            ++c.rays;
            while (stack->size() > 0)
            {
                const Intersection& hit = stack->top();
                Vector3d p = hit.IPoint;
                if (toWorld != nullptr)
                    MInvTransPoint(p, hit.IPoint, toWorld);
                bool in = true;
                for (int axis = X; axis <= Z; ++axis)
                {
                    const double slack = 1e-12 * (std::fabs(hi[axis]) + std::fabs(lo[axis]) + 50.0);
                    in = in && (p[axis] > lo[axis] + slack) && (p[axis] < hi[axis] - slack);
                }
                if (in)
                    std::fprintf(stderr, "%s: hit %.17g %.17g %.17g inside a box classed %s\n", name.c_str(), p.x(), p.y(), p.z(),
                                 k == BoxClass::Inside ? "inside" : "outside");
                assert(!in);
                stack->pop();
            }
        }
    }
    std::printf("%-34s outside %5u inside %5u unknown %5u\n", name.c_str(), c.outside, c.inside, c.unknown);
    total.outside += c.outside;
    total.inside += c.inside;
    total.unknown += c.unknown;
    total.points += c.points;
    total.rays += c.rays;
}

static Sphere *Ball(const Vector3d& centre, double radius)
{
    Sphere *s = new Sphere();
    s->Center = centre;
    s->Radius = radius;
    s->Compute_BBox();
    return s;
}

static ObjectPtr Make(int kind)
{
    switch (kind)
    {
        case 0: return Ball(Vector3d(0.2, -0.1, 0.3), 1.4);
        case 1: { Box *b = new Box(); b->bounds[0] = Vector3d(-1, -0.5, -1.5); b->bounds[1] = Vector3d(1.2, 1, 0.5); b->Compute_BBox(); return b; }
        case 2: { Plane *p = new Plane(); p->Normal_Vector = Vector3d(0.3, 0.9, -0.2).normalized(); p->Distance = -0.4; p->Compute_BBox(); return p; }
        case 3: case 4:
        {
            Cone *c = new Cone();
            c->base = Vector3d(0, -1, 0.2);
            c->apex = Vector3d(0.3, 1.2, -0.1);
            c->base_radius = 1.1;
            c->apex_radius = (kind == 3) ? 0.4 : 1.1;
            Set_Flag(c, CLOSED_FLAG);
            if (kind == 4)
            {
                c->Cylinder();
                c->apex_radius = c->base_radius = 1.1;
                c->Compute_Cylinder_Data();
            }
            else
                c->Compute_Cone_Data();
            c->Compute_BBox();
            return c;
        }
        case 5: { Torus *t = new Torus(); t->MajorRadius = 1.5; t->MinorRadius = 0.5; t->Compute_BBox(); return t; }
        default:
        {
            // a hyperboloid of one sheet
            Quadric *q = new Quadric();
            q->Square_Terms = Vector3d(1, -1, 1);
            q->Constant = -0.6;
            q->Compute_BBox();
            return q;
        }
    }
}

int main()
{
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    Initialize_Noise();
    auto sceneData = std::make_shared<SceneData>();
    TraceThreadData thread(sceneData, 1, false);
    const char *names[] = {"sphere", "box", "plane", "cone", "cylinder", "torus", "quadric"};
    TRANSFORM frame;
    Compute_Rotation_Transform(&frame, Vector3d(30, -45, 60));
    TRANSFORM scale;
    Compute_Scaling_Transform(&scale, Vector3d(0.5, 2.0, 1.0));
    Compose_Transforms(&frame, &scale);
    Counts total;
    for (int kind = 0; kind < 7; ++kind)
    {
        Counts shape;
        for (int how = 0; how <= 2; ++how)
            for (bool inverted : {false, true})
                for (bool mapped : {false, true})
                {
                    ObjectPtr o = Make(kind);
                    assert(Has_Box_Test(o));
                    Move(o, how);
                    if (inverted)
                        o = o->Invert();
                    Check(std::string(names[kind]) + (how == 0 ? "" : how == 1 ? " moved" : " rotated") + (inverted ? " inverted" : "") +
                          (mapped ? " in a frame" : ""), o, mapped ? &frame : nullptr, thread, shape);
                    Destroy_Object(o);
                }
        assert((shape.outside > 0) && (shape.inside > 0));
        total.outside += shape.outside;
        total.inside += shape.inside;
        total.unknown += shape.unknown;
        total.points += shape.points;
        total.rays += shape.rays;
    }

    // an open cylinder answers from its box only
    Cone *open = static_cast<Cone *>(Make(4));
    Clear_Flag(open, CLOSED_FLAG);
    assert(!Has_Box_Test(open));
    Check("open cylinder", open, nullptr, thread, total);

    // clips: outside a clip is outside; inside needs the clip inside too
    Sphere *clipped = Ball(Vector3d(0.0), 1.5);
    Plane *cut = static_cast<Plane *>(Make(2));
    clipped->Clip.push_back(cut);
    Check("clipped sphere", clipped, nullptr, thread, total);

    // CSG: a lens, a bitten box, a union and a merge, each moved and through a frame
    const auto group = [](CSG *csg, std::vector<ObjectPtr> kids) {
        for (ObjectPtr k : kids) { k->Type |= IS_CHILD_OBJECT; csg->children.push_back(k); }
        csg->Compute_BBox();
        return csg;
    };
    for (int how = 0; how <= 2; ++how)
        for (bool mapped : {false, true})
        {
            const std::string tail = std::string(how == 0 ? "" : how == 1 ? " moved" : " rotated") + (mapped ? " in a frame" : "");
            std::vector<ObjectPtr> csgs = {
                group(new CSGIntersection(false), {Ball(Vector3d(0, -1.2, 0), 1.6), Ball(Vector3d(0, 1.2, 0), 1.6)}),
                group(new CSGIntersection(true), {Make(1), Ball(Vector3d(1.2, 1, 0.5), 0.9)->Invert(), Make(5)->Invert()}),
                group(new CSGUnion(), {Ball(Vector3d(-1, 0, 0), 0.8), Ball(Vector3d(0.5, 0, 0), 0.8), Make(1)}),
                group(new CSGMerge(), {Ball(Vector3d(-1, 0, 0), 0.8), Ball(Vector3d(0.5, 0, 0), 0.8), Make(3)})};
            const char *kinds[] = {"lens", "bitten box", "union", "merge"};
            for (size_t k = 0; k < csgs.size(); ++k)
            {
                Move(csgs[k], how);
                Check(std::string(kinds[k]) + tail, csgs[k], mapped ? &frame : nullptr, thread, total);
            }
        }
    std::printf("box classes: %u outside, %u inside, %u unknown; %u points checked plain and precise, %u lines through\n",
                total.outside, total.inside, total.unknown, total.points, total.rays);
    std::printf("box classes: ok\n");
    return 0;
}
