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
        /// Total radiant intensity: power over 4 pi for isotropic emission.
        virtual const MathColour& Intensity() const = 0;
        /// The power-weighted centre, where it is placed when sampled as a point.
        virtual const Vector3d& Centre() const = 0;
        /// A position drawn in proportion to emitted power: s in [0,1) picks the stratum, key the detail within it.
        virtual EmitterSample Sample(double s, std::uint64_t key) const = 0;
        /// A direction leaving a sample from two uniform values, and its pdf per steradian (isotropic by default).
        virtual Vector3d Direction(const EmitterSample&, double u, double v, double& pdf) const
        {
            const double z = 1.0 - 2.0 * u, r = std::sqrt(std::max(0.0, 1.0 - z * z)), phi = 2.0 * M_PI * v;
            pdf = 1.0 / (4.0 * M_PI);
            return Vector3d(r * std::cos(phi), r * std::sin(phi), z);
        }
        /// The least distance lighting puts between a sample and what it lights, so 1/r^2 stays finite.
        virtual double NearDistance() const = 0;
};

struct MediaLight final ///< the `light_source { }` block of an emitting medium
{
    int samples = 32;
    double brightness = 1.0;
    double fadeDistance = 0.0, fadePower = 0.0;
    bool shadowless = false, mediaInteraction = true, mediaAttenuation = true;
};

}
// end of namespace pov

#endif // POVRAY_CORE_EMITTER_H
