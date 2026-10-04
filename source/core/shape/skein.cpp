//******************************************************************************
///
/// @file core/shape/skein.cpp
///
/// Implementation of the skein geometric primitive.
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

// Unit header file must be the first file included within POV-Ray *.cpp files (pulls in config)
#include "core/shape/skein.h"

// C++ variants of C standard header files
#include <cmath>
#include <cstdio>

// C++ standard header files
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include <unordered_map>

// POV-Ray header files (base module)
#include "base/mathutil.h"

// POV-Ray header files (core module)
#include "core/bounding/boundingbox.h"
#include "core/material/noise.h"
#include "core/material/pattern.h"
#include "core/render/ray.h"
#include "core/scene/tracethreaddata.h"
#include "core/support/statistics.h"

// this must be the last file included
#include "base/povdebug.h"

namespace pov
{

namespace
{

const DBL kTau = 2.0 * M_PI;
const DBL kRadiansPerDegree = M_PI / 180.0;
const DBL kDepthTolerance = 1.0e-6;
const int kNewtonSteps = 16;
const int kSplitDepth = 6;
const DBL kSlack = 1.0e-8;
const DBL kPatchTurn = M_PI / 16.0;
const int kMaxGrid = 256;
const int kRingSamples = 64;
const int kMaxArguments = 8;
const int kMaxFunctions = SkeinData::kMaxFunctions;
const int kFrameSamples = 2048;
const int kArcCells = 1024;
const DBL kNeighbour = 1.0e-5;
const DBL kFineNeighbour = 1.0e-8;
const DBL kNearRoot = 1.0e-4;
const DBL kAxisStep = 1.0e-5;
const int kRollCells = 1024;
const DBL kMaxRollTurns = 256.0;
const Vector3d kInsideDirection(0.5381, 0.6012, 0.5907);
const int kMeshDepth = 22;
const DBL kMeshNudge = 0.01;
const DBL kMeshSpread = 1.5;
const DBL kMeshCrease = 3.0;

struct Jet { Vector3d p, pu, pv; };
struct Dual { DBL x, u, v; };
struct AxisState { Vector3d P, P1, e1, e2, omega; };

std::string Format(const char *format, DBL a, DBL b = 0.0)
{
    char text[256];
    snprintf(text, sizeof(text), format, a, b);
    return text;
}

struct Interval
{
    DBL lo, hi;
    Interval(DBL a = 0.0) : lo(a), hi(a) {}
    Interval(DBL a, DBL b) : lo(a), hi(b) {}
};

Interval operator+(const Interval& a, const Interval& b) { return Interval(a.lo + b.lo, a.hi + b.hi); }
Interval operator-(const Interval& a, const Interval& b) { return Interval(a.lo - b.hi, a.hi - b.lo); }
Interval operator*(DBL k, const Interval& a) { return k >= 0.0 ? Interval(k * a.lo, k * a.hi) : Interval(k * a.hi, k * a.lo); }

Interval operator*(const Interval& a, const Interval& b)
{
    const DBL p0 = a.lo * b.lo, p1 = a.lo * b.hi, p2 = a.hi * b.lo, p3 = a.hi * b.hi;
    return Interval(std::min(std::min(p0, p1), std::min(p2, p3)), std::max(std::max(p0, p1), std::max(p2, p3)));
}

Interval Square(const Interval& a)
{
    const DBL p = a.lo * a.lo, q = a.hi * a.hi;
    return Interval((a.lo <= 0.0) && (a.hi >= 0.0) ? 0.0 : std::min(p, q), std::max(p, q));
}

DBL Reach(const Interval& a) { return std::max(fabs(a.lo), fabs(a.hi)); }

Interval Cosine(const Interval& a)
{
    if (!(a.hi - a.lo < kTau))
        return Interval(-1.0, 1.0);
    const DBL c0 = cos(a.lo), c1 = cos(a.hi);
    Interval r(std::min(c0, c1), std::max(c0, c1));
    if (ceil(a.lo / kTau) * kTau <= a.hi)
        r.hi = 1.0;
    if (ceil((a.lo - M_PI) / kTau) * kTau + M_PI <= a.hi)
        r.lo = -1.0;
    return r;
}

Interval Sine(const Interval& a) { return Cosine(Interval(a.lo - 0.5 * M_PI, a.hi - 0.5 * M_PI)); }

Interval Atan2(const Interval& y, const Interval& x)
{
    if ((y.lo <= 0.0) && (y.hi >= 0.0) && (x.lo <= 0.0))
        return Interval(-M_PI, M_PI);
    const DBL a[4] = { atan2(y.lo, x.lo), atan2(y.lo, x.hi), atan2(y.hi, x.lo), atan2(y.hi, x.hi) };
    return Interval(*std::min_element(a, a + 4), *std::max_element(a, a + 4));
}

Interval Project(const Interval b[3], const Vector3d& d) { return d[X] * b[X] + d[Y] * b[Y] + d[Z] * b[Z]; }

DBL WrapAngle(DBL a) { return a - kTau * floor(a / kTau + 0.5); }

Vector3d Rotated(const Vector3d& x, const Vector3d& axis, DBL c, DBL s) { return x * c + cross(axis, x) * s + axis * (dot(axis, x) * (1.0 - c)); }

/// A station as the axial step uses it: past the ends of an open path axis it clamps, where the path itself keeps no tangent.
DBL OnAxis(const SkeinAxis& a, DBL t) { return (!a.path || a.path->closed) ? t : std::min(a.t1, std::max(a.t0, t)); }

bool HitBox(const Vector3d& lo, const Vector3d& hi, const Vector3d& P, const Vector3d& D, DBL tMin, DBL& t0, DBL& t1)
{
    t0 = tMin;
    t1 = BOUND_HUGE;
    for (int k = 0; k < 3; ++k)
    {
        if (D[k] == 0.0)
        {
            if ((P[k] < lo[k]) || (P[k] > hi[k]))
                return false;
            continue;
        }
        DBL a = (lo[k] - P[k]) / D[k], b = (hi[k] - P[k]) / D[k];
        if (a > b)
            std::swap(a, b);
        t0 = std::max(t0, a);
        t1 = std::min(t1, b);
        if (t0 > t1)
            return false;
    }
    return true;
}

/// A range modifier's input wrapped onto 0..1 by its method; `slope` is the derivative of the result.
DBL RangeWrap(const SkeinMap::Node& n, DBL x, DBL& slope)
{
    const DBL scale = 1.0 / (n.b - n.a), w = (x - n.a) * scale;
    slope = scale;
    switch (n.method)
    {
        case SkeinMap::kRepeat:
            return w - floor(w);
        case SkeinMap::kMirror:
        {
            const DBL m = w - 2.0 * floor(0.5 * w);
            if (m <= 1.0)
                return m;
            slope = -scale;
            return 2.0 - m;
        }
        case SkeinMap::kClamp:
            if ((w > 0.0) && (w < 1.0))
                return w;
            slope = 0.0;
            return w <= 0.0 ? 0.0 : 1.0;
        default:
            return w;
    }
}

Interval RangeWrap(const SkeinMap::Node& n, const Interval& x)
{
    const Interval w = (1.0 / (n.b - n.a)) * (x - Interval(n.a));
    switch (n.method)
    {
        case SkeinMap::kRepeat:
        {
            const DBL k = floor(w.lo);
            return floor(w.hi) == k ? Interval(w.lo - k, w.hi - k) : Interval(0.0, 1.0);
        }
        case SkeinMap::kMirror:
        {
            if (w.hi - w.lo >= 2.0)
                return Interval(0.0, 1.0);
            DBL s;
            SkeinMap::Node unit;
            unit.op = SkeinMap::kRange;
            unit.method = SkeinMap::kMirror;
            unit.b = unit.d = 1.0;
            const DBL a = RangeWrap(unit, w.lo, s), b = RangeWrap(unit, w.hi, s);
            Interval r(std::min(a, b), std::max(a, b));
            if (2.0 * ceil(0.5 * w.lo) <= w.hi)
                r.lo = 0.0;
            if (2.0 * ceil(0.5 * (w.lo - 1.0)) + 1.0 <= w.hi)
                r.hi = 1.0;
            return r;
        }
        case SkeinMap::kClamp:
            return Interval(std::min(1.0, std::max(0.0, w.lo)), std::min(1.0, std::max(0.0, w.hi)));
        default:
            return w;
    }
}

/// POV's lattice noise as `bozo` and f_noise3d compute it (PortableNoise, generator 2), with its exact gradient.
DBL LatticeNoise(const Vector3d& p, Vector3d& gradient)
{
    const int minimum[3] = { NOISE_MINX, NOISE_MINY, NOISE_MINZ };
    int cell[3];
    DBL f[3], s[3], ds[3];
    for (int k = 0; k < 3; ++k)
    {
        const int whole = (p[k] >= 0.0) ? int(p[k]) : int(p[k] - (1.0 - EPSILON));
        cell[k] = (whole - minimum[k]) & 0xFFF;
        f[k] = p[k] - whole;
        s[k] = f[k] * f[k] * (3.0 - 2.0 * f[k]);
        ds[k] = 6.0 * f[k] * (1.0 - f[k]);
    }
    DBL sum = 0.0;
    gradient = Vector3d(0.0);
    for (int corner = 0; corner < 8; ++corner)
    {
        const int a = corner & 1, b = (corner >> 1) & 1, c = (corner >> 2) & 1;
        const DBL wx = a ? s[X] : 1.0 - s[X], wy = b ? s[Y] : 1.0 - s[Y], wz = c ? s[Z] : 1.0 - s[Z];
        const DBL dx = a ? ds[X] : -ds[X], dy = b ? ds[Y] : -ds[Y], dz = c ? ds[Z] : -ds[Z];
        const DBL *mp = &RTable[Hash1dRTableIndex(Hash2d(cell[X] + a, cell[Y] + b), cell[Z] + c)];
        const DBL value = mp[1] + mp[2] * (f[X] - a) + mp[4] * (f[Y] - b) + mp[6] * (f[Z] - c), w = wx * wy * wz;
        sum += w * value;
        gradient += Vector3d(dx * wy * wz * value + w * mp[2], wx * dy * wz * value + w * mp[4], wx * wy * dz * value + w * mp[6]);
    }
    const DBL n = (sum + 1.05242) * 0.48985582;
    if ((n <= 0.0) || (n >= 1.0))
    {
        gradient = Vector3d(0.0);
        return n <= 0.0 ? 0.0 : 1.0;
    }
    gradient *= 0.48985582;
    return n;
}

/// Bounds on |grad LatticeNoise| and on its gradient's Lipschitz constant, from the largest table entries (doc/skein.md, batch two).
struct NoiseBound { DBL slope, curvature; };

const NoiseBound& NoiseBounds()
{
    static const NoiseBound bound = []()
    {
        DBL full = 0.0, half = 0.0;
        for (int i = 0; i < 267; ++i)
        {
            full = std::max(full, fabs(RTable[2 * i]));
            half = std::max(half, fabs(RTable[2 * i + 1]));
        }
        const DBL value = half + 3.0 * full;
        return NoiseBound{ 0.48985582 * sqrt(3.0) * (3.0 * value + full),
                           0.48985582 * ((12.0 * value + 6.0 * full) + 2.0 * (9.0 * value + 6.0 * full)) };
    }();
    return bound;
}

/// Distances to the nearest and second-nearest of POV's crackle nuclei (PickInCube) of the cells at z = 0, with their gradients.
void Worley(DBL x, DBL y, DBL& f1, DBL& f2, Vector2d& g1, Vector2d& g2)
{
    const int cx = int(floor(x)), cy = int(floor(y));
    DBL d1 = BOUND_HUGE, d2 = BOUND_HUGE;
    Vector2d n1(0.0), n2(0.0);
    for (int ring = 0; ring <= 3; ++ring)
    {
        if ((ring >= 2) && (DBL(ring - 1) * (ring - 1) >= d2))
            break;
        for (int j = cy - ring; j <= cy + ring; ++j)
        {
            const bool edge = (ring == 0) || (j == cy - ring) || (j == cy + ring);
            for (int i = cx - ring; i <= cx + ring; i += edge ? 1 : 2 * ring)
            {
                const DBL bx = std::max(0.0, std::max(i - x, x - (i + 1))), by = std::max(0.0, std::max(j - y, y - (j + 1)));
                if (bx * bx + by * by >= d2)
                    continue;
                Vector3d nucleus;
                PickInCube(Vector3d(i + 0.5, j + 0.5, 0.5), nucleus);
                const Vector2d q(x - nucleus[X], y - nucleus[Y]);
                const DBL dd = q[U] * q[U] + q[V] * q[V];
                if (dd < d1)
                {
                    d2 = d1;
                    n2 = n1;
                    d1 = dd;
                    n1 = q;
                }
                else if (dd < d2)
                {
                    d2 = dd;
                    n2 = q;
                }
            }
        }
    }
    f1 = sqrt(d1);
    f2 = sqrt(d2);
    g1 = f1 > 0.0 ? n1 / f1 : Vector2d(0.0);
    g2 = f2 > 0.0 ? n2 / f2 : Vector2d(0.0);
}

/// Runs a map's postfix program on dual numbers: the value with its u and v derivatives.
Dual RunMap(const SkeinMap& m, const Dual *in)
{
    Dual stack[SkeinMap::kMaxDepth];
    int top = 0;
    for (const SkeinMap::Node& n : m.nodes)
    {
        if (n.op == SkeinMap::kInput)
        {
            stack[top++] = in[n.input];
            continue;
        }
        const int arity = SkeinMap::Arity(n.op);
        top -= arity;
        const Dual *a = stack + top;
        DBL value = 0.0, slope[3] = { 0.0, 0.0, 0.0 };
        switch (n.op)
        {
            case SkeinMap::kLinear:
                value = n.a * a[0].x + n.b;
                slope[0] = n.a;
                break;
            case SkeinMap::kSin:
            case SkeinMap::kCos:
            {
                const DBL g = kTau * (n.a * a[0].x + n.c);
                value = n.b * (n.op == SkeinMap::kSin ? sin(g) : cos(g));
                slope[0] = kTau * n.a * n.b * (n.op == SkeinMap::kSin ? cos(g) : -sin(g));
                break;
            }
            case SkeinMap::kRange:
            {
                const DBL w = RangeWrap(n, a[0].x, slope[0]);
                value = n.c + (n.d - n.c) * w;
                slope[0] *= n.d - n.c;
                break;
            }
            case SkeinMap::kPath:
            {
                Vector3d p, d1, d2;
                n.path->Evaluate(a[0].x, p, d1, d2);
                value = p[X];
                slope[0] = d1[X];
                break;
            }
            case SkeinMap::kLength:
            case SkeinMap::kAtan2:
            {
                const DBL rr = a[0].x * a[0].x + a[1].x * a[1].x;
                if (n.op == SkeinMap::kLength)
                {
                    value = sqrt(rr);
                    const DBL k = value > 0.0 ? 1.0 / value : 0.0;
                    slope[0] = k * a[0].x;
                    slope[1] = k * a[1].x;
                }
                else
                {
                    value = atan2(a[0].x, a[1].x);
                    const DBL k = rr > 0.0 ? 1.0 / rr : 0.0;
                    slope[0] = k * a[1].x;
                    slope[1] = -k * a[0].x;
                }
                break;
            }
            case SkeinMap::kNoise:
            case SkeinMap::kFbm:
            {
                DBL frequency = n.a, amplitude = n.b;
                for (int octave = 0; octave < n.count; ++octave)
                {
                    Vector3d g;
                    value += amplitude * (2.0 * LatticeNoise(Vector3d(a[0].x, a[1].x, a[2].x) * frequency, g) - 1.0);
                    for (int k = 0; k < 3; ++k)
                        slope[k] += 2.0 * amplitude * frequency * g[k];
                    frequency *= n.c;
                    amplitude *= n.d;
                }
                break;
            }
            case SkeinMap::kCells:
            {
                DBL f1, f2;
                Vector2d g1, g2;
                Worley(n.a * a[0].x, n.a * a[1].x, f1, f2, g1, g2);
                value = n.b * (n.c * f1 + n.d * f2);
                for (int k = 0; k < 2; ++k)
                    slope[k] = n.b * n.a * (n.c * g1[k] + n.d * g2[k]);
                break;
            }
            default:
                value = n.image->Sample(a[0].x, a[1].x, slope[0], slope[1]);
                break;
        }
        Dual r{ value, 0.0, 0.0 };
        for (int k = 0; k < arity; ++k)
        {
            r.u += slope[k] * a[k].u;
            r.v += slope[k] * a[k].v;
        }
        stack[top++] = r;
    }
    return stack[0];
}

/// The centre of a box of inputs and the length of its half diagonal, huge for an unbounded box.
DBL BoxCentre(const Interval *a, int n, DBL *centre)
{
    DBL rr = 0.0;
    for (int k = 0; k < n; ++k)
    {
        centre[k] = 0.5 * (a[k].lo + a[k].hi);
        rr += 0.25 * (a[k].hi - a[k].lo) * (a[k].hi - a[k].lo);
    }
    return (std::isfinite(rr) && (rr < BOUND_HUGE)) ? sqrt(rr) : BOUND_HUGE;
}

/// Encloses a map's value over boxes of its inputs; noise and cells by their slope bounds about the box centre.
Interval MapRange(const SkeinMap& m, const Interval *in)
{
    Interval stack[SkeinMap::kMaxDepth];
    int top = 0;
    for (const SkeinMap::Node& n : m.nodes)
    {
        if (n.op == SkeinMap::kInput)
        {
            stack[top++] = in[n.input];
            continue;
        }
        top -= SkeinMap::Arity(n.op);
        const Interval *a = stack + top;
        Interval r;
        switch (n.op)
        {
            case SkeinMap::kLinear:
                r = Interval(n.b) + n.a * a[0];
                break;
            case SkeinMap::kSin:
            case SkeinMap::kCos:
            {
                const Interval g = Interval(kTau * n.c) + (kTau * n.a) * a[0];
                r = n.b * (n.op == SkeinMap::kSin ? Sine(g) : Cosine(g));
                break;
            }
            case SkeinMap::kRange:
                r = Interval(n.c) + (n.d - n.c) * RangeWrap(n, a[0]);
                break;
            case SkeinMap::kPath:
            {
                Vector3d lo, hi;
                n.path->Bound(a[0].lo, a[0].hi, lo, hi);
                r = Interval(lo[X], hi[X]);
                break;
            }
            case SkeinMap::kLength:
            {
                const Interval rr = Square(a[0]) + Square(a[1]);
                r = Interval(sqrt(rr.lo), sqrt(rr.hi));
                break;
            }
            case SkeinMap::kAtan2:
                r = Atan2(a[0], a[1]);
                break;
            case SkeinMap::kNoise:
            case SkeinMap::kFbm:
            {
                DBL centre[3], frequency = n.a, amplitude = n.b;
                const DBL reach = BoxCentre(a, 3, centre);
                const NoiseBound& bound = NoiseBounds();
                r = Interval(0.0);
                for (int octave = 0; octave < n.count; ++octave)
                {
                    Vector3d g;
                    const DBL c = LatticeNoise(Vector3d(centre[X], centre[Y], centre[Z]) * frequency, g), f = fabs(frequency);
                    DBL spread = bound.slope * f * reach;
                    if ((c > 0.0) && (c < 1.0) && (reach < BOUND_HUGE))
                    {
                        DBL linear = 0.0;
                        for (int k = 0; k < 3; ++k)
                            linear += fabs(g[k]) * 0.5 * (a[k].hi - a[k].lo);
                        spread = std::min(spread, f * linear + 0.5 * bound.curvature * f * f * reach * reach);
                    }
                    const Interval noise(std::max(0.0, c - spread), std::min(1.0, c + spread));
                    r = r + amplitude * (Interval(-1.0) + 2.0 * noise);
                    frequency *= n.c;
                    amplitude *= n.d;
                }
                break;
            }
            case SkeinMap::kCells:
            {
                DBL centre[2], f1, f2;
                Vector2d g1, g2;
                const DBL reach = BoxCentre(a, 2, centre);
                Worley(n.a * centre[0], n.a * centre[1], f1, f2, g1, g2);
                const DBL c = n.c * f1 + n.d * f2, spread = (fabs(n.c) + fabs(n.d)) * fabs(n.a) * reach;
                const DBL limit = (fabs(n.c) + 2.0 * fabs(n.d)) * sqrt(2.0);
                r = n.b * Interval(std::max(-limit, c - spread), std::min(limit, c + spread));
                break;
            }
            default:
            {
                DBL lo, hi;
                n.image->Range(a[0].lo, a[0].hi, a[1].lo, a[1].hi, lo, hi);
                r = Interval(lo, hi);
                break;
            }
        }
        stack[top++] = r;
    }
    return stack[0];
}

/// A list of steps and where it sits: the top chain, or an expression_map item of step `at` in its parent; `end` holds the wrap flags after its last step.
struct Scope
{
    const std::vector<SkeinStep>& steps;
    const Scope *parent;
    int at;
    unsigned char end;
};

/// The surface entering step `index` of a scope, with that surface's wrap flags (bit 0 u, bit 1 v).
struct Stage
{
    const Scope *scope;
    int index;
    unsigned char Flags() const { return index < int(scope->steps.size()) ? scope->steps[index].wrapIn : scope->end; }
};

/// Where a parameter value of an envelope's doubled axis falls: a face at sheet coordinate `a` moving at `rate`, or a rim at edge `a` at angle `turn`.
struct EnvelopePiece
{
    enum Kind { kFront, kBack, kRim } kind;
    DBL a, rate, turn;
};

/// Interval state of the box evaluation of a patch.
struct BoxState
{
    Interval b[3], bu[3], bv[3];
    bool slopes, exact;
};

const DBL kRimShare = 1.0 / 32.0;

/// The first function of an axis anywhere in a chain, blend entries included, or null.
GenericScalarFunctionPtr AxisFunction(const std::vector<SkeinStep>& steps)
{
    for (const SkeinStep& s : steps)
    {
        for (const std::shared_ptr<SkeinAxis>& a : { s.curve, s.target })
            if (a && a->functions[0])
                return a->functions[0].get();
        if (s.blend)
            for (const SkeinBlend::Entry& e : s.blend->entries)
                if (GenericScalarFunctionPtr f = AxisFunction(e.steps))
                    return f;
    }
    return nullptr;
}

/// Evaluates the chain at a sample with its parameter derivatives, using one function context for the thread.
class Evaluator final
{
    public:

        Evaluator(const SkeinData& d, TraceThreadData *t) :
            data(d), thread(t), host(nullptr), context(nullptr), top{ d.steps, nullptr, 0, (unsigned char)((d.wrapU ? 1 : 0) | (d.wrapV ? 2 : 0)) }
        {
            host = d.slots.empty() ? AxisFunction(d.steps) : d.slots.front()->function.get();
            if (host != nullptr)
                context = host->AcquireContext(t);
        }

        ~Evaluator()
        {
            if (host != nullptr)
                host->ReleaseContext(context);
        }

        const Scope& Top() const { return top; }

        /// The parameter step of differenced slopes.
        DBL neighbour = kNeighbour;

        DBL WrapU(DBL u) const { return data.wrapU ? u - floor(u) : u; }
        DBL WrapV(DBL v) const { return data.wrapV ? v - floor(v) : v; }

        /// The whole chain at (u, v); `values`, when given, collects each function value's low and high.
        void Eval(DBL u, DBL v, Jet& J, DBL *values = nullptr, bool slopes = true)
        {
            Prefix(top, int(top.steps.size()), u, v, J, values, slopes);
        }

        /// The surface after the first `count` steps of a scope; slopes are exact up to the last step that reads the normal, and after it only when asked.
        void Prefix(const Scope& s, int count, DBL u, DBL v, Jet& J, DBL *values, bool slopes)
        {
            if (s.parent == nullptr)
            {
                thread->Stats()[Skein_Surface_Evaluations]++;
                J.p = Vector3d(u, v, 0.0);
                J.pu = Vector3d(1.0, 0.0, 0.0);
                J.pv = Vector3d(0.0, 1.0, 0.0);
            }
            else
                Prefix(*s.parent, s.at, u, v, J, values, true);
            Apply(s, 0, count, u, v, J, values, slopes);
        }

        /// Encloses each function value over a patch: proven by the VM's interval plan where it has one, else sampled with a margin.
        bool Ranges(DBL u0, DBL u1, DBL v0, DBL v1, int samples, DBL *out)
        {
            bool proven[kMaxFunctions], all = true;
            for (int f = 0; f < data.functionCount; ++f)
            {
                const SkeinValue& value = *data.slots[f];
                proven[f] = false;
                if (!data.resamples && (value.inputs.size() <= 3))
                {
                    Vector3d a, b;
                    bool uv = true;
                    for (size_t k = 0; k < value.inputs.size(); ++k)
                    {
                        uv = uv && (value.inputs[k] <= SkeinValue::kV);
                        a[k] = value.inputs[k] == SkeinValue::kU ? u0 : v0;
                        b[k] = value.inputs[k] == SkeinValue::kU ? u1 : v1;
                    }
                    DBL lo, hi;
                    if (uv && value.function->EvaluateRange(a, b, lo, hi))
                    {
                        const DBL pad = 1.0e-12 * (1.0 + fabs(lo) + fabs(hi));
                        out[2 * f] = lo - pad;
                        out[2 * f + 1] = hi + pad;
                        proven[f] = true;
                    }
                }
                all = all && proven[f];
            }
            if (all)
                return true;
            DBL record[2 * kMaxFunctions];
            for (int f = 0; f < kMaxFunctions; ++f)
            {
                record[2 * f] = BOUND_HUGE;
                record[2 * f + 1] = -BOUND_HUGE;
            }
            Jet J;
            for (int b = 0; b < samples; ++b)
                for (int a = 0; a < samples; ++a)
                    Eval(WrapU(u0 + (u1 - u0) * a / (samples - 1)), WrapV(v0 + (v1 - v0) * b / (samples - 1)), J, record, false);
            for (int f = 0; f < data.functionCount; ++f)
            {
                if (proven[f])
                    continue;
                const DBL lo = record[2 * f], hi = record[2 * f + 1];
                if (lo > hi)
                {
                    out[2 * f] = -BOUND_HUGE;
                    out[2 * f + 1] = BOUND_HUGE;
                    continue;
                }
                const DBL margin = 0.25 * (hi - lo) + 1.0e-6 * (1.0 + fabs(lo) + fabs(hi));
                out[2 * f] = lo - margin;
                out[2 * f + 1] = hi + margin;
            }
            return false;
        }

        /// Encloses the patch's points in a box and, when every step is affine or a constant straight extrude, dS/du and dS/dv in intervals.
        bool Box(DBL u0, DBL u1, DBL v0, DBL v1, const DBL *ranges, Vector3d& lo, Vector3d& hi, Interval *su = nullptr, Interval *sv = nullptr)
        {
            BoxState st;
            BoxStart(st, u0, u1, v0, v1);
            st.slopes = (su != nullptr);
            BoxRun(top, 0, int(top.steps.size()), u0, u1, v0, v1, ranges, st);
            if (st.slopes)
                for (int i = 0; i < 3; ++i)
                {
                    su[i] = st.bu[i];
                    sv[i] = st.bv[i];
                }
            const DBL pad = 1.0e-9 * data.size;
            lo = Vector3d(st.b[X].lo - pad, st.b[Y].lo - pad, st.b[Z].lo - pad);
            hi = Vector3d(st.b[X].hi + pad, st.b[Y].hi + pad, st.b[Z].hi + pad);
            return st.slopes;
        }

        /// A point on an axis with its first two derivatives; three functions are differenced, a straight line is exact.
        void AxisPoint(const SkeinAxis& a, DBL t, Vector3d& p, Vector3d& d1, Vector3d& d2)
        {
            if (a.line)
            {
                p = a.direction * t;
                d1 = a.direction;
                d2 = Vector3d(0.0);
                return;
            }
            if (a.path)
            {
                a.path->Evaluate(t, p, d1, d2);
                return;
            }
            for (int k = 0; k < 3; ++k)
            {
                const DBL f0 = Axis(a, k, t), fa = Axis(a, k, t + kAxisStep), fb = Axis(a, k, t - kAxisStep);
                p[k] = f0;
                d1[k] = (fa - fb) / (2.0 * kAxisStep);
                d2[k] = (fa - 2.0 * f0 + fb) / (kAxisStep * kAxisStep);
            }
        }

        /// A sample_path placed in space: each (u, v, d) control point goes to the surface entering `index`, offset d along its normal.
        std::shared_ptr<const SkeinPath> PlacedPath(const SkeinPath& p, const Scope& sc, int index)
        {
            auto placed = std::make_shared<SkeinPath>(p);
            std::vector<SkeinPath::Point> points = p.raw;
            const Stage st{ &sc, index };
            // why: the slopes at an edge difference across a seam the surface may not have yet (doc/skein.md, sample_path)
            const DBL inset = 4.0 * kNeighbour;
            for (SkeinPath::Point& q : points)
            {
                const DBL u = WrapU(q.point[X]), v = WrapV(q.point[Y]);
                const DBL un = std::min(1.0 - inset, std::max(inset, u)), vn = std::min(1.0 - inset, std::max(inset, v));
                Jet J, K;
                Prefix(sc, index, u, v, J, nullptr, false);
                Prefix(sc, index, un, vn, K, nullptr, true);
                const Vector3d n = NormalOf(st, un, vn, K);
                if (q.haveIn)
                    q.in = K.pu * q.in[X] + K.pv * q.in[Y] + n * q.in[Z];
                if (q.haveOut)
                    q.out = K.pu * q.out[X] + K.pv * q.out[Y] + n * q.out[Z];
                q.point = J.p + n * q.point[Z];
            }
            placed->Build(points, p.interp);
            if (p.useArc)
                placed->UseArcLength();
            placed->fromSurface = false;
            return placed;
        }

        /// Fills a curved axis's rotation-minimising frame table by double reflection, with the loop's leftover turn and its arc length.
        void BuildFrames(SkeinAxis& a)
        {
            if (a.line)
            {
                const Vector3d ref = fabs(a.direction[X]) > 0.9 ? Vector3d(0.0, 1.0, 0.0) : Vector3d(1.0, 0.0, 0.0);
                a.e1 = (ref - a.direction * dot(ref, a.direction)).normalized();
                a.e2 = cross(a.direction, a.e1);
                a.t0 = 0.0;
                a.t1 = 1.0;
                a.closed = false;
                a.correction = a.turning = a.length = 0.0;
                a.reference.clear();
                a.arc.clear();
                a.rates.clear();
                return;
            }
            if (a.path)
            {
                a.t0 = a.path->Start();
                a.t1 = a.path->End();
            }
            const int n = kFrameSamples;
            std::vector<Vector3d> xs(n + 1), ts(n + 1);
            DBL extent = 1.0;
            a.arc.assign(n + 1, 0.0);
            a.rates.assign(n + 1, 0.0);
            for (int k = 0; k <= n; ++k)
            {
                Vector3d d1, d2;
                AxisPoint(a, a.t0 + (a.t1 - a.t0) * k / n, xs[k], d1, d2);
                ts[k] = d1.normalized();
                a.rates[k] = std::max(d1.length(), 1.0e-12);
                extent = std::max(extent, xs[k].length());
            }
            const DBL h = (a.t1 - a.t0) / n;
            for (int k = 1; k <= n; ++k)
            {
                Vector3d p, d1, d2;
                AxisPoint(a, a.t0 + h * (k - 0.5), p, d1, d2);
                a.arc[k] = a.arc[k - 1] + h / 6.0 * (a.rates[k - 1] + 4.0 * d1.length() + a.rates[k]);
            }
            a.length = a.arc[n];
            const Vector3d up(0.0, 0.0, 1.0);
            Vector3d r = up - ts[0] * dot(up, ts[0]);
            if (r.length() < 1.0e-6)
                r = Vector3d(1.0, 0.0, 0.0) - ts[0] * ts[0][X];
            a.reference.assign(n + 1, r.normalized());
            for (int i = 0; i < n; ++i)
            {
                const Vector3d v1 = xs[i + 1] - xs[i];
                const DBL c1 = dot(v1, v1);
                const Vector3d rl = c1 > 0.0 ? a.reference[i] - v1 * (2.0 / c1 * dot(v1, a.reference[i])) : a.reference[i];
                const Vector3d tl = c1 > 0.0 ? ts[i] - v1 * (2.0 / c1 * dot(v1, ts[i])) : ts[i];
                const Vector3d v2 = ts[i + 1] - tl;
                const DBL c2 = dot(v2, v2);
                a.reference[i + 1] = c2 > 1.0e-30 ? rl - v2 * (2.0 / c2 * dot(v2, rl)) : rl;
            }
            a.closed = (xs[0] - xs[n]).length() < 1.0e-9 * extent;
            const Vector3d& r0 = a.reference[0];
            const Vector3d& rn = a.reference[n];
            a.correction = a.closed ? -atan2(dot(cross(r0, rn), ts[0]), dot(r0, rn)) : 0.0;
            a.turning = 0.0;
            for (int k = 0; k <= n; ++k)
            {
                AxisState f;
                AxisFrame(a, a.t0 + (a.t1 - a.t0) * k / n, f);
                a.turning = std::max(a.turning, f.omega.length());
            }
            a.turning = 1.5 * a.turning + 1.0e-9;
        }

        /// How far a target path parts from the axis it follows, per unit station: the velocity and the spin mismatch, both zero on the same axis.
        void MatchTarget(const SkeinAxis& axis, SkeinAxis& target)
        {
            target.drift = target.twist = 0.0;
            if (axis.line)
                return;
            const int n = 256;
            for (int k = 0; k <= n; ++k)
            {
                const DBL t = axis.t0 + (axis.t1 - axis.t0) * k / n;
                DBL speed, step;
                const DBL tau = target.Parameter(axis.Distance(t, speed), step);
                AxisState f, g;
                AxisFrame(axis, t, f);
                AxisFrame(target, tau, g);
                target.drift = std::max(target.drift, speed * (g.P1.normalized() - f.P1.normalized()).length());
                target.twist = std::max(target.twist, (g.omega * (speed * step) - f.omega).length());
            }
            target.drift *= 1.5;
            target.twist *= 1.5;
        }

        /// Prepares a curl at step `index` of a scope: places a sample_path pivot, takes its radius, and tabulates the travelling section by arc
        /// length over the extent of the sheet past the foot; returns an error message, empty on success.
        std::string BuildRoll(SkeinStep& s, const Scope& sc, int index)
        {
            auto roll = std::make_shared<SkeinRoll>(*s.roll);
            if (roll->source)
            {
                const std::shared_ptr<const SkeinPath> placed = PlacedPath(*roll->source, sc, index);
                Vector3d origin, direction;
                if (!placed->Straight(origin, direction))
                    return "skein curl: the pivot must be a straight line, and this sample_path is not once placed.";
                s.Hinge(direction);
                DBL lo = BOUND_HUGE, hi = -BOUND_HUGE, reach = 1.0;
                for (size_t k = 0; k < placed->raw.size(); ++k)
                {
                    Jet J;
                    Prefix(sc, index, WrapU(roll->source->raw[k].point[X]), WrapV(roll->source->raw[k].point[Y]), J, nullptr, false);
                    const DBL h = dot(placed->raw[k].point - J.p, s.axis);
                    lo = std::min(lo, h);
                    hi = std::max(hi, h);
                    reach = std::max(reach, J.p.length());
                }
                if (hi - lo > 1.0e-9 * reach)
                    return Format("skein curl: the pivot runs from %g to %g off the sheet; its height is the roll's radius, the same all along it.", lo, hi);
                if (fabs(lo) < 1.0e-9 * reach)
                    return "skein curl: a pivot on the sheet has no roll; lift it off the sheet by the roll's radius.";
                s.origin = origin - s.axis * lo;
                s.value[SkeinStep::kRadius].constant = lo;
            }
            const DBL R0 = s.value[SkeinStep::kRadius].constant;
            roll->foot = s.origin;
            roll->T = s.side;
            roll->A = s.along;
            roll->N = s.axis * (R0 < 0.0 ? -1.0 : 1.0);
            roll->radius = fabs(R0);
            Vector3d d1, d2;
            roll->travel->Evaluate(0.0, roll->start, d1, d2);
            DBL ranges[2 * kMaxFunctions];
            BoxState box;
            Ranges(0.0, 1.0, 0.0, 1.0, 5, ranges);
            BoxPrefix(sc, index, 0.0, 1.0, 0.0, 1.0, ranges, box);
            Interval q[3];
            for (int i = 0; i < 3; ++i)
                q[i] = box.b[i] - Interval(roll->foot[i]);
            const DBL extent = Project(q, roll->A).hi;
            if (!(extent < 1.0e9))
                return "skein curl: the sheet entering a curl has no bounded extent past the pivot, so its roll cannot be tabulated.";
            // why: march at a fixed step to the extent, then tabulate the speed by Simpson at no fewer than kFrameSamples (doc/skein.md, curl)
            Vector3d c[3];
            auto speed = [&](DBL phi) { roll->Section(phi, c); return c[1].length(); };
            const DBL coarse = kTau / kRollCells;
            DBL phi = 0.0, length = 0.0;
            int cells = 0;
            do
            {
                if (++cells > kMaxRollTurns * kRollCells)
                    return Format("skein curl: the sheet would roll more than %g turns.", kMaxRollTurns);
                length += coarse / 6.0 * (speed(phi) + 4.0 * speed(phi + 0.5 * coarse) + speed(phi + coarse));
                phi += coarse;
            } while (length < extent);
            const int n = std::max(kFrameSamples, cells);
            const DBL h = phi / n;
            SkeinAxis& t = roll->table;
            t.t0 = 0.0;
            t.t1 = phi;
            t.arc.assign(n + 1, 0.0);
            t.rates.assign(n + 1, 0.0);
            roll->section.assign(n + 1, Vector3d(0.0));
            roll->normals.assign(n + 1, Vector2d(0.0, 1.0));
            roll->step = h;
            DBL fastest = 0.0, turning = 0.0;
            for (int k = 0; k <= n; ++k)
            {
                const DBL r = roll->Section(h * k, c), plane = sqrt(c[1][Y] * c[1][Y] + c[1][Z] * c[1][Z]);
                if (!(r > 0.0))
                    return Format("skein curl: the travel brings the pivot down to the sheet %g turns into the roll; the radius must stay above zero.", h * k / kTau);
                if (!(plane > 1.0e-9 * roll->radius) || ((k == 0) && !(c[1][Y] > 0.0)))
                    return Format("skein curl: %g turns into the roll the travel along the sheet outruns the roll, so the sheet doubles back.", h * k / kTau);
                roll->section[k] = c[0];
                roll->normals[k] = Vector2d(-c[1][Z] / plane, c[1][Y] / plane);
                t.rates[k] = std::max(c[1].length(), 1.0e-12);
                turning = std::max(turning, fabs(c[1][Y] * c[2][Z] - c[1][Z] * c[2][Y]) / (plane * plane));
                fastest = std::max(fastest, t.rates[k]);
                if (k > 0)
                {
                    const DBL middle = speed(h * (k - 0.5));
                    fastest = std::max(fastest, middle);
                    t.arc[k] = t.arc[k - 1] + h / 6.0 * (t.rates[k - 1] + 4.0 * middle + t.rates[k]);
                }
            }
            t.length = t.arc[n];
            roll->Section(phi, c);
            roll->tail = c[1].normalized();
            roll->speedBound = 1.5 * fastest + 1.0e-12;
            roll->turnBound = 1.5 * turning + 1.0e-12;
            roll->ready = true;
            s.roll = roll;
            return std::string();
        }

        /// The largest thickness an envelope with no edge leaves where its two faces must meet (both edges when its axis wraps).
        DBL EnvelopeGap(const Scope& s, int index)
        {
            const SkeinStep& step = s.steps[index];
            const Stage st{ &s, index };
            DBL gap = 0.0;
            for (int e = (step.wrapOut & (step.alongV ? 2 : 1)) ? 0 : 1; e <= 1; ++e)
                for (int i = 0; i <= 8; ++i)
                {
                    const DBL U = step.alongV ? i / 8.0 : DBL(e), V = step.alongV ? DBL(e) : i / 8.0;
                    Jet K;
                    Prefix(s, index, U, V, K, nullptr, true);
                    const Inputs in{ U, V, &K, NormalOf(st, U, V, K), st.Flags() };
                    DBL t[3];
                    Value(step.value[SkeinStep::kThickness], in, t, false);
                    gap = std::max(gap, fabs(t[0]));
                }
            return gap;
        }

    private:

        struct Inputs { DBL u, v; const Jet *J; Vector3d n; unsigned char wrap; };

        /// Applies steps [from, to) of a scope to J at (u, v).
        void Apply(const Scope& s, int from, int to, DBL u, DBL v, Jet& J, DBL *values, bool slopes)
        {
            for (int index = from; index < to; ++index)
            {
                const SkeinStep& step = s.steps[index];
                const Stage st{ &s, index };
                const bool need = slopes || (s.parent != nullptr) || (index < data.lastNormal);
                if (step.kind == SkeinStep::kAffine)
                {
                    MTransPoint(J.p, J.p, &step.affine);
                    MTransDirection(J.pu, J.pu, &step.affine);
                    MTransDirection(J.pv, J.pv, &step.affine);
                    continue;
                }
                if (step.kind == SkeinStep::kDisplace)
                {
                    Displace(step, st, u, v, J, values, need, 1.0);
                    continue;
                }
                if (step.kind == SkeinStep::kEnvelope)
                {
                    Envelope(step, st, u, v, J, values, need);
                    continue;
                }
                if (step.kind == SkeinStep::kBlend)
                {
                    Blend(step, st, u, v, J, values);
                    continue;
                }
                DBL w[3][3];
                StepValues(step, st, u, v, J, values, need, w);
                switch (step.kind)
                {
                    case SkeinStep::kFold:
                        Fold(step, w, J);
                        break;
                    case SkeinStep::kTranslate:
                        for (int k = 0; k < 3; ++k)
                        {
                            J.p[k] += w[k][0];
                            J.pu[k] += w[k][1];
                            J.pv[k] += w[k][2];
                        }
                        break;
                    case SkeinStep::kScale:
                        for (int k = 0; k < 3; ++k)
                        {
                            const DBL q = J.p[k] - step.origin[k];
                            J.pu[k] = w[k][0] * J.pu[k] + w[k][1] * q;
                            J.pv[k] = w[k][0] * J.pv[k] + w[k][2] * q;
                            J.p[k] = step.origin[k] + w[k][0] * q;
                        }
                        break;
                    case SkeinStep::kBend:
                        Bend(step, w, J);
                        break;
                    case SkeinStep::kAxialStep:
                        Axial(step, w, J);
                        break;
                    case SkeinStep::kCurl:
                        if (step.roll->ready)
                            Curl(*step.roll, J);
                        break;
                    case SkeinStep::kSample:
                    {
                        Jet K;
                        Prefix(s, index, w[SkeinStep::kAtU][0], w[SkeinStep::kAtV][0], K, values, need);
                        J.p = K.p;
                        J.pu = K.pu * w[SkeinStep::kAtU][1] + K.pv * w[SkeinStep::kAtV][1];
                        J.pv = K.pu * w[SkeinStep::kAtU][2] + K.pv * w[SkeinStep::kAtV][2];
                        break;
                    }
                    default:
                    {
                        const DBL a = w[0][0] * kRadiansPerDegree, c = cos(a), sn = sin(a);
                        const Vector3d q = Rotated(J.p - step.origin, step.axis, c, sn), turn = cross(step.axis, q) * kRadiansPerDegree;
                        J.pu = Rotated(J.pu, step.axis, c, sn) + turn * w[0][1];
                        J.pv = Rotated(J.pv, step.axis, c, sn) + turn * w[0][2];
                        J.p = step.origin + q;
                        break;
                    }
                }
            }
        }

        DBL Axis(const SkeinAxis& a, int k, DBL t)
        {
            a.functions[k]->InitArguments(context);
            a.functions[k]->PushArgument(context, t);
            return a.functions[k]->Execute(context);
        }

        DBL Call(const SkeinValue& f, const DBL *args, int n)
        {
            f.function->InitArguments(context);
            for (int i = 0; i < n; ++i)
                f.function->PushArgument(context, args[i]);
            return f.function->Execute(context);
        }

        /// A value with its u and v derivatives; a function's slopes are central differences in each argument.
        void Value(const SkeinValue& f, const Inputs& in, DBL out[3], bool slopes, DBL *values = nullptr)
        {
            out[1] = out[2] = 0.0;
            switch (f.kind)
            {
                case SkeinValue::kConstant:
                    out[0] = f.constant;
                    return;
                case SkeinValue::kSum:
                {
                    const SkeinSum& s = *f.sum;
                    Value(s.entries[0], in, out, slopes, values);
                    for (size_t i = 1; i < s.entries.size(); ++i)
                    {
                        DBL e[3];
                        Value(s.entries[i], in, e, slopes, values);
                        if (s.op == SkeinSum::kAdd)
                            for (int k = 0; k < 3; ++k)
                                out[k] += e[k];
                        else if (s.op == SkeinSum::kMultiply)
                        {
                            out[1] = out[1] * e[0] + out[0] * e[1];
                            out[2] = out[2] * e[0] + out[0] * e[2];
                            out[0] *= e[0];
                        }
                        else if ((s.op == SkeinSum::kMin) ? (e[0] < out[0]) : (e[0] > out[0]))
                            std::copy(e, e + 3, out);
                    }
                    return;
                }
                case SkeinValue::kPath:
                {
                    Vector3d p, d1, d2;
                    f.path->Evaluate(in.v, p, d1, d2);
                    out[0] = p[X];
                    out[2] = d1[X];
                    return;
                }
                case SkeinValue::kMap:
                {
                    Dual d[SkeinValue::kInputs] = { { in.u, 1.0, 0.0 }, { in.v, 0.0, 1.0 } };
                    for (int k = 0; k < 3; ++k)
                    {
                        d[SkeinValue::kX + k] = Dual{ in.J->p[k], in.J->pu[k], in.J->pv[k] };
                        d[SkeinValue::kNX + k] = Dual{ in.n[k], 0.0, 0.0 };
                    }
                    const Dual r = RunMap(*f.map, d);
                    out[0] = r.x;
                    out[1] = r.u;
                    out[2] = r.v;
                    return;
                }
                default:
                    break;
            }
            DBL args[kMaxArguments], du[kMaxArguments], dv[kMaxArguments];
            const int n = int(f.inputs.size());
            for (int i = 0; i < n; ++i)
            {
                const int input = f.inputs[i];
                if (input <= SkeinValue::kV)
                {
                    args[i] = input == SkeinValue::kU ? in.u : in.v;
                    du[i] = input == SkeinValue::kU ? 1.0 : 0.0;
                    dv[i] = 1.0 - du[i];
                }
                else if (input <= SkeinValue::kZ)
                {
                    const int axis = input - SkeinValue::kX;
                    args[i] = in.J->p[axis];
                    du[i] = in.J->pu[axis];
                    dv[i] = in.J->pv[axis];
                }
                else
                {
                    args[i] = in.n[input - SkeinValue::kNX];
                    du[i] = dv[i] = 0.0;
                }
            }
            out[0] = Call(f, args, n);
            if ((values != nullptr) && (f.slot >= 0))
            {
                values[2 * f.slot] = std::min(values[2 * f.slot], out[0]);
                values[2 * f.slot + 1] = std::max(values[2 * f.slot + 1], out[0]);
            }
            for (int i = 0; slopes && (i < n); ++i)
            {
                if ((du[i] == 0.0) && (dv[i] == 0.0))
                    continue;
                const DBL keep = args[i], h = 1.0e-7 * std::max(1.0, fabs(keep));
                const DBL a = keep + h, b = keep - h;
                const bool wraps = (f.inputs[i] == SkeinValue::kU) ? (in.wrap & 1) != 0 : (f.inputs[i] == SkeinValue::kV) && ((in.wrap & 2) != 0);
                args[i] = wraps ? a - floor(a) : a;
                const DBL fa = Call(f, args, n);
                args[i] = wraps ? b - floor(b) : b;
                const DBL fb = Call(f, args, n);
                args[i] = keep;
                const DBL slope = (fa - fb) / (a - b);
                out[1] += slope * du[i];
                out[2] += slope * dv[i];
            }
        }

        /// The unit normal of the surface entering a stage, nudged inward in v where that surface pinches.
        Vector3d NormalOf(const Stage& st, DBL u, DBL v, const Jet& J)
        {
            Vector3d n = cross(J.pu, J.pv);
            if (n.length() <= 1.0e-12 * (J.pu.lengthSqr() + J.pv.lengthSqr()) || (n.length() == 0.0))
            {
                Jet K;
                DBL w = v + (v < 0.5 ? 1.0e-6 : -1.0e-6);
                if (st.Flags() & 2)
                    w -= floor(w);
                Prefix(*st.scope, st.index, u, w, K, nullptr, true);
                n = cross(K.pu, K.pv);
            }
            const DBL len = n.length();
            return len > 0.0 ? n / len : Vector3d(0.0, 0.0, 1.0);
        }

        /// Second-order difference weights along u or v at a stage: central inside the domain, one-sided at an open edge.
        struct Stencil { Jet J[2]; Inputs in[2]; DBL w[3], h; };

        void Differences(const Stage& st, DBL u, DBL v, bool alongU, Stencil& s)
        {
            const DBL a = alongU ? u : v, h = neighbour;
            const bool wraps = (st.Flags() & (alongU ? 1 : 2)) != 0;
            DBL offset[2] = { h, -h };
            s.w[0] = 0.0;
            s.w[1] = 0.5;
            s.w[2] = -0.5;
            if (!wraps && ((a - h < 0.0) || (a + h > 1.0)))
            {
                const DBL sign = a - h < 0.0 ? 1.0 : -1.0;
                offset[0] = sign * h;
                offset[1] = 2.0 * sign * h;
                s.w[0] = -1.5 * sign;
                s.w[1] = 2.0 * sign;
                s.w[2] = -0.5 * sign;
            }
            s.h = h;
            for (int k = 0; k < 2; ++k)
            {
                DBL b = a + offset[k];
                if (wraps)
                    b -= floor(b);
                Inputs& in = s.in[k];
                in.u = alongU ? b : u;
                in.v = alongU ? v : b;
                in.wrap = st.Flags();
                Prefix(*st.scope, st.index, in.u, in.v, s.J[k], nullptr, true);
                in.J = &s.J[k];
                in.n = NormalOf(st, in.u, in.v, s.J[k]);
            }
        }

        void StepValues(const SkeinStep& s, const Stage& st, DBL u, DBL v, const Jet& J, DBL *values, bool slopes, DBL out[3][3])
        {
            Inputs in{ u, v, &J, Vector3d(0.0), st.Flags() };
            Stencil near[2];
            bool haveNear = false;
            if (s.normal)
                in.n = NormalOf(st, u, v, J);
            for (int k = 0; k < 3; ++k)
            {
                const SkeinValue& f = s.value[k];
                if (slopes && f.ReadsNormal())
                {
                    if (!haveNear)
                    {
                        Differences(st, u, v, true, near[0]);
                        Differences(st, u, v, false, near[1]);
                        haveNear = true;
                    }
                    Value(f, in, out[k], false, values);
                    for (int d = 0; d < 2; ++d)
                    {
                        DBL a[3], b[3];
                        Value(f, near[d].in[0], a, false);
                        Value(f, near[d].in[1], b, false);
                        out[k][1 + d] = (near[d].w[0] * out[k][0] + near[d].w[1] * a[0] + near[d].w[2] * b[0]) / near[d].h;
                    }
                }
                else
                    Value(f, in, out[k], slopes, values);
            }
        }

        /// Moves the point along the incoming normal by `scale` times the step's value; its slopes are second-order differences of the moved surface.
        void Displace(const SkeinStep& s, const Stage& st, DBL u, DBL v, Jet& J, DBL *values, bool slopes, DBL scale)
        {
            const SkeinValue& f = s.value[SkeinStep::kAmount];
            const Inputs in{ u, v, &J, NormalOf(st, u, v, J), st.Flags() };
            DBL a[3];
            Value(f, in, a, false, values);
            const Vector3d p = J.p + in.n * (scale * a[0]);
            if (!slopes)
            {
                J.p = p;
                return;
            }
            Vector3d slope[2];
            for (int d = 0; d < 2; ++d)
            {
                Stencil near;
                Differences(st, u, v, d == 0, near);
                slope[d] = p * near.w[0];
                for (int k = 0; k < 2; ++k)
                {
                    DBL b[3];
                    Value(f, near.in[k], b, false);
                    slope[d] += (near.J[k].p + near.in[k].n * (scale * b[0])) * near.w[1 + k];
                }
                slope[d] /= near.h;
            }
            J.p = p;
            J.pu = slope[0];
            J.pv = slope[1];
        }

        /// Places a parameter of the doubled axis: front face, far rim (edge 1), back face run backwards, near rim (edge 0) when the axis wraps.
        EnvelopePiece Locate(const SkeinStep& s, DBL t) const
        {
            const bool loop = (s.wrapOut & (s.alongV ? 2 : 1)) != 0;
            const DBL rim = s.edge != SkeinStep::kNoEdge ? kRimShare : 0.0;
            if (loop)
                t -= floor(t);
            const DBL f0 = loop ? 0.5 * rim : 0.0, f1 = 0.5 - 0.5 * rim, b0 = 0.5 + 0.5 * rim, b1 = loop ? 1.0 - 0.5 * rim : 1.0;
            if (loop && (t < f0))
                return EnvelopePiece{ EnvelopePiece::kRim, 0.0, M_PI / rim, M_PI * (t + 0.5 * rim) / rim };
            if (t <= f1)
                return EnvelopePiece{ EnvelopePiece::kFront, (t - f0) / (f1 - f0), 1.0 / (f1 - f0), 0.0 };
            if (t < b0)
                return EnvelopePiece{ EnvelopePiece::kRim, 1.0, M_PI / rim, M_PI * (t - f1) / rim };
            if (!loop || (t <= b1))
                return EnvelopePiece{ EnvelopePiece::kBack, (b1 - t) / (b1 - b0), -1.0 / (b1 - b0), 0.0 };
            return EnvelopePiece{ EnvelopePiece::kRim, 0.0, M_PI / rim, M_PI * (t - b1) / rim };
        }

        /// A point of an envelope's rim at `other` along the edge, and its derivative along the doubled axis.
        /// The unit direction leaving the sheet across an edge, in its tangent plane and square to the edge.
        static Vector3d EdgeDirection(const SkeinStep& s, const Jet& K, const Vector3d& n, bool far)
        {
            Vector3d d = cross(s.alongV ? K.pu : K.pv, n);
            const DBL len = d.length();
            d = len > 0.0 ? d / len : Vector3d(0.0);
            return dot(d, s.alongV ? K.pv : K.pu) * (far ? 1.0 : -1.0) < 0.0 ? -d : d;
        }

        Vector3d RimPoint(const SkeinStep& s, const Stage& st, const EnvelopePiece& p, DBL other, DBL *values, Vector3d& along)
        {
            const DBL U = s.alongV ? other : p.a, V = s.alongV ? p.a : other;
            Jet K;
            Prefix(*st.scope, st.index, U, V, K, values, true);
            const Vector3d n = NormalOf(st, U, V, K);
            const Inputs in{ U, V, &K, n, st.Flags() };
            DBL t[3];
            Value(s.value[SkeinStep::kThickness], in, t, false, values);
            const DBL h = 0.5 * t[0];
            const Vector3d d = EdgeDirection(s, K, n, p.a > 0.5);
            const bool far = p.a > 0.5;
            DBL c, sn, dc, ds;
            if (s.edge == SkeinStep::kRoundEdge)
            {
                c = far ? cos(p.turn) : -cos(p.turn);
                sn = sin(p.turn);
                dc = (far ? -sin(p.turn) : sin(p.turn)) * p.rate;
                ds = cos(p.turn) * p.rate;
            }
            else
            {
                const DBL k = p.turn / M_PI;
                c = far ? 1.0 - 2.0 * k : 2.0 * k - 1.0;
                sn = ds = 0.0;
                dc = (far ? -2.0 : 2.0) * p.rate / M_PI;
            }
            along = (n * dc + d * ds) * h;
            return K.p + (n * c + d * sn) * h;
        }

        /// Doubles the surface along one axis: there with the faces moved by +thickness/2 along the normal, back by -thickness/2, rims between.
        void Envelope(const SkeinStep& s, const Stage& st, DBL u, DBL v, Jet& J, DBL *values, bool slopes)
        {
            const EnvelopePiece p = Locate(s, s.alongV ? v : u);
            const DBL other = s.alongV ? u : v;
            if (p.kind != EnvelopePiece::kRim)
            {
                const DBL U = s.alongV ? other : p.a, V = s.alongV ? p.a : other;
                Jet K;
                Prefix(*st.scope, st.index, U, V, K, values, true);
                Displace(s, st, U, V, K, values, slopes, p.kind == EnvelopePiece::kFront ? 0.5 : -0.5);
                J.p = K.p;
                J.pu = s.alongV ? K.pu : K.pu * p.rate;
                J.pv = s.alongV ? K.pv * p.rate : K.pv;
                return;
            }
            Vector3d along;
            J.p = RimPoint(s, st, p, other, values, along);
            if (!slopes)
                return;
            const bool wraps = (st.Flags() & (s.alongV ? 1 : 2)) != 0;
            DBL a = other + neighbour, b = other - neighbour;
            if (!wraps)
            {
                a = std::min(1.0, a);
                b = std::max(0.0, b);
            }
            Vector3d unused;
            const Vector3d pa = RimPoint(s, st, p, wraps ? a - floor(a) : a, nullptr, unused);
            const Vector3d pb = RimPoint(s, st, p, wraps ? b - floor(b) : b, nullptr, unused);
            const Vector3d across = (pa - pb) / (a - b);
            J.pu = s.alongV ? across : along;
            J.pv = s.alongV ? along : across;
        }

        /// Blends the two entries of an expression_map around its driver, as BlendMap::Search does; slopes include the weight's change.
        void Blend(const SkeinStep& s, const Stage& st, DBL u, DBL v, Jet& J, DBL *values)
        {
            const std::vector<SkeinBlend::Entry>& e = s.blend->entries;
            DBL w[3][3];
            StepValues(s, st, u, v, J, values, true, w);
            const DBL x = w[SkeinStep::kDriver][0];
            const int last = int(e.size()) - 1;
            int i = last, j = last;
            if (x < e[last].value)
            {
                j = 0;
                while (x > e[j].value)
                    ++j;
                i = ((x == e[j].value) || (j == 0)) ? j : j - 1;
            }
            const Scope a{ e[i].steps, st.scope, st.index, s.wrapOut }, b{ e[j].steps, st.scope, st.index, s.wrapOut };
            if (values != nullptr)
                for (int k = 0; k <= last; ++k)
                    if ((k != i) && ((k != j) || (e[i].item == e[j].item)))
                    {
                        Jet K = J;
                        const Scope other{ e[k].steps, st.scope, st.index, s.wrapOut };
                        Apply(other, 0, int(e[k].steps.size()), u, v, K, values, false);
                    }
            Jet A = J;
            Apply(a, 0, int(a.steps.size()), u, v, A, values, true);
            if (e[i].item == e[j].item)
            {
                J = A;
                return;
            }
            Jet B = J;
            Apply(b, 0, int(b.steps.size()), u, v, B, values, true);
            const DBL span = e[j].value - e[i].value, k = (x - e[i].value) / span;
            const Vector3d gap = B.p - A.p;
            J.p = A.p + gap * k;
            J.pu = A.pu + (B.pu - A.pu) * k + gap * (w[SkeinStep::kDriver][1] / span);
            J.pv = A.pv + (B.pv - A.pv) * k + gap * (w[SkeinStep::kDriver][2] / span);
        }

        void Fold(const SkeinStep& s, const DBL w[3][3], Jet& J)
        {
            const DBL *r = w[SkeinStep::kRadius], *a = w[SkeinStep::kArc];
            const Vector3d* in[3] = { &J.p, &J.pu, &J.pv };
            DBL t[3], rho[3], g[3], m[3];
            for (int i = 0; i < 3; ++i)
            {
                t[i] = dot(*in[i], s.axis);
                rho[i] = r[i] + dot(*in[i], s.offset);
                m[i] = dot(*in[i], s.along);
            }
            // the angle is arc times the wrapped coordinate, so a varying arc adds its own slope
            for (int i = 0; i < 3; ++i)
                g[i] = kRadiansPerDegree * (a[0] * m[i] + (i == 0 ? 0.0 : a[i] * m[0]));
            const DBL c = cos(g[0]), sn = sin(g[0]);
            if (!s.curve)
            {
                J.p = s.axis * t[0] + s.along * (rho[0] * c) + s.side * (rho[0] * sn);
                J.pu = s.axis * t[1] + s.along * (rho[1] * c - rho[0] * sn * g[1]) + s.side * (rho[1] * sn + rho[0] * c * g[1]);
                J.pv = s.axis * t[2] + s.along * (rho[2] * c - rho[0] * sn * g[2]) + s.side * (rho[2] * sn + rho[0] * c * g[2]);
                return;
            }
            AxisState f;
            AxisFrame(*s.curve, t[0], f);
            const Vector3d radial = f.e1 * (rho[0] * c) + f.e2 * (rho[0] * sn);
            const Vector3d along = f.P1 + cross(f.omega, radial);
            J.p = f.P + radial;
            J.pu = along * t[1] + f.e1 * (rho[1] * c - rho[0] * sn * g[1]) + f.e2 * (rho[1] * sn + rho[0] * c * g[1]);
            J.pv = along * t[2] + f.e1 * (rho[2] * c - rho[0] * sn * g[2]) + f.e2 * (rho[2] * sn + rho[0] * c * g[2]);
        }

        /// The axis parameter a point belongs to; `rate` is its slope, zero past the ends of an open path axis, which has no tangent there.
        DBL Station(const SkeinStep& s, const Vector3d& p, DBL& rate) const
        {
            const DBL t = dot(p, s.axis), on = OnAxis(*s.curve, t);
            rate = on == t ? 1.0 : 0.0;
            return on;
        }

        /// Turns the point about the axis by `angle`, scales its distance out from it and shifts it along it:
        /// about the world axis, or about the curve's frame at the point's station.
        void Axial(const SkeinStep& s, const DBL w[3][3], Jet& J)
        {
            const DBL *A = w[SkeinStep::kAngle], *L = w[SkeinStep::kShift], *E = w[SkeinStep::kRadial];
            const DBL a = A[0] * kRadiansPerDegree, c = cos(a), sn = sin(a);
            if (s.target)
            {
                Along(s, A, L, E, c, sn, J);
                return;
            }
            if (!s.curve)
            {
                const Vector3d T = s.axis;
                const DBL t = dot(J.p, T);
                const Vector3d flat = Rotated(J.p, T, c, sn) - T * t, turn = cross(T, flat) * kRadiansPerDegree;
                for (int i = 1; i <= 2; ++i)
                {
                    Vector3d& slope = i == 1 ? J.pu : J.pv;
                    const DBL ds = dot(slope, T);
                    slope = (Rotated(slope, T, c, sn) - T * ds) * E[0] + flat * E[i] +
                            turn * (A[i] * E[0]) + T * (ds + L[i]);
                }
                J.p = flat * E[0] + T * (t + L[0]);
                return;
            }
            DBL rate;
            AxisState f;
            AxisFrame(*s.curve, Station(s, J.p, rate), f);
            const Vector3d axis = f.P1.normalized(), r = J.p - f.P;
            const DBL t = dot(r, axis);
            const Vector3d flat = Rotated(r, axis, c, sn) - axis * t, turn = cross(axis, flat) * kRadiansPerDegree;
            const Vector3d moved = flat * E[0] + axis * (t + L[0]);
            // why: a step along the axis carries the frame with it, so the station contributes omega x (offset) (doc/skein.md, bend)
            const Vector3d station = (f.P1 + cross(f.omega, moved)) * rate, carried = (f.P1 + cross(f.omega, r)) * rate;
            const Vector3d* in[2] = { &J.pu, &J.pv };
            Vector3d out[2];
            for (int i = 0; i < 2; ++i)
            {
                const DBL step = dot(*in[i], s.axis);
                const Vector3d slope = *in[i] - carried * step;
                const DBL ds = dot(slope, axis);
                out[i] = station * step + (Rotated(slope, axis, c, sn) - axis * ds) * E[0] + flat * E[1 + i] +
                         turn * (A[1 + i] * E[0]) + axis * (ds + L[1 + i]);
            }
            J.p = f.P + moved;
            J.pu = out[0];
            J.pv = out[1];
        }

        /// The station on the target a point's station on the axis corresponds to: the same arc length, so bending keeps the material's
        /// length. `over` is how far past an open target's end the material reaches, where it carries on along the tangent.
        DBL Target(const SkeinStep& s, DBL t, DBL& rate, DBL& over, DBL& grow) const
        {
            DBL speed, step;
            const DBL d = s.curve->Distance(t, speed), length = s.target->length;
            over = s.target->closed || (length <= 0.0) ? 0.0 : d - std::min(length, std::max(0.0, d));
            const DBL tau = s.target->Parameter(d - over, step);
            rate = over == 0.0 ? speed * step : 0.0;
            grow = over == 0.0 ? 0.0 : speed;
            return tau;
        }

        /// Puts the offset about the axis's frame down in the target's frame at the matching arc length, at the target's point: the bend of a path.
        void Along(const SkeinStep& s, const DBL *A, const DBL *L, const DBL *E, DBL c, DBL sn, Jet& J)
        {
            DBL rate, chain, over, grow;
            AxisState f, g;
            const DBL t = Station(s, J.p, rate);
            AxisFrame(*s.curve, t, f);
            AxisFrame(*s.target, Target(s, t, chain, over, grow), g);
            const Vector3d axis = f.P1.normalized(), r = J.p - f.P, tangent = g.P1.normalized();
            const DBL r0 = dot(r, axis), r1 = dot(r, f.e1), r2 = dot(r, f.e2);
            const DBL q1 = r1 * c - r2 * sn, q2 = r1 * sn + r2 * c;
            const Vector3d placed = tangent * (r0 + L[0] + over) + g.e1 * (E[0] * q1) + g.e2 * (E[0] * q2);
            const Vector3d carried = (f.P1 + cross(f.omega, r)) * rate, swing = (g.P1 + cross(g.omega, placed)) * (chain * rate);
            const Vector3d* in[2] = { &J.pu, &J.pv };
            Vector3d out[2];
            for (int i = 0; i < 2; ++i)
            {
                const DBL step = dot(*in[i], s.axis), spin = A[1 + i] * kRadiansPerDegree;
                const Vector3d slope = *in[i] - carried * step;
                const DBL d0 = dot(slope, axis), d1 = dot(slope, f.e1), d2 = dot(slope, f.e2);
                const DBL p1 = E[0] * (d1 * c - d2 * sn - q2 * spin) + E[1 + i] * q1;
                const DBL p2 = E[0] * (d1 * sn + d2 * c + q1 * spin) + E[1 + i] * q2;
                out[i] = swing * step + tangent * (d0 + L[1 + i] + grow * rate * step) + g.e1 * p1 + g.e2 * p2;
            }
            J.p = g.P + placed;
            J.pu = out[0];
            J.pv = out[1];
        }

        /// Rolls the points past the hinge onto the turning circle, keeping arc length; past the angle limit the sheet runs straight.
        /// The hinge is a straight line through `origin`, or the curve's frame at the point's station; a negative radius curls the other way.
        void Bend(const SkeinStep& s, const DBL w[3][3], Jet& J)
        {
            const DBL *R = w[SkeinStep::kRadius], *L = w[SkeinStep::kLimit];
            const DBL sign = R[0] < 0.0 ? -1.0 : 1.0, radius = sign * R[0];
            DBL rate = 0.0;
            AxisState f;
            Vector3d base = s.origin, T = s.side, A = s.along, N = s.axis * sign;
            if (s.curve)
            {
                AxisFrame(*s.curve, Station(s, J.p, rate), f);
                base = f.P;
                T = f.P1.normalized();
                A = f.e2;
                N = f.e1 * sign;
            }
            const Vector3d d = J.p - base;
            const DBL rho = dot(d, A);
            if (rho <= 0.0)
                return;
            const DBL h = dot(d, N), a = dot(d, T), turn = rho / radius, limit = L[0] * kRadiansPerDegree;
            const bool bent = turn < limit;
            const DBL phi = bent ? turn : limit, extra = rho - phi * radius, c = cos(phi), sn = sin(phi);
            const DBL e = (radius - h) * sn + extra * c, n = radius - (radius - h) * c + extra * sn;
            Vector3d station(0.0), carried(0.0);
            if (s.curve)
            {
                // why: a step along the axis carries the frame with it, so the station contributes omega x (offset) (doc/skein.md, bend)
                station = (f.P1 + cross(f.omega, T * a + A * e + N * n)) * rate;
                carried = (f.P1 + cross(f.omega, d)) * rate;
            }
            const Vector3d* in[2] = { &J.pu, &J.pv };
            Vector3d out[2];
            for (int i = 0; i < 2; ++i)
            {
                const DBL step = s.curve ? dot(*in[i], s.axis) : 0.0;
                const Vector3d slope = *in[i] - carried * step;
                const DBL rhoI = dot(slope, A), hI = dot(slope, N), rI = sign * R[1 + i];
                const DBL phiI = bent ? (rhoI * radius - rho * rI) / (radius * radius) : L[1 + i] * kRadiansPerDegree;
                const DBL extraI = rhoI - phiI * radius - phi * rI;
                out[i] = station * step + T * dot(slope, T) +
                         A * ((rI - hI) * sn + (radius - h) * c * phiI + extraI * c - extra * sn * phiI) +
                         N * (rI - (rI - hI) * c + (radius - h) * sn * phiI + extraI * sn + extra * c * phiI);
            }
            J.p = base + T * a + A * e + N * n;
            J.pu = out[0];
            J.pv = out[1];
        }

        /// Rolls the points past the foot onto the travelling section at their own arc length, so the sheet keeps its length; a point's
        /// offset toward the pivot moves it along the section's normal, and past the table the roll runs straight on along its end tangent.
        void Curl(const SkeinRoll& R, Jet& J)
        {
            const Vector3d d = J.p - R.foot;
            const DBL rho = dot(d, R.A);
            if (rho <= 0.0)
                return;
            const DBL a = dot(d, R.T), h = dot(d, R.N), over = std::max(0.0, rho - R.table.length);
            DBL rate;
            Vector3d c[3];
            R.Section(R.table.Parameter(rho - over, rate), c);
            const DBL speed = c[1].length(), plane = sqrt(c[1][Y] * c[1][Y] + c[1][Z] * c[1][Z]);
            const DBL ty = c[1][Y] / plane, tz = c[1][Z] / plane, bend = (c[1][Y] * c[2][Z] - c[1][Z] * c[2][Y]) / (plane * plane);
            const DBL turn = over > 0.0 ? 0.0 : 1.0 / speed;
            const Vector3d tangent = c[1] / speed, normal(0.0, -tz, ty), swing(0.0, -bend * ty, -bend * tz);
            const Vector3d q = c[0] + tangent * over + normal * h;
            const Vector3d* in[2] = { &J.pu, &J.pv };
            Vector3d out[2];
            for (int i = 0; i < 2; ++i)
            {
                const DBL rhoI = dot(*in[i], R.A), phiI = rhoI * turn;
                const Vector3d local = c[1] * phiI + tangent * (over > 0.0 ? rhoI : 0.0) + normal * dot(*in[i], R.N) + swing * (h * phiI);
                out[i] = R.T * (dot(*in[i], R.T) + local[X]) + R.A * local[Y] + R.N * local[Z];
            }
            J.p = R.foot + R.T * (a + q[X]) + R.A * q[Y] + R.N * q[Z];
            J.pu = out[0];
            J.pv = out[1];
        }

        /// The frame at axis parameter `t`: the table's reference vector, kept normal to the live tangent and turned by the loop correction.
        void AxisFrame(const SkeinAxis& a, DBL t, AxisState& f)
        {
            Vector3d d2;
            AxisPoint(a, t, f.P, f.P1, d2);
            if (a.line)
            {
                f.e1 = a.e1;
                f.e2 = a.e2;
                f.omega = Vector3d(0.0);
                return;
            }
            const DBL speed = std::max(f.P1.length(), 1.0e-300);
            const Vector3d T = f.P1 / speed, turn = (d2 - T * dot(T, d2)) / speed;
            DBL tau = (t - a.t0) / (a.t1 - a.t0);
            tau = a.closed ? tau - floor(tau) : std::min(1.0, std::max(0.0, tau));
            const DBL x = tau * kFrameSamples;
            const int i = std::min(kFrameSamples - 1, int(x));
            Vector3d r = a.reference[i] * (i + 1 - x) + a.reference[i + 1] * (x - i);
            r -= T * dot(T, r);
            r.normalize();
            const DBL phi = a.correction * tau;
            f.e1 = r * cos(phi) + cross(T, r) * sin(phi);
            f.e2 = cross(T, f.e1);
            f.omega = cross(T, turn) + T * (a.correction / (a.t1 - a.t0));
        }

        /// A box around a curved axis over a parameter interval: proven for a path, sampled with a margin for functions.
        void AxisBound(const SkeinAxis& a, const Interval& t, Vector3d& lo, Vector3d& hi)
        {
            if (a.line)
            {
                for (int i = 0; i < 3; ++i)
                {
                    const Interval s = a.direction[i] * t;
                    lo[i] = s.lo;
                    hi[i] = s.hi;
                }
                return;
            }
            if (a.path)
            {
                a.path->Bound(t.lo, t.hi, lo, hi);
                return;
            }
            const int n = 8;
            DBL bend = 0.0;
            lo = Vector3d(BOUND_HUGE);
            hi = Vector3d(-BOUND_HUGE);
            for (int k = 0; k <= n; ++k)
            {
                Vector3d p, d1, d2;
                AxisPoint(a, t.lo + (t.hi - t.lo) * k / n, p, d1, d2);
                bend = std::max(bend, d2.length());
                for (int i = 0; i < 3; ++i)
                {
                    lo[i] = std::min(lo[i], p[i]);
                    hi[i] = std::max(hi[i], p[i]);
                }
            }
            const DBL step = (t.hi - t.lo) / n, margin = 0.25 * bend * step * step + 1.0e-6 * data.size;
            lo -= Vector3d(margin);
            hi += Vector3d(margin);
        }

        /// Encloses the unit normal entering a stage over a patch: from interval slopes when they are exact, else sampled with a margin.
        void NormalRange(const Stage& st, DBL u0, DBL u1, DBL v0, DBL v1, bool exact, const Interval *bu, const Interval *bv, Interval *n)
        {
            if (exact)
            {
                Interval N[3], length(0.0);
                for (int i = 0; i < 3; ++i)
                {
                    const int j = (i + 1) % 3, k = (i + 2) % 3;
                    N[i] = bu[j] * bv[k] - bu[k] * bv[j];
                    length = length + Square(N[i]);
                }
                if (length.lo > 0.0)
                {
                    const DBL lo = sqrt(length.lo), hi = sqrt(length.hi);
                    for (int i = 0; i < 3; ++i)
                        n[i] = Interval(std::max(-1.0, N[i].lo / (N[i].lo < 0.0 ? lo : hi)), std::min(1.0, N[i].hi / (N[i].hi > 0.0 ? lo : hi)));
                    return;
                }
            }
            const unsigned char flags = st.Flags();
            Vector3d lo(BOUND_HUGE), hi(-BOUND_HUGE);
            for (int b = 0; b <= 2; ++b)
                for (int a = 0; a <= 2; ++a)
                {
                    Jet J;
                    DBL u = u0 + 0.5 * (u1 - u0) * a, v = v0 + 0.5 * (v1 - v0) * b;
                    if (flags & 1)
                        u -= floor(u);
                    if (flags & 2)
                        v -= floor(v);
                    Prefix(*st.scope, st.index, u, v, J, nullptr, true);
                    const Vector3d m = NormalOf(st, u, v, J);
                    for (int i = 0; i < 3; ++i)
                    {
                        lo[i] = std::min(lo[i], m[i]);
                        hi[i] = std::max(hi[i], m[i]);
                    }
                }
            for (int i = 0; i < 3; ++i)
            {
                const DBL margin = 0.5 * (hi[i] - lo[i]) + 1.0e-3;
                n[i] = Interval(std::max(-1.0, lo[i] - margin), std::min(1.0, hi[i] + margin));
            }
        }

        Interval ValueRange(const SkeinValue& f, const DBL *ranges, const Interval *in) const
        {
            switch (f.kind)
            {
                case SkeinValue::kConstant:
                    return Interval(f.constant);
                case SkeinValue::kFunction:
                {
                    if (data.resamples && (f.inputs.size() <= 3))
                    {
                        Vector3d a, b;
                        bool uv = true;
                        for (size_t k = 0; k < f.inputs.size(); ++k)
                        {
                            uv = uv && (f.inputs[k] <= SkeinValue::kV);
                            a[k] = in[f.inputs[k] == SkeinValue::kU ? SkeinValue::kU : SkeinValue::kV].lo;
                            b[k] = in[f.inputs[k] == SkeinValue::kU ? SkeinValue::kU : SkeinValue::kV].hi;
                        }
                        DBL lo, hi;
                        if (uv && f.function->EvaluateRange(a, b, lo, hi))
                        {
                            const DBL pad = 1.0e-12 * (1.0 + fabs(lo) + fabs(hi));
                            return Interval(lo - pad, hi + pad);
                        }
                    }
                    return Interval(ranges[2 * f.slot], ranges[2 * f.slot + 1]);
                }
                case SkeinValue::kMap:
                    return MapRange(*f.map, in);
                case SkeinValue::kSum:
                {
                    const SkeinSum& s = *f.sum;
                    Interval r = ValueRange(s.entries[0], ranges, in);
                    for (size_t i = 1; i < s.entries.size(); ++i)
                    {
                        const Interval e = ValueRange(s.entries[i], ranges, in);
                        if (s.op == SkeinSum::kAdd)
                            r = r + e;
                        else if (s.op == SkeinSum::kMultiply)
                            r = r * e;
                        else if (s.op == SkeinSum::kMin)
                            r = Interval(std::min(r.lo, e.lo), std::min(r.hi, e.hi));
                        else
                            r = Interval(std::max(r.lo, e.lo), std::max(r.hi, e.hi));
                    }
                    return r;
                }
                default:
                {
                    Vector3d lo, hi;
                    f.path->Bound(in[SkeinValue::kV].lo, in[SkeinValue::kV].hi, lo, hi);
                    return Interval(lo[X], hi[X]);
                }
            }
        }

        void BoxStart(BoxState& st, DBL u0, DBL u1, DBL v0, DBL v1) const
        {
            st.b[X] = Interval(u0, u1);
            st.b[Y] = Interval(v0, v1);
            st.b[Z] = Interval(0.0);
            for (int i = 0; i < 3; ++i)
            {
                st.bu[i] = Interval(i == X ? 1.0 : 0.0);
                st.bv[i] = Interval(i == Y ? 1.0 : 0.0);
            }
            st.slopes = false;
            st.exact = true;
        }

        /// The box of the surface after the first `count` steps of a scope, over a box of (u, v).
        void BoxPrefix(const Scope& s, int count, DBL u0, DBL u1, DBL v0, DBL v1, const DBL *ranges, BoxState& st)
        {
            if (s.parent == nullptr)
                BoxStart(st, u0, u1, v0, v1);
            else
                BoxPrefix(*s.parent, s.at, u0, u1, v0, v1, ranges, st);
            BoxRun(s, 0, count, u0, u1, v0, v1, ranges, st);
        }

        static void Hull(Interval *a, const Interval *b, bool first)
        {
            for (int i = 0; i < 3; ++i)
                a[i] = first ? b[i] : Interval(std::min(a[i].lo, b[i].lo), std::max(a[i].hi, b[i].hi));
        }

        /// Encloses an axial step that follows a target path: the offset's components about the axis are exact for a straight
        /// axis, and about a curved one the whole step is written as a displacement from the identity so that one cancels.
        void AlongBox(const SkeinStep& s, const Interval *b, const Interval& L, const Interval& E,
                      const Interval& c, const Interval& sn, Interval *o)
        {
            const SkeinAxis& axis = *s.curve;
            const SkeinAxis& target = *s.target;
            if (axis.line)
            {
                DBL rlo, rhi, olo, ohi, glo, ghi;
                const Interval t = Project(b, s.axis);
                const Interval tau(Target(s, t.lo, rlo, olo, glo), Target(s, t.hi, rhi, ohi, ghi));
                const Interval shift = L + Interval(olo, ohi);
                const Interval rho1 = Project(b, axis.e1), rho2 = Project(b, axis.e2);
                const Interval m1 = E * (c * rho1 - sn * rho2), m2 = E * (sn * rho1 + c * rho2);
                Vector3d plo, phi;
                AxisBound(target, tau, plo, phi);
                const DBL half = 0.5 * (tau.hi - tau.lo), turn = target.turning * half;
                const DBL bow = 0.125 * turn * turn * (Reach(m1) + Reach(m2) + Reach(shift));
                for (int k = 0; k < 3; ++k)
                {
                    AxisState g;
                    AxisFrame(target, tau.lo + half * k, g);
                    const Vector3d T = g.P1.normalized();
                    for (int i = 0; i < 3; ++i)
                    {
                        const Interval swing = g.e1[i] * m1 + g.e2[i] * m2 + T[i] * shift;
                        o[i] = k == 0 ? swing : Interval(std::min(o[i].lo, swing.lo), std::max(o[i].hi, swing.hi));
                    }
                }
                for (int i = 0; i < 3; ++i)
                    o[i] = Interval(plo[i] - bow, phi[i] + bow) + o[i];
                return;
            }
            const Interval station = Project(b, s.axis);
            const Interval t(OnAxis(axis, station.lo), OnAxis(axis, station.hi));
            const DBL half = 0.5 * (t.hi - t.lo);
            const int order[3] = { 0, 2, 1 };
            Interval D[3], q[3], span(0.0), shift = L;
            AxisState f, g;
            for (int n = 0; n < 3; ++n)
            {
                DBL chain, over, grow;
                const DBL tk = t.lo + half * order[n];
                AxisFrame(axis, tk, f);
                AxisFrame(target, Target(s, tk, chain, over, grow), g);
                shift = n == 0 ? L + Interval(over) : Interval(std::min(shift.lo, L.lo + over), std::max(shift.hi, L.hi + over));
                for (int i = 0; i < 3; ++i)
                {
                    const DBL d = g.P[i] - f.P[i];
                    D[i] = n == 0 ? Interval(d) : Interval(std::min(D[i].lo, d), std::max(D[i].hi, d));
                }
            }
            Vector3d alo, ahi;
            AxisBound(axis, t, alo, ahi);
            for (int i = 0; i < 3; ++i)
            {
                q[i] = b[i] - Interval(alo[i], ahi[i]);
                span = span + Square(q[i]);
            }
            const Vector3d TA = f.P1.normalized(), TB = g.P1.normalized();
            const DBL reach = sqrt(std::max(0.0, span.hi));
            const DBL pad = half * (target.drift + target.twist * (Reach(E) * reach + Reach(shift))) + 1.0e-12 * data.size;
            for (int i = 0; i < 3; ++i)
            {
                Interval sum = b[i] + D[i] + TB[i] * shift;
                for (int j = 0; j < 3; ++j)
                {
                    const DBL m = TB[i] * TA[j] - (i == j ? 1.0 : 0.0);
                    sum = sum + (Interval(m) + E * (c * (g.e1[i] * f.e1[j] + g.e2[i] * f.e2[j]) +
                                                    sn * (g.e2[i] * f.e1[j] - g.e1[i] * f.e2[j]))) * q[j];
                }
                o[i] = Interval(sum.lo - pad, sum.hi + pad);
            }
        }

        /// The roll's offsets in intervals: `e` past the hinge and `n` toward the centre, for a turning radius `R` of one sign taken as positive.
        static void RollBox(const Interval& rho, const Interval& h, const Interval& R, const Interval& limit, Interval& e, Interval& n)
        {
            const Interval reach(std::max(0.0, rho.lo), rho.hi), arm = R - h;
            const DBL least = R.hi > 0.0 ? reach.lo / R.hi : reach.lo > 0.0 ? BOUND_HUGE : 0.0, most = R.lo > 0.0 ? reach.hi / R.lo : BOUND_HUGE;
            const Interval phi(std::min(least, limit.lo), std::min(most, limit.hi));
            const DBL tail = limit.lo >= 0.0 ? std::max(0.0, reach.lo - R.hi * limit.hi) : 0.0;
            const Interval c = Cosine(phi), sn = Sine(phi), extra(tail, std::max(0.0, reach.hi - phi.lo * R.lo));
            e = arm * sn + extra * c;
            n = R - arm * c + extra * sn;
        }

        /// Encloses a crease: exact in intervals about a straight hinge, each sign of the radius rolled separately; about a curved one it is
        /// a displacement from the incoming box in the frame at the middle station, padded by the frame's turn over the station range.
        void CreaseBox(const SkeinStep& s, const Interval *b, const Interval& R, const Interval& limit, Interval *o)
        {
            Interval q[3], rho, h, across;
            Vector3d T = s.side, A = s.along, N = s.axis;
            DBL turn = 0.0, drift = 0.0;
            if (s.curve)
            {
                const Interval station = Project(b, s.axis), t(OnAxis(*s.curve, station.lo), OnAxis(*s.curve, station.hi));
                Vector3d plo, phi;
                AxisState f;
                AxisBound(*s.curve, t, plo, phi);
                AxisFrame(*s.curve, 0.5 * (t.lo + t.hi), f);
                T = f.P1.normalized();
                A = f.e2;
                N = f.e1;
                Interval span(0.0);
                for (int i = 0; i < 3; ++i)
                {
                    q[i] = b[i] - Interval(plo[i], phi[i]);
                    span = span + Square(q[i]);
                }
                turn = s.curve->turning * 0.5 * (t.hi - t.lo);
                drift = turn * sqrt(std::max(0.0, span.hi));
            }
            else
                for (int i = 0; i < 3; ++i)
                    q[i] = b[i] - Interval(s.origin[i]);
            rho = Project(q, A);
            h = Project(q, N);
            across = Project(q, T);
            rho = Interval(rho.lo - drift, rho.hi + drift);
            h = Interval(h.lo - drift, h.hi + drift);
            for (int i = 0; i < 3; ++i)
                o[i] = b[i];
            if (rho.hi <= 0.0)
                return;
            bool first = rho.lo >= 0.0;
            for (DBL sign = 1.0; sign >= -1.0; sign -= 2.0)
            {
                const Interval Rs = sign * R;
                if (Rs.hi < 0.0)
                    continue;
                Interval e, n, piece[3];
                RollBox(rho, sign * h, Interval(std::max(0.0, Rs.lo), Rs.hi), limit, e, n);
                if (s.curve)
                {
                    const Interval de = e - Interval(std::max(0.0, rho.lo), rho.hi), dn = sign * n - h;
                    const DBL pad = turn * (Reach(de) + Reach(dn)) + 1.0e-12 * data.size;
                    for (int i = 0; i < 3; ++i)
                    {
                        piece[i] = b[i] + A[i] * de + N[i] * dn;
                        piece[i] = Interval(piece[i].lo - pad, piece[i].hi + pad);
                    }
                }
                else
                    for (int i = 0; i < 3; ++i)
                        piece[i] = Interval(s.origin[i]) + T[i] * across + A[i] * e + N[i] * (sign * n);
                Hull(o, piece, first);
                first = false;
            }
        }

        /// Encloses a curl: the patch's distance past the foot gives a run of the table, whose section points and normals
        /// are hulled and padded by the sampled speed and turn over half a step, with the straight run past the table's end.
        static void CurlBox(const SkeinRoll& R, const Interval *b, Interval *o)
        {
            Interval q[3], C[3], m[2], piece[3];
            for (int i = 0; i < 3; ++i)
            {
                q[i] = b[i] - Interval(R.foot[i]);
                o[i] = b[i];
            }
            const Interval rho = Project(q, R.A), a = Project(q, R.T), h = Project(q, R.N);
            if (rho.hi <= 0.0)
                return;
            const std::vector<DBL>& arc = R.table.arc;
            const int n = int(arc.size()) - 1;
            const DBL lo = std::max(0.0, rho.lo), hi = std::min(rho.hi, R.table.length);
            const int j0 = std::min(n, std::max(0, int(std::upper_bound(arc.begin(), arc.end(), lo) - arc.begin()) - 1));
            const int j1 = std::min(n, std::max(j0, int(std::lower_bound(arc.begin(), arc.end(), hi) - arc.begin())));
            for (int k = j0; k <= j1; ++k)
                for (int i = 0; i < 3; ++i)
                {
                    C[i] = k == j0 ? Interval(R.section[k][i]) : Interval(std::min(C[i].lo, R.section[k][i]), std::max(C[i].hi, R.section[k][i]));
                    if (i < 2)
                        m[i] = k == j0 ? Interval(R.normals[k][i]) : Interval(std::min(m[i].lo, R.normals[k][i]), std::max(m[i].hi, R.normals[k][i]));
                }
            const DBL drift = 0.5 * R.step * R.speedBound, swing = 0.5 * R.step * R.turnBound, extra = std::max(0.0, rho.hi - R.table.length);
            for (int i = 0; i < 3; ++i)
            {
                const DBL end = extra > 0.0 ? R.section[n][i] + R.tail[i] * extra : C[i].lo;
                C[i] = Interval(std::min(C[i].lo, end) - drift, std::max(C[i].hi, end) + drift);
                if (i < 2)
                    m[i] = Interval(std::max(-1.0, m[i].lo - swing), std::min(1.0, m[i].hi + swing));
            }
            for (int i = 0; i < 3; ++i)
                piece[i] = Interval(R.foot[i]) + R.T[i] * (a + C[X]) + R.A[i] * (C[Y] + h * m[0]) + R.N[i] * (C[Z] + h * m[1]);
            Hull(o, piece, rho.lo >= 0.0);
        }

        /// Applies steps [from, to) of a scope to a box state; (u0..u1, v0..v1) is the box of the uv input.
        void BoxRun(const Scope& sc, int from, int to, DBL u0, DBL u1, DBL v0, DBL v1, const DBL *ranges, BoxState& state)
        {
            Interval *b = state.b, *bu = state.bu, *bv = state.bv;
            Interval in[SkeinValue::kInputs];
            in[SkeinValue::kU] = Interval(u0, u1);
            in[SkeinValue::kV] = Interval(v0, v1);
            for (int k = SkeinValue::kNX; k <= SkeinValue::kNZ; ++k)
                in[k] = Interval(-1.0, 1.0);
            for (int index = from; index < to; ++index)
            {
                const SkeinStep& s = sc.steps[index];
                const Stage stage{ &sc, index };
                Interval o[3], ou[3], ov[3];
                bool straight = false;
                for (int k = 0; k < 3; ++k)
                    in[SkeinValue::kX + k] = b[k];
                if (s.kind == SkeinStep::kAffine)
                {
                    for (int i = 0; i < 3; ++i)
                    {
                        o[i] = Interval(s.affine.matrix[3][i]) + s.affine.matrix[0][i] * b[X] + s.affine.matrix[1][i] * b[Y] + s.affine.matrix[2][i] * b[Z];
                        ou[i] = s.affine.matrix[0][i] * bu[X] + s.affine.matrix[1][i] * bu[Y] + s.affine.matrix[2][i] * bu[Z];
                        ov[i] = s.affine.matrix[0][i] * bv[X] + s.affine.matrix[1][i] * bv[Y] + s.affine.matrix[2][i] * bv[Z];
                    }
                }
                else if (s.kind == SkeinStep::kEnvelope)
                {
                    EnvelopeBox(s, stage, u0, u1, v0, v1, ranges, o);
                    state.slopes = false;
                }
                else if (s.kind == SkeinStep::kBlend)
                {
                    const std::vector<SkeinBlend::Entry>& e = s.blend->entries;
                    const Interval w = ValueRange(s.value[SkeinStep::kDriver], ranges, in);
                    int first = 0, last = int(e.size()) - 1;
                    while ((first < last) && (e[first + 1].value <= w.lo))
                        ++first;
                    while ((last > first) && (e[last - 1].value >= w.hi))
                        --last;
                    for (int k = first; k <= last; ++k)
                    {
                        BoxState item = state;
                        item.slopes = false;
                        const Scope inner{ e[k].steps, &sc, index, s.wrapOut };
                        BoxRun(inner, 0, int(e[k].steps.size()), u0, u1, v0, v1, ranges, item);
                        Hull(o, item.b, k == first);
                    }
                    state.slopes = false;
                }
                else
                {
                    Interval w[3];
                    for (int k = 0; k < 3; ++k)
                        w[k] = ValueRange(s.value[k], ranges, in);
                    straight = (s.kind == SkeinStep::kFold) && !s.curve && !s.value[0].Varies() && !s.value[1].Varies() && !s.value[2].Varies();
                    state.slopes = state.slopes && straight;
                    switch (s.kind)
                    {
                        case SkeinStep::kFold:
                        {
                            const Interval t = Project(b, s.axis), rho = w[SkeinStep::kRadius] + Project(b, s.offset);
                            const Interval a = kRadiansPerDegree * w[SkeinStep::kArc];
                            const Interval g = a * Project(b, s.along);
                            const Interval c = Cosine(g), sn = Sine(g), rc = rho * c, rs = rho * sn;
                            if (s.curve)
                            {
                                Vector3d plo, phi;
                                AxisBound(*s.curve, t, plo, phi);
                                const DBL half = 0.5 * (t.hi - t.lo), turn = s.curve->turning * half;
                                const DBL bow = 0.125 * turn * turn * (Reach(rc) + Reach(rs));
                                for (int k = 0; k < 3; ++k)
                                {
                                    AxisState f;
                                    AxisFrame(*s.curve, t.lo + half * k, f);
                                    for (int i = 0; i < 3; ++i)
                                    {
                                        const Interval swing = f.e1[i] * rc + f.e2[i] * rs;
                                        o[i] = k == 0 ? swing : Interval(std::min(o[i].lo, swing.lo), std::max(o[i].hi, swing.hi));
                                    }
                                }
                                for (int i = 0; i < 3; ++i)
                                    o[i] = Interval(plo[i] - bow, phi[i] + bow) + o[i];
                                break;
                            }
                            const Interval tu = Project(bu, s.axis), ru = Project(bu, s.offset), gu = a * Project(bu, s.along);
                            const Interval tv = Project(bv, s.axis), rv = Project(bv, s.offset), gv = a * Project(bv, s.along);
                            for (int i = 0; i < 3; ++i)
                            {
                                o[i] = s.axis[i] * t + s.along[i] * rc + s.side[i] * rs;
                                ou[i] = s.axis[i] * tu + s.along[i] * (ru * c - rs * gu) + s.side[i] * (ru * sn + rc * gu);
                                ov[i] = s.axis[i] * tv + s.along[i] * (rv * c - rs * gv) + s.side[i] * (rv * sn + rc * gv);
                            }
                            break;
                        }
                        case SkeinStep::kDisplace:
                        {
                            Interval n[3];
                            NormalRange(stage, u0, u1, v0, v1, state.exact, bu, bv, n);
                            for (int i = 0; i < 3; ++i)
                                o[i] = b[i] + n[i] * w[SkeinStep::kAmount];
                            break;
                        }
                        case SkeinStep::kTranslate:
                            for (int i = 0; i < 3; ++i)
                                o[i] = b[i] + w[i];
                            break;
                        case SkeinStep::kScale:
                            for (int i = 0; i < 3; ++i)
                                o[i] = Interval(s.origin[i]) + w[i] * (b[i] - Interval(s.origin[i]));
                            break;
                        case SkeinStep::kBend:
                            CreaseBox(s, b, w[SkeinStep::kRadius], kRadiansPerDegree * w[SkeinStep::kLimit], o);
                            break;
                        case SkeinStep::kCurl:
                            if (s.roll->ready)
                                CurlBox(*s.roll, b, o);
                            else
                                for (int i = 0; i < 3; ++i)
                                    o[i] = b[i];
                            break;
                        case SkeinStep::kAxialStep:
                        {
                            const Interval L = w[SkeinStep::kShift], E = w[SkeinStep::kRadial];
                            const Interval angle = kRadiansPerDegree * w[SkeinStep::kAngle], c = Cosine(angle), sn = Sine(angle);
                            if (s.target)
                            {
                                AlongBox(s, b, L, E, c, sn, o);
                                break;
                            }
                            if (s.curve)
                            {
                                const SkeinAxis& curve = *s.curve;
                                const Interval station = Project(b, s.axis);
                                const Interval t(OnAxis(curve, station.lo), OnAxis(curve, station.hi));
                                Vector3d plo, phi;
                                AxisState f;
                                AxisBound(curve, t, plo, phi);
                                AxisFrame(curve, 0.5 * (t.lo + t.hi), f);
                                const Vector3d T = f.P1.normalized();
                                Interval P[3], tb(0.0), tp(0.0), span(0.0);
                                for (int i = 0; i < 3; ++i)
                                {
                                    P[i] = Interval(plo[i], phi[i]);
                                    tb = tb + T[i] * b[i];
                                    tp = tp + T[i] * P[i];
                                    span = span + Square(b[i] - P[i]);
                                }
                                const DBL drift = curve.turning * 0.5 * (t.hi - t.lo), reach = sqrt(std::max(0.0, span.hi));
                                // why: two turns of the same angle about axes `drift` apart part by 2 sqrt 2 sin(angle/2) drift (doc/skein.md, bend)
                                const Interval rest = Interval(1.0) - E * c;
                                const DBL pad = drift * (2.0 * sqrt(2.0) * Reach(Sine(0.5 * angle)) * Reach(E) * reach +
                                                         2.0 * Reach(rest) * reach + Reach(L)) + 1.0e-12 * data.size;
                                for (int i = 0; i < 3; ++i)
                                {
                                    const int j = (i + 1) % 3, k = (i + 2) % 3;
                                    const Interval turn = T[j] * b[k] - T[k] * b[j], base = T[j] * P[k] - T[k] * P[j];
                                    o[i] = E * (c * b[i] + sn * (turn - base)) + P[i] * rest + T[i] * (rest * (tb - tp) + L);
                                    o[i] = Interval(o[i].lo - pad, o[i].hi + pad);
                                }
                                break;
                            }
                            // why: E R is the whole linear part, so the coefficient along the axis vanishes at the identity (doc/skein.md, bend)
                            const Interval along = Project(b, s.axis), rest = Interval(1.0) - E * c;
                            for (int i = 0; i < 3; ++i)
                            {
                                const int j = (i + 1) % 3, k = (i + 2) % 3;
                                const Interval turn = s.axis[j] * b[k] - s.axis[k] * b[j];
                                o[i] = E * (c * b[i] + sn * turn) + s.axis[i] * (rest * along + L);
                            }
                            break;
                        }
                        case SkeinStep::kSample:
                        {
                            const Interval U = w[SkeinStep::kAtU], V = w[SkeinStep::kAtV];
                            BoxState at;
                            BoxPrefix(sc, index, std::max(-1.0e6, U.lo), std::min(1.0e6, U.hi), std::max(-1.0e6, V.lo), std::min(1.0e6, V.hi), ranges, at);
                            for (int i = 0; i < 3; ++i)
                                o[i] = at.b[i];
                            state.slopes = false;
                            break;
                        }
                        default:
                        {
                            const Interval angle = kRadiansPerDegree * w[SkeinStep::kAngle], c = Cosine(angle), sn = Sine(angle);
                            Interval q[3], r[3];
                            for (int i = 0; i < 3; ++i)
                                q[i] = b[i] - Interval(s.origin[i]);
                            const Interval along = Project(q, s.axis);
                            for (int i = 0; i < 3; ++i)
                                r[i] = q[i] - s.axis[i] * along;
                            for (int i = 0; i < 3; ++i)
                            {
                                const int j = (i + 1) % 3, k = (i + 2) % 3;
                                const Interval side = s.axis[j] * r[k] - s.axis[k] * r[j];
                                o[i] = Interval(s.origin[i]) + s.axis[i] * along + c * r[i] + sn * side;
                            }
                            break;
                        }
                    }
                }
                state.exact = state.exact && ((s.kind == SkeinStep::kAffine) || straight);
                for (int i = 0; i < 3; ++i)
                {
                    b[i] = o[i];
                    bu[i] = ou[i];
                    bv[i] = ov[i];
                }
            }
        }

        /// Encloses an envelope over a patch: a face is the sheet's box moved by the half thickness along the normal's enclosure, a rim the edge's box moved along the normal and the outward edge direction.
        void EnvelopeBox(const SkeinStep& s, const Stage& st, DBL u0, DBL u1, DBL v0, DBL v1, const DBL *ranges, Interval *o)
        {
            DBL t0 = s.alongV ? v0 : u0, t1 = s.alongV ? v1 : u1;
            const DBL o0 = s.alongV ? u0 : v0, o1 = s.alongV ? u1 : v1;
            if ((t0 < 0.0) || (t1 > 1.0))
            {
                t0 = 0.0;
                t1 = 1.0;
            }
            bool first = true;
            auto flush = [&](DBL lo, DBL hi, EnvelopePiece::Kind kind, const Interval& turn)
            {
                const DBL U0 = s.alongV ? o0 : lo, U1 = s.alongV ? o1 : hi, V0 = s.alongV ? lo : o0, V1 = s.alongV ? hi : o1;
                BoxState sheet;
                BoxPrefix(*st.scope, st.index, U0, U1, V0, V1, ranges, sheet);
                Interval in[SkeinValue::kInputs];
                in[SkeinValue::kU] = Interval(U0, U1);
                in[SkeinValue::kV] = Interval(V0, V1);
                for (int k = 0; k < 3; ++k)
                {
                    in[SkeinValue::kX + k] = sheet.b[k];
                    in[SkeinValue::kNX + k] = Interval(-1.0, 1.0);
                }
                const Interval T = ValueRange(s.value[SkeinStep::kThickness], ranges, in);
                Interval n[3], d[3], moved[3];
                NormalRange(st, U0, U1, V0, V1, sheet.exact, sheet.bu, sheet.bv, n);
                if (kind == EnvelopePiece::kRim)
                {
                    const bool far = lo > 0.5;
                    const Interval H = 0.5 * T, k = (1.0 / M_PI) * turn;
                    Interval c = Cosine(turn), sn = Sine(turn);
                    if (s.edge == SkeinStep::kFlatEdge)
                    {
                        c = Interval(1.0) - 2.0 * k;
                        sn = Interval(0.0);
                    }
                    if (!far)
                        c = -1.0 * c;
                    EdgeRange(s, st, far, o0, o1, d);
                    for (int i = 0; i < 3; ++i)
                        moved[i] = sheet.b[i] + n[i] * (H * c) + d[i] * (H * sn);
                }
                else
                    for (int i = 0; i < 3; ++i)
                        moved[i] = sheet.b[i] + n[i] * ((kind == EnvelopePiece::kFront ? 0.5 : -0.5) * T);
                Hull(o, moved, first);
                first = false;
            };
            const DBL rim = s.edge != SkeinStep::kNoEdge ? kRimShare : 0.0;
            const bool loop = (s.wrapOut & (s.alongV ? 2 : 1)) != 0;
            const DBL f0 = loop ? 0.5 * rim : 0.0, f1 = 0.5 - 0.5 * rim, b0 = 0.5 + 0.5 * rim, b1 = loop ? 1.0 - 0.5 * rim : 1.0;
            const DBL cuts[4][2] = { { f0, f1 }, { b0, b1 }, { f1, b0 }, { b1, 1.0 + f0 } };
            for (int r = 0; r < 4; ++r)
            {
                const DBL lo = std::max(t0, cuts[r][0]), hi = std::min(t1, cuts[r][1]);
                const bool hit = (r == 3) ? (loop && ((t1 >= b1) || (t0 <= f0))) : (lo <= hi);
                if (!hit)
                    continue;
                if (r == 0)
                    flush((lo - f0) / (f1 - f0), (hi - f0) / (f1 - f0), EnvelopePiece::kFront, Interval(0.0));
                else if (r == 1)
                    flush((b1 - hi) / (b1 - b0), (b1 - lo) / (b1 - b0), EnvelopePiece::kBack, Interval(0.0));
                else if ((rim > 0.0) && (r == 2))
                    flush(1.0, 1.0, EnvelopePiece::kRim, Interval(M_PI * (lo - f1) / rim, M_PI * (hi - f1) / rim));
                else if (rim > 0.0)
                {
                    const bool before = t0 <= f0, after = t1 >= b1;
                    const DBL a = after ? M_PI * (std::max(t0, b1) - b1) / rim : M_PI * (t0 + 0.5 * rim) / rim;
                    const DBL b = before ? M_PI * (std::min(t1, f0) + 0.5 * rim) / rim : M_PI * (t1 - b1) / rim;
                    flush(0.0, 0.0, EnvelopePiece::kRim, (before && after) ? Interval(0.0, M_PI) : Interval(a, b));
                }
            }
            if (first)
                flush(0.0, 1.0, EnvelopePiece::kFront, Interval(0.0));
        }

        /// Encloses the outward edge direction along an edge over [o0, o1] of the other parameter, sampled at three points with a margin.
        void EdgeRange(const SkeinStep& s, const Stage& st, bool far, DBL o0, DBL o1, Interval *d)
        {
            Vector3d lo(BOUND_HUGE), hi(-BOUND_HUGE);
            for (int k = 0; k <= 2; ++k)
            {
                const DBL other = o0 + 0.5 * (o1 - o0) * k, U = s.alongV ? other : DBL(far), V = s.alongV ? DBL(far) : other;
                Jet K;
                Prefix(*st.scope, st.index, U, V, K, nullptr, true);
                const Vector3d e = EdgeDirection(s, K, NormalOf(st, U, V, K), far);
                for (int i = 0; i < 3; ++i)
                {
                    lo[i] = std::min(lo[i], e[i]);
                    hi[i] = std::max(hi[i], e[i]);
                }
            }
            for (int i = 0; i < 3; ++i)
            {
                const DBL margin = 0.5 * (hi[i] - lo[i]) + 1.0e-3;
                d[i] = Interval(std::max(-1.0, lo[i] - margin), std::min(1.0, hi[i] + margin));
            }
        }

        const SkeinData& data;
        TraceThreadData *thread;
        GenericScalarFunctionPtr host;
        GenericFunctionContextPtr context;
        const Scope top;
};

/// The outward unit normal at a sample, nudged inward in v where the chain pinches to a pole.
Vector3d SurfaceNormal(const SkeinData& d, Evaluator& ev, DBL u, DBL v, const Jet& J)
{
    Vector3d n = cross(J.pu, J.pv);
    if (n.length() <= 1.0e-10 * d.size * d.size)
    {
        Jet K;
        ev.Eval(u, ev.WrapV(v + (v < 0.5 ? 1.0e-6 : -1.0e-6)), K);
        n = cross(K.pu, K.pv);
    }
    const DBL len = n.length();
    return len > 0.0 ? n * (d.orientation / len) : Vector3d(0.0, 0.0, 1.0);
}

/// Finds every crossing of one object-space ray by bounding-tree descent and per-patch Newton.
class Solver final
{
    public:

        Solver(const SkeinData& d, Evaluator& e, const Vector3d& p, const Vector3d& dir, DBL t, Skein::Crossing *o, RenderStatistics& s) :
            data(d), ev(e), P(p), D(dir), tMin(t), out(o), count(0), stats(s), tol(1.0e-10 * d.size)
        {}

        int Run()
        {
            int stack[2 * kMaxGrid];
            int top = 0;
            stack[top++] = 0;
            while (top > 0)
            {
                const SkeinData::Node& node = data.nodes[stack[--top]];
                DBL t0, t1;
                if (!Hit(node.lo, node.hi, t0, t1))
                    continue;
                if (node.patch >= 0)
                {
                    const SkeinData::Patch& p = data.patches[node.patch];
                    Solve(p.u0, p.u1, p.v0, p.v1, data.ranges.data() + p.ranges, p.flat, 0, t0, t1);
                }
                else
                {
                    stack[top++] = node.child[0];
                    stack[top++] = node.child[1];
                }
            }
            for (int e = 0; e < 2; ++e)
                if (data.caps[e].present)
                    CapCrossing(data.caps[e]);
            return Finish();
        }

    private:

        bool Hit(const Vector3d& lo, const Vector3d& hi, DBL& t0, DBL& t1)
        {
            stats[Skein_Bound_Tests]++;
            if (!HitBox(lo, hi, P, D, tMin, t0, t1))
                return false;
            stats[Skein_Bound_Tests_Succeeded]++;
            return true;
        }

        void Add(DBL t, DBL u, DBL v, const Vector3d& n)
        {
            const int sign = dot(D, n) > 0.0 ? 1 : -1;
            for (int i = 0; i < count; ++i)
                if ((out[i].sign == sign) && (fabs(out[i].t - t) < 1.0e-7 * data.size))
                    return;
            if (count == Skein::kMaxCrossings)
                return;
            Skein::Crossing& c = out[count++];
            c.t = t;
            c.u = u;
            c.v = v;
            c.normal = n;
            c.sign = sign;
        }

        bool Known(DBL u0, DBL u1, DBL v0, DBL v1, DBL grazing) const
        {
            for (int i = 0; i < knownCount; ++i)
                if ((known[i].facing >= grazing) && (known[i].u >= u0 - kSlack) && (known[i].u <= u1 + kSlack) &&
                    (known[i].v >= v0 - kSlack) && (known[i].v <= v1 + kSlack))
                    return true;
            return false;
        }

        /// Newton from (u, v) on S(u, v) = P + tD; records the root it reaches and returns |D.N| there, or -1 for none.
        /// `apart` says whether that root may lie on another smooth piece: outside the patch and turned from its centre.
        DBL Newton(DBL u0, DBL u1, DBL v0, DBL v1, DBL u, DBL v, bool& apart, const Jet *first = nullptr)
        {
            stats[Skein_Newton_Solves]++;
            const DBL wu = u1 - u0, wv = v1 - v0;
            Jet J;
            if (first != nullptr)
                J = *first;
            else
                ev.Eval(ev.WrapU(u), ev.WrapV(v), J);
            const Vector3d start = cross(J.pu, J.pv);
            apart = true;
            DBL t = dot(J.p - P, D), last = BOUND_HUGE;
            bool converged = false, fine = false;
            for (int step = 0; step < kNewtonSteps; ++step)
            {
                stats[Skein_Newton_Iterations]++;
                const Vector3d F = J.p - P - D * t;
                const DBL residual = F.length();
                if (residual < tol)
                {
                    converged = true;
                    break;
                }
                if ((step >= 4) && (residual > 0.5 * last))
                {
                    // Differenced slopes that straddle a crease leave Newton linear by it; narrower ones make it quadratic again.
                    if (fine || !data.readsNormal || !(residual < kNearRoot * data.size))
                        break;
                    fine = true;
                    ev.neighbour = kFineNeighbour;
                    ev.Eval(ev.WrapU(u), ev.WrapV(v), J);
                }
                last = residual;
                const Vector3d c = cross(J.pv, D);
                const DBL det = -dot(J.pu, c);
                if (fabs(det) < 1.0e-300)
                    break;
                DBL du = dot(F, c) / det, dv = dot(J.pu, cross(F, D)) / det, dt = -dot(J.pu, cross(J.pv, F)) / det;
                const DBL m = std::max(fabs(du) / wu, fabs(dv) / wv);
                if (m > 1.0)
                {
                    du /= m;
                    dv /= m;
                    dt /= m;
                }
                u += du;
                v += dv;
                t += dt;
                if ((u < u0 - wu) || (u > u1 + wu) || (v < v0 - wv) || (v > v1 + wv))
                    break;
                ev.Eval(ev.WrapU(u), ev.WrapV(v), J);
            }
            ev.neighbour = kNeighbour;
            const DBL uw = ev.WrapU(u), vw = ev.WrapV(v);
            if (!converged || (uw < -kSlack) || (uw > 1.0 + kSlack) || (vw < -kSlack) || (vw > 1.0 + kSlack))
                return -1.0;
            stats[Skein_Newton_Solves_Succeeded]++;
            const Vector3d n = SurfaceNormal(data, ev, uw, vw, J);
            const bool within = (u >= u0 - kSlack) && (u <= u1 + kSlack) && (v >= v0 - kSlack) && (v <= v1 + kSlack);
            apart = !within && !(fabs(dot(n, start)) >= cos(3.0 * kPatchTurn) * start.length());
            const DBL uc = std::min(1.0, std::max(0.0, uw)), vc = std::min(1.0, std::max(0.0, vw)), facing = fabs(dot(D, n));
            if (knownCount < Skein::kMaxCrossings)
            {
                known[knownCount].u = uc;
                known[knownCount].v = vc;
                known[knownCount++].facing = facing;
            }
            if (t > tMin)
                Add(t, uc, vc, n);
            return facing;
        }

        /// Mean-value bound: the patch lies within the slab about its centre tangent plane; false when the ray misses that slab.
        bool Slab(DBL u0, DBL u1, DBL v0, DBL v1, const DBL *ranges, const Jet& centre, DBL t0, DBL t1)
        {
            Interval su[3], sv[3];
            Vector3d lo, hi, n = cross(centre.pu, centre.pv);
            if ((n.length() == 0.0) || !ev.Box(u0, u1, v0, v1, ranges, lo, hi, su, sv))
                return true;
            n.normalize();
            const Interval a = Project(su, n), b = Project(sv, n);
            const DBL delta = 0.5 * (u1 - u0) * std::max(fabs(a.lo), fabs(a.hi)) + 0.5 * (v1 - v0) * std::max(fabs(b.lo), fabs(b.hi)) + 1.0e-9 * data.size;
            const DBL h = dot(P - centre.p, n), rate = dot(D, n);
            stats[Skein_Bound_Tests]++;
            if (rate == 0.0)
                return fabs(h) <= delta;
            DBL ta = (-delta - h) / rate, tb = (delta - h) / rate;
            if (ta > tb)
                std::swap(ta, tb);
            if (std::max(ta, t0) > std::min(tb, t1))
                return false;
            stats[Skein_Bound_Tests_Succeeded]++;
            return true;
        }

        /// A root met at a steep angle is the patch's only one when the patch is flat; otherwise the patch splits.
        void Solve(DBL u0, DBL u1, DBL v0, DBL v1, const DBL *ranges, DBL flat, int depth, DBL t0, DBL t1)
        {
            const DBL grazing = flat > 1.0 ? flat : flat / DBL(1 << depth);
            if (Known(u0, u1, v0, v1, grazing))
                return;
            Jet centre;
            ev.Eval(ev.WrapU(0.5 * (u0 + u1)), ev.WrapV(0.5 * (v0 + v1)), centre);
            if ((flat <= 1.0) && !Slab(u0, u1, v0, v1, ranges, centre, t0, t1))
                return;
            bool apart;
            const DBL facing = Newton(u0, u1, v0, v1, 0.5 * (u0 + u1), 0.5 * (v0 + v1), apart, &centre);
            if ((facing >= grazing) && !apart)
                return;
            if (depth < kSplitDepth)
            {
                Split(u0, u1, v0, v1, ranges, flat, depth);
                return;
            }
            if (facing < 0.0)
                stats[Skein_Unresolved_Patches]++;
            if (flat > 1.0)
                for (int k = 0; k < 4; ++k)
                    Newton(u0, u1, v0, v1, u0 + (u1 - u0) * ((k & 1) ? 0.9 : 0.1), v0 + (v1 - v0) * ((k & 2) ? 0.9 : 0.1), apart);
        }

        void Split(DBL u0, DBL u1, DBL v0, DBL v1, const DBL *ranges, DBL flat, int depth)
        {
            const DBL um = 0.5 * (u0 + u1), vm = 0.5 * (v0 + v1);
            const DBL us[3] = { u0, um, u1 }, vs[3] = { v0, vm, v1 };
            DBL inner[2 * kMaxFunctions];
            for (int j = 0; j < 2; ++j)
                for (int i = 0; i < 2; ++i)
                {
                    Vector3d lo, hi;
                    DBL t0, t1;
                    ev.Ranges(us[i], us[i + 1], vs[j], vs[j + 1], 3, inner);
                    ev.Box(us[i], us[i + 1], vs[j], vs[j + 1], inner, lo, hi);
                    if (Hit(lo, hi, t0, t1))
                        Solve(us[i], us[i + 1], vs[j], vs[j + 1], inner, flat, depth + 1, t0, t1);
                }
        }

        DBL RingAngle(const SkeinData::Cap& cap, DBL u, DBL& radius, DBL& slope)
        {
            Jet J;
            ev.Eval(u, cap.v, J);
            const Vector3d q = J.p - cap.centre;
            const DBL x = dot(q, cap.e1), y = dot(q, cap.e2), xu = dot(J.pu, cap.e1), yu = dot(J.pu, cap.e2);
            radius = sqrt(x * x + y * y);
            slope = (x * yu - y * xu) / std::max(x * x + y * y, 1.0e-300);
            return atan2(y, x);
        }

        void CapCrossing(const SkeinData::Cap& cap)
        {
            const DBL den = dot(D, cap.normal);
            if (fabs(den) < 1.0e-12)
                return;
            const DBL t = dot(cap.centre - P, cap.normal) / den;
            if (t <= tMin)
                return;
            const Vector3d q = P + D * t - cap.centre;
            const DBL x = dot(q, cap.e1), y = dot(q, cap.e2), r = sqrt(x * x + y * y);
            if (r > cap.reach)
                return;
            const DBL phi = atan2(y, x);
            DBL target = phi;
            if ((cap.turn > 0.0) && (target < 0.0))
                target += kTau;
            if ((cap.turn < 0.0) && (target > 0.0))
                target -= kTau;
            const int n = int(cap.angles.size()) - 1;
            int i = 0;
            while ((i < n - 1) && (cap.turn * cap.angles[i + 1] < cap.turn * target))
                ++i;
            DBL lo = DBL(i) / n, hi = DBL(i + 1) / n;
            const DBL a0 = cap.angles[i], a1 = cap.angles[i + 1];
            DBL u = lo + (hi - lo) * std::min(1.0, std::max(0.0, (target - a0) / (a1 - a0)));
            DBL boundary = 0.0, slope = 0.0;
            for (int step = 0; step < 32; ++step)
            {
                const DBL g = cap.turn * WrapAngle(RingAngle(cap, u, boundary, slope) - phi);
                if (fabs(g) < 1.0e-13)
                    break;
                if (g < 0.0)
                    lo = u;
                else
                    hi = u;
                DBL next = u - g / (cap.turn * slope);
                if (!(next > lo && next < hi))
                    next = 0.5 * (lo + hi);
                if (hi - lo < 1.0e-15)
                    break;
                u = next;
            }
            if (r <= boundary)
                Add(t, u, cap.v, cap.normal);
        }

        int Finish()
        {
            std::sort(out, out + count, [](const Skein::Crossing& a, const Skein::Crossing& b) { return a.t < b.t; });
            int kept = 0;
            for (int i = 0; i < count; ++i)
            {
                if ((kept > 0) && (out[i].sign == out[kept - 1].sign) && (out[i].t - out[kept - 1].t < 1.0e-7 * data.size))
                    continue;
                out[kept++] = out[i];
            }
            return kept;
        }

        const SkeinData& data;
        Evaluator& ev;
        const Vector3d P, D;
        const DBL tMin;
        Skein::Crossing *out;
        int count;
        RenderStatistics& stats;
        const DBL tol;
        struct { DBL u, v, facing; } known[Skein::kMaxCrossings];
        int knownCount = 0;
};

int BuildTree(SkeinData& d, int i0, int i1, int j0, int j1)
{
    const int index = int(d.nodes.size());
    d.nodes.push_back(SkeinData::Node());
    if ((i1 - i0 == 1) && (j1 - j0 == 1))
    {
        const int patch = j0 * d.gridU + i0;
        SkeinData::Node& node = d.nodes[index];
        node.patch = patch;
        node.child[0] = node.child[1] = -1;
        node.lo = d.patches[patch].lo;
        node.hi = d.patches[patch].hi;
        return index;
    }
    int a, b;
    if (i1 - i0 >= j1 - j0)
    {
        const int m = (i0 + i1) / 2;
        a = BuildTree(d, i0, m, j0, j1);
        b = BuildTree(d, m, i1, j0, j1);
    }
    else
    {
        const int m = (j0 + j1) / 2;
        a = BuildTree(d, i0, i1, j0, m);
        b = BuildTree(d, i0, i1, m, j1);
    }
    SkeinData::Node& node = d.nodes[index];
    node.patch = -1;
    node.child[0] = a;
    node.child[1] = b;
    node.lo = d.nodes[a].lo;
    node.hi = d.nodes[a].hi;
    for (int k = 0; k < 3; ++k)
    {
        node.lo[k] = std::min(node.lo[k], d.nodes[b].lo[k]);
        node.hi[k] = std::max(node.hi[k], d.nodes[b].hi[k]);
    }
    return index;
}

/// Total turn of the unit normal along one parameter, the worst of a few iso-lines.
DBL NormalTurn(Evaluator& ev, bool alongU)
{
    const DBL lines[4] = { 0.15, 0.4, 0.65, 0.9 };
    DBL worst = 0.0;
    for (DBL line : lines)
    {
        DBL turn = 0.0;
        Vector3d last;
        bool have = false;
        for (int i = 0; i <= 64; ++i)
        {
            const DBL s = DBL(i) / 64.0;
            Jet J;
            ev.Eval(alongU ? s : line, alongU ? line : s, J);
            Vector3d n = cross(J.pu, J.pv);
            if (n.length() == 0.0)
            {
                have = false;
                continue;
            }
            n.normalize();
            if (have)
                turn += acos(std::min(1.0, std::max(-1.0, dot(n, last))));
            last = n;
            have = true;
        }
        worst = std::max(worst, turn);
    }
    return worst;
}

/// Grid lines along one parameter: uniform, or for a skein that resamples, three quarters by normal turn so a rim's half turn gets its share.
std::vector<DBL> GridLines(Evaluator& ev, bool alongU, int n, bool adapt)
{
    std::vector<DBL> lines(n + 1);
    for (int i = 0; i <= n; ++i)
        lines[i] = DBL(i) / n;
    if (!adapt)
        return lines;
    const int bins = 512;
    const DBL iso[4] = { 0.15, 0.4, 0.65, 0.9 };
    std::vector<DBL> turn(bins, 0.0), total(bins + 1, 0.0);
    for (DBL line : iso)
    {
        Vector3d last;
        bool have = false;
        for (int i = 0; i <= bins; ++i)
        {
            const DBL s = DBL(i) / bins;
            Jet J;
            ev.Eval(alongU ? s : line, alongU ? line : s, J);
            Vector3d m = cross(J.pu, J.pv);
            if (m.length() == 0.0)
            {
                have = false;
                continue;
            }
            m.normalize();
            if (have)
                turn[i - 1] = std::max(turn[i - 1], acos(std::min(1.0, std::max(-1.0, dot(m, last)))));
            last = m;
            have = true;
        }
    }
    DBL sum = 0.0;
    for (DBL t : turn)
        sum += t;
    if (sum <= 0.0)
        return lines;
    for (int k = 0; k < bins; ++k)
        total[k + 1] = total[k] + 0.75 * turn[k] / sum + 0.25 / bins;
    int k = 0;
    for (int i = 1; i < n; ++i)
    {
        const DBL target = DBL(i) / n;
        while ((k < bins - 1) && (total[k + 1] < target))
            ++k;
        lines[i] = (k + std::min(1.0, (target - total[k]) / (total[k + 1] - total[k]))) / bins;
    }
    return lines;
}

/// The steepest |D.N| at which a ray can still meet the patch twice, from the spread of its sampled normals; 2 marks a crease.
DBL Flatness(Evaluator& ev, const SkeinData::Patch& p)
{
    Jet J;
    ev.Eval(0.5 * (p.u0 + p.u1), 0.5 * (p.v0 + p.v1), J);
    Vector3d centre = cross(J.pu, J.pv);
    if (centre.length() == 0.0)
        return 2.0;
    centre.normalize();
    DBL cone = 0.0;
    for (int b = 0; b <= 4; ++b)
        for (int a = 0; a <= 4; ++a)
        {
            ev.Eval(p.u0 + (p.u1 - p.u0) * a / 4.0, p.v0 + (p.v1 - p.v0) * b / 4.0, J);
            const Vector3d n = cross(J.pu, J.pv);
            if (n.length() > 0.0)
                cone = std::max(cone, acos(std::min(1.0, std::max(-1.0, dot(n, centre) / n.length()))));
        }
    return cone > 2.0 * kPatchTurn ? 2.0 : sin(std::min(0.5 * M_PI, 2.5 * cone));
}

/// Angle between two vectors, 0 when either vanishes.
DBL Angle(const Vector3d& a, const Vector3d& b)
{
    const DBL l = a.length() * b.length();
    return l > 0.0 ? acos(std::min(1.0, std::max(-1.0, dot(a, b) / l))) : 0.0;
}

/// Tessellates a prepared skein on a dyadic (u, v) lattice: a cell splits along u or v while the normal or an edge turns more than the
/// limit there, until its edges would fall below the smallest size. A leaf with a neighbour's vertices on its edges fans from its centre.
class Mesher final
{
    public:

        Mesher(const SkeinData& d, TraceThreadData *t, DBL size, DBL angle, SkeinMesh& m) :
            data(d), ev(d, t), minSize(size), maxAngle(angle * kRadiansPerDegree), out(m)
        {}

        std::string Run()
        {
            const int baseU = Power(std::max(data.wrapU ? 4 : 1, data.gridU)), baseV = Power(std::max(data.wrapV ? 4 : 1, data.gridV));
            Nu = baseU << kMeshDepth;
            Nv = baseV << kMeshDepth;
            for (int e = 0; e < 2; ++e)
            {
                const bool ring = data.wrapU && !data.wrapV;
                pole[e] = ring && ((data.ends[e] == SkeinData::kPole) || ((data.ends[e] == SkeinData::kFlat) && !data.caps[e].present));
                sealed[e] = ring && (data.ends[e] == SkeinData::kSealed);
            }
            std::vector<Cell> stack, leaves;
            for (int j = 0; j < baseV; ++j)
                for (int i = 0; i < baseU; ++i)
                    stack.push_back(Cell{ i << kMeshDepth, (i + 1) << kMeshDepth, j << kMeshDepth, (j + 1) << kMeshDepth });
            while (!stack.empty())
            {
                const Cell c = stack.back();
                stack.pop_back();
                const bool su = Split(c, true), sv = Split(c, false);
                if (!su && !sv)
                {
                    leaves.push_back(c);
                    if (2 * leaves.size() > Skein::kMaxMeshTriangles)
                        return TooMany();
                    continue;
                }
                const int im = su ? (c.i0 + c.i1) / 2 : c.i1, jm = sv ? (c.j0 + c.j1) / 2 : c.j1;
                stack.push_back(Cell{ c.i0, im, c.j0, jm });
                if (su)
                    stack.push_back(Cell{ im, c.i1, c.j0, jm });
                if (sv)
                    stack.push_back(Cell{ c.i0, im, jm, c.j1 });
                if (su && sv)
                    stack.push_back(Cell{ im, c.i1, jm, c.j1 });
            }
            for (const Cell& c : leaves)
                for (int k = 0; k < 4; ++k)
                {
                    const int i = (k & 1) ? c.i1 : c.i0, j = (k & 2) ? c.j1 : c.j0;
                    rows[WrapJ(j)].push_back(i);
                    cols[WrapI(i)].push_back(j);
                    if (sealed[0] && (j == 0))
                        rows[j].push_back(Nu - WrapI(i));
                    if (sealed[1] && (j == Nv))
                        rows[j].push_back(Nu - WrapI(i));
                }
            for (auto *lines : { &rows, &cols })
                for (auto& line : *lines)
                {
                    std::sort(line.second.begin(), line.second.end());
                    line.second.erase(std::unique(line.second.begin(), line.second.end()), line.second.end());
                }
            std::vector<std::pair<int, int>> loop;
            for (const Cell& c : leaves)
            {
                loop.clear();
                Edge(loop, true, c.j0, c.i0, c.i1);
                Edge(loop, false, c.i1, c.j0, c.j1);
                Edge(loop, true, c.j1, c.i1, c.i0);
                Edge(loop, false, c.i0, c.j1, c.j0);
                if (loop.size() == 4)
                {
                    const Corner a = Lattice(c.i0, c.j0), b = Lattice(c.i1, c.j0), d = Lattice(c.i1, c.j1), e = Lattice(c.i0, c.j1);
                    if ((d.p - a.p).length() <= (e.p - b.p).length())
                    {
                        Emit(a, b, d);
                        Emit(a, d, e);
                    }
                    else
                    {
                        Emit(a, b, e);
                        Emit(b, d, e);
                    }
                    continue;
                }
                Corner centre;
                centre.u = 0.5 * (DBL(c.i0) + DBL(c.i1)) / Nu;
                centre.v = 0.5 * (DBL(c.j0) + DBL(c.j1)) / Nv;
                Jet J;
                ev.Eval(centre.u, centre.v, J);
                centre.p = J.p;
                centre.n = SurfaceNormal(data, ev, centre.u, centre.v, J);
                centre.weld = centre.id = Unique();
                for (size_t k = 0; k < loop.size(); ++k)
                    Emit(centre, Lattice(loop[k].first, loop[k].second), Lattice(loop[(k + 1) % loop.size()].first, loop[(k + 1) % loop.size()].second));
                if (out.triangles.size() > 3 * Skein::kMaxMeshTriangles)
                    return TooMany();
            }
            for (int e = 0; e < 2; ++e)
            {
                const SkeinData::Cap& cap = data.caps[e];
                if (!cap.present)
                    continue;
                const int j = e ? Nv : 0;
                const std::vector<int>& ring = rows[j];
                Corner centre;
                centre.p = cap.centre;
                centre.n = cap.normal;
                centre.v = cap.v;
                centre.exact = true;
                centre.weld = Unique();
                for (size_t k = 0; k + 1 < ring.size(); ++k)
                {
                    Corner a = Lattice(ring[k], j), b = Lattice(ring[k + 1], j);
                    a.n = b.n = cap.normal;
                    a.exact = b.exact = true;
                    centre.u = 0.5 * (a.u + b.u);
                    centre.id = Unique();
                    Emit(centre, a, b);
                }
            }
            if (out.triangles.empty())
                return "skein_mesh: no triangles; the skein has no area.";
            std::vector<Cell>().swap(leaves);
            decltype(cache)().swap(cache);
            decltype(rows)().swap(rows);
            decltype(cols)().swap(cols);
            decltype(vertices)().swap(vertices);
            std::sort(edges.begin(), edges.end());
            out.openEdges = 0;
            for (size_t k = 0, n; k < edges.size(); k += n)
            {
                for (n = 1; (k + n < edges.size()) && (edges[k + n] == edges[k]); ++n) {}
                out.openEdges += (n == 1);
            }
            return std::string();
        }

    private:

        struct Sample { SnglVector3d p, n; };
        struct Cell { int i0, i1, j0, j1; };
        struct Corner { Vector3d p, n; DBL u = 0.0, v = 0.0; std::uint64_t weld = 0, id = 0; bool lattice = false, exact = false; };
        struct VertexKey
        {
            std::uint64_t id;
            std::uint32_t n[3];
            bool operator==(const VertexKey& o) const { return (id == o.id) && (n[0] == o.n[0]) && (n[1] == o.n[1]) && (n[2] == o.n[2]); }
        };
        struct VertexHash
        {
            size_t operator()(const VertexKey& k) const { return size_t(k.id * 0x9E3779B97F4A7C15ull ^ (std::uint64_t(k.n[0]) << 1) ^ (std::uint64_t(k.n[1]) << 21) ^ (std::uint64_t(k.n[2]) << 42)); }
        };

        static int Power(int n) { int p = 1; while (2 * p <= n) p *= 2; return p; }
        static std::uint64_t Key(int i, int j) { return (std::uint64_t(std::uint32_t(i)) << 32) | std::uint32_t(j); }

        std::string TooMany() const
        {
            return Format("skein_mesh: more than %g million triangles at this min_size and max_angle; raise either (min_size is %g).", 1.0e-6 * Skein::kMaxMeshTriangles, minSize);
        }

        int WrapI(int i) const { return data.wrapU && (i == Nu) ? 0 : i; }
        int WrapJ(int j) const { return data.wrapV && (j == Nv) ? 0 : j; }
        std::uint64_t Unique() { return (std::uint64_t(1) << 63) | unique++; }

        /// The lattice point's position key: seams wrap, a pole row is one point and a sealed row folds u onto 1 - u.
        std::uint64_t Weld(int i, int j) const
        {
            i = WrapI(i);
            j = WrapJ(j);
            for (int e = 0; e < 2; ++e)
                if (j == (e ? Nv : 0))
                {
                    if (pole[e])
                        i = 0;
                    else if (sealed[e])
                        i = std::min(i, Nu - i);
                }
            return Key(i, j);
        }

        const Sample& At(int i, int j)
        {
            i = WrapI(i);
            j = WrapJ(j);
            auto found = cache.find(Key(i, j));
            if (found != cache.end())
                return found->second;
            const DBL u = DBL(i) / Nu, v = DBL(j) / Nv;
            Jet J;
            ev.Eval(u, v, J);
            Sample& s = cache[Key(i, j)];
            s.p = SnglVector3d(J.p);
            s.n = SnglVector3d(SurfaceNormal(data, ev, u, v, J));
            return s;
        }

        Corner Lattice(int i, int j)
        {
            const std::uint64_t weld = Weld(i, j);
            Corner c;
            c.p = Vector3d(At(int(weld >> 32), int(weld & 0xFFFFFFFFu)).p);
            c.n = Vector3d(At(i, j).n);
            c.u = DBL(i) / Nu;
            c.v = DBL(j) / Nv;
            c.weld = weld;
            c.id = Key(i, j);
            c.lattice = true;
            return c;
        }

        /// Whether a cell splits along u (or v): the normal or an edge turns more than the limit, and the halves stay above the smallest size.
        bool Split(const Cell& c, bool alongU)
        {
            if ((alongU ? c.i1 - c.i0 : c.j1 - c.j0) < 2)
                return false;
            const int im = (c.i0 + c.i1) / 2, jm = (c.j0 + c.j1) / 2;
            DBL turn = 0.0, length = 0.0;
            for (int k = 0; k < 3; ++k)
            {
                const int across = alongU ? (k == 0 ? c.j0 : k == 1 ? jm : c.j1) : (k == 0 ? c.i0 : k == 1 ? im : c.i1);
                const Vector3d a = Vector3d(alongU ? At(c.i0, across).p : At(across, c.j0).p), an = Vector3d(alongU ? At(c.i0, across).n : At(across, c.j0).n);
                const Vector3d m = Vector3d(alongU ? At(im, across).p : At(across, jm).p), mn = Vector3d(alongU ? At(im, across).n : At(across, jm).n);
                const Vector3d b = Vector3d(alongU ? At(c.i1, across).p : At(across, c.j1).p), bn = Vector3d(alongU ? At(c.i1, across).n : At(across, c.j1).n);
                turn = std::max(turn, std::max(Angle(an, mn) + Angle(mn, bn), 2.0 * Angle(m - a, b - m)));
                length = std::max(length, (m - a).length() + (b - m).length());
            }
            return (turn > maxAngle) && (0.5 * length >= minSize);
        }

        /// Appends the corner at `from` and every vertex strictly between `from` and `to` on a row (or column) of the lattice.
        void Edge(std::vector<std::pair<int, int>>& loop, bool row, int line, int from, int to)
        {
            loop.push_back(row ? std::make_pair(from, line) : std::make_pair(line, from));
            const std::vector<int>& set = row ? rows[WrapJ(line)] : cols[WrapI(line)];
            auto lo = std::upper_bound(set.begin(), set.end(), std::min(from, to)), hi = std::lower_bound(set.begin(), set.end(), std::max(from, to));
            if (from < to)
                for (auto it = lo; it < hi; ++it)
                    loop.push_back(row ? std::make_pair(*it, line) : std::make_pair(line, *it));
            else
                for (auto it = hi; it > lo; --it)
                    loop.push_back(row ? std::make_pair(*(it - 1), line) : std::make_pair(line, *(it - 1)));
        }

        /// Adds a triangle unless degenerate, wound so the mesh's face normal points out. Where corner normals spread wider than a smooth cell's,
        /// a corner on a crease takes the surface's normal just inside the triangle, and a triangle still spanning a crease takes the face's.
        void Emit(const Corner& a, const Corner& b, const Corner& c)
        {
            if ((a.weld == b.weld) || (b.weld == c.weld) || (c.weld == a.weld))
                return;
            const Corner *k[3] = { &a, &b, &c };
            const Vector3d p0 = Vector3d(SnglVector3d(a.p)), p1 = Vector3d(SnglVector3d(b.p)), p2 = Vector3d(SnglVector3d(c.p));
            const Vector3d raw = cross(p1 - p0, p2 - p0);
            if (raw.length() == 0.0)
                return;
            const Vector3d face = raw.normalized() * data.orientation;
            Vector3d n[3] = { a.n, b.n, c.n };
            if (!a.exact && (Spread(n) > kMeshSpread * maxAngle))
            {
                const DBL uc = (a.u + b.u + c.u) / 3.0, vc = (a.v + b.v + c.v) / 3.0;
                for (int i = 0; i < 3; ++i)
                    if (k[i]->lattice)
                    {
                        const DBL u = ev.WrapU(k[i]->u + kMeshNudge * (uc - k[i]->u)), v = ev.WrapV(k[i]->v + kMeshNudge * (vc - k[i]->v));
                        Jet J;
                        ev.Eval(u, v, J);
                        const Vector3d inside = SurfaceNormal(data, ev, u, v, J);
                        if (Angle(inside, n[i]) > maxAngle)
                            n[i] = inside;
                    }
                if (Spread(n) > kMeshCrease * maxAngle)
                    n[0] = n[1] = n[2] = face;
            }
            const int order[2][3] = { { 0, 1, 2 }, { 0, 2, 1 } };
            const int *o = order[dot(raw, a.exact ? a.n : face) > 0.0 ? 1 : 0];
            for (int i = 0; i < 3; ++i)
                out.triangles.push_back(Vertex(*k[o[i]], n[o[i]]));
            for (int e = 0; e < 3; ++e)
                edges.push_back(std::make_pair(std::min(k[e]->weld, k[(e + 1) % 3]->weld), std::max(k[e]->weld, k[(e + 1) % 3]->weld)));
        }

        static DBL Spread(const Vector3d *n) { return std::max(Angle(n[0], n[1]), std::max(Angle(n[1], n[2]), Angle(n[2], n[0]))); }

        int Vertex(const Corner& c, const Vector3d& normal)
        {
            const SnglVector3d n(normal);
            const SNGL bits[3] = { n[X], n[Y], n[Z] };
            VertexKey key{ c.id, { 0, 0, 0 } };
            std::memcpy(key.n, bits, sizeof(key.n));
            auto found = vertices.find(key);
            if (found != vertices.end())
                return found->second;
            const int index = int(out.points.size());
            vertices[key] = index;
            out.points.push_back(SnglVector3d(c.p));
            out.normals.push_back(n);
            out.uvs.push_back(Vector2d(c.u, c.v));
            return index;
        }

        const SkeinData& data;
        Evaluator ev;
        const DBL minSize, maxAngle;
        SkeinMesh& out;
        int Nu = 0, Nv = 0;
        bool pole[2] = { false, false }, sealed[2] = { false, false };
        std::uint64_t unique = 0;
        std::unordered_map<std::uint64_t, Sample> cache;
        std::unordered_map<int, std::vector<int>> rows, cols;
        std::unordered_map<VertexKey, int, VertexHash> vertices;
        std::vector<std::pair<std::uint64_t, std::uint64_t>> edges;
};

}
// end of anonymous namespace

SkeinValue& SkeinValue::operator=(const SkeinValue& other)
{
    kind = other.kind;
    constant = other.constant;
    function = other.function;
    inputs = other.inputs;
    map = other.map;
    path = other.path;
    sum = other.sum ? std::make_shared<SkeinSum>(*other.sum) : nullptr;
    slot = other.slot;
    return *this;
}

SkeinStep& SkeinStep::operator=(const SkeinStep& other)
{
    kind = other.kind;
    affine = other.affine;
    axis = other.axis;
    along = other.along;
    side = other.side;
    offset = other.offset;
    origin = other.origin;
    for (int k = 0; k < 3; ++k)
        value[k] = other.value[k];
    curve = other.curve ? std::make_shared<SkeinAxis>(*other.curve) : nullptr;
    target = other.target ? std::make_shared<SkeinAxis>(*other.target) : nullptr;
    blend = other.blend ? std::make_shared<SkeinBlend>(*other.blend) : nullptr;
    roll = other.roll;
    normal = other.normal;
    alongV = other.alongV;
    edge = other.edge;
    wrapIn = other.wrapIn;
    wrapOut = other.wrapOut;
    return *this;
}

void SkeinStep::Hinge(const Vector3d& direction)
{
    side = direction.normalized();
    Vector3d normal = Vector3d(0.0, 0.0, 1.0) - side * side[Z];
    if (normal.length() < 1.0e-6)
        normal = Vector3d(1.0, 0.0, 0.0) - side * side[X];
    axis = normal.normalized();
    along = cross(side, axis);
}

bool SkeinValue::ReadsNormal() const
{
    if (kind == kMap)
        return (map->inputs >> kNX) != 0;
    if (kind == kFunction)
        for (unsigned char input : inputs)
            if (input >= kNX)
                return true;
    if (kind == kSum)
        for (const SkeinValue& e : sum->entries)
            if (e.ReadsNormal())
                return true;
    return false;
}

std::string SkeinPath::Build(const std::vector<Point>& points, Interpolation interpolation)
{
    raw = points;
    interp = interpolation;
    const int n = int(points.size());
    if (n < 2)
        return "skein path: at least two points are needed.";
    const int segments = closed ? n : n - 1;
    knots.assign(segments + 1, 0.0);
    const bool given = points[0].haveParameter;
    for (int i = 0; i < n; ++i)
    {
        if (points[i].haveParameter != given)
            return "skein path: give a parameter value for every point or for none.";
        knots[i] = given ? points[i].parameter : DBL(i) / segments;
        if ((i > 0) && !(knots[i] > knots[i - 1]))
            return "skein path: parameter values must increase.";
    }
    if (closed)
    {
        if (given && ((knots[0] != 0.0) || (knots[n - 1] >= 1.0)))
            return "skein path: a closed path's parameter values start at 0 and stay below 1, where it closes.";
        knots[n] = 1.0;
    }
    const DBL period = knots[segments] - knots[0];
    auto P = [&](int i) -> Vector3d { return points[((i % n) + n) % n].point; };
    auto K = [&](int i) -> DBL { return closed ? knots[((i % n) + n) % n] + period * floor(DBL(i) / n) : knots[i]; };

    std::vector<Vector3d> slope(n), curve(n, Vector3d(0.0));
    if (interpolation == kCubic)
        for (int i = 0; i < n; ++i)
        {
            const int a = (closed || (i > 0)) ? i - 1 : i, b = (closed || (i < n - 1)) ? i + 1 : i;
            slope[i] = (P(b) - P(a)) / (K(b) - K(a));
        }
    if (interpolation == kNatural)
    {
        std::vector<std::vector<DBL>> A(n, std::vector<DBL>(n, 0.0));
        std::vector<Vector3d> B(n, Vector3d(0.0));
        for (int i = 0; i < n; ++i)
        {
            if (!closed && ((i == 0) || (i == n - 1)))
            {
                A[i][i] = 1.0;
                continue;
            }
            const DBL hp = K(i) - K(i - 1), hn = K(i + 1) - K(i);
            A[i][((i - 1) % n + n) % n] += hp / 6.0;
            A[i][i] += (hp + hn) / 3.0;
            A[i][(i + 1) % n] += hn / 6.0;
            B[i] = (P(i + 1) - P(i)) / hn - (P(i) - P(i - 1)) / hp;
        }
        for (int i = 0; i < n; ++i)
        {
            int p = i;
            for (int r = i + 1; r < n; ++r)
                if (fabs(A[r][i]) > fabs(A[p][i]))
                    p = r;
            std::swap(A[i], A[p]);
            std::swap(B[i], B[p]);
            if (A[i][i] == 0.0)
                return "skein path: the natural spline through these points is singular.";
            for (int r = i + 1; r < n; ++r)
            {
                const DBL f = A[r][i] / A[i][i];
                for (int c = i; c < n; ++c)
                    A[r][c] -= f * A[i][c];
                B[r] -= B[i] * f;
            }
        }
        for (int i = n - 1; i >= 0; --i)
        {
            Vector3d s = B[i];
            for (int c = i + 1; c < n; ++c)
                s -= curve[c] * A[i][c];
            curve[i] = s / A[i][i];
        }
    }

    control.assign(4 * segments, Vector3d(0.0));
    for (int i = 0; i < segments; ++i)
    {
        const Vector3d a = P(i), b = P(i + 1);
        const DBL h = K(i + 1) - K(i);
        Vector3d ma, mb;
        switch ((interpolation == kQuadratic) && (n < 3) ? kLinear : interpolation)
        {
            case kCubic:
                ma = slope[i];
                mb = slope[(i + 1) % n];
                break;
            case kNatural:
                ma = (b - a) / h - (curve[i] * 2.0 + curve[(i + 1) % n]) * (h / 6.0);
                mb = (b - a) / h + (curve[i] + curve[(i + 1) % n] * 2.0) * (h / 6.0);
                break;
            case kQuadratic:
            {
                const int j = (closed || (i >= 1)) ? i : 1;
                const DBL t0 = K(j - 1), t1 = K(j), t2 = K(j + 1);
                auto derivative = [&](DBL t) -> Vector3d
                {
                    return P(j - 1) * ((2.0 * t - t1 - t2) / ((t0 - t1) * (t0 - t2))) +
                           P(j) * ((2.0 * t - t0 - t2) / ((t1 - t0) * (t1 - t2))) +
                           P(j + 1) * ((2.0 * t - t0 - t1) / ((t2 - t0) * (t2 - t1)));
                };
                ma = derivative(K(i));
                mb = derivative(K(i + 1));
                break;
            }
            default:
                ma = mb = (b - a) / h;
                break;
        }
        const Point& pa = points[i];
        const Point& pb = points[(i + 1) % n];
        control[4 * i] = a;
        control[4 * i + 1] = pa.haveOut ? a + pa.out : a + ma * (h / 3.0);
        control[4 * i + 2] = pb.haveIn ? b + pb.in : b - mb * (h / 3.0);
        control[4 * i + 3] = b;
    }
    return std::string();
}

void SkeinPath::UseArcLength()
{
    const int n = int(knots.size()) - 1, cells = std::max(8, kArcCells / n);
    params.clear();
    lengths.clear();
    speeds.clear();
    DBL before = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const DBL h = knots[i + 1] - knots[i];
        for (int m = (i == 0 ? 0 : 1); m <= cells; ++m)
        {
            const DBL t = knots[i] + h * m / cells;
            Vector3d p, d1, d2;
            Piece(i, t, p, d1, d2);
            const DBL left = std::max(d1.length(), 1.0e-12);
            DBL right = left;
            if ((m == cells) && (i + 1 < n))
            {
                Piece(i + 1, t, p, d1, d2);
                right = std::max(d1.length(), 1.0e-12);
            }
            DBL length = 0.0;
            if (!lengths.empty())
            {
                Piece(i, t - 0.5 * h / cells, p, d1, d2);
                length = lengths.back() + h / cells / 6.0 * (before + 4.0 * d1.length() + left);
            }
            params.push_back(t);
            lengths.push_back(length);
            speeds.push_back(left);
            speeds.push_back(right);
            before = right;
        }
    }
    total = lengths.back();
}

DBL SkeinPath::ArcParameter(DBL t, DBL& d1, DBL& d2) const
{
    const DBL a = Start(), rate = total / (End() - a), target = (t - a) * rate;
    const int last = int(lengths.size()) - 2;
    const int j = std::min(last, std::max(0, int(std::upper_bound(lengths.begin(), lengths.end(), target) - lengths.begin()) - 1));
    const DBL span = lengths[j + 1] - lengths[j], s = span > 0.0 ? (target - lengths[j]) / span : 0.0;
    const DBL t0 = params[j], t1 = params[j + 1], m0 = span / speeds[2 * j + 1], m1 = span / speeds[2 * j + 2];
    const DBL s2 = s * s, s3 = s2 * s;
    const DBL value = t0 * (2 * s3 - 3 * s2 + 1) + m0 * (s3 - 2 * s2 + s) + t1 * (3 * s2 - 2 * s3) + m1 * (s3 - s2);
    const DBL ds = 6 * (s2 - s) * t0 + m0 * (3 * s2 - 4 * s + 1) + 6 * (s - s2) * t1 + m1 * (3 * s2 - 2 * s);
    const DBL dss = (12 * s - 6) * t0 + m0 * (6 * s - 4) + (6 - 12 * s) * t1 + m1 * (6 * s - 2);
    const DBL k = span > 0.0 ? rate / span : 0.0;
    d1 = ds * k;
    d2 = dss * k * k;
    return value;
}

void SkeinPath::Piece(int i, DBL t, Vector3d& p, Vector3d& d1, Vector3d& d2) const
{
    const DBL h = knots[i + 1] - knots[i], x = (t - knots[i]) / h, y = 1.0 - x;
    const Vector3d *c = &control[4 * i];
    p = c[0] * (y * y * y) + c[1] * (3.0 * y * y * x) + c[2] * (3.0 * y * x * x) + c[3] * (x * x * x);
    d1 = ((c[1] - c[0]) * (y * y) + (c[2] - c[1]) * (2.0 * y * x) + (c[3] - c[2]) * (x * x)) * (3.0 / h);
    d2 = ((c[2] - c[1] * 2.0 + c[0]) * y + (c[3] - c[2] * 2.0 + c[1]) * x) * (6.0 / (h * h));
}

void SkeinPath::Segment(DBL t, Vector3d& p, Vector3d& d1, Vector3d& d2) const
{
    const int n = int(knots.size()) - 1;
    const int i = std::min(n - 1, std::max(0, int(std::upper_bound(knots.begin(), knots.end(), t) - knots.begin()) - 1));
    Piece(i, t, p, d1, d2);
}

void SkeinPath::Evaluate(DBL t, Vector3d& p, Vector3d& d1, Vector3d& d2) const
{
    const DBL a = Start(), b = End();
    bool clamped = false;
    if (closed)
        t -= (b - a) * floor((t - a) / (b - a));
    else if ((t < a) || (t > b))
    {
        t = t < a ? a : b;
        clamped = true;
    }
    if (lengths.empty())
        Segment(t, p, d1, d2);
    else
    {
        DBL ds, dss;
        Segment(ArcParameter(t, ds, dss), p, d1, d2);
        d2 = d2 * (ds * ds) + d1 * dss;
        d1 *= ds;
    }
    if (clamped)
        d1 = d2 = Vector3d(0.0);
}

void SkeinPath::Hull(DBL x0, DBL x1, Vector3d& lo, Vector3d& hi) const
{
    if (!lengths.empty())
    {
        const DBL a = Start(), rate = total / (End() - a);
        const int last = int(params.size()) - 1;
        const int k0 = int(std::upper_bound(lengths.begin(), lengths.end(), (x0 - a) * rate) - lengths.begin()) - 1;
        const int k1 = int(std::lower_bound(lengths.begin(), lengths.end(), (x1 - a) * rate) - lengths.begin());
        x0 = params[std::min(last, std::max(0, k0 - 1))];
        x1 = params[std::min(last, std::max(0, k1 + 1))];
    }
    const int n = int(knots.size()) - 1;
    for (int i = 0; i < n; ++i)
    {
        if ((knots[i + 1] < x0) || (knots[i] > x1))
            continue;
        const DBL h = knots[i + 1] - knots[i];
        const DBL p[2] = { std::max(0.0, (x0 - knots[i]) / h), std::min(1.0, (x1 - knots[i]) / h) };
        const Vector3d *c = &control[4 * i];
        for (int m = 0; m < 4; ++m)
        {
            const DBL s[3] = { p[m > 2], p[m > 1], p[m > 0] };
            Vector3d b[3];
            for (int j = 0; j < 3; ++j)
                b[j] = c[j] * (1.0 - s[0]) + c[j + 1] * s[0];
            for (int j = 0; j < 2; ++j)
                b[j] = b[j] * (1.0 - s[1]) + b[j + 1] * s[1];
            const Vector3d q = b[0] * (1.0 - s[2]) + b[1] * s[2];
            for (int k = 0; k < 3; ++k)
            {
                lo[k] = std::min(lo[k], q[k]);
                hi[k] = std::max(hi[k], q[k]);
            }
        }
    }
}

void SkeinPath::Bound(DBL ta, DBL tb, Vector3d& lo, Vector3d& hi) const
{
    lo = Vector3d(BOUND_HUGE);
    hi = Vector3d(-BOUND_HUGE);
    if (ta > tb)
        std::swap(ta, tb);
    const DBL a = Start(), b = End(), period = b - a;
    if (!closed)
        Hull(std::min(b, std::max(a, ta)), std::min(b, std::max(a, tb)), lo, hi);
    else if (tb - ta >= period)
        Hull(a, b, lo, hi);
    else
    {
        const DBL s = ta - period * floor((ta - a) / period), e = s + (tb - ta);
        Hull(s, std::min(b, e), lo, hi);
        if (e > b)
            Hull(a, e - period, lo, hi);
    }
    const DBL pad = 1.0e-12 * (1.0 + std::max(fabs(lo[X]), fabs(hi[X])) + std::max(fabs(lo[Y]), fabs(hi[Y])) + std::max(fabs(lo[Z]), fabs(hi[Z])));
    lo -= Vector3d(pad);
    hi += Vector3d(pad);
}

bool SkeinPath::Straight(Vector3d& origin, Vector3d& direction) const
{
    if (closed || (interp != kLinear) || (raw.size() < 2))
        return false;
    origin = raw.front().point;
    direction = raw.back().point - origin;
    const DBL span = direction.length();
    if (span < EPSILON)
        return false;
    for (const Point& q : raw)
        if (q.haveIn || q.haveOut || (cross(q.point - origin, direction).length() > 1.0e-12 * span * span))
            return false;
    return true;
}

/// A cubic Hermite piece: the value at `x` in 0..1 from end values and end slopes, with its own slope.
static DBL Hermite(DBL p0, DBL p1, DBL m0, DBL m1, DBL x, DBL& slope)
{
    const DBL x2 = x * x, x3 = x2 * x;
    slope = 6.0 * (x2 - x) * p0 + m0 * (3.0 * x2 - 4.0 * x + 1.0) + 6.0 * (x - x2) * p1 + m1 * (3.0 * x2 - 2.0 * x);
    return p0 * (2.0 * x3 - 3.0 * x2 + 1.0) + m0 * (x3 - 2.0 * x2 + x) + p1 * (3.0 * x2 - 2.0 * x3) + m1 * (x3 - x2);
}

DBL SkeinAxis::Distance(DBL t, DBL& rate) const
{
    rate = 1.0;
    if (line || arc.empty())
        return t;
    const int n = int(arc.size()) - 1;
    const DBL h = (t1 - t0) / n, period = closed ? t1 - t0 : 0.0;
    DBL turns = 0.0;
    if (closed)
    {
        turns = floor((t - t0) / period);
        t -= turns * period;
    }
    else if ((t < t0) || (t > t1))
    {
        rate = 0.0;
        return t < t0 ? 0.0 : length;
    }
    const DBL x = (t - t0) / h;
    const int j = std::min(n - 1, std::max(0, int(x)));
    DBL slope;
    const DBL s = Hermite(arc[j], arc[j + 1], h * rates[j], h * rates[j + 1], x - j, slope);
    rate = slope / h;
    return s + turns * length;
}

DBL SkeinAxis::Parameter(DBL s, DBL& rate) const
{
    rate = 1.0;
    if (line || arc.empty())
        return s;
    const int n = int(arc.size()) - 1;
    DBL turns = 0.0;
    if (closed && (length > 0.0))
    {
        turns = floor(s / length);
        s -= turns * length;
    }
    else if ((s < 0.0) || (s > length))
    {
        rate = 0.0;
        return s < 0.0 ? t0 : t1;
    }
    const int j = std::min(n - 1, std::max(0, int(std::upper_bound(arc.begin(), arc.end(), s) - arc.begin()) - 1));
    const DBL span = arc[j + 1] - arc[j], h = (t1 - t0) / n;
    if (!(span > 0.0))
        return t0 + h * j + turns * (t1 - t0);
    DBL slope;
    const DBL t = Hermite(t0 + h * j, t0 + h * (j + 1), span / rates[j], span / rates[j + 1], (s - arc[j]) / span, slope);
    rate = slope / span;
    return t + turns * (t1 - t0);
}

DBL SkeinRoll::Section(DBL phi, Vector3d c[3]) const
{
    Vector3d w[3], p, d1, d2;
    travel->Evaluate(phi / kTau, p, d1, d2);
    const Vector3d frame[3] = { T, A, N };
    for (int k = 0; k < 3; ++k)
    {
        w[0][k] = dot(p - start, frame[k]);
        w[1][k] = dot(d1, frame[k]) / kTau;
        w[2][k] = dot(d2, frame[k]) / (kTau * kTau);
    }
    const DBL r = radius + w[0][Z], r1 = w[1][Z], r2 = w[2][Z], c0 = cos(phi), s0 = sin(phi);
    c[0] = Vector3d(w[0][X], w[0][Y] + r * s0, r * (1.0 - c0));
    c[1] = Vector3d(w[1][X], w[1][Y] + r1 * s0 + r * c0, r1 * (1.0 - c0) + r * s0);
    c[2] = Vector3d(w[2][X], w[2][Y] + r2 * s0 + 2.0 * r1 * c0 - r * s0, r2 * (1.0 - c0) + 2.0 * r1 * s0 + r * c0);
    return r;
}

void SkeinImage::Build()
{
    lows.assign(1, grey);
    highs.assign(1, grey);
    for (int w = width, h = height; (w > 1) || (h > 1);)
    {
        const int nw = (w + 1) / 2, nh = (h + 1) / 2;
        const std::vector<float>& lo = lows.back();
        const std::vector<float>& hi = highs.back();
        std::vector<float> l(size_t(nw) * nh, 1.0e30f), m(size_t(nw) * nh, -1.0e30f);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                float& a = l[size_t(y / 2) * nw + x / 2];
                float& b = m[size_t(y / 2) * nw + x / 2];
                a = std::min(a, lo[size_t(y) * w + x]);
                b = std::max(b, hi[size_t(y) * w + x]);
            }
        lows.push_back(l);
        highs.push_back(m);
        w = nw;
        h = nh;
    }
}

DBL SkeinImage::Sample(DBL x, DBL y, DBL& dx, DBL& dy) const
{
    const DBL W = width, H = height;
    const DBL xc = wrap(fmod(x * W, W) + EPSILON, W) + 0.5, yc = wrap(-(fmod(y * H, H) + EPSILON), H) + 0.5;
    const int ix = int(xc), iy = int(yc);
    auto at = [this](int i, int j) -> DBL { return grey[size_t((j % height + height) % height) * width + (i % width + width) % width]; };
    const DBL p = xc - ix, q = yc - iy;
    DBL value = 0.0, dp = 0.0, dq = 0.0;
    if (!bicubic)
    {
        const DBL g00 = at(ix, iy), g10 = at(ix - 1, iy), g01 = at(ix, iy - 1), g11 = at(ix - 1, iy - 1);
        value = p * q * g00 + (1.0 - p) * q * g10 + p * (1.0 - q) * g01 + (1.0 - p) * (1.0 - q) * g11;
        dp = q * (g00 - g10) + (1.0 - q) * (g01 - g11);
        dq = p * g00 + (1.0 - p) * g10 - p * g01 - (1.0 - p) * g11;
    }
    else
    {
        DBL fx[4], fy[4], gx[4], gy[4];
        for (int axis = 0; axis < 2; ++axis)
        {
            const DBL s = axis == 0 ? p : q, t = 1.0 - s;
            DBL *f = axis == 0 ? fx : fy, *g = axis == 0 ? gx : gy;
            f[0] = -0.5 * s * t * t;
            f[1] = 0.5 * t * (t * (3.0 * s + 1.0) + 1.0);
            f[2] = 0.5 * s * (s * (3.0 * t + 1.0) + 1.0);
            f[3] = -0.5 * t * s * s;
            g[0] = -0.5 * t * (t - 2.0 * s);
            g[1] = 0.5 * (3.0 * t * t - 6.0 * s * t - 2.0 * t - 1.0);
            g[2] = 0.5 * (6.0 * s * t - 3.0 * s * s + 2.0 * s + 1.0);
            g[3] = 0.5 * s * (s - 2.0 * t);
        }
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
            {
                const DBL g = at(ix + i - 2, iy + j - 2);
                value += fx[i] * fy[j] * g;
                dp += gx[i] * fy[j] * g;
                dq += fx[i] * gy[j] * g;
            }
    }
    if ((value <= 0.0) || (value >= 1.0))
    {
        dx = dy = 0.0;
        return value <= 0.0 ? 0.0 : 1.0;
    }
    dx = W * dp;
    dy = -H * dq;
    return value;
}

void SkeinImage::Range(DBL x0, DBL x1, DBL y0, DBL y1, DBL& lo, DBL& hi) const
{
    lo = 0.0;
    hi = 1.0;
    const int reach = bicubic ? 2 : 1;
    const DBL ends[2][2] = { { x0 * width + EPSILON + 0.5, x1 * width + EPSILON + 0.5 }, { -y1 * height - EPSILON + 0.5, -y0 * height - EPSILON + 0.5 } };
    long first[2], last[2];
    for (int axis = 0; axis < 2; ++axis)
    {
        const long n = axis == 0 ? width : height;
        if (!(fabs(ends[axis][0]) < 1.0e9) || !(fabs(ends[axis][1]) < 1.0e9))
            return;
        first[axis] = long(floor(ends[axis][0])) - reach;
        last[axis] = long(floor(ends[axis][1])) + reach - 1;
        if (last[axis] - first[axis] + 1 >= n)
        {
            first[axis] = 0;
            last[axis] = n - 1;
        }
    }
    int level = 0;
    while ((level + 1 < int(lows.size())) && (((last[0] - first[0]) >> level) > 4 || ((last[1] - first[1]) >> level) > 4))
        ++level;
    const int lw = (width + (1 << level) - 1) >> level;
    DBL a = 1.0e30, b = -1.0e30;
    for (long j = first[1]; j <= last[1];)
    {
        const long jw = ((j % height) + height) % height, bj = jw >> level;
        for (long i = first[0]; i <= last[0];)
        {
            const long iw = ((i % width) + width) % width, bi = iw >> level;
            a = std::min(a, DBL(lows[level][size_t(bj) * lw + bi]));
            b = std::max(b, DBL(highs[level][size_t(bj) * lw + bi]));
            i += std::min(long(width), (bi + 1) << level) - iw;
        }
        j += std::min(long(height), (bj + 1) << level) - jw;
    }
    if (bicubic)
    {
        const DBL overshoot = 0.28125 * (b - a);
        a -= overshoot;
        b += overshoot;
    }
    lo = std::max(0.0, a);
    hi = std::min(1.0, b);
}

Skein::Skein() : ObjectBase(BASIC_OBJECT)
{
    Trans = Create_Transform();
}

Skein::~Skein()
{}

ObjectPtr Skein::Copy()
{
    Skein *New = new Skein();
    Destroy_Transform(New->Trans);
    *New = *this;
    New->Trans = Copy_Transform(Trans);
    return New;
}

std::string Skein::Prepare(TraceThreadData *thread)
{
    SkeinData& d = *data;
    d.slots.clear();
    d.functionCount = 0;
    d.readsNormal = d.resamples = false;
    d.lastNormal = -1;
    std::function<bool(SkeinValue&)> assign = [&](SkeinValue& v) -> bool
    {
        v.slot = -1;
        if (v.kind == SkeinValue::kSum)
            for (SkeinValue& e : v.sum->entries)
                if (!assign(e))
                    return false;
        if (v.kind != SkeinValue::kFunction)
            return true;
        if (d.functionCount == kMaxFunctions)
            return false;
        v.slot = d.functionCount++;
        d.slots.push_back(&v);
        return true;
    };
    bool full = false;
    std::function<bool(std::vector<SkeinStep>&, unsigned char, bool)> visit = [&](std::vector<SkeinStep>& steps, unsigned char end, bool topLevel) -> bool
    {
        unsigned char flags = end;
        for (int i = int(steps.size()) - 1; i >= 0; --i)
        {
            steps[i].wrapOut = flags;
            if (steps[i].kind == SkeinStep::kEnvelope)
                flags &= (unsigned char)~(steps[i].alongV ? 2 : 1);
            steps[i].wrapIn = flags;
        }
        bool reads = false;
        for (int i = 0; i < int(steps.size()); ++i)
        {
            SkeinStep& s = steps[i];
            s.normal = false;
            for (SkeinValue& v : s.value)
            {
                s.normal = s.normal || v.ReadsNormal();
                full = !assign(v) || full;
            }
            bool nested = false;
            if (s.blend)
                for (SkeinBlend::Entry& e : s.blend->entries)
                    nested = visit(e.steps, s.wrapOut, false) || nested;
            d.resamples = d.resamples || (s.kind == SkeinStep::kSample) || (s.kind == SkeinStep::kEnvelope);
            d.readsNormal = d.readsNormal || (s.kind == SkeinStep::kEnvelope);
            if (s.normal || (s.kind == SkeinStep::kDisplace) || nested)
            {
                reads = true;
                d.readsNormal = true;
                if (topLevel)
                    d.lastNormal = i;
            }
        }
        return reads;
    };
    visit(d.steps, (unsigned char)((d.wrapU ? 1 : 0) | (d.wrapV ? 2 : 0)), true);
    if (full)
        return Format("skein: at most %g function values per skein.", kMaxFunctions);
    Evaluator ev(d, thread);
    std::string failure;
    std::function<void(std::vector<SkeinStep>&, const Scope&)> frames = [&](std::vector<SkeinStep>& steps, const Scope& sc)
    {
        for (int i = 0; i < int(steps.size()); ++i)
        {
            SkeinStep& s = steps[i];
            if (s.curve)
            {
                if (s.curve->source)
                    s.curve->path = ev.PlacedPath(*s.curve->source, sc, i);
                ev.BuildFrames(*s.curve);
            }
            if (s.target)
            {
                if (s.target->source)
                    s.target->path = ev.PlacedPath(*s.target->source, sc, i);
                ev.BuildFrames(*s.target);
                ev.MatchTarget(*s.curve, *s.target);
            }
            if ((s.kind == SkeinStep::kCurl) && failure.empty())
                failure = ev.BuildRoll(s, sc, i);
            if (s.blend)
                for (SkeinBlend::Entry& e : s.blend->entries)
                {
                    const Scope inner{ e.steps, &sc, i, s.wrapOut };
                    frames(e.steps, inner);
                }
        }
    };
    frames(d.steps, ev.Top());
    if (!failure.empty())
        return failure;
    Jet J;

    Vector3d lo(BOUND_HUGE), hi(-BOUND_HUGE);
    for (int j = 0; j <= 32; ++j)
        for (int i = 0; i <= 32; ++i)
        {
            ev.Eval(i / 32.0, j / 32.0, J);
            for (int k = 0; k < 3; ++k)
            {
                lo[k] = std::min(lo[k], J.p[k]);
                hi[k] = std::max(hi[k], J.p[k]);
            }
        }
    d.size = std::max((hi - lo).length(), 1.0e-9);
    const Vector3d middle = (lo + hi) * 0.5;

    for (int index = 0; index < int(d.steps.size()); ++index)
        if ((d.steps[index].kind == SkeinStep::kEnvelope) && (d.steps[index].edge == SkeinStep::kNoEdge))
        {
            const DBL gap = ev.EnvelopeGap(ev.Top(), index);
            if (gap > 1.0e-6 * d.size)
                return Format("skein envelope: the thickness is %g where its faces meet; taper it to zero there, or add edge round or edge flat.", gap);
        }

    for (int axis = 0; axis < 2; ++axis)
    {
        if (!(axis == 0 ? d.wrapU : d.wrapV))
            continue;
        DBL gap = 0.0, where = 0.0;
        for (int i = 0; i <= 8; ++i)
        {
            Jet A, B;
            const DBL s = i / 8.0;
            ev.Eval(axis == 0 ? 0.0 : s, axis == 0 ? s : 0.0, A);
            ev.Eval(axis == 0 ? 1.0 : s, axis == 0 ? s : 1.0, B);
            if ((A.p - B.p).length() > gap)
            {
                gap = (A.p - B.p).length();
                where = s;
            }
        }
        if (gap > 1.0e-6 * d.size)
            return Format(axis == 0 ? "skein: closed u, but the seam from u = 0 to u = 1 is open by %g at v = %g." :
                                      "skein: closed v, but the seam from v = 0 to v = 1 is open by %g at u = %g.", gap, where);
    }

    d.gridU = std::min(kMaxGrid, std::max(d.wrapU ? 4 : 2, int(ceil(NormalTurn(ev, true) / kPatchTurn))));
    d.gridV = std::min(kMaxGrid, std::max(d.wrapV ? 4 : 2, int(ceil(NormalTurn(ev, false) / kPatchTurn))));

    const int nf = d.functionCount;
    const std::vector<DBL> linesU = GridLines(ev, true, d.gridU, d.resamples), linesV = GridLines(ev, false, d.gridV, d.resamples);
    d.patches.clear();
    d.ranges.assign(size_t(2 * nf) * d.gridU * d.gridV, 0.0);
    d.provenBounds = true;
    for (int j = 0; j < d.gridV; ++j)
        for (int i = 0; i < d.gridU; ++i)
        {
            SkeinData::Patch p;
            p.u0 = linesU[i];
            p.u1 = linesU[i + 1];
            p.v0 = linesV[j];
            p.v1 = linesV[j + 1];
            p.ranges = int(2 * nf * d.patches.size());
            DBL *range = d.ranges.data() + p.ranges;
            d.provenBounds = ev.Ranges(p.u0, p.u1, p.v0, p.v1, 5, range) && d.provenBounds;
            ev.Box(p.u0, p.u1, p.v0, p.v1, range, p.lo, p.hi);
            p.flat = Flatness(ev, p);
            d.patches.push_back(p);
        }
    d.nodes.clear();
    BuildTree(d, 0, d.gridU, 0, d.gridV);

    DBL flux = 0.0;
    for (int j = 0; j < 64; ++j)
        for (int i = 0; i < 64; ++i)
        {
            ev.Eval((i + 0.5) / 64.0, (j + 0.5) / 64.0, J);
            flux += dot(J.p - middle, cross(J.pu, J.pv));
        }
    d.orientation = flux >= 0.0 ? 1.0 : -1.0;

    for (int e = 0; e < 2; ++e)
    {
        SkeinData::Cap& cap = d.caps[e];
        cap = SkeinData::Cap();
        cap.v = DBL(e);
        if (!d.wrapU || d.wrapV || (d.ends[e] == SkeinData::kOpen))
            continue;
        if (d.ends[e] == SkeinData::kSealed)
        {
            DBL gap = 0.0;
            for (int i = 0; i <= 16; ++i)
            {
                Jet A, B;
                ev.Eval(i / 32.0, cap.v, A);
                ev.Eval(1.0 - i / 32.0, cap.v, B);
                gap = std::max(gap, (A.p - B.p).length());
            }
            if (gap > 1.0e-6 * d.size)
                return Format("skein: ends sealed at v = %g, but the end ring does not fold onto itself (open by %g); an envelope's thickness must reach zero there.", cap.v, gap);
            continue;
        }
        std::vector<Vector3d> ring(kRingSamples);
        Vector3d centre(0.0), along(0.0);
        for (int i = 0; i < kRingSamples; ++i)
        {
            ev.Eval(DBL(i) / kRingSamples, cap.v, J);
            ring[i] = J.p;
            centre += J.p;
            along += J.pv;
        }
        centre /= DBL(kRingSamples);
        DBL extent = 0.0;
        Vector3d normal(0.0);
        for (int i = 0; i < kRingSamples; ++i)
        {
            extent = std::max(extent, (ring[i] - centre).length());
            normal += cross(ring[i] - centre, ring[(i + 1) % kRingSamples] - centre);
        }
        if (d.ends[e] == SkeinData::kPole)
        {
            if (extent > 1.0e-6 * d.size)
                return Format("skein: ends pole at v = %g, but the end ring does not close to a point (it spans %g).", cap.v, 2.0 * extent);
            continue;
        }
        if (extent <= 1.0e-6 * d.size)
            continue;
        normal.normalize();
        DBL warp = 0.0;
        for (const Vector3d& r : ring)
            warp = std::max(warp, fabs(dot(r - centre, normal)));
        if (warp > 1.0e-7 * d.size)
            return Format("skein: ends flat at v = %g, but the end ring is not planar (off its plane by %g).", cap.v, warp);
        if ((dot(normal, along) > 0.0) == (e == 0))
            normal = -normal;
        cap.present = true;
        cap.centre = centre;
        cap.normal = normal;
        cap.e1 = ring[0] - centre;
        cap.e1 -= normal * dot(cap.e1, normal);
        if (cap.e1.length() == 0.0)
            return Format("skein: ends flat at v = %g, but the end ring starts at its own centre.", cap.v);
        cap.e1.normalize();
        cap.e2 = cross(normal, cap.e1);
        cap.angles.assign(kRingSamples + 1, 0.0);
        DBL last = 0.0;
        for (int i = 1; i <= kRingSamples; ++i)
        {
            const Vector3d q = ring[i % kRingSamples] - centre;
            const DBL a = atan2(dot(q, cap.e2), dot(q, cap.e1));
            cap.angles[i] = cap.angles[i - 1] + WrapAngle(a - last);
            last = a;
        }
        cap.turn = cap.angles[kRingSamples] > 0.0 ? 1.0 : -1.0;
        for (int i = 0; i < kRingSamples; ++i)
            if (cap.turn * (cap.angles[i + 1] - cap.angles[i]) <= 0.0)
                return Format("skein: ends flat at v = %g needs an end ring that winds once around its centre (a star-shaped section); it turns back near u = %g.", cap.v, DBL(i) / kRingSamples);
        if (fabs(fabs(cap.angles[kRingSamples]) - kTau) > 1.0e-6)
            return Format("skein: ends flat at v = %g needs an end ring that winds once around its centre; it winds %g times.", cap.v, cap.angles[kRingSamples] / kTau);
        for (const SkeinData::Patch& p : d.patches)
        {
            if ((e == 0) ? (p.v0 > 0.0) : (p.v1 < 1.0))
                continue;
            for (int k = 0; k < 8; ++k)
            {
                const Vector3d corner((k & 1) ? p.hi[X] : p.lo[X], (k & 2) ? p.hi[Y] : p.lo[Y], (k & 4) ? p.hi[Z] : p.lo[Z]);
                cap.reach = std::max(cap.reach, (corner - centre).length());
            }
        }
    }

    d.closed = (d.wrapU && d.wrapV) || (d.wrapU && (d.ends[0] != SkeinData::kOpen) && (d.ends[1] != SkeinData::kOpen));
    return std::string();
}

std::string Skein::Tessellate(TraceThreadData *thread, DBL minSize, DBL maxAngle, SkeinMesh& mesh) const
{
    Mesher mesher(*data, thread, minSize, maxAngle, mesh);
    return mesher.Run();
}

int Skein::FindCrossings(const Vector3d& P, const Vector3d& D, DBL tMin, Crossing *out, TraceThreadData *thread) const
{
    if (!data || data->nodes.empty())
        return 0;
    Evaluator ev(*data, thread);
    Solver solver(*data, ev, P, D, tMin, out, thread->Stats());
    return solver.Run();
}

bool Skein::All_Intersections(const Ray& ray, IStack& Depth_Stack, TraceThreadData *Thread)
{
    Thread->Stats()[Ray_Skein_Tests]++;
    Vector3d P, D;
    MInvTransPoint(P, ray.Origin, Trans);
    MInvTransDirection(D, ray.Direction, Trans);
    const DBL len = D.length();
    D /= len;

    Crossing crossings[kMaxCrossings];
    const int n = FindCrossings(P, D, kDepthTolerance * len, crossings, Thread);
    const bool closed = data->closed;
    int winding = 0;
    if (closed)
        for (int i = 0; i < n; ++i)
            winding += crossings[i].sign;

    bool found = false;
    for (int i = 0; i < n; ++i)
    {
        const int after = winding - crossings[i].sign;
        const bool boundary = !closed || ((winding != 0) != (after != 0));
        winding = after;
        const DBL depth = crossings[i].t / len;
        if (!boundary || (depth >= MAX_DISTANCE))
            continue;
        const Vector3d IPoint = ray.Evaluate(depth);
        if (Clip.empty() || Point_In_Clip(IPoint, Clip, Thread))
        {
            Vector3d N;
            MTransNormal(N, crossings[i].normal, Trans);
            N.normalize();
            Depth_Stack->push(Intersection(depth, IPoint, N, Vector2d(crossings[i].u, crossings[i].v), this));
            found = true;
        }
    }
    if (found)
        Thread->Stats()[Ray_Skein_Tests_Succeeded]++;
    return found;
}

bool Skein::Inside(const Vector3d& IPoint, TraceThreadData *Thread) const
{
    if (!data->closed)
        return false;
    Vector3d P;
    MInvTransPoint(P, IPoint, Trans);
    const SkeinData::Node& root = data->nodes[0];
    bool inside = false;
    if ((P[X] >= root.lo[X]) && (P[X] <= root.hi[X]) && (P[Y] >= root.lo[Y]) && (P[Y] <= root.hi[Y]) &&
        (P[Z] >= root.lo[Z]) && (P[Z] <= root.hi[Z]))
    {
        Crossing crossings[kMaxCrossings];
        const int n = FindCrossings(P, kInsideDirection.normalized(), 0.0, crossings, Thread);
        int winding = 0;
        for (int i = 0; i < n; ++i)
            winding += crossings[i].sign;
        inside = (winding != 0);
    }
    return inside != (Test_Flag(this, INVERTED_FLAG) != 0);
}

void Skein::Normal(Vector3d& Result, Intersection *Inter, TraceThreadData *Thread) const
{
    Result = Inter->INormal;
}

void Skein::UVCoord(Vector2d& Result, const Intersection *Inter) const
{
    Result = Inter->Iuv;
}

void Skein::Translate(const Vector3d&, const TRANSFORM *tr)
{
    Transform(tr);
}

void Skein::Rotate(const Vector3d&, const TRANSFORM *tr)
{
    Transform(tr);
}

void Skein::Scale(const Vector3d&, const TRANSFORM *tr)
{
    Transform(tr);
}

void Skein::Transform(const TRANSFORM *tr)
{
    if (Trans == nullptr)
        Trans = Create_Transform();
    Compose_Transforms(Trans, tr);
    Compute_BBox();
}

void Skein::Compute_BBox()
{
    if (!data || data->nodes.empty())
    {
        Make_BBox(BBox, -BOUND_HUGE / 2.0, -BOUND_HUGE / 2.0, -BOUND_HUGE / 2.0, BOUND_HUGE, BOUND_HUGE, BOUND_HUGE);
        return;
    }
    const SkeinData::Node& root = data->nodes[0];
    Make_BBox(BBox, root.lo[X], root.lo[Y], root.lo[Z], root.hi[X] - root.lo[X], root.hi[Y] - root.lo[Y], root.hi[Z] - root.lo[Z]);
    Recompute_BBox(&BBox, Trans);
}

}
// end of namespace pov
