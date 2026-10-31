#ifndef POVRAY_CORE_PREPAREDDIRECTIONALSOURCE_H
#define POVRAY_CORE_PREPAREDDIRECTIONALSOURCE_H

#include "core/configcore.h"
#include "core/material/preparedfield.h"
#include "core/material/preparedsourcelayout.h"

namespace pov
{
class PreparedDirectionalSourceField : public PreparedColourField
{
public:
    virtual pov_base::MathColour Evaluate(const Vector3d& point, const Vector3d& outgoing) const = 0;
    virtual pov_base::MathColour AngularAverage(const Vector3d& point) const = 0;
    virtual const std::shared_ptr<const PreparedSourceDomain>& Domain() const = 0;
    virtual size_t StorageBytes() const = 0;
    pov_base::MathColour Sample(const Vector3d& point) const override
    {
        return AngularAverage(point);
    }
};
} // namespace pov
#endif
