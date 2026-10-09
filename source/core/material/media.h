//******************************************************************************
///
/// @file core/material/media.h
///
/// Declarations related to participating media.
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

#ifndef POVRAY_CORE_MEDIA_H
#define POVRAY_CORE_MEDIA_H

// Module config header file must be the first file included within POV-Ray unit header files
#include "core/configcore.h"

// C++ variants of C standard header files
//  (none at the moment)

// C++ standard header files
#include <memory>
#include <string>
#include <vector>

// POV-Ray header files (base module)
//  (none at the moment)

// POV-Ray header files (core module)
#include "core/render/trace.h"

namespace pov
{

//##############################################################################
///
/// @defgroup PovCoreMaterialMedia Media
/// @ingroup PovCore
///
/// @{

// Scattering types.
enum
{
    ISOTROPIC_SCATTERING            = 1,
    MIE_HAZY_SCATTERING             = 2,
    MIE_MURKY_SCATTERING            = 3,
    RAYLEIGH_SCATTERING             = 4,
    HENYEY_GREENSTEIN_SCATTERING    = 5,
    SCATTERING_TYPES                = 5
};

void Transform_Density(std::vector<PIGMENT*>& Density, const TRANSFORM *Trans);

class Emitter;

/// The light an emitting medium gives from its container: a table of its power over the prepared grid; nullptr and why if none.
std::shared_ptr<const Emitter> MakeVolumeEmitter(Media& medium, ObjectPtr container, TraceThreadData *td, std::string& failure);

/// A `subtract` or `multiply` medium on a ray; it changes the media collected before it, which rank below it.
struct MediaModifier final
{
    Media *medium;
    const Interior *interior;
    size_t below;
};
typedef std::vector<MediaModifier> MediaModifierVector;

/// SceneData::mediaWarningFlags bits for media refraction warnings, each given once per render.
const unsigned kMediaRefractionJump = 64, kMediaRefractionHidden = 128, kMediaRefractionTrapped = 256, kMediaRefractionIndex = 512;

/// The media a ray's interiors show, in precedence order; without modifiers every interior adds its media.
/// Transport leaves out media that only refract; refraction keeps only media that refract.
enum class MediaRole { kTransport, kRefraction };
void CollectInteriorMedia(const RayInteriorVector& interiors, MediaVector& medias, MediaModifierVector *modifiers,
                          MediaRole role = MediaRole::kTransport);

/// The media refraction on a stack of interiors: the refractive index there is the base ior plus Offset().
class RefractionField final
{
    public:
        RefractionField(const RayInteriorVector& interiors, TraceThreadData *td);
        /// Whether the index varies anywhere, so rays inside curve.
        bool Varies() const { return varies; }
        /// Sum of refraction x density over the media at a point, after their blends.
        DBL Offset(const Vector3d& point);
        DBL Offset(const Vector3d& point, Vector3d& gradient);
        /// The longest step a curved ray may take through it.
        DBL MaxStep() const { return maxStep; }
        /// Whether a straight line meets the bounds of its varying media, and from what distance.
        bool Ahead(const Vector3d& origin, const Vector3d& direction, DBL& entry) const;
    private:
        MediaVector medias;
        MediaModifierVector modifiers;
        TraceThreadData *threadData;
        std::vector<DBL> terms;
        Vector3d low, high;
        DBL epsilon, maxStep;
        bool varies, bounded;

        DBL MeanDensity(Media& medium, const Vector3d& point);
};

class ExtinctionPlan;

class MediaFunction : public Trace::MediaFunctor
{
    public:
        MediaFunction(TraceThreadData *td, Trace *t, PhotonGatherer *pg);

        virtual void ComputeMedia(std::vector<Media>& mediasource, const Ray& ray, Intersection& isect, MathColour& colour, ColourChannel& transm) override;
        virtual void ComputeMedia(const RayInteriorVector& mediasource, const Ray& ray, Intersection& isect, MathColour& colour, ColourChannel& transm) override;
        virtual void ComputeMedia(MediaVector& medias, const Ray& ray, Intersection& isect, MathColour& colour, ColourChannel& transm) override;
        void ComputeMedia(MediaVector& medias, const MediaModifierVector *mods, const Ray& ray, Intersection& isect, MathColour& colour, ColourChannel& transm);
    protected:
        /// The current segment's subtract and multiply interiors, or nullptr.
        const MediaModifierVector *modifiers;
        std::vector<MathColour> modifierScratch, densityScratch;

        struct ModifierScope final
        {
            MediaFunction& owner;
            const MediaModifierVector *saved;
            ModifierScope(MediaFunction& f, const MediaModifierVector *m) : owner(f), saved(f.modifiers)
                { owner.modifiers = (m != nullptr && !m->empty()) ? m : nullptr; }
            ~ModifierScope() { owner.modifiers = saved; }
        };

        /// The finest prepared step of the modifiers' varying densities, or HUGE_VAL.
        DBL ModifierResolution() const;
        /// Where a modifier's density comes from: its prepared grid for camera samples or field marching, else its pattern.
        enum class ModifierSource { kPattern, kCamera, kField };
        /// A modifier medium's density at a point, from its prepared grid where the source allows and the grid holds it.
        void ModifierDensity(Media& medium, const Vector3d& point, ModifierSource source, MathColour& local);
        /// Adds the media's coefficients at a point, density[i] being medias[i]'s density there, after the modifiers;
        /// the emission of media that are lights only with lightEmission.
        void AddModifiedCoefficients(MediaVector& medias, const MathColour *density, const Vector3d& point, ModifierSource source,
                                     MathColour& extinction, MathColour *emission, MathColour *scattering, bool lightEmission = true);

        /// The key the current ray's media draws are hashed from.
        std::uint64_t drawKey;
        /// thread data
        TraceThreadData *threadData;
        /// tracing functions
        Trace *trace;
        /// photon gather functions
        PhotonGatherer *photonGatherer;
        /// media samples taken so far on the current ray, and its random shift of the area light sequence (negative until drawn)
        unsigned int lightSampleIndex;
        Vector2d lightSampleShift;

        void ComputeMediaRegularSampling(MediaVector& medias, LightSourceEntryVector& lights, MediaIntervalVector& mediaintervals,
                                         const Ray& ray, const Media *IMedia, int minsamples, bool ignore_photons, bool use_scattering,
                                         bool all_constant_and_light_ray);
        void ComputeMediaFixedSampling(MediaVector& medias, LightSourceEntryVector& lights, MediaIntervalVector& mediaintervals,
                                       const Ray& ray, DBL resolution, bool ignore_photons, bool use_scattering);
        void ComputeMediaAdaptiveSampling(MediaVector& medias, LightSourceEntryVector& lights, MediaIntervalVector& mediaintervals,
                                          const Ray& ray, const Media *IMedia, DBL aa_threshold, int minsamples, bool ignore_photons, bool use_scattering);
        void ComputeMediaColour(MediaIntervalVector& mediaintervals, MathColour& colour, ColourChannel& transm);
        /// Optical depth of each interval of a shadow ray: extinction alone, at the points the media's sampling uses.
        void ComputeMediaTransmittance(MediaVector& medias, MediaIntervalVector& mediaintervals, const Ray& ray, const Media *IMedia);
        /// ComputeMediaTransmittance at every point.
        void ComputeMediaPointTransmittance(MediaVector& medias, MediaIntervalVector& mediaintervals, const Ray& ray,
                                            const ExtinctionPlan *plan, bool method3, int points);
        DBL PreparedResolution(MediaVector& medias, const Ray& ray);
        bool PrepareFields(MediaVector& medias, const Ray& ray);
        DBL PreparedSteps(MediaVector& medias, const Ray& ray, DBL from, DBL to, DBL resolution);
        void SplitPreparedIntervals(MediaVector& medias, const Ray& ray, MediaIntervalVector& intervals);
        DBL PreparedStep(MediaVector& medias, const Ray& ray, const MediaInterval& interval, DBL fallback);
        void PreparedRange(MediaVector& medias, const Ray& ray, DBL& from, DBL& to);
        void ComputeMediaFieldTransmittance(MediaVector& medias, MediaIntervalVector& mediaintervals, const Ray& ray, DBL resolution);
        /// ComputeMediaTransmittance where every density bounds its extinction along a segment; rayLo and rayHi bound the ray.
        void ComputeMediaBoundedTransmittance(MediaVector& medias, MediaIntervalVector& mediaintervals, const Ray& ray,
                                              const ExtinctionPlan& plan, bool method3, int points,
                                              const MathColour& rayLo, const MathColour& rayHi);
        /// Extinction at each of n <= kDensityBatch depths along the ray, from plan where there is one.
        void ComputeMediaExtinction(MediaVector& medias, const ExtinctionPlan *plan, const Ray& ray, const DBL *depths,
                                    MathColour *extinction, size_t n);
        void ComputeMediaSampleInterval(LitIntervalVector& litintervals, MediaIntervalVector& mediaintervals, const Media *media,
                                        bool fixed = false);
        void ComputeMediaLightInterval(LightSourceEntryVector& lights, LitIntervalVector& litintervals, const Ray& ray, const Intersection& isect);
        void ComputeOneMediaLightInterval(LightSource *light, LightSourceEntryVector&lights, const Ray& ray, const Intersection& isect);
        bool ComputeSpotLightInterval(const Ray &ray, const LightSource *Light, DBL *d1, DBL *d2);
        bool ComputeCylinderLightInterval(const Ray &ray, const LightSource *Light, DBL *d1, DBL *d2);
        void ComputeOneMediaSample(MediaVector& medias, LightSourceEntryVector& lights, MediaInterval& mediainterval, const Ray &ray, DBL d0, MathColour& SampCol,
                                   MathColour& SampOptDepth, int sample_method, bool ignore_photons, bool use_scattering, bool photonPass, bool prepared = false);
        void ComputeOneMediaSampleRecursive(MediaVector& medias, LightSourceEntryVector& lights, MediaInterval& mediainterval, const Ray& ray,
                                            DBL d1, DBL d3, MathColour& Result, const MathColour& C1, const MathColour& C3, MathColour& ODResult, const MathColour& od1, const MathColour& od3,
                                            int depth, DBL Jitter, DBL aa_threshold, bool ignore_photons, bool use_scattering, bool photonPass, std::uint64_t key);
        void ComputeMediaPhotons(MediaVector& medias, MathColour& Te, const MathColour& Sc, const BasicRay& ray, const Vector3d& H);
        void ComputeMediaScatteringAttenuation(MediaVector& medias, MathColour& OutputColor, const MathColour& Sc, const MathColour& Light_Colour, const BasicRay &ray, const BasicRay &Light_Ray);
};

/// @}
///
//##############################################################################

}
// end of namespace pov

#endif // POVRAY_CORE_MEDIA_H
