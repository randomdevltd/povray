#ifndef POVRAY_CORE_LIGHTING_BOUNCEFIELD_H
#define POVRAY_CORE_LIGHTING_BOUNCEFIELD_H

#include "core/configcore.h"
#include "base/colour.h"
#include "core/math/vector.h"
#include "core/material/physicalphase.h"
#include <cassert>
#include <vector>

namespace pov
{
// Source colours, effective active-medium coefficients and quadrature weights must be nonnegative.
class BounceFieldNode final
{
public:
    void AddIsotropic(const MathColour& source) { isotropic += source; authoredAverage += source; }

    // Directions are unit propagation vectors; incidentWeight includes the solid-angle quadrature weight.
    void AddScatteringBeam(int type, double eccentricity, const MathColour& effectiveSigmaS,
                           const Vector3d& incoming, const MathColour& incidentWeight)
    {
        const MathColour weight = effectiveSigmaS * incidentWeight * InverseFourPi();
        average += weight;
        if (type == ISOTROPIC_SCATTERING)
            isotropic += weight;
        else if (type == RAYLEIGH_SCATTERING)
        {
            rayleighMass += weight;
            int k = 0;
            for (int i = 0; i < 3; ++i)
                for (int j = i; j < 3; ++j)
                    moment[k++] += weight * (incoming[i] * incoming[j]);
        }
        else
            beams.push_back(Beam{type, eccentricity, incoming, weight});
    }

    MathColour Evaluate(const Vector3d& outgoing) const
    {
        MathColour quadratic;
        int k = 0;
        for (int i = 0; i < 3; ++i)
            for (int j = i; j < 3; ++j)
                quadratic += moment[k++] * (outgoing[i] * outgoing[j] * (i == j ? 1.0 : 2.0));
        MathColour result = isotropic + (rayleighMass + quadratic) * 0.75;
        for (const Beam& beam : beams)
            result += beam.weight * PhysicalPhaseFactor(beam.type, beam.eccentricity, dot(beam.direction, outgoing));
        return result;
    }

    MathColour AngularAverage() const
    {
        return authoredAverage + average;
    }

    void AddScaled(const BounceFieldNode& other, double weight)
    {
        assert(weight >= 0.0);
        if (this == &other)
        {
            const BounceFieldNode copy(other);
            AddScaled(copy, weight);
            return;
        }
        isotropic += other.isotropic * weight;
        authoredAverage += other.authoredAverage * weight;
        average += other.average * weight;
        rayleighMass += other.rayleighMass * weight;
        for (int i = 0; i < 6; ++i) moment[i] += other.moment[i] * weight;
        for (const Beam& beam : other.beams)
            beams.push_back(Beam{beam.type, beam.eccentricity, beam.direction, beam.weight * weight});
    }

    static BounceFieldNode Interpolate(const BounceFieldNode& a, const BounceFieldNode& b, double t)
    {
        assert(t >= 0.0 && t <= 1.0);
        BounceFieldNode result;
        result.AddScaled(a, 1.0 - t);
        result.AddScaled(b, t);
        return result;
    }

private:
    static double InverseFourPi() { return 0.079577471545947667884; }
    struct Beam { int type; double eccentricity; Vector3d direction; MathColour weight; };
    MathColour isotropic, authoredAverage, average, rayleighMass, moment[6];
    std::vector<Beam> beams;
};
}
#endif
