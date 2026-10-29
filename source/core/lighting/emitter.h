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
#include <functional>
#include <memory>

#include "core/coretypes.h"

namespace pov
{

class TraceThreadData;
class PreparedSourceDomain;
class PreparedLightAxis;
class SurfaceAngularGuide;

struct EmitterDraw final
{
    double choose, x, y, z;
    std::uint64_t positionKey;
    EmitterDraw(double choose, double x, double y, double z, std::uint64_t positionKey) :
        choose(choose), x(x), y(y), z(z), positionKey(positionKey) {}
    static double JitterSelection(double value, unsigned int count, double tail)
    {
        unsigned int strata = 1;
        while (strata <= count / 2)
            strata *= 2;
        const double bin = std::floor(value * strata);
        const double result = (bin + tail) / strata;
        return std::min(result, std::nextafter((bin + 1.0) / strata, bin / strata));
    }

};

struct EmitterSample final
{
    Vector3d position;
    MathColour weight;  ///< absolute source density divided by proposal density; connection samples include source geometry
    double pdf;         ///< volume density, reciprocal partitioned surface integration weight, or one for a point
    Vector3d normal;
    ConstObjectPtr surfaceObject = nullptr;
    std::shared_ptr<const Intersection> surfaceIntersection;
};

/// A light's emission drawn as positions, so lighting, media and photon shooting all sample it one way.
class Emitter
{
    public:
        virtual ~Emitter() {}
        virtual bool IsSurface() const { return false; }
        virtual bool HasVolumeEmission() const { return false; }
        virtual void AccumulateSurfaceGuide(SurfaceAngularGuide&) const {}
        virtual MathColour SurfaceRadianceAt(const Intersection&, const Vector3d&, const Vector3d&, TraceThreadData*) const { return MathColour(); }
        virtual MathColour PhotonEmissionWeight(const EmitterSample& sample, const Vector3d& outgoing) const { return sample.weight * PhotonDirectionWeight(sample, outgoing); }
        /// As above, for emitters that evaluate materials on the shooting thread.
        virtual MathColour PhotonEmissionWeight(const EmitterSample& sample, const Vector3d& outgoing, TraceThreadData*) const { return PhotonEmissionWeight(sample, outgoing); }
        virtual double PhotonDirectionWeight(const EmitterSample&, const Vector3d& outgoing) const { return DirectionalWeight(outgoing); }
        virtual void PrepareAxis(std::shared_ptr<const PreparedSourceDomain>, TraceThreadData*, const std::function<void()>&) const {}
        /// Builds the deferred meshes of the emitter's own geometry copy; called on one thread before any parallel use.
        virtual void ResolveGeometry() const {}
        virtual std::shared_ptr<const PreparedLightAxis> PreparedAxis() const { return nullptr; }
        virtual const MathColour& Intensity() const = 0;   ///< prepared estimate of ∫j dV; never a connection normalization
        virtual const Vector3d& Centre() const = 0;        ///< power-weighted centre
        virtual double Radius() const = 0;                 ///< about Centre(), holding every sample
        /// A position from a full-support proposal; its weight evaluates the continuous source, not its table.
        virtual EmitterSample Sample(double s, std::uint64_t key, TraceThreadData *td = nullptr) const = 0;
        /// As Sample(), with t a second stratified dimension for emitters that use one.
        virtual EmitterSample SampleSquare(double s, double, std::uint64_t key, TraceThreadData *td = nullptr) const { return Sample(s, key, td); }
        /// A position seen from `from`, drawn by direction over the emitter's cone and then along the chord inside it,
        /// at any distance: weight carries the absolute ∫j dV / r² estimate, without receiver response.
        virtual bool SampleToward(const Vector3d& from, const EmitterDraw& draw, TraceThreadData *td,
                                  EmitterSample& sample) const { (void)from; (void)draw; (void)td; (void)sample; return false; }
        virtual double DirectionalWeight(const Vector3d& outgoing) const { (void)outgoing; return 1.0; }
        virtual Vector3d Direction(const EmitterSample&, double u, double v, double& pdf) const ///< isotropic, pdf per steradian
        {
            const double z = 1.0 - 2.0 * u, r = std::sqrt(std::max(0.0, 1.0 - z * z)), phi = 2.0 * M_PI * v;
            pdf = 1.0 / (4.0 * M_PI);
            return Vector3d(r * std::cos(phi), r * std::sin(phi), z);
        }
};

/// The `light { }` block of an emitting interior: the light's own radiance, apart from the media's look.
struct MediaLight final
{
    PIGMENT *pigment = nullptr;     ///< the light's own colour or pattern; null for solid white
    unsigned bouncePasses = 0;
    bool useEmission = false;      ///< use the owning interior's combined emission as the input field
    double brightness = 1.0;        ///< the radiance the pigment gives, when no unit follows
    double lumens = -1.0;           ///< brightness given as lumens: the total power under 1 unit = 1 m; negative when not
    double attenuationScale = 1.0;  ///< 1 physical, 0 no falloff; the far field is this times fainter
    double attenuationPower = 2.0;  ///< how steeply the falloff leaves the pivot at the emitter's radius
    int samples = 16;               ///< emitter draws per receiver point
    bool shadowless = false, mediaInteraction = true, mediaAttenuation = true;
    int photonReflection = -1, photonRefraction = -1;
    bool photonOptions = false;
    long long line = 0;             ///< where the block was written, for messages

    MediaLight() = default;
    MediaLight(const MediaLight& source);          ///< deep-copies the pigment
    ~MediaLight();                                 ///< destroys the pigment
    MediaLight& operator=(const MediaLight&) = delete;
};

}
// end of namespace pov

#endif // POVRAY_CORE_EMITTER_H
