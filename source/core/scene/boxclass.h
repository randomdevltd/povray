//******************************************************************************
///
/// @file core/scene/boxclass.h
///
/// Interval helpers for classifying a box against a shape: the box's image in the shape's own space, as the shape's
/// inside test computes it, and outward-rounded squares and roots.
///
//******************************************************************************

#ifndef POVRAY_CORE_BOXCLASS_H
#define POVRAY_CORE_BOXCLASS_H

// Module config header file must be the first file included within POV-Ray unit header files
#include "core/configcore.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "core/coretypes.h"
#include "core/math/halfspacebounds.h"
#include "core/math/matrix.h"

namespace pov
{

namespace box_class
{

using halfspace_bounds::Interval;

inline Interval Square(Interval a)
{
    if (!halfspace_bounds::Finite(a))
        return halfspace_bounds::Unknown();
    if (a.low >= 0.0)
        return a * a;
    if (a.high <= 0.0)
        return (-a) * (-a);
    const double m = std::max(-a.low, a.high);
    return Interval(0.0, (Interval(m) * Interval(m)).high);
}

inline Interval Sqrt(Interval a)
{
    if (!halfspace_bounds::Finite(a))
        return halfspace_bounds::Unknown();
    const double lo = std::max(0.0, a.low), hi = std::max(0.0, a.high);
    return Interval(lo > 0.0 ? halfspace_bounds::Down(std::sqrt(lo)) : 0.0, halfspace_bounds::Up(std::sqrt(hi)));
}

/// x * x + y * y + z * z, in the order Vector3d::lengthSqr adds them.
inline Interval LengthSqr(const Interval v[3])
{
    return Square(v[X]) + Square(v[Y]) + Square(v[Z]);
}

/// Per axis, an interval holding every point of the box lo..hi mapped by toWorld (null: the box is in world space) and
/// then by the inverse of trans (null: none), as MInvTransPoint computes it for any double point of that image. With both
/// maps the product is taken first, so a rotation that one applies and the other undoes cancels instead of widening.
inline void Local_Box(const Vector3d& lo, const Vector3d& hi, const TRANSFORM *toWorld, const TRANSFORM *trans, Interval out[3])
{
    const Interval x[3] = {Interval(lo[X], hi[X]), Interval(lo[Y], hi[Y]), Interval(lo[Z], hi[Z])};
    Interval w[3];
    for (int i = X; i <= Z; ++i)
        w[i] = (toWorld == nullptr) ? x[i] :
               x[X] * Interval(toWorld->matrix[0][i]) + x[Y] * Interval(toWorld->matrix[1][i]) +
               x[Z] * Interval(toWorld->matrix[2][i]) + Interval(toWorld->matrix[3][i]);
    if (trans == nullptr)
    {
        for (int i = X; i <= Z; ++i)
            out[i] = w[i];
        return;
    }
    const MATRIX& inv = trans->inverse;
    for (int i = X; i <= Z; ++i)
    {
        Interval tight;
        if (toWorld == nullptr)
            tight = x[X] * Interval(inv[0][i]) + x[Y] * Interval(inv[1][i]) + x[Z] * Interval(inv[2][i]) + Interval(inv[3][i]);
        else
        {
            const MATRIX& m = toWorld->matrix;
            Interval c[4];
            for (int j = 0; j < 4; ++j)
                c[j] = Interval(m[j][0]) * Interval(inv[0][i]) + Interval(m[j][1]) * Interval(inv[1][i]) +
                       Interval(m[j][2]) * Interval(inv[2][i]);
            tight = x[X] * c[0] + x[Y] * c[1] + x[Z] * c[2] + c[3] + Interval(inv[3][i]);
        }
        // MInvTransPoint's own rounding of a double point: its four terms' gamma bound, under 4 epsilon times their magnitudes
        Interval size = Interval(std::fabs(inv[3][i]));
        for (int j = X; j <= Z; ++j)
            size = size + Interval(std::fabs(inv[j][i])) * Interval(std::max(std::fabs(w[j].low), std::fabs(w[j].high)));
        const double margin = (Interval(4.0 * std::numeric_limits<double>::epsilon()) * size).high;
        out[i] = tight + Interval(-margin, margin);
    }
}

}

}

#endif // POVRAY_CORE_BOXCLASS_H
