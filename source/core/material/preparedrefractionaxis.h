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

enum class PreparedRefractionOperation { Add, Replace, Multiply, Subtract };

struct PreparedRefractionComponent
{
    std::shared_ptr<const PreparedScalarField> density;
    DBL coefficient;
    PreparedRefractionOperation operation;
};

class PreparedRefractionAxis final
{
    public:
        explicit PreparedRefractionAxis(std::vector<PreparedRefractionComponent> components) :
            components(std::move(components))
        {
            for (const auto& component : this->components)
                if (!component.density || !IsFiniteNumber(component.coefficient))
                    throw std::invalid_argument("Prepared refraction requires a published scalar field");
        }

        PreparedScalarSample Evaluate(const Vector3d& point) const
        {
            PreparedScalarSample result;
            for (const auto& component : components)
            {
                const PreparedScalarSample density = component.density->Sample(point);
                switch (component.operation)
                {
                    case PreparedRefractionOperation::Add:
                        result.value += component.coefficient * density.value;
                        result.gradient += component.coefficient * density.gradient;
                        break;
                    case PreparedRefractionOperation::Replace:
                        result.value = component.coefficient * density.value;
                        result.gradient = component.coefficient * density.gradient;
                        break;
                    case PreparedRefractionOperation::Multiply:
                        result.gradient = density.value * result.gradient + result.value * density.gradient;
                        result.value *= density.value;
                        break;
                    case PreparedRefractionOperation::Subtract:
                    {
                        const DBL magnitude = std::fabs(component.coefficient);
                        const DBL direction = result.value > 0.0 ? 1.0 : -1.0;
                        const DBL reduced = result.value - direction * magnitude * density.value;
                        if (result.value == 0.0 || direction * reduced <= 0.0)
                            result = PreparedScalarSample();
                        else
                        {
                            result.value = reduced;
                            result.gradient -= direction * magnitude * density.gradient;
                        }
                        break;
                    }
                }
            }
            return result;
        }

        PreparedScalarSample Sample(const Vector3d& point) const { return Evaluate(point); }

    private:
        const std::vector<PreparedRefractionComponent> components;
};

}

#endif
