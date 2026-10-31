#ifndef POVRAY_CORE_PREPAREDLIGHTAXIS_H
#define POVRAY_CORE_PREPAREDLIGHTAXIS_H

#include "core/configcore.h"
#include "core/material/preparedsourcefield.h"

namespace pov
{

using PreparedLightPositionSample = PreparedSourceDomain::PositionSample;

class PreparedLightAxis final
{
public:
    PreparedLightAxis(std::shared_ptr<const PreparedSourceDomain> domain,
                      std::shared_ptr<const PreparedColourField> colour,
                      std::vector<DBL> cellMasses = {}) :
        PreparedLightAxis(domain, std::move(colour),
                          std::make_shared<PreparedSourceDistribution>(domain, cellMasses)) {}

    PreparedLightAxis(std::shared_ptr<const PreparedSourceDomain> domain,
                      std::shared_ptr<const PreparedColourField> colour,
                      std::shared_ptr<const PreparedSourceDistribution> proposal) :
        domain(std::move(domain)), colour(std::move(colour)), distribution(std::move(proposal))
    {
        if (!this->colour || !this->domain || !distribution || distribution->Domain() != this->domain)
            throw std::invalid_argument("Prepared light axis requires matching domain, colour and proposal");
    }

    PreparedLightPositionSample SamplePosition(std::uint64_t key) const { return distribution->SamplePosition(key); }
    PreparedLightPositionSample SamplePosition(DBL u, const Vector3d& offset) const { return distribution->SamplePosition(u, offset); }
    DBL PositionPdf(const Vector3d& point) const { return distribution->PositionPdf(point); }
    pov_base::MathColour ColourAt(const Vector3d& point) const { return colour->Sample(point); }
    const std::shared_ptr<const PreparedSourceDomain>& Domain() const { return domain; }
    const std::shared_ptr<const PreparedSourceDistribution>& Proposal() const { return distribution; }
    size_t StorageBytes() const { return distribution->StorageBytes(); }

private:
    const std::shared_ptr<const PreparedSourceDomain> domain;
    const std::shared_ptr<const PreparedColourField> colour;
    const std::shared_ptr<const PreparedSourceDistribution> distribution;
};

}
#endif
