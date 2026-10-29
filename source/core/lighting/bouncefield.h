#ifndef POVRAY_CORE_LIGHTING_BOUNCEFIELD_H
#define POVRAY_CORE_LIGHTING_BOUNCEFIELD_H

#include "core/configcore.h"
#include "base/colour.h"
#include "core/math/vector.h"
#include "core/material/physicalphase.h"
#include <cassert>
#include <cstdint>
#include <stdexcept>
#include <memory>
#include <utility>
#include <vector>

namespace pov
{
// Source colours, effective active-medium coefficients and quadrature weights must be nonnegative.
class BounceFieldNode final
{
public:
    explicit BounceFieldNode(size_t beamBudget = 16, std::uint64_t seed = 0) : beamBudget(beamBudget), seed(seed)
    {
        if (beamBudget == 0) throw std::invalid_argument("Bounce beam budget must be positive");
    }
    BounceFieldNode(const BounceFieldNode& other) : isotropic(other.isotropic), beamBudget(other.beamBudget), seed(other.seed)
    {
        if (other.rayleigh) rayleigh.reset(new Rayleigh(*other.rayleigh));
        if (other.anisotropic) anisotropic.reset(new Anisotropic(*other.anisotropic));
    }
    BounceFieldNode(BounceFieldNode&&) = default;
    BounceFieldNode& operator=(BounceFieldNode&&) = default;
    BounceFieldNode& operator=(const BounceFieldNode& other)
    {
        BounceFieldNode copy(other);
        *this = std::move(copy);
        return *this;
    }

    void AddIsotropic(const MathColour& source) { isotropic += source; }

    // Directions are unit propagation vectors; incidentWeight includes the solid-angle quadrature weight.
    void AddScatteringBeam(int type, double eccentricity, const MathColour& effectiveSigmaS,
                           const Vector3d& incoming, const MathColour& incidentWeight)
    {
        const MathColour weight = effectiveSigmaS * incidentWeight * InverseFourPi();
        if (!(weight.Weight() > 0.0))
            return;
        if (type == ISOTROPIC_SCATTERING)
            isotropic += weight;
        else if (type == RAYLEIGH_SCATTERING)
        {
            if (!rayleigh) rayleigh.reset(new Rayleigh());
            rayleigh->mass += weight;
            int k = 0;
            for (int i = 0; i < 3; ++i)
                for (int j = i; j < 3; ++j)
                    rayleigh->moment[k++] += weight * (incoming[i] * incoming[j]);
        }
        else
        {
            for (int channel = 0; channel < MathColour::channels; ++channel)
                AddAnisotropic(channel, Beam{type, eccentricity, incoming}, weight[channel]);
        }
    }

    MathColour Evaluate(const Vector3d& outgoing) const
    {
        MathColour result = isotropic;
        if (rayleigh)
        {
            MathColour quadratic;
            int k = 0;
            for (int i = 0; i < 3; ++i)
                for (int j = i; j < 3; ++j)
                    quadratic += rayleigh->moment[k++] * (outgoing[i] * outgoing[j] * (i == j ? 1.0 : 2.0));
            result += (rayleigh->mass + quadratic) * 0.75;
        }
        if (anisotropic)
            for (int channel = 0; channel < MathColour::channels; ++channel)
            {
                double value = 0.0;
                for (const Beam& beam : anisotropic->beams[channel])
                    value += PhysicalPhaseFactor(beam.type, beam.eccentricity, dot(beam.direction, outgoing));
                result[channel] += anisotropic->average[channel] * (value / beamBudget);
            }
        return result;
    }

    MathColour AngularAverage() const
    {
        MathColour result = isotropic;
        if (rayleigh) result += rayleigh->mass;
        if (anisotropic)
            for (int channel = 0; channel < MathColour::channels; ++channel)
                result[channel] += anisotropic->average[channel];
        return result;
    }

    size_t StorageBytes() const
    {
        size_t bytes = sizeof(*this) + (rayleigh ? sizeof(Rayleigh) : 0);
        if (anisotropic)
        {
            bytes += sizeof(Anisotropic);
            for (const auto& channel : anisotropic->beams) bytes += channel.capacity() * sizeof(Beam);
        }
        return bytes;
    }

    // Anisotropic sums are compressed without bias in angular expectation and retain exact channel energy.
    void AddScaled(const BounceFieldNode& other, double weight)
    {
        assert(weight >= 0.0);
        if (weight == 0.0) return;
        if (this == &other)
        {
            const BounceFieldNode copy(other);
            AddScaled(copy, weight);
            return;
        }
        isotropic += other.isotropic * weight;
        if (other.rayleigh)
        {
            if (!rayleigh) rayleigh.reset(new Rayleigh());
            rayleigh->mass += other.rayleigh->mass * weight;
            for (int i = 0; i < 6; ++i) rayleigh->moment[i] += other.rayleigh->moment[i] * weight;
        }
        if (other.anisotropic)
        {
            for (int channel = 0; channel < MathColour::channels; ++channel)
                for (const Beam& beam : other.anisotropic->beams[channel])
                    AddAnisotropic(channel, beam, other.anisotropic->average[channel] * weight / other.beamBudget);
        }
    }


private:
    static double InverseFourPi() { return 0.079577471545947667884; }
    struct Beam { int type; double eccentricity; Vector3d direction; };
    struct Rayleigh { MathColour mass, moment[6]; };
    struct Anisotropic
    {
        double average[MathColour::channels] = {};
        std::uint64_t inputs[MathColour::channels] = {};
        std::vector<Beam> beams[MathColour::channels];
    };

    static std::uint64_t Mix(std::uint64_t value)
    {
        value = (value ^ (value >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
        value = (value ^ (value >> 27)) * UINT64_C(0x94d049bb133111eb);
        return value ^ (value >> 31);
    }

    void AddAnisotropic(int channel, const Beam& beam, double weight)
    {
        if (!(weight > 0.0)) return;
        if (!anisotropic) anisotropic.reset(new Anisotropic());
        double& total = anisotropic->average[channel];
        total += weight;
        auto& selected = anisotropic->beams[channel];
        const std::uint64_t input = ++anisotropic->inputs[channel];
        if (selected.empty())
        {
            selected.assign(beamBudget, beam);
            return;
        }
        const double probability = weight / total;
        for (size_t slot = 0; slot < beamBudget; ++slot)
        {
            const std::uint64_t random = Mix(seed ^ Mix(input) ^ Mix(channel + UINT64_C(0x9e3779b97f4a7c15)) ^
                Mix(slot + UINT64_C(0xd1b54a32d192ed03)));
            if ((random >> 11) * (1.0 / 9007199254740992.0) < probability)
                selected[slot] = beam;
        }
    }

    MathColour isotropic;
    size_t beamBudget;
    std::uint64_t seed;
    std::unique_ptr<Rayleigh> rayleigh;
    std::unique_ptr<Anisotropic> anisotropic;
};
}
#endif
