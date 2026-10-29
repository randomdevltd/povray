#ifndef POVRAY_CORE_LIGHTING_BOUNCEGRID_H
#define POVRAY_CORE_LIGHTING_BOUNCEGRID_H

#include "core/configcore.h"
#include "core/lighting/bouncefield.h"
#include "core/material/prepareddirectionalsource.h"
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
        : domain(std::move(domain)), nodes(std::move(nodes))
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

} // namespace pov

#endif
