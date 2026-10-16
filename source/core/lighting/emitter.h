//******************************************************************************
///
/// @file core/lighting/emitter.h
///
/// Declarations of light emitters drawn as sample positions.
///
/// @copyright
/// @parblock
///
/// Persistence of Vision Ray Tracer ('POV-Ray') version 3.8.
/// Copyright 1991-2019 Persistence of Vision Raytracer Pty. Ltd.
///
/// POV-Ray is free software: you can redistribute it and/or modify
/// it under the terms of the GNU Affero General Public License as
/// published by the Free Software Foundation, either version 3 of the
/// License, or (at your option) any later version.
///
/// POV-Ray is distributed in the hope that it will be useful,
/// but WITHOUT ANY WARRANTY; without even the implied warranty of
/// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
/// GNU Affero General Public License for more details.
///
/// You should have received a copy of the GNU Affero General Public License
/// along with this program.  If not, see <http://www.gnu.org/licenses/>.
///
/// ----------------------------------------------------------------------------
///
/// POV-Ray is based on the popular DKB raytracer version 2.12.
/// DKBTrace was originally written by David K. Buck.
/// DKBTrace Ver 2.0-2.12 were written by David K. Buck & Aaron A. Collins.
///
/// @endparblock
///
//******************************************************************************

#ifndef POVRAY_CORE_EMITTER_H
#define POVRAY_CORE_EMITTER_H

// Module config header file must be the first file included within POV-Ray unit header files
#include "core/configcore.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "core/coretypes.h"

namespace pov
{

struct EmitterSample final
{
    Vector3d position;
    MathColour weight;  ///< the radiant intensity it stands for, per channel as a multiple of Emitter::Intensity()
    double pdf;         ///< probability density of the position: per unit volume for a volume, 1 for a point
};

/// A light's emission drawn as positions, so lighting, media and photon shooting all sample it one way.
class Emitter
{
    public:
        virtual ~Emitter() {}
        virtual const MathColour& Intensity() const = 0;   ///< total radiant intensity: power over 4 pi
        virtual const Vector3d& Centre() const = 0;        ///< power-weighted centre
        virtual double Radius() const = 0;                 ///< about Centre(), holding every sample
        virtual double NearDistance() const = 0;           ///< least distance lighting uses, so 1/r^2 stays finite
        /// A position drawn in proportion to emitted power: s in [0,1) picks the stratum, key the detail within it.
        virtual EmitterSample Sample(double s, std::uint64_t key) const = 0;
        /// As Sample(), with t a second stratified dimension for emitters that use one.
        virtual EmitterSample SampleSquare(double s, double, std::uint64_t key) const { return Sample(s, key); }
        virtual Vector3d Direction(const EmitterSample&, double u, double v, double& pdf) const ///< isotropic, pdf per steradian
        {
            const double z = 1.0 - 2.0 * u, r = std::sqrt(std::max(0.0, 1.0 - z * z)), phi = 2.0 * M_PI * v;
            pdf = 1.0 / (4.0 * M_PI);
            return Vector3d(r * std::cos(phi), r * std::sin(phi), z);
        }
};

/// Photon emission: a tenth isotropic, nine tenths over target cones (width = 1 - least cosine); a draw below 0.9 replaces
/// the isotropic direction by a cone sample. Returns the mixture density per steradian.
inline double PhotonTargetMixture(const std::vector<Vector3d>& axes, const std::vector<double>& widths, const std::vector<double>& probability,
                                  double u, double v, double draw, double selector, Vector3d& direction, size_t& selected, bool& targeted)
{
    targeted = draw < 0.9;
    selected = 0;
    if (targeted)
    {
        while (selected + 1 < axes.size() && selector >= probability[selected])
            selector -= probability[selected++];
        const Vector3d axis = axes[selected];
        Vector3d s = cross(std::abs(axis[Y]) < 0.9 ? Vector3d(0, 1, 0) : Vector3d(1, 0, 0), axis).normalized();
        const Vector3d t = cross(axis, s);
        const double deficit = u * widths[selected];
        const double z = 1.0 - deficit;
        const double r = std::sqrt(std::max(0.0, deficit * (2.0 - deficit)));
        const double angle = 2.0 * M_PI * v;
        direction = axis * z + s * (r * std::cos(angle)) + t * (r * std::sin(angle));
    }
    double pdf = 0.1 / (4.0 * M_PI);
    for (size_t i = 0; i < axes.size(); ++i)
    {
        const double projection = dot(direction, axes[i]);
        const bool inside = widths[i] >= 1.0 ? projection >= 1.0 - widths[i] :
            projection >= 0.0 && (direction - axes[i] * projection).lengthSqr() <= widths[i] * (2.0 - widths[i]);
        if ((targeted && selected == i) || inside)
            pdf += 0.9 * probability[i] / (2.0 * M_PI * widths[i]);
    }
    return pdf;
}

struct MediaLight final ///< the `light_source { }` block of an emitting medium
{
    int samples = 32;
    double brightness = 1.0;
    double fadeDistance = 0.0, fadePower = 0.0;
    bool shadowless = false, mediaInteraction = true, mediaAttenuation = true;
    int photonReflection = -1, photonRefraction = -1;
    bool photonOptions = false, photonLegacyOptions = false;
};

}
// end of namespace pov

#endif // POVRAY_CORE_EMITTER_H
