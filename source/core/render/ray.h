//******************************************************************************
///
/// @file core/render/ray.h
///
/// Declarations related to rays.
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

#ifndef POVRAY_CORE_RAY_H
#define POVRAY_CORE_RAY_H

// Module config header file must be the first file included within POV-Ray unit header files
#include "core/configcore.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include "core/render/ray_fwd.h"

// C++ variants of C standard header files
// C++ standard header files
//  (none at the moment)

// POV-Ray header files (base module)
//  (none at the moment)

// POV-Ray header files (core module)
#include "core/core_fwd.h"
#include "core/scene/tagfilter.h"
#include "core/bounding/boundingbox.h"
#include "core/colour/spectral.h"
#include "core/math/randomsequence.h"
#include "core/support/simplevector.h"

namespace pov
{

//##############################################################################
///
/// @addtogroup PovCoreRender
///
/// @{

typedef PooledSimpleVector<Interior *, RAYINTERIOR_VECTOR_SIZE> RayInteriorVector;

struct RayRootQuery
{
    virtual ~RayRootQuery() = default;
    virtual bool Admit(ConstObjectPtr owner, double depth) const = 0;
    virtual bool AllRoots() const { return false; }
};

/// The root query active on one ray of a ticket; a copy of a ticket has none, and assigning a ticket keeps it.
struct RayRootQuerySlot final
{
    const Ray* ray = nullptr;
    const RayRootQuery* query = nullptr;
    ConstObjectPtr owner = nullptr;
    RayRootQuerySlot() = default;
    RayRootQuerySlot(const RayRootQuerySlot&) {}
    RayRootQuerySlot& operator=(const RayRootQuerySlot&) { return *this; }
    void Set(const Ray* r, const RayRootQuery* q, ConstObjectPtr o) { ray = r; query = q; owner = o; }
};

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
    /// Set when a ray watching it had its key read (Ray::WatchKey).
    bool keyRead = false;
    /// Filters the roots that intersection tests of one ray on this ticket return (Ray::AdmitRoot).
    RayRootQuerySlot rootQuery;

    TraceTicket(unsigned int mtl, double adcb, bool ab = true, unsigned int rrd = 0, unsigned int ssrd = 0,
                float riq = -1.0, float rq = 1.0):
        traceLevel(0), maxAllowedTraceLevel(mtl), maxFoundTraceLevel(0), adcBailout(adcb), alphaBackground(ab),
        radiosityRecursionDepth(rrd), subsurfaceRecursionDepth(ssrd), radiosityImportanceQueried(riq),
        radiosityImportanceFound(-1.0), radiosityQuality(rq), radiosityShare(1.0), radiosityEye(nullptr), preparedSetId(0)
    {}
};

class Ray final : public BasicRay
{
    // Hot path, deep-copied: add no member without re-measuring every scene for regressions (counters).
    // Why, and how to measure: doc/PERF.md, "Ray size".
    public:

        enum RayType
        {
            OtherRay = 0,
            PrimaryRay = 1,
            ReflectionRay = 2,
            RefractionRay = 3,
            SubsurfaceRay = 4,  ///< Ray is shot from just below a surface; very close intersections shall not be suppressed.
        };

        Ray(TraceTicket& ticket, RayType rt = PrimaryRay, bool shadowTest = false, bool photon = false, bool radiosity = false, bool monochromatic = false, bool pretrace = false);
        Ray(TraceTicket& ticket, const Vector3d& ov, const Vector3d& dv, RayType rt = PrimaryRay, bool shadowTest = false, bool photon = false, bool radiosity = false, bool monochromatic = false, bool pretrace = false);
        ~Ray();

        void AppendInterior(Interior *i);
        void AppendInteriors(RayInteriorVector&);
        bool RemoveInterior(const Interior *i);
        void ClearInteriors() { interiors.clear(); }
        /// Forgets what the ray was inside, open air included.
        void ResetInteriors() { interiors.clear(); hollowRay = true; }

        bool IsInterior(const Interior *i) const;
        const RayInteriorVector& GetInteriors() const { return interiors; }
        RayInteriorVector& GetInteriors() { return interiors; }

        void SetSpectralBand(const SpectralBand&);
        const SpectralBand& GetSpectralBand() const;

        void SetFlags(RayType rt, bool shadowTest = false, bool photon = false, bool radiosity = false, bool monochromatic = false, bool pretrace = false);
        void SetFlags(RayType rt, const Ray& other);

        bool IsPrimaryRay() const { return primaryRay; }
        bool IsImageRay() const { return !photonRay && (primaryRay || (refractionRay && !reflectionRay && !radiosityRay)); }
        bool IsReflectionRay() const { return reflectionRay; }
        bool IsRefractionRay() const { return refractionRay; }
        bool IsSubsurfaceRay() const { return subsurfaceRay; }
        bool IsShadowTestRay() const { return shadowTestRay; }
        bool IsPhotonRay() const { return photonRay; }
        bool IsRadiosityRay() const { return radiosityRay; }
        bool IsMonochromaticRay() const { return monochromaticRay; }
        bool IsHollowRay() const { return hollowRay; }
        bool IsPretraceRay() const { return pretraceRay; }
        /// Shadow tests of this ray count every blocker between its ends, with no fixed margin at either.
        void SetExactShadowWindow() { exactShadowWindow = true; }
        bool HasExactShadowWindow() const { return exactShadowWindow; }
        /// A receiver standing for a region this wide takes a light's falloff no nearer than this to the light.
        void SetReceiverRadius(DBL radius) { receiverRadius = radius; }
        DBL ReceiverRadius() const { return receiverRadius; }

        bool AdmitRoot(ConstObjectPtr object, double depth) const
        {
            const RayRootQuerySlot& slot = ticket.rootQuery;
            return slot.ray != this || !slot.query || slot.query->Admit(slot.owner ? slot.owner : object, depth);
        }
        const RayRootQuery* GetRootQuery() const { return ticket.rootQuery.ray == this ? ticket.rootQuery.query : nullptr; }
        ConstObjectPtr GetRootOwner() const { return ticket.rootQuery.ray == this ? ticket.rootQuery.owner : nullptr; }
        void SetRootQuery(const RayRootQuery* query) const { ticket.rootQuery.Set(this, query, nullptr); }
        void SetRootOwner(ConstObjectPtr owner) const { if (ticket.rootQuery.ray == this) ticket.rootQuery.owner = owner; }
        RayRootQuerySlot& RootQuerySlot() const { return ticket.rootQuery; }

        void SetSurfaceOrigin(ConstObjectPtr object, const Vector3d& point, const Vector3d& normal, ConstObjectPtr surface = nullptr);
        void ClearSurfaceOrigin() { surfaceOrigin.object = nullptr; }
        bool HasSurfaceOrigin() const { return surfaceOrigin.object != nullptr; }
        /// `surface` is the part of `object` the point lies on, whose stated accuracy the match allows; `object` if none.
        void SetSurfaceEndpoint(ConstObjectPtr object, const Vector3d& point, ConstObjectPtr surface = nullptr);
        bool HasSurfaceEndpoint() const { return surfaceEndpoint.object != nullptr; }
        void ClearSurfaceEndpoint() { surfaceEndpoint.object = nullptr; }
        bool MatchesSurfaceEndpoint(ConstObjectPtr object, double depth) const
        { return MatchesSurfacePoint(surfaceEndpoint, object, depth); }
        bool MatchesSurfaceOrigin(ConstObjectPtr object, double depth) const
        {
            return surfaceOrigin.object && dot(Direction, surfaceOrigin.normal) > 0.0 &&
                MatchesSurfacePoint(surfaceOrigin, object, 0.0) && MatchesSurfacePoint(surfaceOrigin, object, depth);
        }

        bool Inside(const BoundingBox& bbox) const { return Inside_BBox(Origin, bbox); }

        inline TraceTicket& GetTicket() { return ticket; }
        inline const TraceTicket& GetTicket() const { return ticket; }

        DBL GetMediaErrorBudget() const { return mediaErrorBudget; }
        void SetMediaErrorBudget(DBL value) const { mediaErrorBudget = value; }
        void ResetMediaErrorBudget() { mediaErrorBudget = 1.0 / 1024.0; }
        const LightSource *GetMediaLight() const { return mediaLight; }
        void SetMediaLight(const LightSource *light) { mediaLight = light; }

        PreparedSetId GetPreparedSetId() const { return preparedSetId; }
        void SetPreparedSetId(PreparedSetId view) { preparedSetId = view; }

        /// The path key this ray's random draws are hashed from.
        std::uint64_t GetKey() const { MarkKeyRead(); return key; }
        void SetKey(std::uint64_t k) { key = k; children = 0; }
        /// Makes this ray the index-th child of its kind below the parent.
        void Derive(const Ray& parent, std::uint64_t kind, std::uint64_t index)
        {
            // A flag lives in a ticket, and so only as long as the rays on it.
            if (&ticket == &parent.ticket)
                watchesKey = parent.watchesKey;
            else
                parent.MarkKeyRead();
            SetKey(DeriveKey(parent.key, kind, index));
        }
        /// A key for the next child of a kind whose place among its siblings is its only index.
        std::uint64_t NextChildKey(std::uint64_t kind) const { MarkKeyRead(); return DeriveKey(key, kind, children++); }
        /// Sets the ticket's keyRead once anything reads the key of this ray, a copy or a derived ray.
        void WatchKey() { watchesKey = true; }

        /// Change of origin and direction per image pixel step in x and y; valid only when @ref hasDifferentials is set.
        Vector3d dOdx, dOdy, dDdx, dDdy;

    private:

        void MarkKeyRead() const { if (watchesKey) ticket.keyRead = true; }

        RayInteriorVector interiors;
        SpectralBand spectralBand;
        TraceTicket& ticket;
        std::uint64_t key = 0;
        mutable DBL mediaErrorBudget = 0.0;
        const LightSource *mediaLight = nullptr;
        PreparedSetId preparedSetId = 0;
        DBL receiverRadius = 0.0;
        mutable std::uint32_t children = 0;
        bool exactShadowWindow = false;

        bool primaryRay : 1;
        bool reflectionRay : 1;
        bool refractionRay : 1;
        bool subsurfaceRay : 1;
        bool shadowTestRay : 1;
        bool photonRay : 1;
        bool radiosityRay : 1;
        bool monochromaticRay : 1;
        bool hollowRay : 1;
        bool pretraceRay : 1;
        bool watchesKey : 1;

    public:
        /// Set on camera rays and the reflected or refracted rays they spawn; @ref SetFlags clears it for every other derived ray.
        bool hasDifferentials = false;

    private:

        struct SurfacePoint
        {
            ConstObjectPtr object = nullptr;
            Vector3d point;
            double errorScale = 0.0;
            double rootTolerance = 0.0;
        };
        struct SurfaceOrigin : SurfacePoint { Vector3d normal; };
        SurfaceOrigin surfaceOrigin;
        SurfacePoint surfaceEndpoint;
        bool MatchesSurfacePoint(const SurfacePoint& surface, ConstObjectPtr object, double depth) const
        {
            if (!surface.object || surface.object != object)
                return false;
            for (int axis = 0; axis < 3; ++axis)
            {
                const double travel = depth * Direction[axis];
                const double scale = std::max(surface.errorScale, std::max(std::abs(surface.point[axis]), std::max(std::abs(Origin[axis]), std::abs(travel))));
                const double tolerance = std::max(16.0 * std::numeric_limits<double>::epsilon() * scale, surface.rootTolerance);
                if (std::abs((Origin[axis] + travel) - surface.point[axis]) > tolerance)
                    return false;
            }
            return true;
        }
};

struct RayRootOwnerScope final
{
    const Ray& ray;
    ConstObjectPtr previous;
    RayRootOwnerScope(const Ray& r, ConstObjectPtr owner) : ray(r), previous(r.GetRootOwner())
    { if (r.GetRootQuery() && !previous) r.SetRootOwner(owner); }
    ~RayRootOwnerScope() { ray.SetRootOwner(previous); }
};

struct RayRootQueryDisabled final
{
    RayRootQuerySlot& slot;
    const Ray* ray;
    const RayRootQuery* query;
    ConstObjectPtr owner;
    explicit RayRootQueryDisabled(const Ray& r) : slot(r.RootQuerySlot()), ray(slot.ray), query(slot.query), owner(slot.owner)
    { if (slot.ray == &r) slot.Set(nullptr, nullptr, nullptr); }
    ~RayRootQueryDisabled() { slot.Set(ray, query, owner); }
};

#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
static_assert(sizeof(Ray) <= 336, "Ray grew: see \"Ray size\" in doc/PERF.md before adding a member");
#endif

/// @}
///
//##############################################################################

}
// end of namespace pov

#endif // POVRAY_CORE_RAY_H
