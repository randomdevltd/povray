#ifndef POVRAY_CORE_LIGHTING_BOUNCEGRID_H
#define POVRAY_CORE_LIGHTING_BOUNCEGRID_H

#include "core/configcore.h"
#include "core/lighting/bouncefield.h"
#include "core/material/prepareddirectionalsource.h"
#include "core/material/volumeincident.h"
#include "core/material/preparedsourcelayout.h"
#include <array>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace pov
{

class BounceFieldGrid final : public PreparedDirectionalSourceField
{
public:
    BounceFieldGrid(std::shared_ptr<const PreparedSourceDomain> domain, std::vector<BounceFieldNode> nodes)
        : domain(std::move(domain)), nodes(Sealed(std::move(nodes)))
    {
        if (!this->domain || this->nodes.size() != this->domain->NodeCount())
            throw std::invalid_argument("Bounce field size does not match its source domain");
    }

    MathColour Evaluate(const Vector3d& point, const Vector3d& outgoing) const override
    {
        return Interpolate(point, [&](const BounceFieldNode& node) { return node.Evaluate(outgoing); });
    }

    MathColour AngularAverage(const Vector3d& point) const override
    {
        return Interpolate(point, [](const BounceFieldNode& node) { return node.AngularAverage(); });
    }

    const std::shared_ptr<const PreparedSourceDomain>& Domain() const override
    {
        return domain;
    }
    size_t StorageBytes() const override
    {
        size_t bytes = sizeof(*this) + nodes.capacity() * sizeof(BounceFieldNode);
        for (const auto& node : nodes)
            bytes += node.StorageBytes() - sizeof(BounceFieldNode);
        return bytes;
    }

private:
    static std::vector<BounceFieldNode> Sealed(std::vector<BounceFieldNode> nodes)
    {
        for (auto& node : nodes)
            node.Seal();
        return nodes;
    }
    template <class EvaluateNode> MathColour Interpolate(const Vector3d& point, EvaluateNode evaluate) const
    {
        const auto stencil = domain->SampleStencil(point);
        MathColour result(0.0);
        if (stencil.valid)
            for (size_t i = 0; i < stencil.nodes.size(); ++i)
                if (stencil.weights[i] > 0.0)
                    result += evaluate(nodes[stencil.nodes[i]]) * stencil.weights[i];
        return result;
    }

    const std::shared_ptr<const PreparedSourceDomain> domain;
    const std::vector<BounceFieldNode> nodes;
};

/// Light reaching each node of a source domain, gathered as directional lobes and scattered by one phase; nodes
/// whose cell receives nowhere are left out of the interpolation.
class BounceMomentGrid final : public PreparedDirectionalSourceField
{
public:
    BounceMomentGrid(std::shared_ptr<const PreparedSourceDomain> domain, std::shared_ptr<const std::vector<VolumeIncident>> nodes,
                     int type, double eccentricity, bool ownsNodes) :
        domain(std::move(domain)), nodes(std::move(nodes)), phase(PhaseLobeTable::For(type, eccentricity)), ownsNodes(ownsNodes)
    {
        if (!this->domain || !this->nodes || this->nodes->size() != this->domain->NodeCount())
            throw std::invalid_argument("Bounce field size does not match its source domain");
    }

    MathColour Evaluate(const Vector3d& point, const Vector3d& outgoing) const override
    {
        return Interpolate(point, [&](const VolumeIncident& node) { return phase->Scatter(node, outgoing); });
    }

    MathColour AngularAverage(const Vector3d& point) const override
    {
        return Interpolate(point, [](const VolumeIncident& node)
        {
            MathColour total;
            for (const VolumeLobe& lobe : node.lobes)
                for (int c = 0; c < 3; ++c)
                    total[c] += lobe.light[c];
            return total;
        });
    }

    const std::shared_ptr<const PreparedSourceDomain>& Domain() const override
    {
        return domain;
    }
    /// Its own storage; the nodes it shares with the other phases of its volume count once, with the grid that owns them.
    size_t StorageBytes() const override
    {
        return sizeof(*this) + (ownsNodes ? nodes->capacity() * sizeof(VolumeIncident) : 0);
    }

private:
    static double InverseFourPi() { return 0.079577471545947667884; }
    template <class EvaluateNode> MathColour Interpolate(const Vector3d& point, EvaluateNode evaluate) const
    {
        const auto stencil = domain->SampleStencil(point);
        MathColour result(0.0);
        double total = 0.0;
        if (stencil.valid)
            for (size_t i = 0; i < stencil.nodes.size(); ++i)
                if ((stencil.weights[i] > 0.0) && (*nodes)[stencil.nodes[i]].valid)
                {
                    result += evaluate((*nodes)[stencil.nodes[i]]) * stencil.weights[i];
                    total += stencil.weights[i];
                }
        return total > 0.0 ? result * (InverseFourPi() / total) : result;
    }

    const std::shared_ptr<const PreparedSourceDomain> domain;
    const std::shared_ptr<const std::vector<VolumeIncident>> nodes;
    const std::shared_ptr<const PhaseLobeTable> phase;
    const bool ownsNodes;
};

} // namespace pov

#endif
