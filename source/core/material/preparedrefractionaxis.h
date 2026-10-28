#ifndef POVRAY_CORE_PREPAREDREFRACTIONAXIS_H
#define POVRAY_CORE_PREPAREDREFRACTIONAXIS_H

#include "core/configcore.h"
#include "core/math/finite.h"
#include "core/material/preparedfield.h"
#include <cmath>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace pov
{

struct PreparedRefractionComponent
{
    std::shared_ptr<const PreparedScalarField> field;
    DBL coefficient;
};

class PreparedRefractionAxis final
{
    public:
        explicit PreparedRefractionAxis(std::vector<PreparedRefractionComponent> components) :
            components(std::move(components))
        {
            for (const auto& component : this->components)
                if (!component.field || !IsFiniteNumber(component.coefficient))
                    throw std::invalid_argument("Prepared refraction requires a published scalar field");
        }

        PreparedScalarSample Evaluate(const Vector3d& point) const
        {
            PreparedScalarSample result;
            for (const auto& component : components)
            {
                const PreparedScalarSample sample = component.field->Sample(point);
                result.value += component.coefficient * sample.value;
                result.gradient += component.coefficient * sample.gradient;
            }
            return result;
        }

        PreparedScalarSample Sample(const Vector3d& point) const { return Evaluate(point); }

    private:
        const std::vector<PreparedRefractionComponent> components;
};

}

#endif
