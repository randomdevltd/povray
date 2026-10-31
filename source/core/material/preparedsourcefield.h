#ifndef POVRAY_CORE_PREPAREDSOURCEFIELD_H
#define POVRAY_CORE_PREPAREDSOURCEFIELD_H

#include "core/configcore.h"
#include "core/material/preparedfield.h"
#include "core/material/preparedsourcelayout.h"

namespace pov
{

class PreparedSourceColourField final : public PreparedColourField
{
public:
    PreparedSourceColourField(std::shared_ptr<const PreparedSourceDomain> domain,
                              std::vector<pov_base::MathColour> values) : domain(std::move(domain)), values(std::move(values))
    {
        if (!this->domain || this->values.size() != this->domain->NodeCount())
            throw std::invalid_argument("Prepared source colour count must match its domain");
        for (const auto& value : this->values)
            for (int channel = 0; channel < pov_base::MathColour::channels; ++channel)
                if (!IsFiniteNumber(value[channel]) || value[channel] < 0)
                    throw std::invalid_argument("Prepared source colour must be finite and nonnegative");
    }

    pov_base::MathColour Sample(const Vector3d& point) const override
    {
        const auto stencil = domain->SampleStencil(point);
        pov_base::MathColour result;
        if (stencil.valid)
            for (size_t i = 0; i < stencil.nodes.size(); ++i)
                result += values[stencil.nodes[i]] * stencil.weights[i];
        return result;
    }

    const std::shared_ptr<const PreparedSourceDomain>& Domain() const { return domain; }
    size_t StorageBytes() const { return values.capacity() * sizeof(pov_base::MathColour); }

private:
    const std::shared_ptr<const PreparedSourceDomain> domain;
    const std::vector<pov_base::MathColour> values;
};

}
#endif
