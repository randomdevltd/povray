#ifndef POVRAY_CORE_LIGHTING_SHAREDINCIDENTFIELD_H
#define POVRAY_CORE_LIGHTING_SHAREDINCIDENTFIELD_H

#include "core/configcore.h"
#include "core/material/prepareddirectionalsource.h"
#include <stdexcept>
#include <vector>

namespace pov
{
struct PreparedBouncePhase
{
    int type;
    DBL eccentricity;
    bool operator==(const PreparedBouncePhase& other) const
    {
        return type == other.type && eccentricity == other.eccentricity;
    }
};

class PreparedOwnerBounceField final : public PreparedDirectionalSourceField
{
public:
    struct Component
    {
        std::shared_ptr<const PreparedDirectionalSourceField> incidentResponse;
        std::shared_ptr<const PreparedColourField> coefficient;
    };
    PreparedOwnerBounceField(std::shared_ptr<const PreparedSourceDomain> domain,
                             std::vector<Component> components)
        : domain(std::move(domain)), components(std::move(components))
    {
        if (!this->domain)
            throw std::invalid_argument("Owner bounce field requires a source domain");
        for (const auto& component : this->components)
            if (!component.incidentResponse || !component.coefficient ||
                component.incidentResponse->Domain() != this->domain)
                throw std::invalid_argument("Owner bounce component requires shared source coordinates");
    }
    pov_base::MathColour Evaluate(const Vector3d& point, const Vector3d& outgoing) const override
    {
        pov_base::MathColour result;
        for (const auto& component : components)
            result +=
                component.coefficient->Sample(point) * component.incidentResponse->Evaluate(point, outgoing);
        return result;
    }
    pov_base::MathColour AngularAverage(const Vector3d& point) const override
    {
        pov_base::MathColour result;
        for (const auto& component : components)
            result +=
                component.coefficient->Sample(point) * component.incidentResponse->AngularAverage(point);
        return result;
    }
    const std::shared_ptr<const PreparedSourceDomain>& Domain() const override
    {
        return domain;
    }
    size_t StorageBytes() const override
    {
        return sizeof(*this) + components.capacity() * sizeof(Component);
    }

private:
    const std::shared_ptr<const PreparedSourceDomain> domain;
    const std::vector<Component> components;
};

class SumPreparedBounceField final : public PreparedDirectionalSourceField
{
public:
    SumPreparedBounceField(std::shared_ptr<const PreparedSourceDomain> domain,
                           std::vector<std::shared_ptr<const PreparedDirectionalSourceField>> orders)
        : domain(std::move(domain)), orders(std::move(orders))
    {
        if (!this->domain)
            throw std::invalid_argument("Bounce sum requires a source domain");
        for (const auto& order : this->orders)
            if (!order || order->Domain() != this->domain)
                throw std::invalid_argument("Bounce orders require shared source coordinates");
    }
    pov_base::MathColour Evaluate(const Vector3d& point, const Vector3d& outgoing) const override
    {
        pov_base::MathColour result;
        for (const auto& order : orders)
            result += order->Evaluate(point, outgoing);
        return result;
    }
    pov_base::MathColour AngularAverage(const Vector3d& point) const override
    {
        pov_base::MathColour result;
        for (const auto& order : orders)
            result += order->AngularAverage(point);
        return result;
    }
    const std::shared_ptr<const PreparedSourceDomain>& Domain() const override
    {
        return domain;
    }
    size_t StorageBytes() const override
    {
        return sizeof(*this) + orders.capacity() * sizeof(orders.front());
    }

private:
    const std::shared_ptr<const PreparedSourceDomain> domain;
    const std::vector<std::shared_ptr<const PreparedDirectionalSourceField>> orders;
};
} // namespace pov
#endif
