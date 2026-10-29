#include "core/material/preparedtransfer.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
using namespace pov;
using pov_base::MathColour;

static void Near(DBL actual, DBL expected, DBL tolerance = 2e-6)
{
    assert(std::fabs(actual - expected) <= tolerance * std::max(1.0, std::fabs(expected)));
}

int main()
{
    const DBL infinity = std::numeric_limits<DBL>::infinity();
    volatile DBL runtimeInfinity = infinity;
    volatile DBL runtimeNaN = std::numeric_limits<DBL>::quiet_NaN();
    assert(IsInfiniteNumber(runtimeInfinity));
    assert(!IsFiniteNumber(runtimeInfinity));
    assert(IsNaNNumber(runtimeNaN));
    assert(!IsFiniteNumber(runtimeNaN));
    assert(!IsInfiniteNumber(runtimeNaN));
    assert(IsFiniteNumber(1.0));
    auto transfer = EvaluatePreparedConstantTransfer(MathColour(0.5), MathColour(2.0), 3.0);
    Near(transfer.transmittance[0], std::exp(-1.5));
    Near(transfer.emitted[0], 4.0 * (1.0 - std::exp(-1.5)));
    transfer = EvaluatePreparedConstantTransfer(MathColour(0.0), MathColour(-2.0), 3.0);
    Near(transfer.transmittance[0], 1.0);
    Near(transfer.emitted[0], -6.0);
    transfer = EvaluatePreparedConstantTransfer(MathColour(1e-20), MathColour(2.0), 1e-10);
    Near(transfer.emitted[0] / 2e-10, 1.0);
    transfer = EvaluatePreparedConstantTransfer(MathColour(0.5), MathColour(2.0), infinity);
    Near(transfer.transmittance[0], 0.0);
    Near(transfer.emitted[0], 4.0);
    MathColour sigma(0.5), source(2.0);
    sigma[0] = source[0] = 0.0;
    transfer = EvaluatePreparedConstantTransfer(sigma, source, infinity);
    Near(transfer.transmittance[0], 1.0);
    Near(transfer.emitted[0], 0.0);
    Near(transfer.emitted[1], 4.0);
    bool divergent = false;
    try { EvaluatePreparedConstantTransfer(MathColour(0.0), MathColour(1.0), infinity); }
    catch (const std::domain_error&) { divergent = true; }
    assert(divergent);
    for (DBL invalid : {-1.0, infinity, std::numeric_limits<DBL>::quiet_NaN()})
    {
        bool rejected = false;
        try { EvaluatePreparedConstantTransfer(MathColour(invalid), MathColour(0.0), 1.0); }
        catch (const std::invalid_argument&) { rejected = true; }
        assert(rejected);
    }
    for (DBL start : {0.0, 2.0, 10.0})
        for (DBL scale : {0.1, 1.0, 20.0})
        {
            Near(PreparedDistanceMass(start, infinity, scale), scale / (scale + start));
            Near(PreparedDistanceMass(start, start + 3.0, scale),
                 scale * 3.0 / ((scale + start) * (scale + start + 3.0)));
            DBL normalized = 0.0;
            const int count = 10000;
            for (int i = 0; i < count; ++i)
            {
                const DBL u = (i + 0.5) / count;
                const auto sample = SamplePreparedDistance(start, infinity, u, scale);
                Near((sample.distance - start) / (sample.distance + scale), u);
                Near(sample.pdf, (start + scale) / std::pow(sample.distance + scale, 2));
                const DBL jacobian = (start + scale) / std::pow(1.0 - u, 2);
                normalized += sample.pdf * jacobian / count;
            }
            Near(normalized, 1.0);
            const auto uniform = SamplePreparedDistance(start, start + 4.0, 0.25, scale);
            Near(uniform.distance, start + 1.0);
            Near(uniform.pdf, 0.25);
            Near(SamplePreparedDistance(start, infinity, 0.0, scale).distance, start);
        }
    for (DBL end : {5.0, infinity})
        for (DBL rate : {0.01, 1.0, 10.0})
        {
            const DBL start = 2.0;
            const DBL normalization = IsInfiniteNumber(end) ? 1.0 : 1.0 - std::exp(-rate * (end-start));
            DBL integral = 0.0;
            for (int i = 0; i < 10000; ++i)
            {
                const DBL u = (i + 0.5)/10000.0;
                const auto sample = SamplePreparedExponentialDistance(start, end, u, rate);
                Near((1.0 - std::exp(-rate*(sample.distance-start))) / normalization, u);
                Near(sample.pdf, PreparedExponentialDistancePdf(start, end, sample.distance, rate));
                const DBL jacobian = normalization / (rate*(1.0-u*normalization));
                integral += sample.pdf * jacobian / 10000.0;
            }
            Near(integral, 1.0);
            Near(SamplePreparedExponentialDistance(start, end, 0.0, rate).distance, start);
            Near(PreparedExponentialDistancePdf(start, end, start-0.1, rate), 0.0);
        }
    const auto zeroRate = SamplePreparedExponentialDistance(2.0, 6.0, 0.25, 0.0);
    Near(zeroRate.distance, 3.0);
    Near(zeroRate.pdf, 0.25);
    bool zeroInfiniteRejected = false;
    try { SamplePreparedExponentialDistance(0.0, infinity, 0.5, 0.0); }
    catch (const std::invalid_argument&) { zeroInfiniteRejected = true; }
    assert(zeroInfiniteRejected);
    std::cout << "prepared transfer checks passed\n";
}
