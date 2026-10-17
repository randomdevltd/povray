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
#include "core/lighting/progressivephoton.h"

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

/// The target that selector, uniform in [0, 1), picks by probability; selector keeps its remainder.
inline size_t PhotonTargetPick(const std::vector<double>& probability, double& selector)
{
    size_t selected = 0;
    while (selected + 1 < probability.size() && selector >= probability[selected])
        selector -= probability[selected++];
    return selected;
}

/// The cone frame shared by sampling and by locating a direction in a target's emission square.
inline void PhotonConeFrame(const Vector3d& axis, Vector3d& s, Vector3d& t)
{
    s = cross(std::abs(axis[Y]) < 0.9 ? Vector3d(0, 1, 0) : Vector3d(1, 0, 0), axis).normalized();
    t = cross(axis, s);
}

/// The turn of offset about a frame, in [0, 1).
inline double PhotonTurn(const Vector3d& offset, const Vector3d& s, const Vector3d& t)
{
    const double turn = std::atan2(dot(offset, t), dot(offset, s)) / (2.0 * M_PI);
    return turn < 0.0 ? std::min(turn + 1.0, std::nextafter(1.0, 0.0)) : turn;
}

/// Whether direction lies in the cone about axis of the given width; projection is their dot product.
inline bool PhotonConeContains(const Vector3d& direction, const Vector3d& axis, double width, double projection)
{
    return width >= 1.0 ? projection >= 1.0 - width :
        projection >= 0.0 && (direction - axis * projection).lengthSqr() <= width * (2.0 - width);
}

/// A light's photon intensity scale in direction: the cone-weighted mean of its targets' scales, else its nearest target's.
inline double PhotonDirectionScale(const Vector3d& direction, const std::vector<Vector3d>& axes, const std::vector<double>& widths,
                                   const std::vector<double>& probability, const std::vector<double>& scales)
{
    if (scales.size() == 1)
        return scales[0];
    double sum = 0.0, weight = 0.0, best = -2.0;
    size_t nearest = 0;
    for (size_t i = 0; i < axes.size(); ++i)
    {
        const double projection = dot(direction, axes[i]);
        if (projection > best)
        {
            best = projection;
            nearest = i;
        }
        if (PhotonConeContains(direction, axes[i], widths[i], projection))
        {
            sum += probability[i] / widths[i] * scales[i];
            weight += probability[i] / widths[i];
        }
    }
    return weight > 0.0 ? sum / weight : scales[nearest];
}

/// Photon emission: a tenth isotropic, nine tenths over target cones (width = 1 - least cosine), each cone's (share of width,
/// turn) square warped by its learned density if any. Returns the density per steradian and the cell of a cone sample.
inline double PhotonTargetMixture(const std::vector<Vector3d>& axes, const std::vector<double>& widths, const std::vector<double>& probability,
                                  const std::vector<const PhotonTargetDensity*>& learned, double u, double v, double draw, double selector,
                                  Vector3d& direction, size_t& selected, bool& targeted, unsigned int& cell)
{
    targeted = draw < 0.9;
    selected = PhotonTargetPick(probability, selector);
    double warped = 1.0;
    if (targeted)
    {
        if (!learned.empty() && learned[selected])
            warped = learned[selected]->Warp(u, v, cell);
        else
            cell = PhotonTargetDensity::Cell(u, v);
        Vector3d s, t;
        PhotonConeFrame(axes[selected], s, t);
        const double deficit = u * widths[selected];
        const double z = 1.0 - deficit;
        const double r = std::sqrt(std::max(0.0, deficit * (2.0 - deficit)));
        const double angle = 2.0 * M_PI * v;
        direction = axes[selected] * z + s * (r * std::cos(angle)) + t * (r * std::sin(angle));
    }
    double pdf = 0.1 / (4.0 * M_PI);
    for (size_t i = 0; i < axes.size(); ++i)
    {
        double density = 1.0;
        if (targeted && selected == i)
            density = warped;
        else
        {
            const double projection = dot(direction, axes[i]);
            if (!PhotonConeContains(direction, axes[i], widths[i], projection))
                continue;
            if (!learned.empty() && learned[i] && !learned[i]->Uniform())
            {
                Vector3d s, t;
                PhotonConeFrame(axes[i], s, t);
                density = learned[i]->Density((1.0 - projection) / widths[i], PhotonTurn(direction, s, t));
            }
        }
        pdf += 0.9 * probability[i] * density / (2.0 * M_PI * widths[i]);
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
