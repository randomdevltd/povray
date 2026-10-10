//******************************************************************************
///
/// @file core/render/trace.h
///
/// Declarations related to the @ref pov::Trace class.
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

#ifndef POVRAY_CORE_TRACE_H
#define POVRAY_CORE_TRACE_H

// Module config header file must be the first file included within POV-Ray unit header files
#include "core/configcore.h"

// C++ variants of C standard header files
//  (none at the moment)

// C++ standard header files
#include <memory>
#include <unordered_map>
#include <vector>

// POV-Ray header files (base module)
//  (none at the moment)

// POV-Ray header files (core module)
#include "core/coretypes.h"
#include "core/bounding/bsptree.h"
#include "core/lighting/subsurface.h"
#include "core/material/texture.h"
#include "core/math/randomsequence.h"
#include "core/render/ray.h"
#include "core/scene/atmosphere_fwd.h"

namespace pov
{

//##############################################################################
///
/// @defgroup PovCoreRender Ray Tracing
/// @ingroup PovCore
///
/// @{

class PhotonGatherer;
struct Photon;
class Portal;
struct ScreenPattern;
class RefractionField;

struct NoSomethingFlagRayObjectCondition final  : public RayObjectCondition
{
    virtual bool operator()(const Ray& ray, ConstObjectPtr object, double) const override;
};

struct HasInteriorPointObjectCondition final : public PointObjectCondition
{
    virtual bool operator()(const Vector3d& point, ConstObjectPtr object) const override;
};

struct ContainingInteriorsPointObjectCondition final : public PointObjectCondition
{
    ContainingInteriorsPointObjectCondition(RayInteriorVector& ci) : containingInteriors(ci) {}
    virtual bool operator()(const Vector3d& point, ConstObjectPtr object) const override;
    RayInteriorVector &containingInteriors;
};

struct LitInterval final
{
    bool lit;
    double s0, s1, ds;
    size_t l0, l1;

    LitInterval() :
        lit(false), s0(0.0), s1(0.0), ds(0.0), l0(0), l1(0) { }
    LitInterval(bool nlit, double ns0, double ns1, size_t nl0, size_t nl1) :
        lit(nlit), s0(ns0), s1(ns1), ds(ns1 - ns0), l0(nl0), l1(nl1) { }
};

struct MediaInterval final
{
    bool lit;
    int samples;
    double s0, s1, ds;
    size_t l0, l1;
    MathColour od;
    MathColour te;
    MathColour te2;

    MediaInterval() :
        lit(false), samples(0), s0(0.0), s1(0.0), ds(0.0), l0(0), l1(0) { }
    MediaInterval(bool nlit, int nsamples, double ns0, double ns1, double nds, size_t nl0, size_t nl1) :
        lit(nlit), samples(nsamples), s0(ns0), s1(ns1), ds(nds), l0(nl0), l1(nl1) { }
    MediaInterval(bool nlit, int nsamples, double ns0, double ns1, double nds, size_t nl0, size_t nl1, const MathColour& nod, const MathColour& nte, const MathColour& nte2) :
        lit(nlit), samples(nsamples), s0(ns0), s1(ns1), ds(nds), l0(nl0), l1(nl1), od(nod), te(nte), te2(nte2) { }

    bool operator<(const MediaInterval& other) const { return (s0 < other.s0); }
};

struct LightSourceIntersectionEntry final
{
    double s;
    size_t l;
    bool lit;

    LightSourceIntersectionEntry() :
        s(0.0), l(0), lit(false) { }
    LightSourceIntersectionEntry(double ns, size_t nl, bool nlit) :
        s(ns), l(nl), lit(nlit) { }

    bool operator<(const LightSourceIntersectionEntry& other) const { return (s < other.s); }
};

struct LightSourceEntry final
{
    double s0, s1;
    LightSource *light;

    LightSourceEntry() :
        s0(0.0), s1(0.0), light(nullptr) { }
    LightSourceEntry(LightSource *nlight) :
        s0(0.0), s1(0.0), light(nlight) { }
    LightSourceEntry(double ns0, double ns1, LightSource *nlight) :
        s0(ns0), s1(ns1), light(nlight) { }

    bool operator<(const LightSourceEntry& other) const { return (s0 < other.s0); }
};

// TODO: these sizes will need tweaking.
typedef PooledSimpleVector<Media *, MEDIA_VECTOR_SIZE> MediaVector;
typedef PooledSimpleVector<MediaInterval, MEDIA_INTERVAL_VECTOR_SIZE> MediaIntervalVector;
typedef PooledSimpleVector<LitInterval, LIT_INTERVAL_VECTOR_SIZE> LitIntervalVector;
typedef PooledSimpleVector<LightSourceIntersectionEntry, LIGHT_INTERSECTION_VECTOR_SIZE> LightSourceIntersectionVector;
typedef PooledSimpleVector<LightSourceEntry, LIGHTSOURCE_VECTOR_SIZE> LightSourceEntryVector;


struct TraceTicket final
{
    /// trace recursion level
    unsigned int traceLevel;

    /// maximum trace recursion level allowed
    unsigned int maxAllowedTraceLevel;

    /// maximum trace recursion level found
    unsigned int maxFoundTraceLevel;

    /// adc bailout
    double adcBailout;

    /// whether background should be rendered all transparent
    bool alphaBackground;

    /// something the radiosity algorithm needs
    unsigned int radiosityRecursionDepth;

    /// something the radiosity algorithm needs
    float radiosityImportanceQueried;
    /// something the radiosity algorithm needs
    float radiosityImportanceFound;
    /// set by radiosity code according to the sample quality encountered (1.0 is ideal, 0.0 really sucks)
    float radiosityQuality;

    /// something the subsurface scattering algorithm needs
    unsigned int subsurfaceRecursionDepth;

    /// share of its radiosity sample that a ray below a gather ray carries, for Russian roulette
    double radiosityShare;

    /// eye whose distance spaces radiosity samples; null takes the radiosity functor's camera
    const Vector3d *radiosityEye;
    PreparedSetId preparedSetId;

    TraceTicket(unsigned int mtl, double adcb, bool ab = true, unsigned int rrd = 0, unsigned int ssrd = 0,
                float riq = -1.0, float rq = 1.0):
        traceLevel(0), maxAllowedTraceLevel(mtl), maxFoundTraceLevel(0), adcBailout(adcb), alphaBackground(ab),
        radiosityRecursionDepth(rrd), subsurfaceRecursionDepth(ssrd), radiosityImportanceQueried(riq),
        radiosityImportanceFound(-1.0), radiosityQuality(rq), radiosityShare(1.0), radiosityEye(nullptr), preparedSetId(0)
    {}
};


/// Ray tracing and shading engine.
///
/// This class provides the fundamental functionality to trace rays and determine the effective colour of an object.
///
class Trace
{
    public:

        /// @todo This interface might also come in hand at other places,
        /// so we should pull it out of the @ref Trace class.
        class CooperateFunctor
        {
            public:
                virtual ~CooperateFunctor() {}
                virtual void operator()() { }
        };

        class MediaFunctor
        {
            public:
                virtual ~MediaFunctor() {}
                virtual void ComputeMedia(std::vector<Media>&, const Ray&, Intersection&, MathColour&, ColourChannel&) { }
                virtual void ComputeMedia(const RayInteriorVector&, const Ray&, Intersection&, MathColour&, ColourChannel&) { }
                virtual void ComputeMedia(MediaVector&, const Ray&, Intersection&, MathColour&, ColourChannel&) { }
        };

        class RadiosityFunctor
        {
            public:
                virtual ~RadiosityFunctor() {}
                virtual void ComputeAmbient(const Vector3d& ipoint, const Vector3d& raw_normal, const Vector3d& layer_normal, double brilliance, MathColour& ambient_colour, double weight, TraceTicket& ticket) { }
                virtual bool CheckRadiosityTraceLevel(const TraceTicket& ticket) { return false; }
                virtual bool IsFinalTrace() const { return false; } ///< whether the pretrace's samples are all there
                virtual bool LookupPretraceAmbient(const Vector3d& ipoint, const Vector3d& normal, MathColour& ambient_colour, PreparedSetId preparedSetId) { return false; }
        };

        /// @todo TraceThreadData already holds a reference to SceneData.
        Trace(std::shared_ptr<SceneData> sd, TraceThreadData *td, const QualityFlags& qf,
              CooperateFunctor& cf, MediaFunctor& mf, RadiosityFunctor& af);

        virtual ~Trace();

        /// Make rays with differentials average their pigments over this multiple of the pixel footprint; zero turns it off.
        void SetTextureFilterScale(DBL scale) { textureFilterScale = scale; }
        /// Start each filtered pigment with 3 taps, or with 8 (the default).
        void SetTextureFilterTaps(int taps) { textureFilterTaps = taps; }

        /// The object the last primary ray hit, and its top layer's pigment there.
        const void *primaryObject = nullptr;
        TransColour primaryPigment;

        /// Trace a ray.
        ///
        /// Call this if transmittance matters.
        ///
        /// @param[in,out]  ray             Ray and associated information.
        /// @param[out]     colour          Computed colour.
        /// @param[out]     transm          Computed transmittance.
        /// @param[in]      weight          Importance of this computation.
        /// @param[in]      continuedRay    Set to true when tracing a ray after it went through some surface
        ///                                 without a change in direction; this governs trace level handling.
        /// @param[in]      maxDepth        Objects at or beyond this distance won't be hit by the ray (ignored if
        ///                                 < EPSILON).
        /// @param[in]      missOpen        Whether a ray that hits nothing returns at once, leaving sky and atmosphere to the caller.
        /// @return                         The distance to the nearest object hit.
        ///
        virtual double TraceRay(Ray& ray, MathColour& colour, ColourChannel& transm, COLC weight, bool continuedRay, DBL maxDepth = 0.0,
                                bool missOpen = false);
/*
        /// Trace a ray.
        ///
        /// Call this if transmittance will be ignored anyway.
        ///
        /// @param[in,out]  ray             Ray and associated information.
        /// @param[out]     colour          Computed colour.
        /// @param[in]      weight          Importance of this computation.
        /// @param[in]      continuedRay    Set to true when tracing a ray after it went through some surface
        ///                                 without a change in direction; this governs trace level handling.
        /// @param[in]      maxDepth        Objects at or beyond this distance won't be hit by the ray (ignored if
        ///                                 < EPSILON).
        /// @return                         The distance to the nearest object hit.
        ///
        virtual double TraceRay(Ray& ray, MathColour& colour, COLC weight, bool continuedRay, DBL maxDepth = 0.0);
*/
        bool FindIntersection(Intersection& isect, const Ray& ray);
        bool FindIntersection(Intersection& isect, const Ray& ray, const RayObjectCondition& precondition, const RayObjectCondition& postcondition);
        bool FindIntersection(ObjectPtr object, Intersection& isect, const Ray& ray, double closest = HUGE_VAL);
        bool FindIntersection(ObjectPtr object, Intersection& isect, const Ray& ray, const RayObjectCondition& postcondition, double closest = HUGE_VAL);
        /// Adds the interiors of the objects containing a point.
        void FindContainingInteriors(const Vector3d& point, RayInteriorVector& found, PreparedSetId preparedSetId = 0);
        /// Whether interior media on the ray's interiors are integrated: a non-hollow one hides them only before version 4.0.
        bool InteriorMediaReach(const Ray& ray) const;

        unsigned int GetHighestTraceLevel();

        /// Traces a `screen`'s view at window point (u, v), v up; false where screens nest too deep to trace.
        bool TraceScreen(const ScreenPattern& screen, double u, double v, const Intersection *isect, const Ray& ray,
                         COLC weight, TransColour& result);

        /// Whether a random draw has shaped a trace's result since the last @ref ClearGrain(): partly shadowed
        /// jittered area lights, media, `crand`, rainbow jitter and subsurface light.
        bool Grainy() const { return grain; }
        void ClearGrain() { grain = false; }
        void MarkGrain() { grain = true; }

        bool TestShadow(const LightSource &light, double& depth, Ray& light_source_ray, const Vector3d& p, MathColour& colour,
                        const Vector2d* areaSample = nullptr); // TODO FIXME - this should not be exposed here

    protected: // TODO FIXME - should be private

        /// Structure used to cache reflection information for multi-layered textures.
        struct WNRX final
        {
            double weight;
            Vector3d normal;
            MathColour reflec;
            SNGL reflex;

            WNRX(DBL w, const Vector3d& n, const MathColour& r, SNGL x) :
                weight(w), normal(n), reflec(r), reflex(x) { }
        };

        /// How a hit point and its unperturbed normal change per image pixel step in x and y.
        struct SurfaceDifferentials final
        {
            Vector3d dPdx, dPdy, dNdx, dNdy;
            bool haveNormal = false;
        };

        /// Footprint means a filtered layer's shading needs that its averaged colour cannot give where taps filter.
        struct FilteredLayer final
        {
            MathColour transmitted;
            DBL opacity = 1.0;
            bool filters = false;
        };

        typedef std::vector<const TEXTURE*> TextureVectorData;
        typedef RefPool<TextureVectorData> TextureVectorPool;
        typedef Ref<TextureVectorData, RefClearContainer<TextureVectorData>> TextureVector;

        typedef std::vector<WNRX> WNRXVectorData;
        typedef RefPool<WNRXVectorData> WNRXVectorPool;
        typedef Ref<WNRXVectorData, RefClearContainer<WNRXVectorData>> WNRXVector;

        /// Structure used to cache shadow test results for complex textures.
        struct LightColorCache final
        {
            bool        tested;
            MathColour  colour;
        };

        typedef std::vector<LightColorCache> LightColorCacheList;
        typedef std::vector<LightColorCacheList> LightColorCacheListList;

        /// List (well really vector) of lists of LightColorCaches.
        /// Each list is expected to have as many elements as there are global light sources.
        /// The number of lists should be at least that of max trace level.
        LightColorCacheListList lightColorCache;

        /// Current index into lightColorCaches.
        int lightColorCacheIndex;

        /// A light's unshadowed contribution, pending its shadow test.
        struct LightCandidate final
        {
            const LightSource*  light;
            int                 index;
            double              depth;
            Vector3d            direction;
            MathColour          colour;
            bool                backside;
            MathColour          potential;
            double              weight;
        };

        /// Stack of lights awaiting shadow tests in @ref ComputeSampledDiffuseLight().
        std::vector<LightCandidate> lightCandidates;
        /// Stack of indices into @ref lightCandidates, brightest first.
        std::vector<size_t> lightOrder;

        /// Scene data.
        std::shared_ptr<SceneData> sceneData;

        /// Maximum trace recursion level found.
        unsigned int maxFoundTraceLevel;
        /// Various quality-related flags.
        QualityFlags qualityFlags;
        /// Pixel footprint scale that hits of rays with differentials average their pigments over; zero turns filtering off.
        DBL textureFilterScale = 0.0;
        int textureFilterTaps = 8;

        /// Bounding slabs priority queue.
        BBoxPriorityQueue priorityQueue;
        /// BSP tree mailbox.
        BSPTree::Mailbox mailbox;
        /// Area light grid buffer.
        std::vector<MathColour> lightGrid;
        /// Fast stack pool.
        IStackPool stackPool;
        /// Fast texture list pool.
        TextureVectorPool texturePool;
        /// Fast WNRX list pool.
        WNRXVectorPool wnrxPool;
        /// Light source shadow cache for shadow tests of first trace level intersections.
        std::vector<ObjectPtr> lightSourceLevel1ShadowCache;
        /// Light source shadow cache for shadow tests of higher trace level intersections.
        std::vector<ObjectPtr> lightSourceOtherShadowCache;
        size_t shadowCacheLights;
        /// Sub-random uniform directions; each diffuse subsurface sample takes one at a keyed place.
        IndexedVectorGeneratorPtr ssltUniformDirections;
        /// Whether a random draw has shaped the result since @ref ClearGrain().
        bool grain = false;
        /// The subsurface cache's camera, once set: where it is, and a pixel's span as size plus angle times distance.
        Vector3d ssltCameraLocation;
        double ssltPixelSize = 0.0, ssltPixelAngle = 0.0;
        bool ssltCameraKnown = false;
        /// Per light and channel, the lowest, mean and highest visibility of a few cloud points.
        struct SubsurfaceVisibility
        {
            std::vector<float> lo, mean, hi;
            void Reset(size_t lights);
            void Add(const float *visibility);
            void Finish();
            bool Agrees(int light) const;
            int count = 0;
        };
        /// Scratch space for subsurface cloud queries.
        std::vector<int> ssltScratchStack;
        std::vector<char> ssltScratchClosed;
        SubsurfaceVisibility ssltScratchVisibility;
        /// Thread data.
        TraceThreadData *threadData;

        CooperateFunctor& cooperate;
        MediaFunctor& media;
        /// Recent interfaces by hit point and interior: the surfaces meeting there merged into one, which TakeCoincident,
        /// ComputeRelativeIOR and ComputeInterfaceIor share for one hit.
        struct InterfaceCache final
        {
            RayInteriorVector before, after;
            Vector3d point;
            const Interior *interior = nullptr;
            ConstObjectPtr take = nullptr;       ///< the object whose surface there is hit instead, if not the hit's own
            std::vector<ConstObjectPtr> faces;  ///< clear objects whose textures blend there, empty unless two or more
            double leave = 0.0;
            bool photon = false;
        };
        static const size_t kInterfaceCacheSize = 16;
        InterfaceCache sidesCache[kInterfaceCacheSize];
        /// An object with a surface near a hit, and the depth of that surface along the probe.
        typedef std::pair<ConstObjectPtr, double> Surface;
        /// Objects with a surface where an interface is being merged, each once at its nearest depth, in SurfaceBefore order.
        std::vector<Surface> coincident;
        bool loneSurface = false;            ///< whether the probe met one surface only, so crossing it toggles its interior
        size_t InterfaceSlot(const Interior *interior, const Vector3d& point) const;
        const InterfaceCache& InterfaceSides(const Ray& ray, Interior *interior, const Vector3d& point, const Vector3d& normal);
        void FindInterfaceSides(InterfaceCache& sides, const Ray& ray, Interior *interior, const Vector3d& point, const Vector3d& normal);
        /// Fills `coincident` from a probe along the ray from `back` before `point` to `reach` past it.
        void FindCoincident(const Ray& ray, const Vector3d& point, double back, double reach, const RayObjectCondition& precond);
        /// Gives `after` (a copy of `before`) the interiors beyond the surfaces in `coincident`, `interior` being the hit's.
        void CrossCoincident(RayInteriorVector& after, const RayInteriorVector& before, Interior *interior,
                             const Vector3d& beyond, bool photon);
        void StartProbe(Ray& probe, const Ray& ray);
        RadiosityFunctor& radiosity;

    ///
    //*****************************************************************************
    ///
    /// @name Texture Computations
    ///
    /// The following methods compute the effective colour of a given, possibly complex, texture.
    ///
    /// @{
    ///

        /// Compute the effective contribution of an intersection point as seen from the ray's origin, or deposits
        /// photons.
        ///
        /// Computations include any media effects between the ray's origin and the point of intersection.
        ///
        /// @remark         The computed contribution is _added_ to the value passed in `colour` (does not apply to
        ///                 photon pass).
        ///
        /// @todo           Some input parameters are non-const references.
        ///
        /// @param[in]      isect           Intersection information.
        /// @param[in,out]  resultColour    Computed colour [in,out]; during photon pass: light colour [in].
        /// @param[in,out]  resultTransm    Computed transparency; not used during photon pass.
        /// @param[in,out]  ray             Ray and associated information.
        /// @param[in]      weight          Importance of this computation.
        /// @param[in]      photonpass      Whether to deposit photons instead of computing a colour
        ///
        void ComputeTextureColour(Intersection& isect, MathColour& resultColour, ColourChannel& resultTransm, Ray& ray, COLC weight, bool photonpass);

        /// Compute the effective colour of an arbitrarily complex texture, or deposits photons.
        ///
        /// @remark         The computed contribution _overwrites_ any value passed in `colour` (does not apply to
        ///                 photon pass).
        ///
        /// @remark         Computations do _not_ include media effects between the ray's origin and the point of
        ///                 intersection any longer.
        ///
        /// @todo           Some input parameters are non-const references or pointers.
        ///
        /// @param[in,out]  resultColour    Computed colour [out]; during photon pass: light colour [in].
        /// @param[out]     resultTransm    Computed transparency; not used during photon pass.
        /// @param[in]      texture         Texture.
        /// @param[in]      warps           Stack of warps to be applied.
        /// @param[in]      ipoint          Intersection point (possibly with earlier warps already applied).
        /// @param[in]      rawnormal       Geometric (possibly smoothed) surface normal.
        /// @param[in,out]  ray             Ray and associated information.
        /// @param[in]      weight          Importance of this computation.
        /// @param[in]      isect           Intersection information.
        /// @param[in]      shadowflag      Whether to perform only computations necessary for shadow testing.
        /// @param[in]      photonpass      Whether to deposit photons instead of computing a colour.
        ///
        void ComputeOneTextureColour(MathColour& resultColour, ColourChannel& resultTransm, const TEXTURE *texture, std::vector<const TEXTURE *>& warps,
                                     const Vector3d& ipoint, const Vector3d& rawnormal, Ray& ray, COLC weight,
                                     Intersection& isect, bool shadowflag, bool photonpass);

        /// Compute the effective colour of an averaged texture, or deposits photons.
        ///
        /// @remark         The computed contribution _overwrites_ any value passed in `colour` (does not apply to
        ///                 photon pass).
        ///
        /// @remark         Computations do _not_ include media effects between the ray's origin and the point of
        ///                 intersection any longer.
        ///
        /// @todo           Some input parameters are non-const references or pointers.
        ///
        /// @param[in,out]  resultColour    Computed colour [out]; during photon pass: light colour [in].
        /// @param[out]     resultTransm    Computed transparency; not used during photon pass.
        /// @param[in]      texture         Texture.
        /// @param[in]      warps           Stack of warps to be applied.
        /// @param[in]      ipoint          Intersection point (possibly with earlier warps already applied).
        /// @param[in]      rawnormal       Geometric (possibly smoothed) surface normal.
        /// @param[in,out]  ray             Ray and associated information.
        /// @param[in]      weight          Importance of this computation.
        /// @param[in]      isect           Intersection information.
        /// @param[in]      shadowflag      Whether to perform only computations necessary for shadow testing.
        /// @param[in]      photonpass      Whether to deposit photons instead of computing a colour.
        ///
        void ComputeAverageTextureColours(MathColour& resultColour, ColourChannel& resultTransm, const TEXTURE *texture, std::vector<const TEXTURE *>& warps,
                                          const Vector3d& ipoint, const Vector3d& rawnormal, Ray& ray, COLC weight,
                                          Intersection& isect, bool shadowflag, bool photonpass);

        /// Compute the effective colour of a simple or layered texture.
        ///
        /// Computations include secondary rays.
        ///
        /// @remark         The computed contribution _overwrites_ any value passed in `colour`.
        ///
        /// @remark         Computations do _not_ include media effects between the ray's origin and the point of
        ///                 intersection any longer.
        ///
        /// @remark         pov::PhotonTrace overrides this method to deposit photons instead.
        ///
        /// @todo           Some input parameters are non-const references or pointers.
        ///
        /// @param[in,out]  resultColour    Computed colour [out]; during photon pass: light colour [in].
        /// @param[out]     resultTransm    Computed transparency; not used during photon pass.
        /// @param[in]      texture         Texture.
        /// @param[in]      warps           Stack of warps to be applied.
        /// @param[in]      ipoint          Intersection point (possibly with earlier warps already applied).
        /// @param[in]      rawnormal       Geometric (possibly smoothed) surface normal.
        /// @param[in,out]  ray             Ray and associated information.
        /// @param[in]      weight          Importance of this computation.
        /// @param[in]      isect           Intersection information.
        ///
        virtual void ComputeLightedTexture(MathColour& resultColour, ColourChannel& resultTransm, const TEXTURE *texture, std::vector<const TEXTURE *>& warps,
                                           const Vector3d& ipoint, const Vector3d& rawnormal, Ray& ray, COLC weight,
                                           Intersection& isect);

        /// Transfer the ray's differentials to the tangent plane at the hit; false when the ray has none or grazes it.
        bool TransferDifferentials(const Ray& ray, const Intersection& isect, const Vector3d& rawnormal, SurfaceDifferentials& diff) const;

        /// Derivative of the unperturbed normal across the footprint, by central differences of the object's own normal.
        void ComputeNormalDifferentials(const Intersection& isect, const Vector3d& rawnormal, SurfaceDifferentials& diff);

        /// World-space footprint of the ray's image pixel at the hit to filter pigments over; false when it has none.
        bool ComputePixelFootprint(const Intersection& isect, const std::vector<const TEXTURE *>& warps, const Vector3d& ipoint,
                                   const SurfaceDifferentials& diff, Vector3d& footX, Vector3d& footY) const;

        /// Average a pigment over the pixel footprint with adaptively added, deterministic taps; `means` is set where a tap filters.
        bool ComputeFilteredPigment(TransColour& colour, FilteredLayer& means, const PIGMENT *pigment, const std::vector<const TEXTURE *>& warps,
                                    const Vector3d& footX, const Vector3d& footY, Intersection& isect, Ray& ray);

        /// Compute the effective filtering effect of a simple or layered texture.
        ///
        /// @remark         The computed contribution _overwrites_ any value passed in `colour`.
        ///
        /// @remark         Computations do _not_ include media effects between the ray's origin and the point of
        ///                 intersection any longer.
        ///
        /// @todo           Some input parameters are non-const references or pointers.
        ///
        /// @param[out]     filtercolour    Computed filter colour.
        /// @param[in]      texture         Texture.
        /// @param[in]      warps           Stack of warps to be applied.
        /// @param[in]      ipoint          Intersection point (possibly with earlier warps already applied).
        /// @param[in]      rawnormal       Geometric (possibly smoothed) surface normal.
        /// @param[in,out]  ray             Ray and associated information.
        /// @param[in]      isect           Intersection information.
        ///
        void ComputeShadowTexture(MathColour& filtercolour, const TEXTURE *texture, std::vector<const TEXTURE *>& warps,
                                  const Vector3d& ipoint, const Vector3d& rawnormal, const Ray& ray,
                                  Intersection& isect);

    ///
    /// @}
    ///
    //*****************************************************************************
    ///
    /// @name Reflection and Refraction Computations
    ///
    /// The following methods compute the contribution of secondary (reflected and refracted) rays.
    ///
    /// @{
    ///

        /// Shades a hit on a portal: the view beyond it where it is open and entered, the ray going on past it elsewhere.
        void TracePortal(const Portal& portal, Intersection& isect, Ray& ray, MathColour& colour, ColourChannel& transm, COLC weight);
        /// Traces the view through a portal entered at a hit; false where it leaves nothing to show.
        bool TracePortalView(const Portal& portal, Intersection& isect, const Ray& ray, const Vector3d& rawnormal, bool frontSide,
                             COLC weight, MathColour& view, ColourChannel& transm);

        /// Compute the refraction contribution.
        ///
        /// @remark         The computed contribution _overwrites_ any value passed in `colour`.
        ///
        /// @param[in]      finish          Object's finish.
        /// @param[in]      ipoint          Intersection point.
        /// @param[in,out]  ray             Ray and associated information.
        /// @param[in]      normal          Effective (possibly pertubed) surface normal.
        /// @param[in]      rawnormal       Geometric (possibly smoothed) surface normal.
        /// @param[out]     colour          Computed colour.
        /// @param[in]      weight          Importance of this computation.
        /// @param[in]      diff            Differentials at the hit for the reflected ray to carry, or `nullptr`.
        ///
        void ComputeReflection(const FINISH* finish, const Vector3d& ipoint, Ray& ray, const Vector3d& normal,
                               const Vector3d& rawnormal, MathColour& colour, COLC weight, const SurfaceDifferentials *diff = nullptr);

        /// Compute the refraction contribution.
        ///
        /// @remark         The computed contribution _overwrites_ any value passed in `colour`.
        ///
        /// @param[in]      finish          Object's finish.
        /// @param[in]      interior        Stack of currently effective interiors.
        /// @param[in]      ipoint          Intersection point.
        /// @param[in,out]  ray             Ray and associated information.
        /// @param[in]      normal          Effective (possibly pertubed) surface normal.
        /// @param[in]      rawnormal       Geometric (possibly smoothed) surface normal.
        /// @param[out]     colour          Computed colour.
        /// @param[out]     transm          Computed transmittance.
        /// @param[in]      weight          Importance of this computation.
        /// @param[in]      diff            Differentials at the hit for the refracted ray to carry, or `nullptr`.
        /// @return                         `true` if total internal reflection _did_ occur.
        ///
        bool ComputeRefraction(const FINISH* finish, Interior *interior, const Vector3d& ipoint, Ray& ray,
                               const Vector3d& normal, const Vector3d& rawnormal, MathColour& colour, ColourChannel& transm, COLC weight,
                               const SurfaceDifferentials *diff = nullptr);

        /// Compute the contribution of a single refracted ray.
        ///
        /// @remark         The computed contribution _overwrites_ any value passed in `colour`.
        ///
        /// @param[in]      finish          object's finish.
        /// @param[in]      ipoint          Intersection point.
        /// @param[in,out]  ray             Original ray and associated information.
        /// @param[in,out]  nray            Refracted ray [out] and associated information [in,out].
        /// @param[in]      ior             Relative index of refraction.
        /// @param[in]      n               Cosine of angle of incidence.
        /// @param[in]      normal          Effective (possibly pertubed) surface normal.
        /// @param[in]      rawnormal       Geometric (possibly smoothed) surface normal.
        /// @param[in]      localnormal     Effective surface normal, possibly flipped to match ray.
        /// @param[out]     colour          Computed colour.
        /// @param[out]     transm          Computed transmittance.
        /// @param[in]      weight          Importance of this computation.
        /// @param[in]      leave           How far past the surface the refracted ray starts.
        /// @param[in]      diff            Differentials at the hit for the refracted ray to carry, or `nullptr`.
        /// @return                         `true` if total internal reflection _did_ occur.
        ///
        bool TraceRefractionRay(const FINISH* finish, const Vector3d& ipoint, Ray& ray, Ray& nray, double ior, double n,
                                const Vector3d& normal, const Vector3d& rawnormal, const Vector3d& localnormal,
                                MathColour& colour, ColourChannel& transm, COLC weight, double leave,
                                const SurfaceDifferentials *diff = nullptr);

    ///
    /// @}
    ///
    //*****************************************************************************
    ///
    /// @name Classic Light Source Computations
    ///
    /// The following methods compute the (additional) contribution of classic lighting.
    ///
    /// @{
    ///

        /// @todo The name is misleading, as it computes all contributions of classic lighting, including highlights.
        void ComputeDiffuseLight(const FINISH *finish, const Vector3d& ipoint, const  Ray& eye, const Vector3d& layer_normal, const MathColour& layer_pigment_colour,
                                 MathColour& colour, double attenuation, ObjectPtr object, double relativeIor);
        /// @todo The name is misleading, as it computes all contributions of classic lighting, including highlights.
        void ComputeOneDiffuseLight(const LightSource &lightsource, const Vector3d& reye, const FINISH *finish, const Vector3d& ipoint, const Ray& eye,
                                    const Vector3d& layer_normal, const MathColour& Layer_Pigment_Colour, MathColour& colour, double Attenuation, ConstObjectPtr Object, double relativeIor, int light_index = -1);
        /// The ray, distance and unshadowed colour of a light; `false` if it cannot light this side of the surface.
        bool ComputeOneLightReach(const LightSource &lightsource, const FINISH *finish, const Vector3d& ipoint, const Vector3d& layer_normal, ConstObjectPtr object,
                                  double& lightsourcedepth, Ray& lightsourceray, MathColour& lightcolour, bool& backside);
        /// Filters a light's colour by what lies between it and the point.
        void TestOneLightShadow(const LightSource &lightsource, double lightsourcedepth, Ray& lightsourceray, const Vector3d& ipoint, MathColour& lightcolour, int light_index);
        /// Adds the classic lighting of one light of the given (possibly shadowed) colour.
        void ComputeOneLightContribution(const LightSource &lightsource, const Vector3d& reye, const FINISH *finish, const Vector3d& ipoint, const Ray& eye,
                                         const Vector3d& layer_normal, const MathColour& layer_pigment_colour, MathColour& colour, double attenuation,
                                         ConstObjectPtr object, double relativeIor, double lightsourcedepth, Ray& lightsourceray,
                                         const MathColour& lightcolour, bool backside);
        /// Adds the classic lighting of a light as seen through a portal; `image` is the light under the portal's inverse map.
        void ComputePortalDiffuseLight(const LightSource& image, const Vector3d& reye, const FINISH *finish, const Vector3d& ipoint, const Ray& eye,
                                       const Vector3d& layer_normal, const MathColour& layer_pigment_colour, MathColour& colour, double attenuation,
                                       ConstObjectPtr object, double relativeIor);
        /// Whether to trace a reflection or refraction of this weight spawned by `ray`; below a gather ray, one whose share
        /// of its sample is under the ADC bailout survives in proportion to it, and `scale` compensates.
        bool SurvivesRadiosityRoulette(const Ray& ray, const Vector3d& point, double weight, unsigned int salt, double& scale);
        /// Classic lighting for radiosity rays: shadow-tests lights brightest first until the untested ones carry at most a
        /// quarter of the unshadowed light; unless they carry 5% or less, one drawn by its light then stands for them all.
        void ComputeSampledDiffuseLight(const FINISH *finish, const Vector3d& ipoint, const Ray& eye, const Vector3d& layer_normal,
                                        const MathColour& layer_pigment_colour, MathColour& colour, double attenuation, ObjectPtr object, double relativeIor);
        /// @todo The name is misleading, as it computes all contributions of classic lighting, including highlights.
        void ComputeFullAreaDiffuseLight(const LightSource &lightsource, const Vector3d& reye, const FINISH *finish, const Vector3d& ipoint, const Ray& eye,
                                         const Vector3d& layer_normal, const MathColour& layer_pigment_colour, MathColour& colour, double attenuation,
                                         double lightsourcedepth, Ray& lightsourceray, const MathColour& lightcolour,
                                         ConstObjectPtr object, double relativeIor); // JN2007: Full area lighting

        /// Compute the direction, distance and unshadowed brightness of an unshadowed light source.
        ///
        /// Computations include spotlight falloff and distance-based attenuation.
        ///
        /// @param[in]      lightsource         Light source.
        /// @param[out]     lightsourcedepth    Distance to the light source.
        /// @param[in,out]  lightsourceray      Ray to the light source.
        /// @param[in]      ipoint              Intersection point.
        /// @param[out]     lightcolour         Effective brightness.
        /// @param[in]      forceAttenuate      `true` to immediately apply distance-based attenuation even for full
        ///                                     area lights.
        ///
        void ComputeOneLightRay(const LightSource &lightsource, double& lightsourcedepth, Ray& lightsourceray,
                                const Vector3d& ipoint, MathColour& lightcolour, bool forceAttenuate = false);

        void TraceShadowRay(const LightSource &light, double depth, Ray& lightsourceray, const Vector3d& point, MathColour& colour,
                            const Vector2d* areaSample = nullptr);
        void TracePointLightShadowRay(const LightSource &lightsource, double& lightsourcedepth, Ray& lightsourceray, MathColour& lightcolour);
        /// Shadow-tests one point of a light, `offset` from its centre: straight, or across the portal of an image.
        void TraceSampleShadowRay(const LightSource &lightsource, double& lightsourcedepth, Ray& lightsourceray, MathColour& lightcolour,
                                  const Vector3d& offset);
        /// Carries a light image's sample across its portal: the crossing, the pigment, the light's own cone and fade over the
        /// unfolded path, and shadows on both sides of the crossing; dark where the ray does not enter the portal.
        void TracePortalLightShadowRay(const LightSource &image, double& lightsourcedepth, Ray& lightsourceray, MathColour& lightcolour,
                                       const Vector3d& offset);
        /// Attenuates light through the media along one piece of a path through a portal, as a straight shadow ray is.
        void AttenuatePortalLightPiece(const LightSource& light, Ray& piece, double depth, MathColour& lightcolour);
        /// Dims straight light at each side short of `reach` that hands it to a partner, except `crossed` and its partner.
        void DivertPortalLight(const Ray& ray, double reach, MathColour& lightcolour, const Portal *crossed);
        /// The nearest hit on a portal's body short of `reach` through an open side, as a camera ray would enter it.
        bool FindPortalCrossing(const Portal& portal, const Ray& ray, double reach, Intersection& crossing);
        void TraceAreaLightShadowRay(const LightSource &lightsource, double& lightsourcedepth, Ray& lightsourceray,
                                     const Vector3d& ipoint, MathColour& lightcolour);
        void TraceAreaLightSubsetShadowRay(const LightSource &lightsource, double& lightsourcedepth, Ray& lightsourceray,
                                           const Vector3d& ipoint, MathColour& lightcolour, int u1, int  v1, int  u2, int  v2, int level, const Vector3d& axis1, const Vector3d& axis2);
        /// Test one point of an area light; `sample` in [0,1)^2 picks where on the light.
        void TraceAreaLightSampleShadowRay(const LightSource &lightsource, double& lightsourcedepth, Ray& lightsourceray,
                                           const Vector3d& ipoint, MathColour& lightcolour, const Vector2d& sample);
        void ComputeAreaLightAxes(const LightSource &lightsource, double& lightsourcedepth, Ray& lightsourceray,
                                  const Vector3d& ipoint, Vector3d& axis1, Vector3d& axis2);
        Vector3d AreaLightOffset(const LightSource &lightsource, double jitter_u, double jitter_v, const Vector3d& axis1, const Vector3d& axis2);
        /// A light's sample point drawn from its emitter as an offset from its centre, s in [0,1) picking the stratum.
        Vector3d EmitterOffset(const LightSource &lightsource, double s, std::uint64_t key, MathColour& weight);
        /// One emitter sample's shadow: the walk to it, then the media of the interiors it ends inside.
        void TraceEmitterSampleShadowRay(const LightSource &lightsource, double& lightsourcedepth, Ray& lightsourceray,
                                         MathColour& lightcolour, const Vector3d& offset);

        /// Compute the filtering effect of an object on incident light from a particular light source.
        ///
        /// Computations include any media effects between the ray's origin and the point of intersection.
        ///
        /// @todo           Some input parameters are non-const references.
        ///
        /// @param[in]      lightsource     Light source.
        /// @param[in]      isect           Intersection information.
        /// @param[in,out]  lightsourceray  Ray to the light source.
        /// @param[in,out]  colour          Computed effect on the incident light.
        ///
        void ComputeShadowColour(const LightSource &lightsource, Intersection& isect, Ray& lightsourceray,
                                 MathColour& colour);

        /// Compute the direction and distance of a single light source to a given intersection
        /// point.
        ///
        /// @remark         The `Origin` and `Direction` member of `lightsourceray` are updated, all other members are
        ///                 left unchanged; the distance is returned in a separate parameter. For cylindrical light
        ///                 sources, the values are set accordingly.
        ///
        /// @todo           The name is misleading, as it just computes direction and distance.
        ///
        /// @param[in]      lightsource         Light source.
        /// @param[out]     lightsourcedepth    Distance to the light source.
        /// @param[in,out]  lightsourceray      Ray to the light source.
        /// @param[in]      ipoint              Intersection point.
        /// @param[in]      jitter              Jitter to apply to the light source.
        ///
        void ComputeOneWhiteLightRay(const LightSource &lightsource, double& lightsourcedepth, Ray& lightsourceray,
                                     const Vector3d& ipoint, const Vector3d& jitter = Vector3d());

    ///
    /// @}
    ///
    //*****************************************************************************
    ///
    /// @name Photon Light Source Computations
    ///
    /// The following methods compute the (additional) contribution of photon-based lighting.
    ///
    /// @{
    ///

        /// @todo The name is misleading, as it computes all contributions of classic lighting, including highlights.
        void ComputePhotonDiffuseLight(const FINISH *Finish, const Vector3d& IPoint, const Ray& Eye, const Vector3d& Layer_Normal, const Vector3d& Raw_Normal,
                                       const MathColour& Layer_Pigment_Colour, MathColour& colour, double Attenuation,
                                       ConstObjectPtr Object, double relativeIor, PhotonGatherer& renderer);

    ///
    /// @}
    ///
    //*****************************************************************************
    ///
    /// @name Material Finish Computations
    ///
    /// The following methods compute the contribution of a finish illuminated by light from a given direction.
    ///
    /// @{
    ///

        /// Compute the diffuse contribution of a finish illuminated by light from a given direction.
        ///
        /// @remark         The computed contribution is _added_ to the value passed in `colour`.
        ///
        /// @param[in]      finish                  Finish.
        /// @param[in]      lightDirection          Direction of incoming light.
        /// @param[in]      eyeDirection            Direction from observer.
        /// @param[in]      layer_normal            Effective (possibly pertubed) surface normal.
        /// @param[in,out]  colour                  Effective surface colour.
        /// @param[in]      light_colour            Effective light colour.
        /// @param[in]      layer_pigment_colour    Nominal pigment colour.
        /// @param[in]      attenuation             Attenuation factor to account for partial transparency.
        /// @param[in]      backside                Whether to use backside instead of frontside diffuse brightness
        ///                                         factor.
        ///
        void ComputeDiffuseColour(const FINISH *finish, const Vector3d& lightDirection, const Vector3d& eyeDirection, const Vector3d& layer_normal,
                                  MathColour& colour, const MathColour& light_colour,
                                  const MathColour& layer_pigment_colour, double relativeIor, double attenuation, bool backside,
                                  std::uint64_t key, std::uint64_t index);

        /// Compute the iridescence contribution of a finish illuminated by light from a given direction.
        ///
        /// @remark         The computed contribution is _added_ to the value passed in `colour`.
        ///
        /// @param[in]      finish          Finish.
        /// @param[in]      lightDirection  Direction of incoming light.
        /// @param[in]      eyeDirection    Direction from observer.
        /// @param[in]      layer_normal    Effective (possibly pertubed) surface normal.
        /// @param[in]      ipoint          Intersection point (possibly with earlier warps already applied).
        /// @param[in,out]  colour          Effective surface colour.
        ///
        void ComputeIridColour(const FINISH *finish, const Vector3d& lightDirection, const Vector3d& eyeDirection,
                               const Vector3d& layer_normal, const Vector3d& ipoint, MathColour& colour);

        /// Compute the Phong highlight contribution of a finish illuminated by light from a given direction.
        ///
        /// Computation uses the classic Phong highlight model.
        ///
        /// @remark         The model used is _not_ energy-conserving.
        ///
        /// @remark         The computed contribution is _added_ to the value passed in `colour`.
        ///
        /// @param[in]      finish                  Finish.
        /// @param[in]      lightDirection          Direction of ray from light source.
        /// @param[in]      eyeDirection            Direction from observer.
        /// @param[in]      layer_normal            Effective (possibly pertubed) surface normal.
        /// @param[in,out]  colour                  Effective surface colour.
        /// @param[in]      light_colour            Effective light colour.
        /// @param[in]      layer_pigment_colour    Nominal pigment colour.
        /// @param[in]      fresnel                 Whether to apply fresnel-based attenuation.
        ///
        void ComputePhongColour(const FINISH *finish, const Vector3d& lightDirection, const Vector3d& eyeDirection,
                                const Vector3d& layer_normal, MathColour& colour, const MathColour& light_colour,
                                const MathColour& layer_pigment_colour, double relativeIor);

        /// Compute the specular highlight contribution of a finish illuminated by light from a given direction.
        ///
        /// Computation uses the Blinn-Phong highlight model
        ///
        /// @remark     The model used is _not_ energy-conserving.
        ///
        /// @remark     The computed contribution is _added_ to the value passed in `colour`.
        ///
        /// @param[in]      finish                  Finish.
        /// @param[in]      lightDirection          Direction of ray from light source.
        /// @param[in]      eyeDirection            Direction from observer.
        /// @param[in]      layer_normal            Effective (possibly pertubed) surface normal.
        /// @param[in,out]  colour                  Effective surface colour.
        /// @param[in]      light_colour            Effective light colour.
        /// @param[in]      layer_pigment_colour    Nominal pigment colour.
        /// @param[in]      fresnel                 Whether to apply fresnel-based attenuation.
        ///
        void ComputeSpecularColour(const FINISH *finish, const Vector3d& lightDirection, const Vector3d& eyeDirection,
                                   const Vector3d& layer_normal, MathColour& colour, const MathColour& light_colour,
                                   const MathColour& layer_pigment_colour, double relativeIor);

    ///
    /// @}
    ///
    //*****************************************************************************
    ///

        /// Compute relative index of refraction.
        void ComputeRelativeIOR(const Ray& ray, const Interior* interior, const Vector3d& point, const Vector3d& normal, double& ior);

        /// The ratios a surface refracts by.
        struct InterfaceIor final
        {
            double ior;                      ///< index on the ray's side over the index beyond
            double dispersion;               ///< the same for dispersion
            unsigned int dispersionElements;
            double radiance;                 ///< radiance scale across it: (n / base ior) squared, the ray's side over beyond
            double leave;                    ///< how far past it the child ray starts: the tolerance where the crossing was seen, else 0
        };
        /// The one rule for the indices either side of a surface, for camera rays and photons; crosses every surface meeting there.
        void ComputeInterfaceIor(Ray& ray, Interior *interior, const Vector3d& point, const Vector3d& normal, InterfaceIor& result);

        /// How far either side of a surface other surfaces merge with it, and how far past it child rays start: small beside the
        /// coordinates and the interior's smallest extent, and at least twice the shortest hit distance where the object allows.
        double InterfaceTolerance(const Vector3d& point, const Interior *interior) const;
        /// Where other surfaces meet a hit that lets light through, hits an opaque one, else one of an object the ray enters.
        /// Returns whether it did.
        bool TakeCoincident(Ray& ray, Intersection& isect, const Vector3d& normal, COLC weight);
        /// Under interface_texture blend, fills `textures` with the textures of the clear faces meeting at a hit, if several.
        bool SharedFaceTextures(const Ray& ray, const Intersection& isect, const Vector3d& normal, WeightedTextureVector& textures,
                                InterfaceCache& sides);
        /// Whether interfaces merge coincident surfaces: version 4.0 or media refraction, with an ior or an opaque object.
        bool MergesSurfaces() const;
        /// Whether a surface's plain pigment lets any light through, or might.
        bool SurfaceTransmits(const Intersection& isect, const Ray& ray, COLC weight);
        /// Whether shading a hit needs the relative ior: it may let light through, or a finish uses Fresnel.
        bool NeedsRelativeIor(const TEXTURE *texture, ConstObjectPtr object) const;
        /// The ior where a ray holds `interiors`, by their ior_mix, else the atmosphere's; `dispersion` gets the dispersion
        /// mixed the same way, and `elements` the largest dispersion sample count among the interiors that count.
        double MixedIor(const RayInteriorVector& interiors, double *dispersion, unsigned int *elements) const;
        /// The index where a ray holds `interiors`: their mixed ior plus media refraction, whose part `offset` gets.
        double StackIndex(const RayInteriorVector& interiors, const Vector3d& point, double& offset, double *base = nullptr,
                          double *dispersion = nullptr, unsigned int *elements = nullptr);
        /// Warns once per render for a SceneData::mediaWarningFlags bit.
        void WarnRefraction(unsigned flag, const char *format, ...);
        /// An index held at or above the smallest a curved ray may meet, warning once when it is not.
        double ClampIndex(double index, const Vector3d& point);
        bool RefractingRay(const Ray& ray) const;

        struct CurvedStep final
        {
            Vector3d origin, direction;
            double length, index;            ///< index: the refractive index at the chord's start
        };
        typedef std::vector<CurvedStep> CurvedPath;
        /// Follows a ray chord by chord to its first hit (1), none (0) or trapped (-1); `chord` ends as the last chord.
        int MarchCurvedRay(Ray& chord, RefractionField& field, double base, Intersection& isect, CurvedPath& path);
        double TraceCurvedRay(Ray& ray, RefractionField& field, MathColour& colour, ColourChannel& transm, COLC weight, bool continuedRay);
        double ShadeRay(Ray& ray, Intersection& bestisect, bool found, MathColour& colour, ColourChannel& transm, COLC weight,
                        bool continuedRay);

        /// Compute Reflectivity.
        ///
        /// @remark         In Fresnel mode, light is presumed to be unpolarized on average, using
        ///                 @f$ R = \frac{1}{2} \left( R_s + R_p \right) @f$.
        ///
        void ComputeReflectivity(double& weight, MathColour& reflectivity, const MathColour& reflection_max,
                                 const MathColour& reflection_min, bool fresnel, double reflection_falloff,
                                 double cos_angle, double relativeIor);

        /// Compute metallic attenuation
        void ComputeMetallic(MathColour& colour, double metallic, const MathColour& metallicColour, double cosAngle);

        /// Compute fresnel-based reflectivity.
        void ComputeFresnel(MathColour& colour, const MathColour& rMax, const MathColour& rMin, double cos_angle, double relativeIor);

        /// Compute Fresnel reflectance term.
        ///
        /// This function computes the reflectance term _R_ of the Fresnel equations for the special
        /// case of a dielectric material and unpolarized light. The transmittance term _T_ can
        /// trivially be computed as _T=1-R_.
        ///
        /// @param[in]      cosTi           Cosine of angle between incident ray and surface normal.
        /// @param[in]      n               Relative refractive index of the material entered.
        ///
        static double FresnelR(double cosTi, double n);

        /// Compute Sky & Background Colour.
        ///
        /// @remark         The computed colour _overwrites_ any value passed in `colour` and `transm`.
        ///
        /// @param[in]      ray             Ray.
        /// @param[out]     colour          Computed sky/background colour.
        /// @param[out]     transm          Computed transmittance.
        ///
        void ComputeSky(const Ray& ray, MathColour& colour, ColourChannel& transm);

        void ComputeFog(const Ray& ray, const Intersection& isect, MathColour& colour, ColourChannel& transm);
        double ComputeConstantFogDepth(const Ray &ray, double depth, double width, const FOG *fog);
        double ComputeGroundFogDepth(const Ray& ray, double depth, double width, const FOG *fog);
        void ComputeRainbow(const Ray& ray, const Intersection& isect, MathColour& colour, ColourChannel& transm);

        /// Compute media effect on traversing light rays.
        ///
        /// @note           This computes two things:
        ///                   - media and fog attenuation of the shadow ray (optional)
        ///                   - entry/exit of interiors
        ///                   .
        ///                 In other words, you can't skip this whole thing, because the entry/exit is important.
        ///
        void ComputeShadowMedia(Ray& light_source_ray, Intersection& isect, MathColour& resultcolour,
                                bool media_attenuation_and_interaction);

        /// Test whether an object is part of (or identical to) a given other object.
        ///
        /// @todo           The name is misleading, as the object to test against (`parent`) does not necessarily have
        ///                 to be a CSG compound object, but can actually be of any type. In that case, the function
        ///                 serves to test for identity.
        ///
        /// @param[in]      object          The object to test.
        /// @param[in]      parent          The object to test against.
        /// @return                         True if `object` is part of, or identical to, `parent`.
        ///
        bool IsObjectInCSG(ConstObjectPtr object, ConstObjectPtr parent);


    ///
    //*****************************************************************************
    ///
    /// @name Subsurface Light Transport
    ///
    /// The following methods implement the BSSRDF approximation as outlined by Jensen et al.
    ///
    /// @{
    ///

        /// Per-channel constants of the dipole diffusion profile at one shading point.
        struct SubsurfaceProfile
        {
            PreciseMathColour scale, sigma_tr, z_r, z_v;
            PreciseMathColour Rd(double distSqr) const;
            PreciseMathColour RdDisc(double radius) const;
            PreciseMathColour RdEdgeShare(double inner, double outer, double d) const;
            PreciseMathColour RdTotal() const;
        };

        /// A texture's subsurface layers blended as the viewer sees them: reflectance and colour weighted by each layer's
        /// opacity and filter, their sum, and the topmost layer, whose finish they scatter with.
        struct SubsurfaceLayers
        {
            MathColour reflectance, tint, weight;
            const TEXTURE *top = nullptr;
            void Add(const TEXTURE *layer, const MathColour& pigment, const MathColour& visibility);
            bool Finish();
        };

        /// Where light enters the flesh: the flesh there, what the light is multiplied by (flesh times skin transmittance),
        /// and the flesh's emission times the flesh.
        struct SubsurfaceEntry
        {
            MathColour flesh{1.0}, factor{1.0}, emission;
        };

        /// The flesh and skin of one shading point, and what is looked up where light enters; see doc/PERF.md.
        struct SubsurfaceFlesh
        {
            const FINISH *finish = nullptr;
            const PIGMENT *skin = nullptr; ///< the skin's tint where light enters, when it is looked up there
            double depth = 0.0; ///< mean depth of the flesh's lookups below the surface, in scene units
            double spread = 0.0; ///< their standard deviation, in scene units
            double outside = 0.0; ///< the share of that normal curve above the surface, which the draws leave out
            double DepthAt(double draw) const;
            bool fleshAtDepth = false, emissionAtDepth = false;
            MathColour exitSkin{1.0}, entrySkin{1.0}; ///< the skin's transmittance here, and where light enters unless looked up there
            MathColour emission; ///< the flesh's emission where it is the same at every entry point
            PreciseMathColour reference{1.0}; ///< the flesh colour the looked-up one is taken relative to
            SubsurfaceEntry nearby; ///< the mean of a few entry points around this one, for light entering as it does here
            bool PerEntry() const { return fleshAtDepth || emissionAtDepth || (skin != nullptr); }
        };

        /// A subsurface sample lit by one light, before its shadow is tested.
        struct SubsurfaceCandidate
        {
            Vector3d point;
            MathColour factor, unshadowed;
            Vector3d normal;
            double bound = 0.0;
            void SetLight(const Vector3d& p, const MathColour& lightcolour);
        };

        /// An object's irradiance cloud as one shading point sees it: its levels, lights and the cells within reach.
        struct SubsurfaceCloud
        {
            PreparedSetId preparedSetId = 0;
            ObjectPtr object = nullptr;
            const SubsurfaceFlesh *flesh = nullptr;
            const void *medium = nullptr; ///< what the points look up where light enters, keying their cells; null for nothing
            int sizeLevel = 0;
            double size = 0.0, spacing = 0.0, reach = 0.0, eta = 1.0; ///< spacing: that of the cell holding the exit point
            bool local = false; ///< diffusion within about a pixel: all of it is lit as the exit point is
            bool photons = false;
            bool radiosity = false; ///< the points carry the radiosity cache's light too
            double footprint = 0.0; ///< a pixel's span at the exit point, in mm
            std::vector<const LightSource*> lights;
            std::vector<MathColour> exitLight; ///< per light, at the exit point, shadowed
            std::vector<Vector3d> exitDirection; ///< zero where the light sends nothing to the exit point
            std::vector<char> exitAgreed; ///< whether the light's shadow holds around the exit point
            bool edge = false; ///< the surface runs out within the ring
            bool exitMismatch = false; ///< on a coarse cloud, whether the exit point's shadow differs from its neighbours'
            std::vector<const SubsurfaceCell*> cells;
            std::vector<Vector3d> coords;
        };

        /// The cloud of each subsurface recursion level, kept between shading points to save allocations.
        SubsurfaceCloud ssltClouds[2];
        std::unique_ptr<PhotonGatherer> ssltPhotonGatherers[2];
        std::vector<std::unique_ptr<PhotonGatherer>> surfacePhotonGatherers;
        size_t surfacePhotonGatherDepth = 0;
        /// The finished cells this thread has used, so it need not ask the shared cache again.
        std::unordered_map<SubsurfaceCellKey, const SubsurfaceCell*, SubsurfaceCellKeyHash> ssltCells;

        struct ScreenViews;
        struct ScreenViewsDeleter final { void operator()(ScreenViews *views) const; };
        /// Tracers for the views shown on screens, made when first seen.
        std::unique_ptr<ScreenViews, ScreenViewsDeleter> screenViews;

        double ComputeFt(double cos_angle, double eta);
        void ComputeSurfaceTangents(const Vector3d& normal, Vector3d& u, Vector3d& v);
        void ComputeSSLTNormal (Intersection& Ray_Intersection);
        bool IsSameSSLTObject(ConstObjectPtr obj1, ConstObjectPtr obj2);
        void ComputeDiffuseSampleBase(Vector3d& basePoint, const Intersection& out, const Vector3d& vOut, double avgFreeDist, TraceTicket& ticket);
        void ComputeDiffuseSamplePoint(const Vector3d& basePoint, ObjectPtr object, Intersection& in, double& sampleArea, TraceTicket& ticket,
                                       std::uint64_t key, int sample);
        ObjectPtr SubsurfaceObject(const Intersection& isect);
        void ComputeDiffuseCandidate(const LightSource& lightsource, const Intersection& in, const PreciseMathColour& rd, double eta, SubsurfaceCandidate& candidate, TraceTicket& ticket);
        void ComputeDiffuseAmbientContribution1(const Intersection& in, const PreciseMathColour& rd, MathColour& Total_Colour, double eta, double weight, TraceTicket& ticket);
        void ComputeSingleScatteringCandidate(const LightSource& lightsource, const Intersection& out, const PreciseMathColour& sigma_t_xo, const PreciseMathColour& sigma_s,
                                              const PreciseMathColour& weightOut, double eta, const Vector3d& bend_point, double ftOut, double cos_out_prime,
                                              SubsurfaceCandidate& candidate, TraceTicket& ticket);
        void ComputeSingleScatteringContribution(const Intersection& out, double dist, double ftOut, double cos_out_prime, const Vector3d& refractedREye,
                                                 const PreciseMathColour& sigma_t_xo, const PreciseMathColour& sigma_s, int numSamples, MathColour& Lo, double eta,
                                                 const std::vector<const LightSource*>& lights, const SubsurfaceCloud* cloud, const SubsurfaceFlesh& flesh,
                                                 TraceTicket& ticket, std::uint64_t key);
        void ShadeSubsurfaceCandidates(const std::vector<const LightSource*>& lights, const SubsurfaceCandidate* candidates, int count, MathColour& total, TraceTicket& ticket,
                                       std::uint64_t key);
        MathColour DrawSubsurfaceShadows(const LightSource& lightsource, const SubsurfaceCandidate* candidates, int count, double sum, int budget, TraceTicket& ticket,
                                         std::uint64_t key);
        MathColour ComputeSubsurfaceIrradiance(const Vector3d& point, const Vector3d& normal, const std::vector<const LightSource*>& lights, double eta,
                                               int areaPoints, const Vector2d* areaShift, float* visibility, TraceTicket& ticket, std::uint64_t key);
        bool SubsurfacePhotonsEnabled(ConstObjectPtr receiver, PreparedSetId preparedSetId) const;
        bool UniformSubsurfacePhotonReceiver(ConstObjectPtr receiver, ConstObjectPtr root) const;
        bool RecoverSubsurfacePhotonBoundary(const Vector3d& location, const Vector3d& normal, ObjectPtr receiver, double radius,
                                             Vector3d& outward, TraceTicket& ticket);
        bool ProbeSubsurfacePhotonBoundary(const Vector3d& location, const Vector3d& normal, ObjectPtr receiver, double tolerance,
                                           double probe, Vector3d& outward, TraceTicket& ticket);
        bool SubsurfacePhotonDeposit(const Photon& photon, const Vector3d& incoming, ObjectPtr receiver, double rootTolerance,
                                     Vector3d& outward, TraceTicket& ticket);
        MathColour ComputeSubsurfacePhotonIrradiance(const Vector3d& point, const Vector3d& normal, double eta, ObjectPtr receiver,
                                                     PhotonGatherer& gatherer, TraceTicket& ticket, bool cloud);
        bool ComputeProjectedSubsurfacePhotons(const Intersection& out, const Vector3d& base, const SubsurfaceProfile& profile,
                                               const SubsurfaceFlesh& flesh, double ftOut, int samples, PhotonGatherer& gatherer,
                                               TraceTicket& ticket, std::uint64_t key, MathColour& diffuse);
        MathColour ComputeCloudExitIrradiance(const Intersection& out, const SubsurfaceVisibility& disc, SubsurfaceCloud& cloud, bool coarse,
                                              TraceTicket& ticket, std::uint64_t key);
        MathColour ComputeCloudExitAmbient(const Intersection& out, const Vector3d& n, const SubsurfaceCloud& cloud, TraceTicket& ticket);
        void BuildSubsurfaceCell(const SubsurfaceCloud& cloud, const SubsurfaceCellKey& key, SubsurfaceCell& cell);
        void FinishSubsurfaceCell(SubsurfaceCell& cell, bool usable);
        void AddSubsurfaceAmbient(const SubsurfaceCloud& cloud, SubsurfaceCell& cell);
        void WorkOnSubsurfaceCell(const SubsurfaceCloud& cloud, const SubsurfaceCellKey& key, SubsurfaceCell& cell, bool builder);
        void CastSubsurfaceLines(const SubsurfaceCloud& cloud, const SubsurfaceCellKey& key, SubsurfaceCell& cell, int job, TraceTicket& ticket);
        void LightSubsurfacePoints(const SubsurfaceCloud& cloud, const SubsurfaceCellKey& key, SubsurfaceCell& cell, int job, TraceTicket& ticket);
        void CollectCrossings(ObjectPtr object, const Vector3d& origin, const Vector3d& dir, double from, double to, std::vector<Intersection>& hits, TraceTicket& ticket);
        bool OpenSubsurfaceCloud(const Intersection& out, const Ray& eye, const SubsurfaceProfile& profile,
                                 const std::vector<const LightSource*>& lights, SubsurfaceCloud& cloud);
        bool GatherSubsurfaceCells(SubsurfaceCloud& cloud, const Vector3d& centre, double radius);
        const SubsurfaceCell *FindSubsurfaceCell(const SubsurfaceCloud& cloud, const Vector3d& q);
        bool LookupSubsurfaceVisibility(const SubsurfaceCloud& cloud, const Vector3d& q, const Vector3d& normal, SubsurfaceVisibility& visibility);
        bool ComputeSubsurfaceCloud(const Intersection& out, const Vector3d& base, const SubsurfaceProfile& profile, double ftOut, SubsurfaceCloud& cloud,
                                    MathColour& diffuse, TraceTicket& ticket, std::uint64_t key);
        void CollectSubsurfaceLights(ConstObjectPtr object, std::vector<const LightSource*>& lights, PreparedSetId preparedSetId);
        void ComputeSubsurfaceScattering(const SubsurfaceLayers& layers, const Intersection& isect, Ray& Eye, MathColour& colour);
        void SetUpSubsurfaceFlesh(SubsurfaceFlesh& flesh, const SubsurfaceLayers& layers, const Intersection& out, const Vector3d& inward,
                                  std::uint64_t key);
        MathColour ComputeSubsurfaceSkin(const SubsurfaceFlesh& flesh, const Vector3d& point);
        void ComputeSubsurfaceEntry(const SubsurfaceFlesh& flesh, const Vector3d& point, const Vector3d& inward, double draw, SubsurfaceEntry& entry);
        bool SSLTComputeRefractedDirection(const Vector3d& v, const Vector3d& n, double eta, Vector3d& refracted);

    ///
    /// @}
    ///
    //*****************************************************************************
    ///

};

/// @}
///
//##############################################################################

}
// end of namespace pov

#endif // POVRAY_CORE_TRACE_H
