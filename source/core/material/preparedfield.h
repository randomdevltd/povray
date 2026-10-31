#ifndef POVRAY_CORE_PREPAREDFIELD_H
#define POVRAY_CORE_PREPAREDFIELD_H

#include "core/configcore.h"
#include "core/math/finite.h"
#include "core/math/vector.h"
#include "base/colour.h"
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace pov
{

struct PreparedScalarSample
{
    DBL value;
    Vector3d gradient;

    PreparedScalarSample(DBL value = 0.0, const Vector3d& gradient = Vector3d(0.0)) :
        value(value), gradient(gradient) {}
};

// Published implementations own immutable sampled data; Sample never invokes procedural sources.
class PreparedScalarField
{
    public:
        virtual ~PreparedScalarField() = default;
        // Values are signed and unwrapped; gradients are derivatives in world coordinates.
        virtual PreparedScalarSample Sample(const Vector3d& point) const = 0;
};

class PreparedConstantScalarField final : public PreparedScalarField
{
    public:
        explicit PreparedConstantScalarField(DBL value) : value(value)
        {
            if (!IsFiniteNumber(value))
                throw std::invalid_argument("Prepared scalar constant must be finite");
        }
        PreparedScalarSample Sample(const Vector3d&) const override { return PreparedScalarSample(value); }
    private:
        const DBL value;
};

struct PreparedGridLayout
{
    const Vector3d low;
    const Vector3d size;
    const std::array<size_t, 3> counts;

    PreparedGridLayout(const Vector3d& low, const Vector3d& size, std::array<size_t, 3> counts) :
        low(low), size(size), counts(counts)
    {
        size_t total = 1;
        for (int axis = X; axis <= Z; ++axis)
        {
            if (!IsFiniteNumber(low[axis]) || !IsFiniteNumber(size[axis]) || size[axis] <= 0.0 ||
                !IsFiniteNumber(low[axis] + size[axis]) || counts[axis] == 0 ||
                counts[axis] > std::numeric_limits<size_t>::max() / total ||
                !IsFiniteNumber(static_cast<DBL>(counts[axis]) / size[axis]))
                throw std::invalid_argument("Invalid prepared grid layout");
            total *= counts[axis];
        }
    }

    size_t ValueCount() const { return counts[X] * counts[Y] * counts[Z]; }
};

class PreparedScalarGrid final : public PreparedScalarField
{
    public:
        PreparedScalarGrid(std::shared_ptr<const PreparedGridLayout> layout, std::vector<DBL> values) :
            layout(std::move(layout)), values(std::move(values))
        {
            if (!this->layout || this->values.size() != this->layout->ValueCount())
                throw std::invalid_argument("Prepared scalar grid size does not match its layout");
            for (DBL value : this->values)
                if (!IsFiniteNumber(value))
                    throw std::invalid_argument("Prepared scalar grid values must be finite");
        }

        PreparedScalarSample Sample(const Vector3d& point) const override
        {
            std::array<size_t, 3> lower, upper;
            std::array<DBL, 3> fraction, derivative;
            for (int axis = X; axis <= Z; ++axis)
            {
                const DBL position = point[axis] - layout->low[axis];
                if (!IsFiniteNumber(position) || position < 0.0 || position > layout->size[axis])
                    return PreparedScalarSample();
                const DBL scale = static_cast<DBL>(layout->counts[axis]) / layout->size[axis];
                const DBL coordinate = position * scale - 0.5;
                const DBL last = static_cast<DBL>(layout->counts[axis] - 1);
                if (coordinate <= 0.0 || coordinate >= last)
                {
                    lower[axis] = upper[axis] = coordinate <= 0.0 ? 0 : layout->counts[axis] - 1;
                    fraction[axis] = derivative[axis] = 0.0;
                }
                else
                {
                    lower[axis] = static_cast<size_t>(std::floor(coordinate));
                    upper[axis] = lower[axis] + 1;
                    fraction[axis] = coordinate - static_cast<DBL>(lower[axis]);
                    derivative[axis] = scale;
                }
            }
            PreparedScalarSample result;
            for (int z = 0; z != 2; ++z)
                for (int y = 0; y != 2; ++y)
                    for (int x = 0; x != 2; ++x)
                    {
                        const size_t ix = x ? upper[X] : lower[X];
                        const size_t iy = y ? upper[Y] : lower[Y];
                        const size_t iz = z ? upper[Z] : lower[Z];
                        const DBL value = values[(iz * layout->counts[Y] + iy) * layout->counts[X] + ix];
                        const DBL wx = x ? fraction[X] : 1.0 - fraction[X];
                        const DBL wy = y ? fraction[Y] : 1.0 - fraction[Y];
                        const DBL wz = z ? fraction[Z] : 1.0 - fraction[Z];
                        result.value += value * wx * wy * wz;
                        result.gradient[X] += value * (x ? derivative[X] : -derivative[X]) * wy * wz;
                        result.gradient[Y] += value * wx * (y ? derivative[Y] : -derivative[Y]) * wz;
                        result.gradient[Z] += value * wx * wy * (z ? derivative[Z] : -derivative[Z]);
                    }
            return result;
        }

    private:
        const std::shared_ptr<const PreparedGridLayout> layout;
        const std::vector<DBL> values;
};

class PreparedColourField
{
    public:
        virtual ~PreparedColourField() = default;
        virtual pov_base::MathColour Sample(const Vector3d& point) const = 0;
};

class PreparedConstantColourField final : public PreparedColourField
{
    public:
        explicit PreparedConstantColourField(const pov_base::MathColour& value) : value(value)
        {
            for (int channel = 0; channel < pov_base::MathColour::channels; ++channel)
                if (!IsFiniteNumber(value[channel]))
                    throw std::invalid_argument("Prepared colour constant must be finite");
        }
        pov_base::MathColour Sample(const Vector3d&) const override { return value; }
    private:
        const pov_base::MathColour value;
};

class PreparedColourGrid final : public PreparedColourField
{
    public:
        PreparedColourGrid(std::shared_ptr<const PreparedGridLayout> layout, std::vector<pov_base::MathColour> values) :
            layout(std::move(layout)), values(std::move(values))
        {
            if (!this->layout || this->values.size() != this->layout->ValueCount())
                throw std::invalid_argument("Prepared colour grid size does not match its layout");
            for (const pov_base::MathColour& value : this->values)
                for (int channel = 0; channel < pov_base::MathColour::channels; ++channel)
                    if (!IsFiniteNumber(value[channel]))
                        throw std::invalid_argument("Prepared colour grid values must be finite");
        }

        pov_base::MathColour Sample(const Vector3d& point) const override
        {
            std::array<size_t, 3> lower, upper;
            std::array<DBL, 3> fraction;
            for (int axis = X; axis <= Z; ++axis)
            {
                const DBL position = point[axis] - layout->low[axis];
                if (!IsFiniteNumber(position) || position < 0.0 || position > layout->size[axis])
                    return pov_base::MathColour(0.0);
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
            pov_base::MathColour result(0.0);
            for (int z = 0; z != 2; ++z)
                for (int y = 0; y != 2; ++y)
                    for (int x = 0; x != 2; ++x)
                    {
                        const size_t ix = x ? upper[X] : lower[X];
                        const size_t iy = y ? upper[Y] : lower[Y];
                        const size_t iz = z ? upper[Z] : lower[Z];
                        const pov_base::MathColour& value = values[(iz * layout->counts[Y] + iy) * layout->counts[X] + ix];
                        const DBL weight = (x ? fraction[X] : 1.0 - fraction[X]) *
                                           (y ? fraction[Y] : 1.0 - fraction[Y]) *
                                           (z ? fraction[Z] : 1.0 - fraction[Z]);
                        for (int channel = 0; channel < pov_base::MathColour::channels; ++channel)
                            result[channel] += value[channel] * weight;
                    }
            return result;
        }

    private:
        const std::shared_ptr<const PreparedGridLayout> layout;
        const std::vector<pov_base::MathColour> values;
};

class TransformedPreparedScalarField final : public PreparedScalarField
{
    public:
        TransformedPreparedScalarField(std::shared_ptr<const PreparedScalarField> field,
                                       std::array<Vector3d, 3> worldToLocalRows, const Vector3d& translation) :
            field(std::move(field)), rows(std::move(worldToLocalRows)), translation(translation)
        {
            if (!this->field)
                throw std::invalid_argument("Prepared scalar transform requires a field");
            for (int i = X; i <= Z; ++i)
            {
                if (!IsFiniteNumber(translation[i]))
                    throw std::invalid_argument("Prepared scalar transform must be finite");
                for (int j = X; j <= Z; ++j)
                    if (!IsFiniteNumber(rows[i][j]))
                        throw std::invalid_argument("Prepared scalar transform must be finite");
            }
        }

        PreparedScalarSample Sample(const Vector3d& point) const override
        {
            Vector3d local = translation;
            for (int i = X; i <= Z; ++i)
                for (int j = X; j <= Z; ++j)
                    local[i] += rows[i][j] * point[j];
            PreparedScalarSample result = field->Sample(local);
            const Vector3d localGradient = result.gradient;
            result.gradient = Vector3d(0.0);
            for (int i = X; i <= Z; ++i)
                for (int j = X; j <= Z; ++j)
                    result.gradient[j] += rows[i][j] * localGradient[i];
            return result;
        }

    private:
        const std::shared_ptr<const PreparedScalarField> field;
        const std::array<Vector3d, 3> rows;
        const Vector3d translation;
};

}

#endif
