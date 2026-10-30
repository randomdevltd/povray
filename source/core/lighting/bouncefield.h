#ifndef POVRAY_CORE_LIGHTING_BOUNCEFIELD_H
#define POVRAY_CORE_LIGHTING_BOUNCEFIELD_H

#include "core/configcore.h"
#include "base/colour.h"
#include "core/math/vector.h"
#include "core/material/physicalphase.h"
#include <array>
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
        if (other.slots) slots.reset(new Slots(*other.slots));
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

    // Directions are unit propagation vectors, or zero for light from every direction, which every phase scatters
    // evenly; incidentWeight includes the solid-angle quadrature weight.
    void AddScatteringBeam(int type, double eccentricity, const MathColour& effectiveSigmaS,
                           const Vector3d& incoming, const MathColour& incidentWeight)
    {
        const MathColour weight = effectiveSigmaS * incidentWeight * InverseFourPi();
        if (!(weight.Weight() > 0.0))
            return;
        if (type == ISOTROPIC_SCATTERING || incoming.IsNull())
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
            std::uint32_t index = kNoBeam;
            for (int channel = 0; channel < MathColour::channels; ++channel)
                AddAnisotropic(channel, Beam{type, eccentricity, incoming}, index, weight[channel]);
        }
    }

    // Keeps only what evaluation reads: the slots' distinct beams, each with how many slots of each channel hold it.
    void Seal()
    {
        if (!slots) return;
        slots.reset();
        Anisotropic& a = *anisotropic;
        size_t kept = 0;
        for (size_t i = 0; i < a.beams.size(); ++i)
            if (a.counts[i][0] || a.counts[i][1] || a.counts[i][2])
            {
                a.beams[kept] = a.beams[i];
                a.counts[kept++] = a.counts[i];
            }
        a.beams.resize(kept);
        a.counts.resize(kept);
        a.beams.shrink_to_fit();
        a.counts.shrink_to_fit();
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
        {
            double value[MathColour::channels] = {};
            for (size_t i = 0; i < anisotropic->beams.size(); ++i)
            {
                const Beam& beam = anisotropic->beams[i];
                const double phase = PhysicalPhaseFactor(beam.type, beam.eccentricity, dot(beam.direction, outgoing));
                for (int channel = 0; channel < MathColour::channels; ++channel)
                    value[channel] += anisotropic->counts[i][channel] * phase;
            }
            for (int channel = 0; channel < MathColour::channels; ++channel)
                result[channel] += anisotropic->average[channel] * (value[channel] / beamBudget);
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
            bytes += sizeof(Anisotropic) + anisotropic->beams.capacity() * sizeof(Beam) +
                     anisotropic->counts.capacity() * sizeof(Counts);
        if (slots)
        {
            bytes += sizeof(Slots);
            for (const auto& channel : slots->held) bytes += channel.capacity() * sizeof(std::uint32_t);
            bytes += slots->free.capacity() * sizeof(std::uint32_t);
        }
        return bytes;
    }

    // Each beam of the other node joins this one once, with its slots' share; sums keep exact channel energy.
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
            for (size_t i = 0; i < other.anisotropic->beams.size(); ++i)
            {
                std::uint32_t index = kNoBeam;
                for (int channel = 0; channel < MathColour::channels; ++channel)
                    AddAnisotropic(channel, other.anisotropic->beams[i], index, other.anisotropic->average[channel] *
                                   weight * other.anisotropic->counts[i][channel] / other.beamBudget);
            }
    }


private:
    static double InverseFourPi() { return 0.079577471545947667884; }
    static constexpr std::uint32_t kNoBeam = UINT32_MAX;
    struct Beam { int type; double eccentricity; Vector3d direction; };
    struct Rayleigh { MathColour mass, moment[6]; };
    typedef std::array<std::uint32_t, MathColour::channels> Counts;
    struct Anisotropic
    {
        double average[MathColour::channels] = {};
        std::vector<Beam> beams;
        std::vector<Counts> counts;
    };
    // Which beam each reservoir slot holds, and beams no slot holds, only while beams are still added.
    struct Slots
    {
        std::uint64_t inputs[MathColour::channels] = {};
        std::vector<std::uint32_t> held[MathColour::channels], free;
    };

    static std::uint64_t Mix(std::uint64_t value)
    {
        value = (value ^ (value >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
        value = (value ^ (value >> 27)) * UINT64_C(0x94d049bb133111eb);
        return value ^ (value >> 31);
    }

    static bool Same(const Beam& a, const Beam& b)
    {
        return a.type == b.type && a.eccentricity == b.eccentricity && a.direction[X] == b.direction[X] &&
               a.direction[Y] == b.direction[Y] && a.direction[Z] == b.direction[Z];
    }

    // Each slot takes the beam with probability weight over the channel's total; `index` is the beam's entry, shared by
    // every beam alike, once found or made.
    void AddAnisotropic(int channel, const Beam& beam, std::uint32_t& index, double weight)
    {
        if (!(weight > 0.0)) return;
        if (!anisotropic) anisotropic.reset(new Anisotropic());
        if (!slots)
        {
            if (!anisotropic->beams.empty())
                throw std::logic_error("A sealed bounce node takes no more beams");
            slots.reset(new Slots());
        }
        double& total = anisotropic->average[channel];
        total += weight;
        auto& held = slots->held[channel];
        const std::uint64_t input = ++slots->inputs[channel];
        auto take = [&](std::uint32_t& slot)
        {
            for (std::uint32_t i = 0; index == kNoBeam && i < anisotropic->beams.size(); ++i)
                if (anisotropic->counts[i] != Counts{} && Same(anisotropic->beams[i], beam)) index = i;
            if (index == kNoBeam && !slots->free.empty())
            {
                index = slots->free.back();
                slots->free.pop_back();
                anisotropic->beams[index] = beam;
            }
            else if (index == kNoBeam)
            {
                index = std::uint32_t(anisotropic->beams.size());
                anisotropic->beams.push_back(beam);
                anisotropic->counts.push_back(Counts{});
            }
            if (slot == index) return;
            if (slot != kNoBeam)
            {
                Counts& counts = anisotropic->counts[slot];
                if (--counts[channel] == 0 && counts == Counts{}) slots->free.push_back(slot);
            }
            ++anisotropic->counts[index][channel];
            slot = index;
        };
        if (held.empty())
        {
            held.assign(beamBudget, std::uint32_t(kNoBeam));
            for (auto& slot : held) take(slot);
            return;
        }
        const double probability = weight / total;
        for (size_t slot = 0; slot < beamBudget; ++slot)
        {
            const std::uint64_t random = Mix(seed ^ Mix(input) ^ Mix(channel + UINT64_C(0x9e3779b97f4a7c15)) ^
                Mix(slot + UINT64_C(0xd1b54a32d192ed03)));
            if ((random >> 11) * (1.0 / 9007199254740992.0) < probability)
                take(held[slot]);
        }
    }

    MathColour isotropic;
    size_t beamBudget;
    std::uint64_t seed;
    std::unique_ptr<Rayleigh> rayleigh;
    std::unique_ptr<Anisotropic> anisotropic;
    std::unique_ptr<Slots> slots;
};
}
#endif
