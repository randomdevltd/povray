#ifndef POVRAY_CORE_PREPAREDTRANSFER_H
#define POVRAY_CORE_PREPAREDTRANSFER_H

#include "core/configcore.h"
#include "core/math/finite.h"
#include "base/colour.h"
#include <cmath>
#include <stdexcept>

namespace pov
{

struct PreparedConstantTransfer
{
    pov_base::MathColour transmittance;
    pov_base::MathColour emitted;
};

inline PreparedConstantTransfer EvaluatePreparedConstantTransfer(
    const pov_base::MathColour& extinction, const pov_base::MathColour& emission, DBL length)
{
    if (IsNaNNumber(length) || length < 0.0)
        throw std::invalid_argument("Prepared transfer length must be nonnegative");
    PreparedConstantTransfer result;
    for (int channel = 0; channel < pov_base::MathColour::channels; ++channel)
    {
        const DBL sigma = extinction[channel], source = emission[channel];
        if (!IsFiniteNumber(sigma) || sigma < 0.0 || !IsFiniteNumber(source))
            throw std::invalid_argument("Prepared transfer requires finite nonnegative extinction and finite emission");
        if (sigma == 0.0)
        {
            if (IsInfiniteNumber(length) && source != 0.0)
                throw std::domain_error("Unbounded emission with zero extinction diverges");
            result.transmittance[channel] = 1.0;
            result.emitted[channel] = source == 0.0 ? 0.0 : source * length;
        }
        else
        {
            const DBL depth = sigma * length;
            result.transmittance[channel] = std::exp(-depth);
            // The thin limit avoids dividing an underflowed optical depth by extinction.
            const DBL integral = depth == 0.0 ? length : -std::expm1(-depth) / sigma;
            result.emitted[channel] = source == 0.0 ? 0.0 : source * integral;
        }
    }
    return result;
}

struct PreparedDistanceSample
{
    DBL distance;
    DBL pdf;
};

inline void ValidatePreparedDistanceInterval(DBL start, DBL end, DBL scale)
{
    if (!IsFiniteNumber(start) || start < 0.0 || IsNaNNumber(end) || end <= start ||
        !IsFiniteNumber(scale) || scale <= 0.0)
        throw std::invalid_argument("Prepared distance requires a positive interval and finite positive scale");
}

inline PreparedDistanceSample SamplePreparedDistance(DBL start, DBL end, DBL u, DBL scale)
{
    ValidatePreparedDistanceInterval(start, end, scale);
    if (!IsFiniteNumber(u) || u < 0.0 || u >= 1.0)
        throw std::invalid_argument("Prepared distance variate must be in [0,1)");
    if (IsFiniteNumber(end))
        return {start + (end - start) * u, 1.0 / (end - start)};
    const DBL remaining = 1.0 - u;
    const DBL ratio = u / remaining;
    const DBL distance = start + start * ratio + scale * ratio;
    const DBL inverseSum = start > scale ? (1.0 / start) / (1.0 + scale / start) :
                                                       (1.0 / scale) / (1.0 + start / scale);
    return {distance, (remaining * inverseSum) * remaining};
}

inline DBL PreparedDistanceMass(DBL start, DBL end, DBL scale)
{
    ValidatePreparedDistanceInterval(start, end, scale);
    const DBL tail = start > scale ? (scale / start) / (1.0 + scale / start) :
                                    1.0 / (1.0 + start / scale);
    if (IsInfiniteNumber(end))
        return tail;
    const DBL fraction = end > scale ? ((end - start) / end) / (1.0 + scale / end) :
                                      ((end - start) / scale) / (1.0 + end / scale);
    return tail * fraction;
}

inline DBL PreparedExponentialDistancePdf(DBL start, DBL end, DBL distance, DBL rate)
{
    ValidatePreparedDistanceInterval(start, end, 1.0);
    if (!IsFiniteNumber(rate) || rate < 0.0 || (rate == 0.0 && IsInfiniteNumber(end)))
        throw std::invalid_argument("Prepared exponential rate must be positive on an infinite interval");
    if (IsNaNNumber(distance) || distance < start || distance > end || IsInfiniteNumber(distance))
        return 0.0;
    if (rate == 0.0)
        return 1.0 / (end - start);
    const DBL depth = rate * (end - start);
    if (depth == 0.0)
        return 1.0 / (end - start);
    const DBL normalization = -std::expm1(-depth);
    return (rate / normalization) * std::exp(-rate * (distance - start));
}

inline PreparedDistanceSample SamplePreparedExponentialDistance(DBL start, DBL end, DBL u, DBL rate)
{
    ValidatePreparedDistanceInterval(start, end, 1.0);
    if (!IsFiniteNumber(u) || u < 0.0 || u >= 1.0 || !IsFiniteNumber(rate) || rate < 0.0 ||
        (rate == 0.0 && IsInfiniteNumber(end)))
        throw std::invalid_argument("Invalid prepared exponential distance parameters");
    const DBL depth = rate == 0.0 ? 0.0 : rate * (end - start);
    if (depth == 0.0)
        return SamplePreparedDistance(start, end, u, 1.0);
    const DBL normalization = -std::expm1(-depth);
    const DBL distance = start - std::log1p(-u * normalization) / rate;
    return {distance, PreparedExponentialDistancePdf(start, end, distance, rate)};
}

}

#endif
