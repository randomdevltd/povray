//******************************************************************************
///
/// @file core/shape/spheresweep.cpp
///
/// Implementation of the sphere sweep geometric primitive.
///
/// Idea: J.J. Van Wijk, "Raytracing Objects Defined by Sweeping a Sphere",
/// Eurographics 1984.
///
/// @author Jochen Lippert
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

/*****************************************************************************
*
*  Version history (most recent changes first):
*
*  24. Feb. 1998: Fixed a bug in Compute_Sphere_Sweep_BBox that could
*                 result in much too large bounding boxes for
*                 Catmull-Rom-Spline Sphere Sweeps. Added statistics
*                 output for Sphere Sweep primitive.
*
*  19. Jan. 1998: Corrected a problem with the calculation of single
*                 spheres in Compute_Sphere_Sweep which I introduced
*                 in the previous version, and cleaned up the code in
*                 this function a bit. All_Sphere_Sweep_Intersections
*                 now uses QSORT to sort the intersection list.
*
*  6.  Jan. 1998: Fixed a bug that affected the geometry of
*                 Catmull-Rom-Spline Sphere Sweeps, fixed a bug in
*                 Inside_Sphere_Sweep which affected non-proportionally
*                 scaled Sphere Sweeps.
*
*  22. Dec. 1997: Simplyfied code for finding invalid intersections,
*                 fixed some bugs related to this.
*
*  8.  Dec. 1997: Some cleanup & bug fixes.
*
*  1.  Dec. 1997: Made depth tolerance user-selectable.
*
*  29. Nov. 1997: Added support for Catmull-Rom-Splines.
*
*  26. Nov. 1997: Fixed a bug where the wrong value was returned by
*                 All_Sphere_Sweep_Intersections when the clipped_by
*                 statement was used with a sphere sweep.
*
*  24. Nov. 1997: Fixed a pretty silly bug in Copy_Sphere_Sweep where
*                 the sphere sweep wasn't copied completely.
*
*  21. Nov. 1997: Origin release.
*
******************************************************************************/

// Unit header file must be the first file included within POV-Ray *.cpp files (pulls in config)
#include "core/shape/spheresweep.h"
#include "core/math/intersectiondepth.h"

// C++ variants of C standard header files
#include <cstdlib>

// C++ standard header files
#include <algorithm>
#include <vector>
#include <cmath>
#include <limits>

// POV-Ray header files (base module)
#include "base/povassert.h"

// POV-Ray header files (core module)
#include "core/bounding/boundingbox.h"
#include "core/math/matrix.h"
#include "core/math/polynomialsolver.h"
#include "core/render/ray.h"
#include "core/scene/tracethreaddata.h"
#include "core/support/statistics.h"

// this must be the last file included
#include "base/povdebug.h"

namespace pov
{

/*****************************************************************************
* Local preprocessor defines
******************************************************************************/

const DBL DEPTH_TOLERANCE       = 1.0e-6;
const DBL ZERO_TOLERANCE        = 1.0e-4;
const DBL RADIUS_TOLERANCE      = 1.0e-6;

// TODO - Technically this should be `(Num_Spheres + Num_Segments * order) * 2` [CLi]
#define SPHSWEEP_MAX_ISECT  ((Num_Spheres + Num_Segments) * 2 + 64)



// A cubic segment is the union of the spheres |p - c(u)| <= |r(u)| for u in [0,1]; see doc/PERF.md.
namespace
{

const int kSweepMaxRoots = 32;
const int kSweepMaxDepth = 44;

// Ascending monomial coefficients on [0,1] with the size its terms reach before they cancel.
struct SweepPoly final
{
    int degree;
    double coef[11];
    double size;
    double At(double u) const
    {
        double value = coef[degree];
        for (int k = degree - 1; k >= 0; --k) value = value * u + coef[k];
        return value;
    }
    double Slope(double u) const
    {
        double value = degree * coef[degree];
        for (int k = degree - 1; k >= 1; --k) value = value * u + k * coef[k];
        return value;
    }
    // Rounding of a value from these terms.
    double Floor() const { return 64.0 * std::numeric_limits<double>::epsilon() * size; }
};

struct SweepBinomials final
{
    double choose[11][11];
    SweepBinomials()
    {
        for (int n = 0; n <= 10; ++n)
            for (int k = 0; k <= 10; ++k)
                choose[n][k] = (k > n) ? 0.0 : ((k == 0 || k == n) ? 1.0 : choose[n - 1][k - 1] + choose[n - 1][k]);
    }
};
const SweepBinomials sweepBinomials;

void SweepBernstein(const SweepPoly& poly, double* bernstein)
{
    const int n = poly.degree;
    for (int i = 0; i <= n; ++i)
    {
        double sum = 0.0;
        for (int k = 0; k <= i; ++k)
            sum += sweepBinomials.choose[i][k] / sweepBinomials.choose[n][k] * poly.coef[k];
        bernstein[i] = sum;
    }
}

void SweepSplit(const double* bernstein, int n, double* left, double* right)
{
    double work[11];
    for (int i = 0; i <= n; ++i) work[i] = bernstein[i];
    for (int level = 0; level <= n; ++level)
    {
        left[level] = work[0];
        right[n - level] = work[n - level];
        for (int i = 0; i < n - level; ++i) work[i] = 0.5 * (work[i] + work[i + 1]);
    }
}

// Every root of poly in [lo,hi], given its Bernstein form there. A span whose control values change sign once holds one
// root; more changes split it; values within rounding of zero carry no sign, and a middle value there is itself a root.
void SweepRoots(const SweepPoly& poly, const double* bernstein, double lo, double hi, int depth, double* roots, int& count)
{
    const int n = poly.degree;
    const double floor = poly.Floor();
    int changes = 0, last = 0;
    for (int i = 0; i <= n; ++i)
    {
        const int sign = (bernstein[i] > floor) ? 1 : ((bernstein[i] < -floor) ? -1 : 0);
        if (sign == 0) continue;
        if (last != 0 && sign != last) ++changes;
        last = sign;
    }
    if (changes == 0 || count >= kSweepMaxRoots) return;
    if (changes == 1 && std::abs(bernstein[0]) > floor && std::abs(bernstein[n]) > floor)
    {
        double a = lo, b = hi, u = 0.5 * (lo + hi);
        const bool rising = bernstein[0] < 0.0;
        for (int step = 0; step < 200 && b > a; ++step)
        {
            const double value = poly.At(u);
            if (std::abs(value) <= floor) break;
            if ((value > 0.0) == rising) b = u; else a = u;
            const double slope = poly.Slope(u);
            double next = (slope != 0.0) ? u - value / slope : a;
            if (!(next > a && next < b)) next = 0.5 * (a + b);
            if (next == u || !(next > a && next < b)) break;
            u = next;
        }
        roots[count++] = u;
        return;
    }
    const double middle = 0.5 * (lo + hi);
    if (depth >= kSweepMaxDepth)
    {
        roots[count++] = middle;
        return;
    }
    double left[11], right[11];
    SweepSplit(bernstein, n, left, right);
    SweepRoots(poly, left, lo, middle, depth + 1, roots, count);
    if (std::abs(right[0]) <= floor && count < kSweepMaxRoots) roots[count++] = middle;
    SweepRoots(poly, right, middle, hi, depth + 1, roots, count);
}

int SweepRoots(const SweepPoly& poly, double* roots)
{
    double bernstein[11];
    int count = 0;
    SweepBernstein(poly, bernstein);
    SweepRoots(poly, bernstein, 0.0, 1.0, 0, roots, count);
    return count;
}

// Whether poly reaches zero or below in the span of its Bernstein form.
bool SweepReachesZero(const double* bernstein, int n, int depth)
{
    if (bernstein[0] <= 0.0 || bernstein[n] <= 0.0) return true;
    bool below = false;
    for (int i = 1; i < n; ++i) below = below || (bernstein[i] <= 0.0);
    if (!below || depth >= kSweepMaxDepth) return false;
    double left[11], right[11];
    SweepSplit(bernstein, n, left, right);
    return SweepReachesZero(left, n, depth + 1) || SweepReachesZero(right, n, depth + 1);
}

// |centre(u) - from|^2 - radius(u)^2 with `along` projected out of the offset when given.
SweepPoly SweepDistance(const SPHSWEEP_SEG& segment, const Vector3d& from, const Vector3d* along, SweepPoly* depth)
{
    Vector3d offset[4];
    double length[4];
    SweepPoly result;
    result.degree = 6;
    result.size = 0.0;
    if (depth != nullptr) { depth->degree = 3; depth->size = 0.0; }
    for (int k = 0; k < 4; ++k)
    {
        offset[k] = (k == 0) ? segment.Center_Coef[0] - from : segment.Center_Coef[k];
        if (along != nullptr)
        {
            const double part = dot(offset[k], *along);
            offset[k] -= *along * part;
            depth->coef[k] = part;
            depth->size += std::abs(part);
        }
        length[k] = offset[k].length();
    }
    for (int m = 0; m <= 6; ++m) result.coef[m] = 0.0;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
        {
            result.coef[i + j] += dot(offset[i], offset[j]) - segment.Radius_Coef[i] * segment.Radius_Coef[j];
            result.size += length[i] * length[j] + std::abs(segment.Radius_Coef[i] * segment.Radius_Coef[j]);
        }
    return result;
}

struct SweepSpan final
{
    double enter, leave, enterU, leaveU;
    int enterSegment, leaveSegment;
};

// The stretches of the ray inside one cubic segment. Sphere u covers depth(u) -+ sqrt(-gap(u)) where the ray passes
// within its radius; over each stretch of u where it does, those intervals join into one, from the least entry to the
// greatest exit, both among the ends of the stretch and the roots of 4 depth'^2 gap + gap'^2.
int SweepSegmentSpans(const BasicRay& ray, const SPHSWEEP_SEG& segment, int index, SweepSpan* spans)
{
    Vector3d middle = segment.Center_Coef[0];
    for (int k = 1; k < 4; ++k) middle += segment.Center_Coef[k] * std::ldexp(1.0, -k);
    const double base = dot(middle - ray.Origin, ray.Direction);
    const Vector3d near = ray.Origin + ray.Direction * base;
    SweepPoly depth;
    const SweepPoly gap = SweepDistance(segment, near, &ray.Direction, &depth);
    double breaks[kSweepMaxRoots + 2];
    int count = SweepRoots(gap, breaks + 1);
    if (count == 0 && !(gap.At(0.5) < 0.0)) return 0;
    std::sort(breaks + 1, breaks + 1 + count);
    breaks[0] = 0.0;
    breaks[count + 1] = 1.0;

    // 4 depth'^2 gap + gap'^2, whose roots are where an entry or an exit depth is stationary
    SweepPoly turn;
    turn.degree = 10;
    turn.size = 0.0;
    for (int m = 0; m <= 10; ++m) turn.coef[m] = 0.0;
    double slope[3], square[5] = {0.0, 0.0, 0.0, 0.0, 0.0}, squareSize = 0.0, gapSlope[6];
    for (int k = 0; k < 3; ++k) slope[k] = (k + 1) * depth.coef[k + 1];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
        {
            square[i + j] += slope[i] * slope[j];
            squareSize += std::abs(slope[i] * slope[j]);
        }
    for (int k = 0; k < 6; ++k) gapSlope[k] = (k + 1) * gap.coef[k + 1];
    for (int i = 0; i < 5; ++i)
        for (int j = 0; j <= 6; ++j)
            turn.coef[i + j] += 4.0 * square[i] * gap.coef[j];
    for (int i = 0; i < 6; ++i)
        for (int j = 0; j < 6; ++j)
            turn.coef[i + j] += gapSlope[i] * gapSlope[j];
    turn.size = 4.0 * squareSize * gap.size + Sqr(6.0 * gap.size);
    double turns[kSweepMaxRoots];
    int turnCount = -1;

    int found = 0;
    for (int piece = 0; piece <= count; ++piece)
    {
        const double lo = breaks[piece], hi = breaks[piece + 1];
        if (!(hi > lo) || !(gap.At(0.5 * (lo + hi)) < 0.0)) continue;
        if (turnCount < 0) turnCount = SweepRoots(turn, turns);
        SweepSpan span = {HUGE_VAL, -HUGE_VAL, lo, lo, index, index};
        auto offer = [&](double u)
        {
            const double reach = std::sqrt(std::max(0.0, -gap.At(u))), centre = depth.At(u);
            if (centre - reach < span.enter) { span.enter = centre - reach; span.enterU = u; }
            if (centre + reach > span.leave) { span.leave = centre + reach; span.leaveU = u; }
        };
        offer(lo);
        offer(hi);
        for (int k = 0; k < turnCount; ++k)
            if (turns[k] > lo && turns[k] < hi) offer(turns[k]);
        if (!(span.leave > span.enter)) continue;
        span.enter += base;
        span.leave += base;
        spans[found++] = span;
    }
    return found;
}

}
// end of anonymous namespace

// Precise linear sweeps are convex hulls of their two endpoint balls; see doc/PERF.md.
static int IntersectPreciseLinearSweep(const BasicRay& ray, const SPHSWEEP_SEG& segment, SPHSWEEP_INT* intersections)
{
    long double v[3], b[3], d[3];
    long double vv = 0.0L, bb = 0.0L, dd = 0.0L, vb = 0.0L, vd = 0.0L, db = 0.0L;
    for (int axis = 0; axis < 3; ++axis)
    {
        v[axis] = static_cast<long double>(ray.Origin[axis]) - segment.Center_Coef[0][axis];
        b[axis] = segment.Center_Coef[1][axis];
        d[axis] = ray.Direction[axis];
        vv += v[axis] * v[axis]; bb += b[axis] * b[axis]; dd += d[axis] * d[axis];
        vb += v[axis] * b[axis]; vd += v[axis] * d[axis]; db += d[axis] * b[axis];
    }
    const long double radius = segment.Radius_Coef[0], change = segment.Radius_Coef[1];
    const long double a = bb - change * change, h = vb + radius * change;
    struct Candidate { long double distance, position; } candidates[6];
    int count = 0;
    auto roots = [](long double qa, long double qb, long double qc, long double (&result)[2])
    {
        if (qa == 0.0L)
        {
            if (qb == 0.0L) return false;
            result[0] = result[1] = -qc / qb;
            return true;
        }
        long double discriminant = qb * qb - 4.0L * qa * qc;
        const long double error = 64.0L * std::numeric_limits<long double>::epsilon() *
            (qb * qb + std::abs(4.0L * qa * qc));
        if (discriminant < -error) return false;
        discriminant = std::max(0.0L, discriminant);
        const long double q = -0.5L * (qb + std::copysign(std::sqrt(discriminant), qb));
        if (q == 0.0L) result[0] = result[1] = -qb / (2.0L * qa);
        else { result[0] = q / qa; result[1] = qc / q; }
        return true;
    };
    long double distances[2];
    for (int endpoint = 0; endpoint < 2; ++endpoint)
    {
        const long double r = radius + endpoint * change;
        const long double oc2 = vv - 2.0L * endpoint * vb + endpoint * bb;
        if (roots(dd, 2.0L * (vd - endpoint * db), oc2 - r * r, distances))
            for (long double t : distances) candidates[count++] = {t, static_cast<long double>(endpoint)};
    }
    if (a > 0.0L && roots(a * dd - db * db, 2.0L * (a * vd - h * db), a * (vv - radius * radius) - h * h, distances))
        for (long double t : distances)
        {
            const long double u = (h + db * t) / a;
            if (u >= 0.0L && u <= 1.0L) candidates[count++] = {t, u};
        }
    if (count < 2) return 0;
    std::sort(candidates, candidates + count, [](const Candidate& x, const Candidate& y) { return x.distance < y.distance; });
    for (int side = 0; side < 2; ++side)
    {
        const auto& candidate = candidates[side == 0 ? 0 : count - 1];
        intersections[side].t = static_cast<double>(candidate.distance);
        if (!IsFiniteIntersectionDepth(intersections[side].t)) return 0;
        intersections[side].Point = ray.Evaluate(intersections[side].t);
        for (int axis = 0; axis < 3; ++axis)
            intersections[side].Normal[axis] = static_cast<double>(v[axis] + d[axis] * candidate.distance - b[axis] * candidate.position);
        if (!(intersections[side].Normal.lengthSqr() > 0.0))
            throw POV_EXCEPTION_STRING("Precise linear sphere-sweep transport encountered a singular zero-radius tip.");
        intersections[side].Normal.normalize();
    }
    return 2;
}

/*****************************************************************************
* Local variables
******************************************************************************/

const MATRIX Catmull_Rom_Matrix =
{
    { 0.0 / 2.0,  2.0 / 2.0,  0.0 / 2.0,  0.0 / 2.0},
    {-1.0 / 2.0,  0.0 / 2.0,  1.0 / 2.0,  0.0 / 2.0},
    { 2.0 / 2.0, -5.0 / 2.0,  4.0 / 2.0, -1.0 / 2.0},
    {-1.0 / 2.0,  3.0 / 2.0, -3.0 / 2.0,  1.0 / 2.0}
};

const MATRIX B_Matrix =
{
    { 1.0 / 6.0,  4.0 / 6.0,  1.0 / 6.0,  0.0 / 6.0},
    {-3.0 / 6.0,  0.0 / 6.0,  3.0 / 6.0,  0.0 / 6.0},
    { 3.0 / 6.0, -6.0 / 6.0,  3.0 / 6.0,  0.0 / 6.0},
    {-1.0 / 6.0,  3.0 / 6.0, -3.0 / 6.0,  1.0 / 6.0}
};



/*****************************************************************************
*
* FUNCTION
*
*   All_Sphere_Sweep_Intersections
*
* INPUT
*
*   Object, Ray, Depth stack
*
* OUTPUT
*
*   Depth stack
*
* RETURNS
*
*   Boolean - found any intersections?
*
* AUTHOR
*
*   Jochen Lippert
*
* DESCRIPTION
*
*   Find all intersections of ray with a sphere sweep.
*
* CHANGES
*
*   -
*
******************************************************************************/

bool SphereSweep::All_Intersections(const Ray& ray, IStack& Depth_Stack, TraceThreadData *Thread)
{
    static thread_local std::vector<SPHSWEEP_INT> IsectBuffer;
    static thread_local std::vector<SPHSWEEP_INT> SegmentBuffer;
    if (IsectBuffer.size() < size_t(SPHSWEEP_MAX_ISECT))
        IsectBuffer.resize(SPHSWEEP_MAX_ISECT);
    if (SegmentBuffer.size() < size_t(12 * Num_Segments))
        SegmentBuffer.resize(12 * Num_Segments);
    SPHSWEEP_INT    *Isect = IsectBuffer.data();
    SPHSWEEP_INT    *Segment_Isect = SegmentBuffer.data();
    BasicRay        New_Ray;
    DBL             len;
    bool            Intersection_Found = false;
    int             Num_Isect = 0;
    int             Num_Seg_Isect;
    int             i, j;

    Thread->Stats()[Ray_Sphere_Sweep_Tests]++;

    if (Trans == nullptr)
    {
        New_Ray.Origin = ray.Origin;
        New_Ray.Direction = ray.Direction;
    }
    else
    {
        MInvTransRay(New_Ray, ray, Trans);

        len = New_Ray.Direction.length();
        New_Ray.Direction /= len;
    }

    if (Interpolation != LINEAR_SPHERE_SWEEP)
    {
        // The ray's stretches inside each segment, joined where they overlap; each joined stretch enters and leaves once.
        static thread_local std::vector<SweepSpan> spans;
        spans.resize(size_t(Num_Segments) * (kSweepMaxRoots + 1));
        int count = 0;
        for (i = 0; i < Num_Segments; i++)
            count += SweepSegmentSpans(New_Ray, Segment[i], i, spans.data() + count);
        std::sort(spans.begin(), spans.begin() + count, [](const SweepSpan& a, const SweepSpan& b) { return a.enter < b.enter; });
        auto push = [&](double depth, int segment, double u, bool leaving)
        {
            const SPHSWEEP_SEG& part = Segment[segment];
            Vector3d point = New_Ray.Evaluate(depth);
            Vector3d centre = part.Center_Coef[3];
            for (int k = 2; k >= 0; --k) centre = centre * u + part.Center_Coef[k];
            Vector3d normal = point - centre;
            const double length = normal.length();
            if (length > 0.0) normal /= length;
            else normal = leaving ? New_Ray.Direction : -New_Ray.Direction;
            if (Trans != nullptr)
            {
                depth /= len;
                MTransPoint(point, point, Trans);
                MTransNormal(normal, normal, Trans);
                normal.normalize();
            }
            if ((depth > Depth_Tolerance) && (Clip.empty() || Point_In_Clip(point, Clip, Thread)))
            {
                Depth_Stack->push(Intersection(depth, point, normal, this));
                Intersection_Found = true;
            }
        };
        for (i = 0; i < count; )
        {
            SweepSpan run = spans[i];
            for (++i; i < count && spans[i].enter <= run.leave; ++i)
                if (spans[i].leave > run.leave)
                {
                    run.leave = spans[i].leave;
                    run.leaveU = spans[i].leaveU;
                    run.leaveSegment = spans[i].leaveSegment;
                }
            push(run.enter, run.enterSegment, run.enterU, false);
            push(run.leave, run.leaveSegment, run.leaveU, true);
        }
        if (Intersection_Found)
            Thread->Stats()[Ray_Sphere_Sweep_Tests_Succeeded]++;
        return Intersection_Found;
    }

    const bool preciseLinear = Num_Segments > 0 &&
        ray.GetRootQuery() && ray.GetRootQuery()->PreciseBoundary();
    // Intersections with single spheres
    for(i = 0; !preciseLinear && i < Num_Spheres; i++)
    {
        // Test for end of vector
        if (Num_Isect + 2 <= SPHSWEEP_MAX_ISECT)
        {
            // Are there intersections with this sphere?
            if (Intersect_Sphere(New_Ray, &Sphere[i], Isect + Num_Isect))
                Num_Isect += 2;
        }
    }

    // Intersections with segments
    for(i = 0; i < Num_Segments; i++)
    {
        // Are there intersections with this segment?
        Num_Seg_Isect = preciseLinear ? IntersectPreciseLinearSweep(New_Ray, Segment[i], Segment_Isect) :
            Intersect_Segment(New_Ray, &Segment[i], Segment_Isect, Thread->Stats());

        // Test for end of vector
        if(Num_Isect + Num_Seg_Isect <= SPHSWEEP_MAX_ISECT)
        {
            for (j = 0; j < Num_Seg_Isect; j++)
            {
                // Add intersection
                Isect[Num_Isect] = Segment_Isect[j];
                Num_Isect++;
            }
        }
    }

    // Any intersections?
    if(Num_Isect > 0)
    {
        // Sort intersections
        std::qsort(Isect, Num_Isect, sizeof(SPHSWEEP_INT), Comp_Isects);

        // Delete invalid intersections inside the sphere sweep
        Num_Isect = Find_Valid_Points(Isect, Num_Isect, New_Ray);

        // Push valid intersections
        for (i = 0; i < Num_Isect; i++)
        {
            // Was the ray transformed?
            if (Trans != nullptr)
            {
                // Yes, invert the transformation
                Isect[i].t /= len;
                MTransPoint(Isect[i].Point, Isect[i].Point, Trans);
                MTransNormal(Isect[i].Normal, Isect[i].Normal, Trans);
                Isect[i].Normal.normalize();
            }

            // Check for positive values of t (it's a ray after all...)
            if(Isect[i].t > Depth_Tolerance)
            {
                // Test for clipping volume
                if (Clip.empty() || Point_In_Clip(Isect[i].Point, Clip, Thread))
                {
                    Depth_Stack->push(Intersection(Isect[i].t, Isect[i].Point, Isect[i].Normal, this));
                    Intersection_Found = true;
                }
            }
        }

        if(Intersection_Found)
            Thread->Stats()[Ray_Sphere_Sweep_Tests_Succeeded]++;
    }

    return Intersection_Found;
}



/*****************************************************************************
*
* FUNCTION
*
*   Intersect_Sphere_Sweep_Sphere
*
* INPUT
*
*   Ray     - Ray to test intersection with
*   Sphere  - Sphere
*   Inter   - Intersections (two element vector)
*
* OUTPUT
*
*   -
*
* RETURNS
*
*   Boolean - is there at least one intersection?
*
* AUTHOR
*
*   Jochen Lippert
*
* DESCRIPTION
*
*   Find intersections of ray (line in fact) with a sphere.
*
* CHANGES
*
*   -
*
******************************************************************************/

bool SphereSweep::Intersect_Sphere(const BasicRay &ray, const SPHSWEEP_SPH *Sphere, SPHSWEEP_INT *Inter)
{
    DBL         Radius2;
    DBL         OCSquared;
    DBL         t_Closest_Approach;
    DBL         Half_Chord;
    DBL         t_Half_Chord_Squared;
    Vector3d    Origin_To_Center;

    Radius2 = Sqr(Sphere->Radius);

    Origin_To_Center = Sphere->Center - ray.Origin;

    OCSquared = Origin_To_Center.lengthSqr();

    t_Closest_Approach = dot(Origin_To_Center, ray.Direction);

    if((OCSquared >= Radius2) && (t_Closest_Approach < EPSILON))
        return false;

    t_Half_Chord_Squared = Radius2 - OCSquared + Sqr(t_Closest_Approach);

    if(t_Half_Chord_Squared > EPSILON)
    {
        Half_Chord = sqrt(t_Half_Chord_Squared);

        // Calculate smaller depth
        Inter[0].t = t_Closest_Approach - Half_Chord;

        // Calculate point
        Inter[0].Point = ray.Evaluate(Inter[0].t);

        // Calculate normal
        Inter[0].Normal = (Inter[0].Point - Sphere->Center) / fabs(Sphere->Radius);

        // Calculate bigger depth
        Inter[1].t = t_Closest_Approach + Half_Chord;

        // Calculate point
        Inter[1].Point = ray.Evaluate(Inter[1].t);

        // Calculate normal
        Inter[1].Normal = (Inter[1].Point - Sphere->Center) / fabs(Sphere->Radius);

        return true;
    }

    return false;
}



/*****************************************************************************
*
* FUNCTION
*
*   Intersect_Sphere_Sweep_Segment
*
* INPUT
*
*   Ray     - Ray to test intersection with
*   Segment - Segment of a sphere sweep
*   Isect   - intersection list (depth, point, normal)
*
* OUTPUT
*
*   Isect   - intersection list (depth, point, normal)
*
* RETURNS
*
*   Number of intersections
*
* AUTHOR
*
*   Jochen Lippert
*
* DESCRIPTION
*
*   Find all intersections of a ray (line in fact) with one segment
*   of a sphere sweep.
*
* CHANGES
*
*   -
*
******************************************************************************/

/// @bug
///     The current implementation exhibits numeric instabilities for 3rd order polynomial splines.
///     (See GitHub issue #147.)
///
int SphereSweep::Intersect_Segment(const BasicRay &ray, const SPHSWEEP_SEG *Segment, SPHSWEEP_INT *Isect, RenderStatistics& stats)
{
    int             Isect_Count;
    DBL             Dot1, Dot2;
    DBL             t1, t2;
    Vector3d        Vector;
    Vector3d        IPoint;
    Vector3d        DCenter;
    DBL             DCenterDot;
    DBL             temp;
    DBL             b, c, d, e, f;
    DBL             Coef[3];
    DBL             Root[2];
    int             Num_Poly_Roots, m, n;
    DBL             fp0, fp1;
    DBL             t;
    SPHSWEEP_SPH    Temp_Sphere;

    Isect_Count = 0;

    // Calculate intersections with closing discs near the ends of the spline segment.
    // Note that the positions of the closing discs account for the rate of change in radius
    // at the respective end of the spline segment.

    // We need to compute these intersections even though we will inevitably discard them later,
    // because the algorithm downstream expects intersections to always come in pairs, one entering
    // the segment and the other one exiting it. The intersections with the closing discs will
    // take the place of any intersections we discard because they fall outside the interval [0,1].
    // (Also, if the spline happens to be parallel to the ray direction, we will get no roots
    // at all but still need to have these intersections.)

    // Closing disk near u=1.
    Dot1 = dot(ray.Direction, Segment->Center_Deriv[0]);
    if(fabs(Dot1) > EPSILON)
    {
        Vector = ray.Origin - Segment->Closing_Sphere[0].Center;
        Dot2 = dot(Vector, Segment->Center_Deriv[0]);
        t1 = -(Dot2 + Segment->Closing_Sphere[0].Radius * Segment->Radius_Deriv[0]) / Dot1;
        if (IsFiniteIntersectionDepth(t1))
        {
            // Calculate point
            IPoint = ray.Evaluate(t1);

            // Is the point inside the closing sphere?
            DCenter = IPoint - Segment->Closing_Sphere[0].Center;
            DCenterDot = DCenter.lengthSqr();
            if(DCenterDot < Sqr(Segment->Closing_Sphere[0].Radius))
            {
                // Add intersection
                Isect[Isect_Count].t = t1;

                // Copy point
                Isect[Isect_Count].Point = IPoint;

                // Calculate normal
                Isect[Isect_Count].Normal = -Segment->Center_Deriv[0].normalized();

                Isect_Count++;
            }
        }
    }

    // Closing disk near u=1.
    Dot1 = dot(ray.Direction, Segment->Center_Deriv[1]);
    if(fabs(Dot1) > EPSILON)
    {
        Vector = ray.Origin - Segment->Closing_Sphere[1].Center;
        Dot2 = dot(Vector, Segment->Center_Deriv[1]);
        t2 = -(Dot2 + Segment->Closing_Sphere[1].Radius * Segment->Radius_Deriv[1]) / Dot1;
        if(IsFiniteIntersectionDepth(t2))
        {
            // Calculate point
            IPoint = ray.Evaluate(t2);

            // Is the point inside the closing sphere?
            DCenter = IPoint - Segment->Closing_Sphere[1].Center;
            DCenterDot = DCenter.lengthSqr();
            if(DCenterDot < Sqr(Segment->Closing_Sphere[1].Radius))
            {
                // Add intersection
                Isect[Isect_Count].t = t2;

                // Copy point
                Isect[Isect_Count].Point = IPoint;

                // Calculate normal
                Isect[Isect_Count].Normal = Segment->Center_Deriv[1].normalized();

                Isect_Count++;
            }
        }
    }

    // Calculate intersections with sides of the segment

    switch (Segment->Num_Coefs)
    {
        case 2:   // First order Polynomial

            Vector = ray.Origin - Segment->Center_Coef[0];

            // a is always 1.0

            b = dot(Segment->Center_Coef[1], ray.Direction);
            b *= -2.0;

            c = dot(Vector, ray.Direction);
            c *= 2.0;

            d = Segment->Center_Coef[1].lengthSqr();
            d -= Sqr(Segment->Radius_Coef[1]);

            e = dot(Vector, Segment->Center_Coef[1]);
            e += Segment->Radius_Coef[0] * Segment->Radius_Coef[1];
            e *= -2.0;

            f = Vector.lengthSqr();
            f -= Sqr(Segment->Radius_Coef[0]);

            Coef[0] = 4.0 * Sqr(d) - Sqr(b) * d;
            Coef[1] = 4.0 * d * e - 2.0 * b * c * d;
            Coef[2] = Sqr(e) - b * c * e + Sqr(b) * f;

            Num_Poly_Roots = Solve_Polynomial(2, Coef, Root, true, 1e-10, stats);
            break;

        default:
            POV_SHAPE_ASSERT(false);
            break;
    }

    // Remove roots not in interval [0, 1].

    // Note that the corresponding intersections are either removed in balanced pairs, or are
    // balanced by intersections with the closing discs.

    m = 0;
    while(m < Num_Poly_Roots)
    {
        if(Root[m] < 0.0 || Root[m] > 1.0)
        {
            for(n = m; n < Num_Poly_Roots - 1; n++)
                Root[n] = Root[n + 1];
            Num_Poly_Roots--;
        }
        else
            m++;
    }

    for (m = 0; m < Num_Poly_Roots; m++)
    {
        // Calculate center and radius of sphere at root

        DBL rootPwr = 1;
        Temp_Sphere.Center = Segment->Center_Coef[0];
        Temp_Sphere.Radius = Segment->Radius_Coef[0];
        for (n = 1; n < Segment->Num_Coefs; ++n)
        {
            rootPwr *= Root[m];
            Temp_Sphere.Center += Segment->Center_Coef[n] * rootPwr;
            Temp_Sphere.Radius += Segment->Radius_Coef[n] * rootPwr;
        }

        switch (Segment->Num_Coefs)
        {
            case 2:

                fp0 = 2.0 * d * Root[m]
                    + e;
                fp1 = b;
                break;

            default:
                POV_SHAPE_ASSERT(false);
                break;
        }

        bool doubleIsect = (fabs(fp1) < ZERO_TOLERANCE);
        if (!doubleIsect)
        {
            // Looks like a single-intersection root at first glance

            t = -fp0 / fp1;

            if (IsFiniteIntersectionDepth(t))
            {
                // Calculate intersection point
                Isect[Isect_Count].Point = ray.Evaluate(t);

                // Calculate normal
                Isect[Isect_Count].Normal = (Isect[Isect_Count].Point - Temp_Sphere.Center).normalized();

                // Sanity-check intersection point for proper radius
                DBL rIsect = (Isect[Isect_Count].Point - Temp_Sphere.Center).length();
                DBL rRoot  = Temp_Sphere.Radius;

                // Check for numeric instability; this may (only?) happen if the root corresponds to two intersections
                doubleIsect = fabs(rIsect - rRoot) > RADIUS_TOLERANCE;
                if (!doubleIsect)
                {
                    // Add intersection
                    Isect[Isect_Count].t = t;
                    Isect_Count++;
                }
            }
        }

        if (doubleIsect)
        {
            // Calculate intersections
            if(Intersect_Sphere(ray, &Temp_Sphere, Isect + Isect_Count))
                Isect_Count += 2;
        }
    }

    return Isect_Count;
}



/*****************************************************************************
*
* FUNCTION
*
*   Inside_Sphere_Sweep
*
* INPUT
*
*   Point, Object
*
* OUTPUT
*
*   -
*
* RETURNS
*
*   Boolean - is the point inside the sphere sweep?
*
* AUTHOR
*
*   Jochen Lippert
*
* DESCRIPTION
*
*   Test if point is inside sphere sweep.
*
* CHANGES
*
*   -
*
******************************************************************************/

bool SphereSweep::Inside(const Vector3d& IPoint, TraceThreadData *Thread) const
{
    int     inside;
    Vector3d    New_Point;
    int     i, j;
    Vector3d    Vector;
    DBL     temp;
    DBL     Coef[3];
    DBL     Root[2];
    int     Num_Poly_Roots;

    inside = false;

    if (Trans == nullptr)
        New_Point = IPoint;
    else
        MInvTransPoint(New_Point, IPoint, Trans);

    switch(Interpolation)
    {
        case LINEAR_SPHERE_SWEEP:
            // For each segment...
            for(i = 0; i < Num_Segments; i++)
            {
                // Pre-calculate vector
                Vector = New_Point - Segment[i].Center_Coef[0];

                // Coefficient for u^2
                Coef[0] = Segment[i].Center_Coef[1].lengthSqr();
                Coef[0] -= Sqr(Segment[i].Radius_Coef[1]);

                // Coefficient for u^1
                Coef[1] = dot(Vector, Segment[i].Center_Coef[1]);
                Coef[1] += Segment[i].Radius_Coef[0]
                         * Segment[i].Radius_Coef[1];
                Coef[1] *= -2.0;

                // Coefficient for u^0
                Coef[2] = Vector.lengthSqr();
                Coef[2] -= Sqr(Segment[i].Radius_Coef[0]);

                // Find roots
                Num_Poly_Roots = Solve_Polynomial(2, Coef, Root, true, 1e-10, Thread->Stats());

                // Test for interval [0, 1]
                for(j = 0; j < Num_Poly_Roots; j++)
                {
                    if(Root[j] >= 0.0 && Root[j] <= 1.0)
                    {
                        // At least one root inside interval,
                        // so we are inside sphere sweep
                        inside = true;
                        break;
                    }
                }
            }
            break;
        case CATMULL_ROM_SPLINE_SPHERE_SWEEP:
        case B_SPLINE_SPHERE_SWEEP:
            // Inside where some sphere of a segment holds the point: its squared distance less radius reaches zero.
            for(i = 0; !inside && i < Num_Segments; i++)
            {
                double bernstein[11];
                const SweepPoly gap = SweepDistance(Segment[i], New_Point, nullptr, nullptr);
                SweepBernstein(gap, bernstein);
                inside = SweepReachesZero(bernstein, gap.degree, 0);
            }
            break;
    }

    if(Test_Flag(this, INVERTED_FLAG))
        inside = !inside;

    return inside;
}



/*****************************************************************************
*
* FUNCTION
*
*   Sphere_Sweep_Normal
*
* INPUT
*
*   Object, Intersection
*
* OUTPUT
*
*   Normal
*
* RETURNS
*
*   -
*
* AUTHOR
*
*   Jochen Lippert
*
* DESCRIPTION
*
*   Calculate the surface normal of a sphere sweep.
*
* CHANGES
*
*   -
*
******************************************************************************/

void SphereSweep::Normal(Vector3d& Result, Intersection *Inter, TraceThreadData *) const
{
    Result = Inter->INormal;
}



/*****************************************************************************
*
* FUNCTION
*
*   Copy_Sphere_Sweep
*
* INPUT
*
*   Object
*
* OUTPUT
*
*   -
*
* RETURNS
*
*   Copy of sphere sweep
*
* AUTHOR
*
*   Jochen Lippert
*
* DESCRIPTION
*
*   Copy a sphere sweep.
*
* CHANGES
*
*   -
*
******************************************************************************/

ObjectPtr SphereSweep::Copy()
{
    SphereSweep *New = new SphereSweep();
    int         i;

    Destroy_Transform(New->Trans);
    *New = *this;

    New->Segment = nullptr;
    New->Sphere = nullptr;
    New->Interpolation = Interpolation;

    New->Num_Modeling_Spheres = Num_Modeling_Spheres;
    New->Modeling_Sphere = reinterpret_cast<SPHSWEEP_SPH *>(POV_MALLOC(Num_Modeling_Spheres * sizeof(SPHSWEEP_SPH), "modeling sphere"));
    for(i = 0; i < New->Num_Modeling_Spheres; i++)
        New->Modeling_Sphere[i] = Modeling_Sphere[i];

    New->Depth_Tolerance = Depth_Tolerance;

    New->Compute();

    New->Trans = Copy_Transform(Trans);

    return New;
}



/*****************************************************************************
*
* FUNCTION
*
*   Translate_Sphere_Sweep
*
* INPUT
*
*   Object, Vector, Transformation
*
* OUTPUT
*
*   Object
*
* RETURNS
*
*   -
*
* AUTHOR
*
*   Jochen Lippert
*
* DESCRIPTION
*
*   Translate a sphere sweep.
*
* CHANGES
*
*   -
*
******************************************************************************/

void SphereSweep::Translate(const Vector3d& Vector, const TRANSFORM *tr)
{
    if (Trans == nullptr)
    {
        for(int i = 0; i < Num_Modeling_Spheres; i++)
            Modeling_Sphere[i].Center += Vector;
        Compute();
        Compute_BBox();
    }
    else
        Transform(tr);
}



/*****************************************************************************
*
* FUNCTION
*
*   Rotate_Sphere_Sweep
*
* INPUT
*
*   Object, Vector, Transformation
*
* OUTPUT
*
*   Object
*
* RETURNS
*
*   -
*
* AUTHOR
*
*   Jochen Lippert
*
* DESCRIPTION
*
*   Rotate a sphere sweep.
*
* CHANGES
*
*   -
*
******************************************************************************/

void SphereSweep::Rotate(const Vector3d&, const TRANSFORM *tr)
{
    if (Trans == nullptr)
    {
        for (int i = 0; i < Num_Modeling_Spheres; i++)
            MTransPoint(Modeling_Sphere[i].Center, Modeling_Sphere[i].Center, tr);
        Compute();
        Compute_BBox();
    }
    else
        Transform(tr);
}



/*****************************************************************************
*
* FUNCTION
*
*   Scale_Sphere_Sweep
*
* INPUT
*
*   Object, Vector, Transformation
*
* OUTPUT
*
*   Object
*
* RETURNS
*
*   -
*
* AUTHOR
*
*   Jochen Lippert
*
* DESCRIPTION
*
*   Scale a sphere sweep.
*
* CHANGES
*
*   -
*
******************************************************************************/

void SphereSweep::Scale(const Vector3d& Vector, const TRANSFORM *tr)
{
    if((Vector[X] != Vector[Y]) || (Vector[X] != Vector[Z]))
    {
        if (Trans == nullptr)
            Trans = Create_Transform();
    }

    if (Trans == nullptr)
    {
        for(int i = 0; i < Num_Modeling_Spheres; i++)
        {
            Modeling_Sphere[i].Center *= Vector[X];
            Modeling_Sphere[i].Radius *= Vector[X];
        }
        Compute();
        Compute_BBox();
    }
    else
        Transform(tr);
}



/*****************************************************************************
*
* FUNCTION
*
*   Create_Sphere_Sweep
*
* INPUT
*
*   -
*
* OUTPUT
*
*   -
*
* RETURNS
*
*   Sphere_Sweep
*
* AUTHOR
*
*   Jochen Lippert
*
* DESCRIPTION
*
*   Create a new, "empty" sphere sweep (no modeling spheres).
*
* CHANGES
*
*   -
*
******************************************************************************/

SphereSweep::SphereSweep() : ObjectBase(SPHERE_SWEEP_OBJECT)
{
    Interpolation = -1;

    Num_Modeling_Spheres = 0;
    Modeling_Sphere = nullptr;

    Num_Spheres = 0;
    Sphere = nullptr;

    Num_Segments = 0;
    Segment = nullptr;

    Depth_Tolerance = DEPTH_TOLERANCE;

    Trans = nullptr;
}



/*****************************************************************************
*
* FUNCTION
*
*   Transform_Sphere_Sweep
*
* INPUT
*
*   Object, Transformation
*
* OUTPUT
*
*   Object
*
* RETURNS
*
*   -
*
* AUTHOR
*
*   Jochen Lippert
*
* DESCRIPTION
*
*   Transform a sphere sweep.
*
* CHANGES
*
*   -
*
******************************************************************************/

void SphereSweep::Transform(const TRANSFORM *tr)
{
    if (Trans == nullptr)
        Trans = Create_Transform();

    Compose_Transforms(Trans, tr);

    Compute_BBox();
}



/*****************************************************************************
*
* FUNCTION
*
*   Destroy_Sphere_Sweep
*
* INPUT
*
*   Object
*
* OUTPUT
*
*   -
*
* RETURNS
*
*   -
*
* AUTHOR
*
*   Jochen Lippert
*
* DESCRIPTION
*
*   Free memory allocated for a sphere sweep.
*
* CHANGES
*
*   -
*
******************************************************************************/

SphereSweep::~SphereSweep()
{
    POV_FREE(Modeling_Sphere);
    POV_FREE(Sphere);
    POV_FREE(Segment);
}



/*****************************************************************************
*
* FUNCTION
*
*   Compute_Sphere_Sweep_BBox
*
* INPUT
*
*   Sphere Sweep
*
* OUTPUT
*
*   Sphere Sweep
*
* RETURNS
*
*   -
*
* AUTHOR
*
*   Jochen Lippert
*
* DESCRIPTION
*
*   Calculate the bounding box of a sphere sweep.
*
* CHANGES
*
*   -
*
******************************************************************************/

void SphereSweep::Compute_BBox()
{
    Vector3d    mins;
    Vector3d    maxs;
    int         i;

    mins[X] = mins[Y] = mins[Z] = BOUND_HUGE;
    maxs[X] = maxs[Y] = maxs[Z] = -BOUND_HUGE;

    if (Interpolation == CATMULL_ROM_SPLINE_SPHERE_SWEEP)
    {
        // With Catmull-Rom splines, the spline may overshoot the space defined by the control spheres.
        // For now, we solve this by translating each segment of the sphere sweep into (temporary) corresponding
        // Bezier-style control spheres, and computing the bounding box from those.

        for (i = 1; i < Num_Modeling_Spheres-2; i++)
        {
            // Compute temporary Bezier-style control spheres for the segment.

            SPHSWEEP_SPH tempSphere[4];
            tempSphere[0]        = Modeling_Sphere[i];
            tempSphere[1].Center = Modeling_Sphere[i].Center + (Modeling_Sphere[i+1].Center - Modeling_Sphere[i-1].Center) / 6;
            tempSphere[1].Radius = Modeling_Sphere[i].Radius + (Modeling_Sphere[i+1].Radius - Modeling_Sphere[i-1].Radius) / 6;
            tempSphere[2].Center = Modeling_Sphere[i+1].Center + (Modeling_Sphere[i].Center - Modeling_Sphere[i+2].Center) / 6;
            tempSphere[2].Radius = Modeling_Sphere[i+1].Radius + (Modeling_Sphere[i].Radius - Modeling_Sphere[i+2].Radius) / 6;
            tempSphere[3]        = Modeling_Sphere[i+1];

            // From the Bezier-style control spheres, compute the bounding box.

            for (int j = 0; j < 4; ++j)
            {
                mins = min(mins, tempSphere[j].Center - Vector3d(fabs(tempSphere[j].Radius)));
                maxs = max(maxs, tempSphere[j].Center + Vector3d(fabs(tempSphere[j].Radius)));
            }
        }
    }
    else
    {
        for (i = 0; i < Num_Modeling_Spheres; i++)
        {
            mins = min(mins, Modeling_Sphere[i].Center - Vector3d(fabs(Modeling_Sphere[i].Radius)));
            maxs = max(maxs, Modeling_Sphere[i].Center + Vector3d(fabs(Modeling_Sphere[i].Radius)));
        }
    }

    Make_BBox_from_min_max(BBox, mins, maxs);

    if (Trans != nullptr)
        Recompute_BBox(&BBox, Trans);
}



/*****************************************************************************
*
* FUNCTION
*
*   Compute_Sphere_Sweep
*
* INPUT
*
*   Sphere sweep
*
* OUTPUT
*
*   Sphere sweep
*
* RETURNS
*
*   -
*
* AUTHOR
*
*   Jochen Lippert
*
* DESCRIPTION
*
*   Calculate the internal representation of a sphere sweep.
*
* CHANGES
*
*   -
*
******************************************************************************/

void SphereSweep::Compute()
{
    size_t  size;
    int     i;
    int     coef;
    int     msph;
    int     last_sph;
    int     last_seg;

    switch(Interpolation)
    {
        case LINEAR_SPHERE_SWEEP:
            // Allocate memory if necessary
            if (Segment == nullptr)
            {
                Num_Segments = Num_Modeling_Spheres - 1;
                size = Num_Segments * sizeof(SPHSWEEP_SEG);
                Segment = reinterpret_cast<SPHSWEEP_SEG *>(POV_MALLOC(size, "sphere sweep segments"));
            }

            // Calculate polynomials for each segment
            for(i = 0; i < Num_Segments; i++)
            {
                // Polynomial has two coefficients
                Segment[i].Num_Coefs = 2;

                // Coefficients for u^1
                Segment[i].Center_Coef[1] =
                     Modeling_Sphere[i + 1].Center -
                     Modeling_Sphere[i].Center;

                Segment[i].Radius_Coef[1] =
                     Modeling_Sphere[i + 1].Radius -
                     Modeling_Sphere[i].Radius;

                // Coefficients for u^0
                Segment[i].Center_Coef[0] =
                     Modeling_Sphere[i].Center;

                Segment[i].Radius_Coef[0] =
                     Modeling_Sphere[i].Radius;
            }
            break;
        case CATMULL_ROM_SPLINE_SPHERE_SWEEP:
            // Allocate memory if necessary
            if (Segment == nullptr)
            {
                Num_Segments = Num_Modeling_Spheres - 3;
                size = Num_Segments * sizeof(SPHSWEEP_SEG);
                Segment = reinterpret_cast<SPHSWEEP_SEG *>(POV_MALLOC(size, "sphere sweep segments"));
            }

            // Calculate polynomials for each segment
            for(i = 0; i < Num_Segments; i++)
            {
                // Polynomial has four coefficients
                Segment[i].Num_Coefs = 4;

                // Calculate coefficients
                for(coef = 0; coef < 4; coef++)
                {
                    // Center
                    Segment[i].Center_Coef[coef] =
                           Modeling_Sphere[i].Center *
                           Catmull_Rom_Matrix[coef][0];

                    // Radius
                    Segment[i].Radius_Coef[coef] =
                           Modeling_Sphere[i].Radius *
                           Catmull_Rom_Matrix[coef][0];

                    for(msph = 1; msph < 4; msph++)
                    {
                        // Center
                        Segment[i].Center_Coef[coef] +=
                                     Catmull_Rom_Matrix[coef][msph] *
                                     Modeling_Sphere[i + msph].Center;

                        // Radius
                        Segment[i].Radius_Coef[coef] +=
                                     Catmull_Rom_Matrix[coef][msph] *
                                     Modeling_Sphere[i + msph].Radius;
                    }
                }
            }
            break;
        case B_SPLINE_SPHERE_SWEEP:
            // Allocate memory if necessary
            if (Segment == nullptr)
            {
                Num_Segments = Num_Modeling_Spheres - 3;
                size = Num_Segments * sizeof(SPHSWEEP_SEG);
                Segment = reinterpret_cast<SPHSWEEP_SEG *>(POV_MALLOC(size, "sphere sweep segments"));
            }

            // Calculate polynomials for each segment
            for(i = 0; i < Num_Segments; i++)
            {
                // Polynomial has four coefficients
                Segment[i].Num_Coefs = 4;

                // Calculate coefficients
                for(coef = 0; coef < 4; coef++)
                {
                    // Center
                    Segment[i].Center_Coef[coef] =
                           Modeling_Sphere[i].Center *
                           B_Matrix[coef][0];

                    // Radius
                    Segment[i].Radius_Coef[coef] =
                           Modeling_Sphere[i].Radius *
                           B_Matrix[coef][0];

                    for(msph = 1; msph < 4; msph++)
                    {
                        // Center
                        Segment[i].Center_Coef[coef] +=
                                     B_Matrix[coef][msph] *
                                     Modeling_Sphere[i + msph].Center;

                        // Radius
                        Segment[i].Radius_Coef[coef] +=
                                     B_Matrix[coef][msph] *
                                     Modeling_Sphere[i + msph].Radius;
                    }
                }
            }
            break;

        default:
            POV_SHAPE_ASSERT (false);
            break;
    }

    // Pre-calculate several constants

    for(i = 0; i < Num_Segments; i++)
    {
        // Calculate closing sphere for u = 0

        // Center
        Segment[i].Closing_Sphere[0].Center =
                      Segment[i].Center_Coef[0];

        // Radius
        Segment[i].Closing_Sphere[0].Radius =
                      Segment[i].Radius_Coef[0];

        // Calculate derivatives for u = 0

        // Center
        Segment[i].Center_Deriv[0] =
                      Segment[i].Center_Coef[1];

        // Radius
        Segment[i].Radius_Deriv[0] =
                      Segment[i].Radius_Coef[1];

        // Calculate closing sphere for u = 1

        // Center
        Segment[i].Closing_Sphere[1].Center =
                      Segment[i].Center_Coef[0];

        // Radius
        Segment[i].Closing_Sphere[1].Radius =
                      Segment[i].Radius_Coef[0];

        for(coef = 1; coef < Segment[i].Num_Coefs; coef++)
        {
            // Center
            Segment[i].Closing_Sphere[1].Center +=
                   Segment[i].Center_Coef[coef];

            // Radius
            Segment[i].Closing_Sphere[1].Radius +=
                   Segment[i].Radius_Coef[coef];
        }

        // Calculate derivatives for u = 1

        // Center
        Segment[i].Center_Deriv[1] =
                      Segment[i].Center_Coef[1];

        // Radius
        Segment[i].Radius_Deriv[1] =
                      Segment[i].Radius_Coef[1];

        for(coef = 2; coef < Segment[i].Num_Coefs; coef++)
        {
            // Center
            Segment[i].Center_Deriv[1] += double(coef) *
                         Segment[i].Center_Coef[coef];

            // Radius
            Segment[i].Radius_Deriv[1] += coef *
                         Segment[i].Radius_Coef[coef];
        }
    }

    // Calculate single spheres

    // Allocate memory if necessary
    if (Sphere == nullptr)
    {
        Num_Spheres = Num_Segments + 1;
        size = Num_Spheres * sizeof(SPHSWEEP_SPH);
        Sphere = reinterpret_cast<SPHSWEEP_SPH *>(POV_MALLOC(size, "sphere sweep spheres"));
    }

    // Calculate first sphere of every segment

    for(i = 0; i < Num_Segments; i++)
    {
        // Center
        Sphere[i].Center =
                      Segment[i].Center_Coef[0];

        // Radius
        Sphere[i].Radius =
                      Segment[i].Radius_Coef[0];
    }

    // Calculate last sphere of last segment

    last_sph = Num_Spheres - 1;
    last_seg = Num_Segments - 1;

    // Center
    Sphere[last_sph].Center =
                  Segment[last_seg].Center_Coef[0];
    // Radius
    Sphere[last_sph].Radius =
                  Segment[last_seg].Radius_Coef[0];

    for(coef = 1; coef < Segment[last_seg].Num_Coefs; coef++)
    {
        // Center
        Sphere[last_sph].Center +=
               Segment[last_seg].Center_Coef[coef];

        // Radius
        Sphere[last_sph].Radius +=
               Segment[last_seg].Radius_Coef[coef];
    }
}



/*****************************************************************************
*
* FUNCTION
*
*   Find_Valid_Points
*
* INPUT
*
*   Intersection list, number of intersections, ray
*
* OUTPUT
*
*   Intersection list
*
* RETURNS
*
*   Number of valid intersections
*
* AUTHOR
*
*   Jochen Lippert
*
* DESCRIPTION
*
*   Delete invalid intersections.
*
* CHANGES
*
*   -
*
******************************************************************************/

int SphereSweep::Find_Valid_Points(SPHSWEEP_INT *Inter, int Num_Inter, const BasicRay &ray)
{
    int     i;
    int     j;
    int     Inside;
    bool    Keep;
    DBL     NormalDotDirection;

    // NB: First intersection point always enters, last one always leaves, so we're not explicitly testing them.

    Inside = 1;
    i = 1;
    while(i < Num_Inter - 1)
    {
        // Angle between normal and ray
        NormalDotDirection = dot(Inter[i].Normal, ray.Direction);

        // Does the ray enter the part?
        if(NormalDotDirection < 0.0)
        {
            // Ray enters part, keep intersection if ray was outside any part
            Keep = (Inside == 0);

            // increase inside counter
            Inside++;
        }
        else
        {
            // Ray exits part, keep intersection if ray was inside one part
            Keep = (Inside == 1);

            // decrease inside counter
            Inside--;
        }

        // Keep intersection?
        if(Keep)
            i++; // Yes, advance to next one
        else
        {
            // No, delete it
            for (j = i + 1; j < Num_Inter; j++)
                Inter[j - 1] = Inter[j];
            Num_Inter--;
        }
    }

    return Num_Inter;
}



/*****************************************************************************
*
* FUNCTION
*
*   Comp_Isects
*
* INPUT
*
* OUTPUT
*
* RETURNS
*
* AUTHOR
*
*   Jochen Lippert
*
* DESCRIPTION
*
*   Compare the depths of two sphere sweep intersections.
*
* CHANGES
*
*   -
*
******************************************************************************/

int SphereSweep::Comp_Isects(const void *Intersection_1, const void *Intersection_2)
{
    const SPHSWEEP_INT *Int_1;
    const SPHSWEEP_INT *Int_2;

    Int_1 = reinterpret_cast<const SPHSWEEP_INT *>(Intersection_1);
    Int_2 = reinterpret_cast<const SPHSWEEP_INT *>(Intersection_2);

    if(Int_1->t < Int_2->t)
        return -1;

    if (Int_1->t == Int_2->t)
        return 0;
    else
        return 1;
}

}
// end of namespace pov
