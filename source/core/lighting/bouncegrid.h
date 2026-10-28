#ifndef POVRAY_CORE_LIGHTING_BOUNCEGRID_H
#define POVRAY_CORE_LIGHTING_BOUNCEGRID_H

#include "core/configcore.h"
#include "core/lighting/bouncefield.h"
#include "core/material/preparedfield.h"
#include <array>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace pov
{

class BounceFieldGrid final : public PreparedColourField
{
public:
    BounceFieldGrid(std::shared_ptr<const PreparedGridLayout> layout, std::vector<BounceFieldNode> nodes) :
        layout(std::move(layout)), nodes(std::move(nodes))
    {
        if (!this->layout || this->nodes.size() != this->layout->ValueCount())
            throw std::invalid_argument("Bounce field grid size does not match its layout");
    }

    MathColour Evaluate(const Vector3d& point, const Vector3d& outgoing) const
    {
        return Interpolate(point, [&](const BounceFieldNode& node) { return node.Evaluate(outgoing); });
    }

    MathColour AngularAverage(const Vector3d& point) const
    {
        return Interpolate(point, [](const BounceFieldNode& node) { return node.AngularAverage(); });
    }

    MathColour Sample(const Vector3d& point) const override { return AngularAverage(point); }
    const std::shared_ptr<const PreparedGridLayout>& Layout() const { return layout; }

private:
    template<class EvaluateNode>
    MathColour Interpolate(const Vector3d& point, EvaluateNode evaluate) const
    {
        std::array<size_t, 3> lower, upper;
        std::array<DBL, 3> fraction;
        for (int axis = X; axis <= Z; ++axis)
        {
            const DBL position = point[axis] - layout->low[axis];
            if (!IsFiniteNumber(position) || position < 0.0 || position > layout->size[axis])
                return MathColour(0.0);
            const DBL coordinate = position * (static_cast<DBL>(layout->counts[axis]) / layout->size[axis]) - 0.5;
            const DBL last = static_cast<DBL>(layout->counts[axis] - 1);
            if (coordinate <= 0.0 || coordinate >= last)
            {
                lower[axis] = upper[axis] = coordinate <= 0.0 ? 0 : layout->counts[axis] - 1;
                fraction[axis] = 0.0;
            }
            else
            {
                lower[axis] = static_cast<size_t>(std::floor(coordinate));
                upper[axis] = lower[axis] + 1;
                fraction[axis] = coordinate - static_cast<DBL>(lower[axis]);
            }
        }
        MathColour result(0.0);
        for (int z = 0; z != 2; ++z)
            for (int y = 0; y != 2; ++y)
                for (int x = 0; x != 2; ++x)
                {
                    const DBL weight = (x ? fraction[X] : 1.0 - fraction[X]) *
                                       (y ? fraction[Y] : 1.0 - fraction[Y]) *
                                       (z ? fraction[Z] : 1.0 - fraction[Z]);
                    if (weight == 0.0)
                        continue;
                    const size_t ix = x ? upper[X] : lower[X];
                    const size_t iy = y ? upper[Y] : lower[Y];
                    const size_t iz = z ? upper[Z] : lower[Z];
                    result += evaluate(nodes[(iz * layout->counts[Y] + iy) * layout->counts[X] + ix]) * weight;
                }
        return result;
    }

    const std::shared_ptr<const PreparedGridLayout> layout;
    const std::vector<BounceFieldNode> nodes;
};

}

#endif
