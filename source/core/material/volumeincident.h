#ifndef POVRAY_CORE_VOLUMEINCIDENT_H
#define POVRAY_CORE_VOLUMEINCIDENT_H

#include "core/configcore.h"
#include "base/colour.h"
#include "core/math/vector.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace pov
{

// Directional lobes a volume node keeps of the light arriving over its cell.
const int kVolumeLobes = 6;

/// Light arriving from one cluster of directions: its total, and its first moment, the unit directions toward the light
/// weighted by each draw's signed channel mean. The moment's length over the total's mean is the cluster's spread.
struct VolumeLobe final
{
    float light[3] = {0.0f, 0.0f, 0.0f};
    float moment[3] = {0.0f, 0.0f, 0.0f};
};

/// Light arriving over a volume node's cell, as up to kVolumeLobes lobes; invalid where no point of the cell receives.
struct VolumeIncident final
{
    std::array<VolumeLobe, kVolumeLobes> lobes;
    bool valid = false;
};

/// One draw of light arriving at a volume node: the unit direction toward where it comes from, and its share of the light.
struct IncidentDraw final
{
    Vector3d toward;
    MathColour light;
};

/// Clusters the draws by direction into lobes: deterministic farthest-point seeds, then fixed Lloyd steps on the sphere.
VolumeIncident FitIncidentLobes(const std::vector<IncidentDraw>& draws, std::vector<int>& scratch);

/// A phase's angular factor (spherical mean one) for light spread evenly over a cone, tabulated by the cone's mean cosine.
class PhaseLobeTable final
{
    public:
        /// The shared table for one phase; built on first use, identical for every caller.
        static std::shared_ptr<const PhaseLobeTable> For(int type, double eccentricity);

        PhaseLobeTable(int type, double eccentricity);

        /// The factor at scattering cosine `cosine` from the cone's axis, for a cone whose directions have mean cosine
        /// `spread` about it: exactly the phase for light from one direction, one for light from all alike.
        double Factor(double cosine, double spread) const;

        /// The light `incident` scatters toward `outgoing`, times 4 pi: each lobe's light weighted by its factor.
        MathColour Scatter(const VolumeIncident& incident, const Vector3d& outgoing) const;

        std::size_t StorageBytes() const { return sizeof(*this) + values.capacity() * sizeof(float); }

        const int type;
        const double eccentricity;

    private:
        static const int kAngles = 1024, kCones = 64;
        std::vector<float> values;
};

}

#endif
