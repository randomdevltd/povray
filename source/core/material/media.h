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
#include <functional>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

// POV-Ray header files (base module)
//  (none at the moment)

// POV-Ray header files (core module)
#include "core/render/trace.h"
#include "core/material/preparedrefractionaxis.h"

namespace pov
{
class PreparedRefractionDefinition;

struct RefractionSpec final
{
    DBL detail = 0.0;
    std::vector<GenericScalarFunctionPtr> functions;
    TRANSFORM *transform = nullptr;
    bool preparedParallelSafe = false;
    RefractionSpec() = default;
    RefractionSpec(const RefractionSpec&);
    ~RefractionSpec();
    void Transform(const TRANSFORM *mapping);
};

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
class PreparedDirectionalSourceField;
class PreparedSourceDistribution;

/// The light an interior's `light { }` block gives from its container: a table of each cell's power, its radiance
/// times the media's density-weighted extinction, or their mean chord when none has any; nullptr and why if none.
std::shared_ptr<const Emitter> MakeInteriorEmitter(const Interior& interior, ObjectPtr container, TraceThreadData *td, std::string& failure);
std::shared_ptr<const Emitter> MakeBouncedInteriorEmitter(const Emitter& primary, std::shared_ptr<const PreparedDirectionalSourceField> field, TraceThreadData *td, bool includePrimary = false,
    std::shared_ptr<const PreparedSourceDistribution> proposal = nullptr);
void PrepareInteriorFields(ObjectPtr owner, TraceThreadData *td, const std::function<void()>& cooperate);
void PrepareAtmosphereFields(TraceThreadData *td, const std::function<void()>& cooperate);
std::shared_ptr<const PreparedGridLayout> PrepareInteriorFieldLayout(ObjectPtr owner, TraceThreadData *td);
/// The work units PrepareLightFields reports: interiors that may refract, then lights.
std::size_t PreparedLightUnits(const SceneData& scene);
void PrepareLightFields(TraceThreadData *td, const std::function<void()>& cooperate);

/// A `subtract` or `multiply` medium on a ray; it changes the media collected before it, which rank below it.
struct MediaModifier final
{
    Media *medium;
    const Interior *interior;
    size_t below;
};
typedef std::vector<MediaModifier> MediaModifierVector;

/// SceneData::mediaWarningFlags bits for refraction field warnings, each given once per render.
const unsigned kRefractionJump = 64, kThinOverlap = 128, kRefractionTrapped = 256, kRefractionIndex = 512;

/// The media a ray's interiors show, in precedence order; without modifiers every interior adds its media.
void CollectInteriorMedia(const RayInteriorVector& interiors, MediaVector& medias, MediaModifierVector *modifiers);

struct BounceScatteringComponent final
{
    int type;
    double eccentricity;
    MathColour coefficient;
};

std::vector<BounceScatteringComponent> EvaluateOwnerBounceScattering(
    const RayInteriorVector& interiors, const Interior& owner, const Vector3d& point, TraceThreadData *td);

/// The refraction on a stack of interiors: the refractive index there is the base ior plus Offset().
class RefractionField final
{
    public:
        explicit RefractionField(const RayInteriorVector& interiors);
        /// Whether the index varies anywhere, so rays inside curve.
        bool Varies() const { return varies; }
        /// Sum of the interiors' refraction fields at a point.
        DBL Offset(const Vector3d& point);
        DBL Offset(const Vector3d& point, Vector3d& gradient);
        /// The longest step a curved ray may take through it.
        DBL MaxStep(const Vector3d& point) const;
        /// Whether a straight line meets the bounds of its varying media, and from what distance.
        bool Ahead(const Vector3d& origin, const Vector3d& direction, DBL& entry) const;
    private:
        std::unique_ptr<PreparedRefractionAxis> axis;
        std::vector<const PreparedRefractionDefinition *> fields;
        DBL maxStep;
        bool varies, bounded;

};

class ExtinctionPlan;
struct ProgressiveVolumeDomain;

class MediaFunction : public Trace::MediaFunctor
{
    public:
        MediaFunction(TraceThreadData *td, Trace *t, PhotonGatherer *pg);
        virtual std::unique_ptr<Trace::MediaFunctor::Path> BeginPath(const Ray& ray) override;
        virtual void ScalePathRadiance(const MathColour& factor) override;
        virtual void SetPathOffset(DBL offset) override;

        virtual void ComputeMedia(std::vector<Media>& mediasource, const Ray& ray, Intersection& isect, MathColour& colour, ColourChannel& transm) override;
        virtual void ComputeMedia(const RayInteriorVector& mediasource, const Ray& ray, Intersection& isect, MathColour& colour, ColourChannel& transm) override;
        virtual void ComputeMedia(MediaVector& medias, const Ray& ray, Intersection& isect, MathColour& colour, ColourChannel& transm) override;
        void ComputeMedia(MediaVector& medias, const MediaModifierVector *mods, const Ray& ray, Intersection& isect, MathColour& colour, ColourChannel& transm);
        virtual ConstObjectPtr SetPathReceiver(ConstObjectPtr receiver) override
            { const ConstObjectPtr previous = pathReceiver; pathReceiver = receiver; return previous; }
    protected:
        struct FixedLightPath;
        FixedLightPath *currentPath;
        /// The current segment's subtract and multiply interiors, or nullptr.
        const MediaModifierVector *modifiers;
        std::vector<MathColour> modifierScratch, densityScratch;
        std::shared_ptr<const ProgressiveVolumeDomain> volumeDomain;
        std::shared_ptr<const ProgressiveVolumeDomain> VolumeDomain(const MediaVector& medias);

        struct ModifierScope final
        {
            MediaFunction& owner;
            const MediaModifierVector *saved;
            ModifierScope(MediaFunction& f, const MediaModifierVector *m) : owner(f), saved(f.modifiers)
                { owner.modifiers = (m != nullptr && !m->empty()) ? m : nullptr; }
            ~ModifierScope() { owner.modifiers = saved; }
        };

        /// The surface ending the current segment, whose light membership decides which media photons it receives.
        ConstObjectPtr photonReceiver = nullptr, pathReceiver = nullptr;
        struct ReceiverScope final
        {
            MediaFunction& owner;
            ConstObjectPtr saved;
            ReceiverScope(MediaFunction& f, ConstObjectPtr receiver) : owner(f), saved(f.photonReceiver) { owner.photonReceiver = receiver; }
            ~ReceiverScope() { owner.photonReceiver = saved; }
        };

        /// The finest prepared step of the modifiers' varying densities, or HUGE_VAL.
        DBL ModifierResolution(const Ray& ray, DBL from, DBL to) const;
        /// Where a modifier's density comes from: its prepared grid for camera samples or field marching, else its pattern.
        enum class ModifierSource { kPattern, kCamera, kField };
        /// A modifier medium's density at a point, from its prepared grid where the source allows and the grid holds it.
        void ModifierDensity(Media& medium, const Vector3d& point, ModifierSource source, MathColour& local);
        /// Adds the media's coefficients at a point, density[i] being medias[i]'s density there, after the modifiers;
        void AddModifiedCoefficients(MediaVector& medias, const MathColour *density, const Vector3d& point, ModifierSource source,
                                     MathColour& extinction, MathColour *emission, MathColour *scattering, const DBL *phaseCosine = nullptr);

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
                                       const Ray& ray, DBL resolution, bool ignore_photons, bool use_scattering, DBL coarsening = 1.0);
        void ComputeMediaConstantSampling(MediaVector& medias, LightSourceEntryVector& lights, MediaIntervalVector& intervals,
                                          const Ray& ray, bool use_scattering, bool prepared);
        void CollectLegacyLightIntervals(MediaVector& medias, LightSourceEntryVector& lights, const MediaIntervalVector& intervals,
                                          const Ray& ray);
        void ComputeMediaAdaptiveSampling(MediaVector& medias, LightSourceEntryVector& lights, MediaIntervalVector& mediaintervals,
                                          const Ray& ray, const Media *IMedia, DBL aa_threshold, int minsamples, bool ignore_photons, bool use_scattering);
        void ComputeMediaColour(MediaIntervalVector& mediaintervals, MathColour& colour, ColourChannel& transm);
        /// Optical depth of each interval of a shadow ray: extinction alone, at the points the media's sampling uses.
        void ComputeMediaTransmittance(MediaVector& medias, MediaIntervalVector& mediaintervals, const Ray& ray, const Media *IMedia);
        /// ComputeMediaTransmittance at every point.
        void ComputeMediaPointTransmittance(MediaVector& medias, MediaIntervalVector& mediaintervals, const Ray& ray,
                                            const ExtinctionPlan *plan, bool method3, int points);
        DBL PreparedResolution(MediaVector& medias, const Ray& ray, DBL end);
        bool PreparedRequired(const MediaVector& medias, const Ray& ray) const;
        bool ConstantMediaCoefficients(const MediaVector& medias, const Ray& ray) const;
        bool PrepareFields(MediaVector& medias, const Ray& ray);
        DBL PreparedSteps(MediaVector& medias, const Ray& ray, DBL from, DBL to, DBL resolution, DBL coarsening = 1.0);
        /// Coarsens the prepared resolution uniformly until the march fits the step budget; coarsening 1 when it already fits.
        void CoarsenPreparedResolution(MediaVector& medias, const Ray& ray, DBL from, DBL to, DBL resolution, DBL& coarsening);
        void SplitPreparedIntervals(MediaVector& medias, const Ray& ray, MediaIntervalVector& intervals);
        DBL PreparedStep(MediaVector& medias, const Ray& ray, const MediaInterval& interval, DBL fallback, DBL coarsening = 1.0);
        void PreparedRange(MediaVector& medias, const Ray& ray, DBL& from, DBL& to);
        void ComputeMediaFieldTransmittance(MediaVector& medias, MediaIntervalVector& mediaintervals, const Ray& ray,
                                            DBL resolution, DBL coarsening = 1.0);
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
        void ComputeMediaCoefficients(MediaVector& medias, const Ray& ray, const Vector3d& point, bool prepared,
                                      MathColour& emission, MathColour& extinction, MathColour& scattering);
        void DensityForDeclaredMode(Media& medium, const Vector3d& point, MathColour& density);
        void ComputeMediaSampleLighting(MediaVector& medias, LightSourceEntryVector& lights,
                                        const MediaInterval& interval, const Ray& ray, DBL depth, const Vector3d& point,
                                        const MathColour& scattering, MathColour& emission, bool prepared, bool use_scattering,
                                        bool volumeLighting = true);
        void ComputeOneMediaSample(MediaVector& medias, LightSourceEntryVector& lights, MediaInterval& mediainterval, const Ray &ray, DBL d0, MathColour& SampCol,
                                   MathColour& SampOptDepth, int sample_method, bool ignore_photons, bool use_scattering, bool photonPass, bool prepared = false,
                                   MathColour *scattering = nullptr);
        /// Adds to Emission the light the media's scattering coefficients Scattering send along the ray at depth d1 (point P).
        void ComputeMediaLight(MediaVector& medias, LightSourceEntryVector& lights, const MediaInterval& mediainterval, const Ray& ray,
                               DBL d1, const Vector3d& P, const MathColour& Scattering, MathColour& Emission, bool volumeLighting = true, bool preparedSampling = false);
        /// The step the scattering media would sample an interval at alone: a uniform one by its samples, a prepared one by its grid.
        DBL ScatteringStep(MediaVector& medias, const MediaInterval& interval, const Ray& ray) const;
        void ComputeOneMediaSampleRecursive(MediaVector& medias, LightSourceEntryVector& lights, MediaInterval& mediainterval, const Ray& ray,
                                            DBL d1, DBL d3, MathColour& Result, const MathColour& C1, const MathColour& C3, MathColour& ODResult, const MathColour& od1, const MathColour& od3,
                                            int depth, DBL Jitter, DBL aa_threshold, bool ignore_photons, bool use_scattering, bool photonPass, std::uint64_t key);
        void ComputeMediaPhotons(MediaVector& medias, MathColour& Te, const MathColour& Sc, const BasicRay& ray, const Vector3d& H);
        void ComputePhysicalMediaScattering(MediaVector& medias, MathColour& output, const MathColour& incoming,
                                            const Vector3d& point, const BasicRay& ray, const BasicRay& lightRay);
        void ComputeMediaScatteringAttenuation(MediaVector& medias, MathColour& OutputColor, const MathColour& Sc, const MathColour& Light_Colour, const BasicRay &ray, const BasicRay &Light_Ray);
};

/// Fills unclaimed slabs of an eager density grid on `thread` until none are left, it is stopped or `stop` says so (with no
/// `stop`, a stopped grid is filled too); returns the TraceThreadData::globalsRead bits that reading took.
unsigned char WorkEagerField(Media::EagerField& field, TraceThreadData *thread, const std::function<bool()>& stop);
/// How many of its slabs have been claimed so far, and how many it has.
void EagerFieldSlabs(const Media::EagerField& field, int& claimed, int& total);
/// Stops the eager threads claiming more of its slabs; whoever finishes it fills the rest.
void StopEagerField(Media::EagerField& field);
/// The CPU time its fills took.
POV_LONG EagerFieldCpuTime(const Media::EagerField& field);
/// Adds the prepared caches whose container is `object` itself, the ones a kept object may hand a grid to.
void CollectPreparedCaches(ConstObjectPtr object, std::unordered_set<const void *>& caches);
/// Whether the medium it was planned for still has its cache, and that cache is one of `caches`.
bool EagerFieldKept(const Media::EagerField& field, const std::unordered_set<const void *>& caches);
/// For a kept field only: fills what the eager threads left on `threads`, then gives the grid to its medium if the container's bounds
/// and `cellLimit` still match and `changed` finds no setting it read changed; returns whether it did. Else the render builds it.
bool FinishEagerField(const std::shared_ptr<Media::EagerField>& field, DBL cellLimit, const std::vector<TraceThreadData *>& threads,
                      const std::function<bool(unsigned char)>& changed, const std::function<void()>& cooperate);

/// @}
///
//##############################################################################

}
// end of namespace pov

#endif // POVRAY_CORE_MEDIA_H
