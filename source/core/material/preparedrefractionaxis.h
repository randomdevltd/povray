#ifndef POVRAY_CORE_PREPAREDREFRACTIONAXIS_H
#define POVRAY_CORE_PREPAREDREFRACTIONAXIS_H

#include "core/configcore.h"
#include "core/material/preparedfield.h"
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace pov
{

class PreparedRefractionAxis final
{
    public:
        explicit PreparedRefractionAxis(std::vector<std::shared_ptr<const PreparedScalarField>> fields) :
            fields(std::move(fields))
        {
            for (const auto& field : this->fields)
                if (!field)
                    throw std::invalid_argument("Prepared refraction requires a published scalar field");
        }

        PreparedScalarSample Evaluate(const Vector3d& point) const
        {
            PreparedScalarSample result;
            for (const auto& field : fields)
            {
                const PreparedScalarSample sample = field->Sample(point);
                result.value += sample.value;
                result.gradient += sample.gradient;
            }
            return result;
        }

        PreparedScalarSample Sample(const Vector3d& point) const { return Evaluate(point); }

    private:
        const std::vector<std::shared_ptr<const PreparedScalarField>> fields;
};

}

#endif
