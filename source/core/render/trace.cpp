//******************************************************************************
///
/// @file core/render/trace.cpp
///
/// Implementations related to the @ref pov::Trace class.
///
/// @copyright
/// @parblock
///
/// Persistence of Vision Ray Tracer ('POV-Ray') version 3.8.
/// Copyright 1991-2021 Persistence of Vision Raytracer Pty. Ltd.
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

// Unit header file must be the first file included within POV-Ray *.cpp files (pulls in config)
#include "core/render/trace.h"

// C++ variants of C standard header files
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstring>

// C++ standard header files
#include <algorithm>
#include <limits>

// POV-Ray header files (base module)
#include "base/povassert.h"

// POV-Ray header files (core module)
#include "core/bounding/boundingbox.h"
#include "core/bounding/bsptree.h"
#include "core/lighting/lightsource.h"
#include "core/lighting/radiosity.h"
#include "core/lighting/subsurface.h"
#include "core/material/interior.h"
#include "core/material/noise.h"
#include "core/material/normal.h"
#include "core/material/pattern.h"
#include "core/material/pigment.h"
#include "core/material/texture.h"
#include "core/material/warp.h"
#include "core/math/matrix.h"
#include "core/render/ray.h"
#include "core/scene/atmosphere.h"
#include "core/scene/object.h"
#include "core/scene/scenedata.h"
#include "core/scene/tracethreaddata.h"
#include "core/shape/box.h"
#include "core/shape/csg.h"
#include "core/shape/portal.h"
#include "core/shape/cone.h"
#include "core/shape/disc.h"
#include "core/shape/isosurface.h"
#include "core/shape/mesh.h"
#include "core/shape/plane.h"
#include "core/shape/polynomial.h"
#include "core/shape/quadric.h"
#include "core/shape/sphere.h"
#include "core/shape/superellipsoid.h"
#include "core/shape/torus.h"
#include "core/support/imageutil.h"
#include "core/support/statistics.h"

// this must be the last file included
#include "base/povdebug.h"

namespace pov
{

using std::min;
using std::max;
using std::vector;

#define SHADOW_TOLERANCE 1.0e-3

// Directions in the pool diffuse subsurface samples take theirs from.
static const size_t kSubsurfaceDirectionPool = 32767;

// A light's index among a path's shadow rays; global and light-group lights are numbered separately.
static std::uint64_t LightSlot(const LightSource& light)
{
    return 2 * std::uint64_t(light.index) + (light.lightGroupLight ? 1 : 0);
}


bool NoSomethingFlagRayObjectCondition::operator()(const Ray& ray, ConstObjectPtr object, double) const
{
    if(ray.IsImageRay() && Test_Flag(object, NO_IMAGE_FLAG))
        return false;
    if(ray.IsReflectionRay() && Test_Flag(object, NO_REFLECTION_FLAG))
        return false;
    if(ray.IsRadiosityRay() && Test_Flag(object, NO_RADIOSITY_FLAG))
        return false;
    if(ray.IsPhotonRay() && Hidden_From_Photons(object))
        return false;
    return true;
}

Trace::Trace(std::shared_ptr<SceneData> sd, TraceThreadData *td, const QualityFlags& qf,
             CooperateFunctor& cf, MediaFunctor& mf, RadiosityFunctor& rf) :
    threadData(td),
    sceneData(sd),
    maxFoundTraceLevel(0),
    qualityFlags(qf),
    mailbox(0),
    ssltUniformDirections(GetIndexedSubRandomDirectionGenerator(0, kSubsurfaceDirectionPool)),
    cooperate(cf),
    media(mf),
    radiosity(rf),
    lightColorCacheIndex(-1)
{
    lightSourceLevel1ShadowCache.resize(max(1, (int) threadData->lightSources.size()));
    for(vector<ObjectPtr>::iterator i(lightSourceLevel1ShadowCache.begin()); i != lightSourceLevel1ShadowCache.end(); i++)
        *i = nullptr;

    lightSourceOtherShadowCache.resize(max(1, (int) threadData->lightSources.size()));
    for(vector<ObjectPtr>::iterator i(lightSourceOtherShadowCache.begin()); i != lightSourceOtherShadowCache.end(); i++)
        *i = nullptr;

    lightColorCache.resize(max(20U, sd->parsedMaxTraceLevel + 1));
    for(LightColorCacheListList::iterator it = lightColorCache.begin(); it != lightColorCache.end(); it++)
        it->resize(max(1, (int) threadData->lightSources.size()));

    if(sceneData->boundingMethod == 2)
        mailbox = BSPTree::Mailbox(sceneData->numberOfFiniteObjects);
}

Trace::~Trace()
{
}

namespace
{
struct PortalNesting final
{
    TraceThreadData *thread;
    explicit PortalNesting(TraceThreadData *t) : thread(t) { ++t->portalDepth; }
    ~PortalNesting() { --thread->portalDepth; }
};

struct SurfacePhotonGatherNesting final
{
    size_t& depth;
    bool active;
    SurfacePhotonGatherNesting(size_t& d, bool enabled) : depth(d), active(enabled) { if (active) ++depth; }
    ~SurfacePhotonGatherNesting() { if (active) --depth; }
};

void FindContainingInteriorsTree(const Vector3d& point, const BBOX_TREE *node, RayInteriorVector& found, TraceThreadData *thread)
{
    if (!Inside_BBox(point, node->BBox))
        return;
    if (node->Entries == 0)
    {
        ObjectPtr object = ObjectPtr(node->Node);
        if ((object->interior != nullptr) && object->Inside(point, thread))
            found.push_back(object->interior.get());
    }
    else
        for (int i = 0; i < node->Entries; i++)
            FindContainingInteriorsTree(point, node->Node[i], found, thread);
}
}

void Trace::FindContainingInteriors(const Vector3d& point, RayInteriorVector& found)
{
    if (sceneData->boundingMethod == 2)
    {
        HasInteriorPointObjectCondition precond;
        ContainingInteriorsPointObjectCondition postcond(found);
        BSPInsideCondFunctor ifn(point, sceneData->objects, threadData, precond, postcond);

        mailbox.clear();
        (*sceneData->tree)(point, ifn, mailbox);

        // test infinite objects
        for (std::vector<ObjectPtr>::iterator object = sceneData->objects.begin() + sceneData->numberOfFiniteObjects; object != sceneData->objects.end(); object++)
            if (((*object)->interior != nullptr) && Inside_BBox(point, (*object)->BBox) && (*object)->Inside(point, threadData))
                found.push_back((*object)->interior.get());
    }
    else if ((sceneData->boundingMethod == 0) || (sceneData->boundingSlabs == nullptr))
    {
        for (std::vector<ObjectPtr>::iterator object = sceneData->objects.begin(); object != sceneData->objects.end(); object++)
            if (((*object)->interior != nullptr) && Inside_BBox(point, (*object)->BBox) && (*object)->Inside(point, threadData))
                found.push_back((*object)->interior.get());
    }
    else
        FindContainingInteriorsTree(point, sceneData->boundingSlabs, found, threadData);
}

void Trace::TracePortal(const Portal& portal, Intersection& isect, Ray& ray, MathColour& colour, ColourChannel& transm, COLC weight)
{
    Vector3d rawnormal;
    isect.Object->Normal(rawnormal, &isect, threadData);
    if (Test_Flag(isect.Object, INVERTED_FLAG))
        rawnormal.invert();

    const bool entered = portal.Admits(rawnormal, ray.Direction);
    if (dot(rawnormal, ray.Direction) > 0.0)
        rawnormal.invert();

    double open = 0.0;
    MathColour tint(1.0);
    if (entered)
    {
        open = 1.0;
        TransColour opening;
        bool found;
        {
            ActiveTraceScope activeTrace(threadData, this, weight);
            found = (portal.pigment != nullptr) && Compute_Pigment(opening, portal.pigment, isect.IPoint, &isect, &ray, threadData);
        }
        if (found)
        {
            open = std::max(0.0, std::min(1.0, double(opening.Opacity())));
            tint = opening.colour();
        }
    }

    MathColour view;
    ColourChannel viewTransm = 0.0;
    if ((open > 0.0) && !TracePortalView(portal, isect, ray, rawnormal, weight * open, view, viewTransm))
        open = 0.0;

    colour = tint * view * open;
    transm = viewTransm * open;
    if (open < 1.0)
    {
        Ray nray(ray);
        nray.Origin = isect.IPoint;
        MathColour past;
        ColourChannel pastTransm = 0.0;
        TraceRay(nray, past, pastTransm, weight * (1.0 - open), true);
        colour += past * (1.0 - open);
        transm += pastTransm * (1.0 - open);
    }
}

bool Trace::TracePortalView(const Portal& portal, Intersection& isect, const Ray& ray, const Vector3d& rawnormal, COLC weight,
                            MathColour& view, ColourChannel& transm)
{
    const TraceTicket& ticket = ray.GetTicket();
    const unsigned int limit = (portal.maxDepth > 0) ? portal.maxDepth : ticket.maxAllowedTraceLevel;
    if ((threadData->portalDepth >= limit) ||
        (threadData->screenTraceLevels + ticket.traceLevel + threadData->screenDepth + threadData->portalDepth >= NESTED_VIEW_DEPTH_LIMIT))
    {
        TransColour fallback;
        if ((portal.fallback == nullptr) || !Compute_Pigment(fallback, portal.fallback, isect.IPoint, &isect, &ray, threadData))
            return false;
        view = fallback.colour();
        return true;
    }

    Vector3d bent = ray.Direction;
    if (portal.perturb != nullptr)
    {
        Vector3d normal = rawnormal;
        Perturb_Normal(normal, portal.perturb, isect.IPoint, &isect, &ray, threadData);
        bent += (normal.normalized() - rawnormal) * portal.perturbAmount;
        // As reflection does with its raw normal: a view bent back out of the surface is mirrored in again.
        const double out = dot(bent, rawnormal);
        if (out > -EPSILON)
            bent -= (2.0 * out + EPSILON) * rawnormal;
        bent.normalize();
    }

    Vector3d origin, direction;
    MTransPoint(origin, isect.IPoint, &portal.map);
    MTransDirection(direction, bent, &portal.map);

    // A footprint does not carry through a portal or across its body.
    Ray nray(ray);
    nray.hasDifferentials = false;
    nray.Origin = origin;
    nray.Direction = direction.normalized();
    nray.ResetInteriors();
    RayInteriorVector containing;
    FindContainingInteriors(origin, containing);
    nray.AppendInteriors(containing);

    if (!portal.exit)
    {
        PortalNesting nesting(threadData);
        TraceRay(nray, view, transm, weight, true);
        return true;
    }

    // Across the body the view reaches only as far as the ray would have crossed it, then resumes beyond the body.
    const double chord = portal.Chord(ray, isect.IPoint, threadData);
    if (chord < 0.0)
        return false;
    Vector3d stretch;
    MTransDirection(stretch, ray.Direction, &portal.map);
    const double reach = chord * stretch.length();
    PortalNesting nesting(threadData);
    if ((reach >= EPSILON) && (TraceRay(nray, view, transm, weight, true, reach, true) < HUGE_VAL))
        return true;
    Ray resumed(ray);
    resumed.hasDifferentials = false;
    resumed.Origin = isect.IPoint + ray.Direction * chord;
    TraceRay(resumed, view, transm, weight, true);
    if ((reach >= EPSILON) && qualityFlags.media && !nray.IsPhotonRay() && nray.IsHollowRay())
    {
        Intersection across;
        across.Depth = reach;
        across.Object = nullptr;
        media.ComputeMedia(sceneData->atmosphere, nray, across, view, transm);
        if (sceneData->fog != nullptr)
            ComputeFog(nray, across, view, transm);
    }
    return true;
}

double Trace::TraceRay(Ray& ray, MathColour& colour, ColourChannel& transm, COLC weight, bool continuedRay, DBL maxDepth, bool missOpen)
{
    Intersection bestisect;
    bool found;
    NoSomethingFlagRayObjectCondition precond;
    TrueRayObjectCondition postcond;

    POV_ULONG nrays = threadData->Stats()[Number_Of_Rays]++;
    if(ray.IsPrimaryRay() || (((unsigned char) nrays & 0x0f) == 0x00))
        cooperate();

    // Check for max. trace level or ADC bailout.
    if((ray.GetTicket().traceLevel >= ray.GetTicket().maxAllowedTraceLevel) || (weight < ray.GetTicket().adcBailout))
    {
        if(weight < ray.GetTicket().adcBailout)
            threadData->Stats()[ADC_Saves]++;

        colour.Clear();
        transm = 0.0;
        return HUGE_VAL;
    }

    if (maxDepth >= EPSILON)
        bestisect.Depth = maxDepth;

    found = FindIntersection(bestisect, ray, precond, postcond);
    if (!found && missOpen)
        return HUGE_VAL;
    if (ray.IsPrimaryRay())
    {
        primaryObject = found ? bestisect.Object : nullptr;
        primaryPigment.Clear();
    }

    // Check if we're busy shooting too many radiosity sample rays at an unimportant object
    if (ray.GetTicket().radiosityImportanceQueried >= 0.0)
    {
        if (found && bestisect.Object->RadiosityImportanceSet)
            ray.GetTicket().radiosityImportanceFound = bestisect.Object->RadiosityImportance;
        else
            ray.GetTicket().radiosityImportanceFound = sceneData->radiositySettings.defaultImportance;

        if (ray.GetTicket().radiosityImportanceFound < ray.GetTicket().radiosityImportanceQueried)
        {
            if(found == false)
                return HUGE_VAL;
            else
                return bestisect.Depth;
        }
    }
    float oldRadiosityImportanceQueried = ray.GetTicket().radiosityImportanceQueried;
    ray.GetTicket().radiosityImportanceQueried = -1.0; // indicates that recursive calls to TraceRay() should not check for radiosity importance

    const bool traceLevelIncremented = !continuedRay;

    if(traceLevelIncremented)
    {
        // Set highest level traced.
        ray.GetTicket().traceLevel++;
        ray.GetTicket().maxFoundTraceLevel = (unsigned int) max(ray.GetTicket().maxFoundTraceLevel, ray.GetTicket().traceLevel);
    }

    if(qualityFlags.media && (ray.IsPhotonRay() == true) && (ray.IsHollowRay() == true))
    {
        // Note: this version of ComputeMedia does not deposit photons. This is
        // intentional.  Even though we're processing a photon ray, we don't want
        // to deposit photons in the infinite atmosphere, only in contained
        // media, which is processed later (in ComputeLightedTexture).  [nk]
        media.ComputeMedia(sceneData->atmosphere, ray, bestisect, colour, transm);

        if (sceneData->fog != nullptr)
            ComputeFog(ray, bestisect, colour, transm);
    }

    if (found && (bestisect.Csg != nullptr) && Test_Flag(bestisect.Csg, PORTAL_FLAG))
        TracePortal(*static_cast<const Portal *>(bestisect.Csg), bestisect, ray, colour, transm, weight);
    else if(found)
        ComputeTextureColour(bestisect, colour, transm, ray, weight, false);
    else
        ComputeSky(ray, colour, transm);

    if(qualityFlags.media && (ray.IsPhotonRay() == false) && (ray.IsHollowRay() == true))
    {
        if ((sceneData->rainbow != nullptr) && (ray.IsShadowTestRay() == false))
            ComputeRainbow(ray, bestisect, colour, transm);

        media.ComputeMedia(sceneData->atmosphere, ray, bestisect, colour, transm);

        if (sceneData->fog != nullptr)
            ComputeFog(ray, bestisect, colour, transm);
    }

    if(traceLevelIncremented)
        ray.GetTicket().traceLevel--;
    maxFoundTraceLevel = (unsigned int) max(maxFoundTraceLevel, ray.GetTicket().maxFoundTraceLevel);

    ray.GetTicket().radiosityImportanceQueried = oldRadiosityImportanceQueried;

    if(found == false)
        return HUGE_VAL;
    else
        return bestisect.Depth;
}

bool Trace::FindIntersection(Intersection& bestisect, const Ray& ray)
{
    switch(sceneData->boundingMethod)
    {
        case 2:
        {
            BSPIntersectFunctor ifn(bestisect, ray, sceneData->objects, threadData);
            bool found = false;

            mailbox.clear();

            found = (*(sceneData->tree))(ray, ifn, mailbox, bestisect.Depth);

            // test infinite objects
            for(vector<ObjectPtr>::iterator it = sceneData->objects.begin() + sceneData->numberOfFiniteObjects; it != sceneData->objects.end(); it++)
            {
                Intersection isect;

                if(FindIntersection(*it, isect, ray) && (isect.Depth < bestisect.Depth))
                {
                    bestisect = isect;
                    found = true;
                }
            }

            return found;
        }
        case 1:
        {
            if (sceneData->flatSlabs != nullptr)
                return (Intersect_Flat_BBox_Tree(*sceneData->flatSlabs, ray, &bestisect, threadData));
            if (sceneData->boundingSlabs != nullptr)
                return (Intersect_BBox_Tree(priorityQueue, sceneData->boundingSlabs, ray, &bestisect, threadData));
        }
        // FALLTHROUGH
        case 0:
        {
            bool found = false;

            for(vector<ObjectPtr>::iterator it = sceneData->objects.begin(); it != sceneData->objects.end(); it++)
            {
                Intersection isect;

                if(FindIntersection(*it, isect, ray) && (isect.Depth < bestisect.Depth))
                {
                    bestisect = isect;
                    found = true;
                }
            }

            return found;
        }
    }

    return false;
}

bool Trace::FindIntersection(Intersection& bestisect, const Ray& ray, const RayObjectCondition& precondition, const RayObjectCondition& postcondition)
{
    switch(sceneData->boundingMethod)
    {
        case 2:
        {
            BSPIntersectCondFunctor ifn(bestisect, ray, sceneData->objects, threadData, precondition, postcondition);
            bool found = false;

            mailbox.clear();

            found = (*(sceneData->tree))(ray, ifn, mailbox, bestisect.Depth);

            // test infinite objects
            for(vector<ObjectPtr>::iterator it = sceneData->objects.begin() + sceneData->numberOfFiniteObjects; it != sceneData->objects.end(); it++)
            {
                if(precondition(ray, *it, 0.0) == true)
                {
                    Intersection isect;

                    if(FindIntersection(*it, isect, ray, postcondition) && (isect.Depth < bestisect.Depth))
                    {
                        bestisect = isect;
                        found = true;
                    }
                }
            }

            return found;
        }
        case 1:
        {
            if (sceneData->flatSlabs != nullptr)
                return (Intersect_Flat_BBox_Tree(*sceneData->flatSlabs, ray, &bestisect, precondition, postcondition, threadData));
            if (sceneData->boundingSlabs != nullptr)
                return (Intersect_BBox_Tree(priorityQueue, sceneData->boundingSlabs, ray, &bestisect, precondition, postcondition, threadData));
        }
        // FALLTHROUGH
        case 0:
        {
            bool found = false;

            for(vector<ObjectPtr>::iterator it = sceneData->objects.begin(); it != sceneData->objects.end(); it++)
            {
                if(precondition(ray, *it, 0.0) == true)
                {
                    Intersection isect;

                    if(FindIntersection(*it, isect, ray, postcondition) && (isect.Depth < bestisect.Depth))
                    {
                        bestisect = isect;
                        found = true;
                    }
                }
            }

            return found;
        }
    }

    return false;
}

bool Trace::FindIntersection(ObjectPtr object, Intersection& isect, const Ray& ray, double closest)
{
    if (object != nullptr)
    {
        BBoxVector3d origin;
        BBoxVector3d invdir;
        BBoxDirection variant;

        Vector3d tmp(1.0 / ray.GetDirection()[X], 1.0 / ray.GetDirection()[Y], 1.0 /ray.GetDirection()[Z]);
        origin = BBoxVector3d(ray.Origin);
        invdir = BBoxVector3d(tmp);
        variant = (BBoxDirection)((int(invdir[X] < 0.0) << 2) | (int(invdir[Y] < 0.0) << 1) | int(invdir[Z] < 0.0));

        if(object->Intersect_BBox(variant, origin, invdir, closest) == false)
            return false;

        if(object->Bound.empty() == false)
        {
            if(Ray_In_Bound(ray, object->Bound, threadData) == false)
                return false;
        }

        IStack depthstack(stackPool);
        POV_REFPOOL_ASSERT(depthstack->empty()); // verify that the IStack pulled from the pool is in a cleaned-up condition

        if(object->All_Intersections(ray, depthstack, threadData))
        {
            bool found = false;
            double tmpDepth = 0;

            while(depthstack->size() > 0)
            {
                tmpDepth = depthstack->top().Depth;
                // TODO FIXME - This was SMALL_TOLERANCE, but that's too rough for some scenes [cjc] need to check what it was in the old code [trf]
                if(tmpDepth < closest && (ray.IsSubsurfaceRay() || tmpDepth >= MIN_ISECT_DEPTH))
                {
                    isect = depthstack->top();
                    closest = tmpDepth;
                    found = true;
                }

                depthstack->pop();
            }

            return (found == true);
        }

        POV_REFPOOL_ASSERT(depthstack->empty()); // verify that the IStack is in a cleaned-up condition (again)
    }

    return false;
}

bool Trace::FindIntersection(ObjectPtr object, Intersection& isect, const Ray& ray, const RayObjectCondition& postcondition, double closest)
{
    if (object != nullptr)
    {
        BBoxVector3d origin;
        BBoxVector3d invdir;
        BBoxDirection variant;

        Vector3d tmp(1.0 / ray.GetDirection()[X], 1.0 / ray.GetDirection()[Y], 1.0 /ray.GetDirection()[Z]);
        origin = BBoxVector3d(ray.Origin);
        invdir = BBoxVector3d(tmp);
        variant = (BBoxDirection)((int(invdir[X] < 0.0) << 2) | (int(invdir[Y] < 0.0) << 1) | int(invdir[Z] < 0.0));

        if(object->Intersect_BBox(variant, origin, invdir, closest) == false)
            return false;

        if(object->Bound.empty() == false)
        {
            if(Ray_In_Bound(ray, object->Bound, threadData) == false)
                return false;
        }

        IStack depthstack(stackPool);
        POV_REFPOOL_ASSERT(depthstack->empty()); // verify that the IStack pulled from the pool is in a cleaned-up condition

        if(object->All_Intersections(ray, depthstack, threadData))
        {
            bool found = false;
            double tmpDepth = 0;

            while(depthstack->size() > 0)
            {
                tmpDepth = depthstack->top().Depth;
                // TODO FIXME - This was SMALL_TOLERANCE, but that's too rough for some scenes [cjc] need to check what it was in the old code [trf]
                if(tmpDepth < closest && (ray.IsSubsurfaceRay() || tmpDepth >= MIN_ISECT_DEPTH) && postcondition(ray, object, tmpDepth))
                {
                    isect = depthstack->top();
                    closest = tmpDepth;
                    found = true;
                }

                depthstack->pop();
            }

            return (found == true);
        }

        POV_REFPOOL_ASSERT(depthstack->empty()); // verify that the IStack is in a cleaned-up condition (again)
    }

    return false;
}

unsigned int Trace::GetHighestTraceLevel()
{
    return maxFoundTraceLevel;
}

void Trace::ComputeTextureColour(Intersection& isect, MathColour& colour, ColourChannel& transm, Ray& ray, COLC weight, bool photonPass)
{
    // NOTE: when called during the photon pass this method is used to deposit photons
    // on the surface and not, per se, to compute texture color.
    WeightedTextureVector wtextures;
    double normaldirection;
    MathColour tmpCol;
    ColourChannel tmpTransm = 0.0;
    MathColour c1;
    ColourChannel t1 = 0.0;
    Vector2d uvcoords;
    Vector3d rawnormal;
    Vector3d ipoint(isect.IPoint);

    if (++lightColorCacheIndex >= lightColorCache.size())
    {
        lightColorCache.resize(lightColorCacheIndex + 10);
        for (LightColorCacheListList::iterator it = lightColorCache.begin() + lightColorCacheIndex; it != lightColorCache.end(); it++)
            it->resize(lightColorCache[0].size());
    }
    for (LightColorCacheList::iterator it = lightColorCache[lightColorCacheIndex].begin(); it != lightColorCache[lightColorCacheIndex].end(); it++)
        it->tested = false;

    // compute the surface normal
    isect.Object->Normal(rawnormal, &isect, threadData);

    // I added this to flip the normal if the object is inverted (for CSG).
    // However, I subsequently commented it out for speed reasons - it doesn't
    // make a difference (no pun intended). The preexisting flip code below
    // produces a similar (though more extensive) result. [NK]
    // Actually, we should keep this code to guarantee that Normal_Direction
    // is set properly. [NK]
    if(Test_Flag(isect.Object, INVERTED_FLAG))
        rawnormal.invert();

    // if the surface normal points away, flip its direction
    normaldirection = dot(rawnormal, ray.Direction);
    if(normaldirection > 0.0)
        rawnormal.invert();

    isect.INormal = rawnormal;
    isect.PNormal = rawnormal;

    // now switch to UV mapping if we need to
    if(Test_Flag(isect.Object, UV_FLAG))
    {
        // TODO FIXME
        //  I think we have a serious problem here regarding bump mapping:
        //  The UV vector doesn't contain any information about the (local) *orientation* of U and V in our XYZ co-ordinate system!
        //  This causes slopes do be applied in the wrong directions.

        // get the UV vect of the intersection
        isect.Object->UVCoord(uvcoords, &isect);
        // save the normal and UV coords into Intersection
        isect.Iuv = uvcoords;

        ipoint = Vector3d(uvcoords.u(), uvcoords.v(), 0.0);
    }

    bool isMultiTextured = Test_Flag(isect.Object, MULTITEXTURE_FLAG) ||
                           ((isect.Object->Texture == nullptr) && Test_Flag(isect.Object, CUTAWAY_TEXTURES_FLAG));

    // get textures and weights
    if(isMultiTextured == true)
    {
        isect.Object->Determine_Textures(&isect, normaldirection > 0.0, wtextures, threadData);
    }
    else if (isect.Object->Texture != nullptr)
    {
        if ((normaldirection > 0.0) && (isect.Object->Interior_Texture != nullptr))
            wtextures.push_back(WeightedTexture(1.0, isect.Object->Interior_Texture)); /* Chris Huff: Interior Texture patch */
        else
            wtextures.push_back(WeightedTexture(1.0, isect.Object->Texture));
    }
    else
    {
        // don't need to do anything as the texture list will be empty.
        // TODO: could we perform these tests earlier ? [cjc]
        lightColorCacheIndex--;
        return;
    }

    // Now, we perform the lighting calculations by stepping through
    // the list of textures and summing the weighted color.

    for(WeightedTextureVector::iterator i(wtextures.begin()); i != wtextures.end(); i++)
    {
        TextureVector warps(texturePool);
        POV_REFPOOL_ASSERT(warps->empty()); // verify that the TextureVector pulled from the pool is in a cleaned-up condition

        // if the contribution of this texture is negligible skip ahead
        if ((i->weight < ray.GetTicket().adcBailout) || (i->texture == nullptr))
            continue;

        if(photonPass == true)
        {
            // For the photon pass, colour (and thus c1) represents the
            // light energy being transmitted by the photon.  Because of this, we
            // compute the weighted energy value, then pass it to the texture for
            // processing.
            c1 = colour * i->weight;

            // NOTE that ComputeOneTextureColor is being used for a secondary purpose, and
            // that to place photons on the surface and trigger recursive photon shooting
            ComputeOneTextureColour(c1, t1, i->texture, *warps, ipoint, rawnormal, ray, weight, isect, false, true);
        }
        else
        {
            ComputeOneTextureColour(c1, t1, i->texture, *warps, ipoint, rawnormal, ray, weight, isect, false, false);

            tmpCol    += i->weight * c1;
            tmpTransm += i->weight * t1;
        }
    }

    // [CLi] moved this here from Trace::ComputeShadowTexture() and Trace::ComputeLightedTexture(), respectively,
    // to avoid media to be computed twice when dealing with averaged textures.
    // TODO - For photon rays we're still potentially doing double work on media.
    // TODO - For shadow rays we're still potentially doing double work on distance-based attenuation.
    // Calculate participating media effects.
    if(!photonPass && qualityFlags.media && (!ray.GetInteriors().empty()) && (ray.IsHollowRay() == true))
    {
        media.ComputeMedia(ray.GetInteriors(), ray, isect, tmpCol, tmpTransm);
    }

    colour += tmpCol;
    transm += tmpTransm;

    lightColorCacheIndex--;
}

void Trace::ComputeOneTextureColour(MathColour& resultColour, ColourChannel& resultTransm, const TEXTURE *texture, vector<const TEXTURE *>& warps, const Vector3d& ipoint,
                                    const Vector3d& rawnormal, Ray& ray, COLC weight, Intersection& isect, bool shadowflag, bool photonPass)
{
    // NOTE: this method is used by the photon pass to deposit photons on the surface
    // (and not, per se, to compute texture color)
    const TextureBlendMapPtr& blendmap = texture->Blend_Map;
    const TextureBlendMapEntry *prev, *cur;
    DBL prevWeight, curWeight;
    double value1; // TODO FIXME - choose better name!
    Vector3d tpoint;
    Vector2d uvcoords;
    MathColour c2;
    ColourChannel t2;

    switch(texture->Type)
    {
        case NO_PATTERN:
        case PLAIN_PATTERN:
            break;
        case AVERAGE_PATTERN:
        case UV_MAP_PATTERN:
        case BITMAP_PATTERN:
        default:
            warps.push_back(texture);
            break;
    }

    // ipoint - interseciton point (and evaluation point)
    // epoint - evaluation point
    // tpoint - turbulated/transformed point

    if(texture->Type <= LAST_SPECIAL_PATTERN)
    {
        switch(texture->Type)
        {
            case NO_PATTERN:
                POV_PATTERN_ASSERT(false);  // in Create_Texture(), TEXTURE->Type is explicitly set to PLAIN_PATTERN (in deviation
                                            // from the default TPat settings), and there is no piece of code ever setting it to
                                            // NO_PATTERN (except during parsing of magnet patterns, but that code makes sure it
                                            // doesn't remain set to NO_PATTERN).
                resultColour.Clear();
                resultTransm = 1.0;
                break;
            case AVERAGE_PATTERN:
                Warp_EPoint(tpoint, ipoint, warps.back());
                ComputeAverageTextureColours(resultColour, resultTransm, texture, warps, tpoint, rawnormal, ray, weight, isect, shadowflag, photonPass);
                break;
            case UV_MAP_PATTERN:
                // TODO FIXME
                //  I think we have a serious problem here regarding bump mapping:
                //  The UV vector doesn't contain any information about the (local) *orientation* of U and V in our XYZ co-ordinate system!
                //  This causes slopes do be applied in the wrong directions.

                // Don't bother warping, simply get the UV vect of the intersection
                isect.Object->UVCoord(uvcoords, &isect);
                tpoint = Vector3d(uvcoords[U], uvcoords[V], 0.0);
                cur = &(texture->Blend_Map->Blend_Map_Entries[0]);
                ComputeOneTextureColour(resultColour, resultTransm, cur->Vals, warps, tpoint, rawnormal, ray, weight, isect, shadowflag, photonPass);
                break;
            case BITMAP_PATTERN:
                Warp_EPoint(tpoint, ipoint, texture);
                ComputeOneTextureColour(resultColour, resultTransm, material_map(tpoint, texture), warps, tpoint, rawnormal, ray, weight, isect, shadowflag, photonPass);
                break;
            case PLAIN_PATTERN:
                if(shadowflag == true)
                    ComputeShadowTexture(resultColour, texture, warps, ipoint, rawnormal, ray, isect);
                    // NB: filter and transmit components are ignored by the caller when tracing shadow rays, so no need to set them
                else
                    ComputeLightedTexture(resultColour, resultTransm, texture, warps, ipoint, rawnormal, ray, weight, isect);
                break;
            default:
                throw POV_EXCEPTION_STRING("Bad texture type in ComputeOneTextureColour");
        }
    }
    else
    {
        // NK 19 Nov 1999 added Warp_EPoint
        Warp_EPoint(tpoint, ipoint, texture);
        value1 = Evaluate_TPat(texture, tpoint, &isect, &ray, threadData);

        blendmap->Search(value1, prev, cur, prevWeight, curWeight);

        // NK phmap
        if(photonPass)
        {
            if(prev == cur)
                ComputeOneTextureColour(resultColour, resultTransm, cur->Vals, warps, tpoint, rawnormal, ray, weight, isect, shadowflag, photonPass);
            else
            {
                c2 = resultColour * curWeight;
                ComputeOneTextureColour(c2, t2, cur->Vals, warps, tpoint, rawnormal, ray, weight, isect, shadowflag, photonPass);
                c2 = resultColour * prevWeight; // modifies RGB, but leaves Filter and Transmit unchanged
                ComputeOneTextureColour(c2, t2, prev->Vals, warps, tpoint, rawnormal, ray, weight, isect, shadowflag, photonPass);
            }
        }
        else
        {
            ComputeOneTextureColour(resultColour, resultTransm, cur->Vals, warps, tpoint, rawnormal, ray, weight, isect, shadowflag, photonPass);

            if(prev != cur)
            {
                ComputeOneTextureColour(c2, t2, prev->Vals, warps, tpoint, rawnormal, ray, weight, isect, shadowflag, photonPass);
                resultColour = curWeight * resultColour + prevWeight * c2;
                resultTransm = curWeight * resultTransm + prevWeight * t2;
            }
        }
    }
}

void Trace::ComputeAverageTextureColours(MathColour& resultColour, ColourChannel& resultTransm, const TEXTURE *texture, vector<const TEXTURE *>& warps, const Vector3d& ipoint,
                                         const Vector3d& rawnormal, Ray& ray, COLC weight, Intersection& isect, bool shadowflag, bool photonPass)
{
    const TextureBlendMapPtr& bmap = texture->Blend_Map;
    SNGL total = 0.0;
    MathColour lc;
    ColourChannel lt;

    if(photonPass == false)
    {
        resultColour.Clear();
        resultTransm = 0.0;

        for(vector<TextureBlendMapEntry>::const_iterator i = bmap->Blend_Map_Entries.begin(); i != bmap->Blend_Map_Entries.end(); i++)
        {
            SNGL val = i->value;

            ComputeOneTextureColour(lc, lt, i->Vals, warps, ipoint, rawnormal, ray, weight, isect, shadowflag, photonPass);

            resultColour += lc * val;
            resultTransm += lt * val;

            total += val;
        }

        resultColour /= total;
        resultTransm /= total;
    }
    else
    {
        for(vector<TextureBlendMapEntry>::const_iterator i = bmap->Blend_Map_Entries.begin(); i != bmap->Blend_Map_Entries.end(); i++)
            total += i->value;

        for(vector<TextureBlendMapEntry>::const_iterator i = bmap->Blend_Map_Entries.begin(); i != bmap->Blend_Map_Entries.end(); i++)
        {
            lc = resultColour * (i->value / total);

            ComputeOneTextureColour(lc, lt, i->Vals, warps, ipoint, rawnormal, ray, weight, isect, shadowflag, photonPass);
        }
    }
}

// Direction differential of d mirrored about the unit normal n (Igehy 1999).
static Vector3d ReflectDifferential(const Vector3d& d, const Vector3d& dd, const Vector3d& n, const Vector3d& dn)
{
    return dd - 2.0 * (dot(d, n) * dn + (dot(dd, n) + dot(d, dn)) * n);
}

// Give a derived ray differentials, spreading at most one radian per pixel step so a focus or a grazing bend stays bounded.
static void SetRayDifferentials(Ray& nray, const Vector3d& dOdx, const Vector3d& dOdy, const Vector3d& dDdx, const Vector3d& dDdy)
{
    const DBL lenX = dDdx.length(), lenY = dDdy.length();
    nray.hasDifferentials = std::isfinite(lenX) && std::isfinite(lenY) && std::isfinite(dOdx.length()) && std::isfinite(dOdy.length());
    nray.dOdx = dOdx;
    nray.dOdy = dOdy;
    nray.dDdx = (lenX > 1.0) ? dDdx / lenX : dDdx;
    nray.dDdy = (lenY > 1.0) ? dDdy / lenY : dDdy;
}

bool Trace::TransferDifferentials(const Ray& ray, const Intersection& isect, const Vector3d& rawnormal, SurfaceDifferentials& diff) const
{
    if (!ray.hasDifferentials)
        return false;
    const DBL dn = dot(ray.Direction, rawnormal);
    if (fabs(dn) < 1.0e-12)
        return false;

    const Vector3d dPdx = ray.dOdx + isect.Depth * ray.dDdx;
    const Vector3d dPdy = ray.dOdy + isect.Depth * ray.dDdy;
    diff.dPdx = dPdx - (dot(dPdx, rawnormal) / dn) * ray.Direction;
    diff.dPdy = dPdy - (dot(dPdy, rawnormal) / dn) * ray.Direction;

    // grazing hits stretch the footprint without bound; cap it at 64 pixel widths across the view
    const DBL capX = 64.0 * dPdx.length(), capY = 64.0 * dPdy.length();
    const DBL lenX = diff.dPdx.length(), lenY = diff.dPdy.length();
    if (lenX > capX)
        diff.dPdx *= capX / lenX;
    if (lenY > capY)
        diff.dPdy *= capY / lenY;
    diff.haveNormal = false;
    return true;
}

void Trace::ComputeNormalDifferentials(const Intersection& isect, const Vector3d& rawnormal, SurfaceDifferentials& diff)
{
    // only shapes whose normal depends on the point alone; the rest read state cached for the exact hit and count as flat
    const ObjectBase *obj = isect.Object;
    const bool analytic = dynamic_cast<const Sphere*>(obj) || dynamic_cast<const Plane*>(obj) || dynamic_cast<const Quadric*>(obj) ||
                          dynamic_cast<const Poly*>(obj) || dynamic_cast<const Cone*>(obj) || dynamic_cast<const Disc*>(obj) ||
                          dynamic_cast<const Superellipsoid*>(obj) || dynamic_cast<const IsoSurface*>(obj) ||
                          (dynamic_cast<const Torus*>(obj) && !dynamic_cast<const SpindleTorus*>(obj));
    if (!analytic)
    {
        diff.dNdx = diff.dNdy = Vector3d(0.0);
        diff.haveNormal = true;
        return;
    }
    Intersection probe(isect);
    auto normalAt = [&](const Vector3d& offset) -> Vector3d
    {
        Vector3d n;
        probe.IPoint = isect.IPoint + offset;
        probe.haveLocalIPoint = false;
        isect.Object->Normal(n, &probe, threadData);
        const DBL len = n.length();
        if (!(len > 1.0e-12))
            return rawnormal;
        n /= len;
        return (dot(n, rawnormal) < 0.0) ? -n : n;
    };
    diff.dNdx = normalAt(0.5 * diff.dPdx) - normalAt(-0.5 * diff.dPdx);
    diff.dNdy = normalAt(0.5 * diff.dPdy) - normalAt(-0.5 * diff.dPdy);
    diff.haveNormal = true;
}

bool Trace::ComputePixelFootprint(const Intersection& isect, const vector<const TEXTURE *>& warps, const Vector3d& ipoint,
                                  const SurfaceDifferentials& diff, Vector3d& footX, Vector3d& footY) const
{
    // taps are re-warped from the world point, so only filter where that reproduces the point the texture sees
    if (Test_Flag(isect.Object, UV_FLAG))
        return false;
    Vector3d p = isect.IPoint;
    for (const TEXTURE *warp : warps)
    {
        if (warp->Type == UV_MAP_PATTERN)
            return false;
        Warp_EPoint(p, p, warp);
    }
    if ((p - ipoint).length() > 1.0e-9 * (1.0 + ipoint.length()))
        return false;

    footX = diff.dPdx * textureFilterScale;
    footY = diff.dPdy * textureFilterScale;

    const DBL tiny = 1.0e-9 * (1.0 + isect.IPoint.length());
    return (footX.length() > tiny) || (footY.length() > tiny);
}

bool Trace::ComputeFilteredPigment(TransColour& colour, FilteredLayer& means, const PIGMENT *pigment, const vector<const TEXTURE *>& warps,
                                   const Vector3d& footX, const Vector3d& footY, Intersection& isect, Ray& ray)
{
    static const DBL kRotatedGrid[4][2] = { { -0.375, -0.125 }, { 0.125, -0.375 }, { 0.375, 0.125 }, { -0.125, 0.375 } };
    const DBL kSpread = 0.01;
    TransColour sum, first, tap;
    MathColour premultiplied, transmitted;
    DBL opacity = 0.0;
    bool found = false, filters = false;
    const bool legacy = (sceneData->EffectiveLanguageVersion() < 370);
    int taps = 0;
    DBL spread = 0.0;

    auto sample = [&](DBL u, DBL v)
    {
        Vector3d p = isect.IPoint + u * footX + v * footY;
        for (const TEXTURE *warp : warps)
            Warp_EPoint(p, p, warp);
        tap.Clear();
        found = Compute_Pigment(tap, pigment, p, &isect, &ray, threadData) || found;
        sum += tap;
        const DBL att = legacy ? DBL(tap.LegacyOpacity()) : DBL(tap.Opacity());
        premultiplied += tap.colour() * att;
        opacity += att;
        transmitted += tap.TransmittedColour();
        filters = filters || (tap.filter() != 0.0);
        if (taps++ == 0)
            first = tap;
        else
        {
            const TransColour d = tap - first;
            spread = max(spread, max(DBL(d.colour().MaxAbs()), max(fabs(DBL(d.filter())), fabs(DBL(d.transm())))));
        }
    };

    const DBL lenX = footX.length(), lenY = footY.length();
    const DBL aniso = max(lenX, lenY) / max(min(lenX, lenY), 1.0e-6 * max(lenX, lenY));

    // the cheap start: the centre and two opposite corners, which are enough where all three agree
    bool early = false;
    if (textureFilterTaps == 3)
    {
        sample(0.0, 0.0);
        sample(-0.35, -0.35);
        sample(0.35, 0.35);
        early = (spread <= kSpread) && (aniso <= 2.0);
    }

    if (!early)
    {
        // a few agreeing taps can all land on one colour of a fine pattern, so confirm with the mirrored grid
        for (int i = 0; i < 4; i++)
            sample(kRotatedGrid[i][0], kRotatedGrid[i][1]);
        if ((spread <= kSpread) && (aniso <= 2.0))
            for (int i = 0; i < 4; i++)
                sample(-kRotatedGrid[i][0], kRotatedGrid[i][1]);
    }

    if (!early && ((spread > kSpread) || (aniso > 2.0)))
    {
        // a sheared lattice: rows across the short side, columns along the long side growing with anisotropy
        const int nLong = clip(int(ceil(4.0 * aniso)), 4, 32);
        const int nShort = clip(64 / nLong, 2, 4);
        const int nu = (lenX >= lenY) ? nLong : nShort;
        const int nv = (lenX >= lenY) ? nShort : nLong;
        for (int j = 0; j < nv; j++)
            for (int i = 0; i < nu; i++)
                sample((i + (j + 0.5) / nv) / nu - 0.5, (j + 0.5) / nv - 0.5);
    }

    colour = sum / DBL(taps);
    // averaged premultiplied, so the colour that shows, colour times opacity, is the footprint's true mean
    if (opacity > 1.0e-6 * DBL(taps))
        colour.colour() = premultiplied / opacity;
    // what a filtering tap lets through is its colour times its filter, so that mean is kept apart from the lit one
    means.filters = filters;
    means.transmitted = transmitted / DBL(taps);
    means.opacity = opacity / DBL(taps);
    return found;
}

void Trace::ComputeLightedTexture(MathColour& resultColour, ColourChannel& resultTransm, const TEXTURE *texture, vector<const TEXTURE *>& warps, const Vector3d& ipoint,
                                  const Vector3d& rawnormal, Ray& ray, COLC weight, Intersection& isect)
{
    Interior *interior;
    const TEXTURE *layer;
    int i;
    bool radiosity_done, radiosity_back_done, radiosity_needed;
    int layer_number;
    double w1;
    double new_Weight;
    double att, trans, max_Radiosity_Contribution;
    double cos_Angle_Incidence;
    Vector3d layNormal, topNormal;
    MathColour attCol;
    TransColour layCol;
    MathColour rflCol;
    MathColour rfrCol;
    ColourChannel rfrTransm;
    MathColour filCol;
    MathColour tmpCol, tmp;
    MathColour ambCol; // Note that there is no gathering of filter or transparency
    MathColour ambBackCol;
    bool one_colour_found, colour_found;
    bool tir_occured;
    PhotonGatherer *surfacePhotonGatherer = nullptr;
    const bool gatherSurfacePhotons = sceneData->photonSettings.photonsEnabled && sceneData->surfacePhotonMap.numPhotons > 0;
    const size_t gatherIndex = surfacePhotonGatherDepth;
    SurfacePhotonGatherNesting gatherNesting(surfacePhotonGatherDepth, gatherSurfacePhotons);

    double relativeIor;
    ComputeRelativeIOR(ray, isect.Object->interior.get(), relativeIor);

    WNRXVector listWNRX(wnrxPool); // "Weight, Normal, Reflectivity, eXponent"
    POV_REFPOOL_ASSERT(listWNRX->empty()); // verify that the WNRXVector pulled from the pool is in a cleaned-up condition

    // resultColour builds up the apparent visible color of the point.
    // resultTransm builds up the apparent visible color of whatever is behind the point (presuming 100% white background).
    resultColour.Clear();
    resultTransm = 0.0;

    // filCol serves two purposes. It accumulates the filter properties
    // of a multi-layer texture so that if a ray makes it all the way through
    // all layers, the color of object behind is filtered by this object.
    // It also is used to attenuate how much of an underlayer you
    // can see in a layered texture.  Note that when computing the reflective
    // properties of a layered texture, the upper layers don't filter the
    // light from the lower layers -- the layer colors add together (even
    // before we added additive transparency via the "transmit" 5th
    // color channel).  However when computing the transmitted rays, all layers
    // filter the light from any objects behind this object. [CY 1/95]

    // NK layers - switched transmit component to zero
    // [CLi] changed filCol to RGB, as filter and transmit were always pinned to 1.0 and 0.0 respectively anyway
    filCol = MathColour(1.0);

    trans = 1.0;

    // Add in radiosity (stochastic interreflection-based ambient light) if desired
    radiosity_done = false;
    radiosity_back_done = false;

    // This block just sets up radiosity for the code inside the loop, which is first-time-through.
    radiosity_needed = (sceneData->radiositySettings.radiosityEnabled == true) && qualityFlags.radiosity &&
                       (radiosity.CheckRadiosityTraceLevel(ray.GetTicket()) == true) &&
                       (Test_Flag(isect.Object, IGNORE_RADIOSITY_FLAG) == false);

    // Loop through the layers and compute the ambient, diffuse,
    // phong and specular for these textures.
    one_colour_found = false;

    if(gatherSurfacePhotons)
    {
        if(gatherIndex == surfacePhotonGatherers.size())
            surfacePhotonGatherers.emplace_back(new PhotonGatherer(&sceneData->surfacePhotonMap, sceneData->photonSettings));
        surfacePhotonGatherer = surfacePhotonGatherers[gatherIndex].get();
        surfacePhotonGatherer->gathered = false;
    }

    SubsurfaceLayers subsurfaceLayers;

    SurfaceDifferentials hitDiff;
    Vector3d footX, footY;
    const bool haveDiff = (textureFilterScale > 0.0) && TransferDifferentials(ray, isect, rawnormal, hitDiff);
    const bool filterPigments = haveDiff && ComputePixelFootprint(isect, warps, ipoint, hitDiff, footX, footY);
    FilteredLayer layMeans;
    auto secondaryDiff = [&]() -> const SurfaceDifferentials *
    {
        if (haveDiff && !hitDiff.haveNormal)
            ComputeNormalDifferentials(isect, rawnormal, hitDiff);
        return haveDiff ? &hitDiff : nullptr;
    };

    for (layer_number = 0, layer = texture; (layer != nullptr) && (trans > ray.GetTicket().adcBailout); layer_number++, layer = layer->Next)
    {
        // Get perturbed surface normal.
        layNormal = rawnormal;

        if (qualityFlags.normals && (layer->Tnormal != nullptr))
        {
            for(vector<const TEXTURE *>::iterator i(warps.begin()); i != warps.end(); i++)
                Warp_Normal(layNormal, layNormal, *i, Test_Flag(*i, DONT_SCALE_BUMPS_FLAG));

            Perturb_Normal(layNormal, layer->Tnormal, ipoint, &isect, &ray, threadData);

            if((Test_Flag(layer->Tnormal, DONT_SCALE_BUMPS_FLAG)))
                layNormal.normalize();

            // TODO - Reverse iterator may be less performant than forward iterator; we might want to
            //        compare performance with using forward iterators and decrement, or using random access.
            for(vector<const TEXTURE *>::reverse_iterator i(warps.rbegin()); i != warps.rend(); i++)
                UnWarp_Normal(layNormal, layNormal, *i, Test_Flag(*i, DONT_SCALE_BUMPS_FLAG));
        }

        // Store top layer normal.
        if(layer_number == 0)
            topNormal = layNormal;

        // Get surface colour.
        new_Weight = weight * trans;
        layMeans.filters = false;
        {
            ActiveTraceScope activeTrace(threadData, this, new_Weight);
            if (filterPigments && (layer->Pigment->Type != PLAIN_PATTERN) && (layer->Pigment->Type != UV_MAP_PATTERN) &&
                !(qualityFlags.quickColour && layer->Pigment->Quick_Colour.IsValid()))
                colour_found = ComputeFilteredPigment(layCol, layMeans, layer->Pigment, warps, footX, footY, isect, ray);
            else
                colour_found = Compute_Pigment(layCol, layer->Pigment, ipoint, &isect, &ray, threadData);
        }
        if ((layer_number == 0) && ray.IsPrimaryRay())
            primaryPigment = layCol;

        // If a valid color was returned set one_colour_found to true.
        // An invalid color is returned if a surface point is outside
        // an image map used just once.
        one_colour_found = (one_colour_found || colour_found);

        // This section of code used to be the routine Compute_Reflected_Colour.
        // I copied it in here to rearrange some of it more easily and to
        // see if we could eliminate passing a zillion parameters for no
        // good reason. [CY 1/95]

        if(qualityFlags.ambientOnly)
        {
            // Only use top layer and kill transparency if low quality.
            resultColour = layCol.colour();
            resultTransm = 0.0;
        }
        else
        {
            // Store vital information for later reflection.
            listWNRX->push_back(WNRX(new_Weight, layNormal, MathColour(), layer->Finish->Reflect_Exp));

            // angle-dependent reflectivity
            cos_Angle_Incidence = -dot(ray.Direction, layNormal);

            if ((isect.Object->interior == nullptr) && layer->Finish->Reflection_Fresnel)
                throw POV_EXCEPTION_STRING("fresnel reflection used with no interior."); // TODO FIXME - wrong place to report this [trf]

            ComputeReflectivity(listWNRX->back().weight, listWNRX->back().reflec,
                                layer->Finish->Reflection_Max, layer->Finish->Reflection_Min,
                                layer->Finish->Reflection_Fresnel, layer->Finish->Reflection_Falloff,
                                cos_Angle_Incidence, relativeIor);

            ComputeMetallic(listWNRX->back().reflec, layer->Finish->Reflect_Metallic, layCol.colour(), cos_Angle_Incidence);

            // We need to reduce the layer's own brightness if it is transparent.
            if (sceneData->EffectiveLanguageVersion() < 370)
                att = layCol.LegacyOpacity();
            else
                att = layCol.Opacity();
            if (layMeans.filters)
                att = layMeans.opacity;

            if (layer->Finish->AlphaKnockout)
                listWNRX->back().reflec *= att;

            // now compute the BRDF or BSSRDF contribution
            tmpCol.Clear();

            if(sceneData->useSubsurface && layer->Finish->UseSubsurface && qualityFlags.subsurface)
            {
                // A single layer scatters here; several are blended as they are seen and scattered once after the loop.
                if (texture->Next == nullptr)
                {
                    SubsurfaceLayers single;
                    single.Add(layer, layCol.colour(), MathColour(att));
                    if (single.Finish())
                        ComputeSubsurfaceScattering(single, isect, ray, tmpCol);
                }
                else
                    subsurfaceLayers.Add(layer, layCol.colour(), filCol * att);

                // Radiosity-style ambient may be subject to subsurface light transport.
                // In that case, the respective computations are handled by the BSSRDF code already.
                if (sceneData->subsurfaceUseRadiosity)
                    radiosity_needed = false;
            }
            // Add radiosity ambient contribution.
            if(radiosity_needed)
            {
                DBL diffuse    = layer->Finish->Diffuse;
                DBL brilliance = 1.0;

                if (sceneData->radiositySettings.brilliance)
                {
                    diffuse    *= layer->Finish->BrillianceAdjustRad;
                    brilliance =  layer->Finish->Brilliance;
                }

                // if radiosity calculation needed, but not yet done, do it now
                // TODO FIXME - [CLi] with "normal on", shouldn't we compute radiosity for each layer separately (if it has pertubed normals)?
                // TODO FIXME - [CLi] with "brilliance on", shouldn't we compute radiosity for each layer separately (if it has non-default brilliance)?
                if(radiosity_done == false)
                {
                    // calculate max possible contribution of radiosity, to see if calculating it is worthwhile
                    // TODO FIXME - other layers may see a higher weight!
                    // Maybe we should go along and compute *first* the total contribution radiosity will make,
                    // and at the *end* apply it.
                    max_Radiosity_Contribution = (filCol * layCol.colour()).WeightGreyscale() * att * diffuse;

                    if(max_Radiosity_Contribution > ray.GetTicket().adcBailout)
                    {
                        radiosity.ComputeAmbient(isect.IPoint, rawnormal, layNormal, brilliance, ambCol, weight * max_Radiosity_Contribution, ray.GetTicket());
                        radiosity_done = true;
                    }
                }

                // [CLi] moved multiplication with filCol to further below
                MathColour radiosityContribution = (layCol.colour() * ambCol) * (att * diffuse);

                diffuse = layer->Finish->DiffuseBack;
                if (sceneData->radiositySettings.brilliance)
                    diffuse *= layer->Finish->BrillianceAdjustRad;

                // if backside radiosity calculation needed, but not yet done, do it now
                // TODO FIXME - [CLi] with "normal on", shouldn't we compute radiosity for each layer separately (if it has pertubed normals)?
                // TODO FIXME - [CLi] with "brilliance on", shouldn't we compute radiosity for each layer separately (if it has non-default brilliance)?
                if(layer->Finish->DiffuseBack != 0.0)
                {
                    if(radiosity_back_done == false)
                    {
                        // calculate max possible contribution of radiosity, to see if calculating it is worthwhile
                        // TODO FIXME - other layers may see a higher weight!
                        // Maybe we should go along and compute *first* the total contribution radiosity will make,
                        // and at the *end* apply it.
                        max_Radiosity_Contribution = (filCol * layCol.colour()).WeightGreyscale() * att * diffuse;

                        if(max_Radiosity_Contribution > ray.GetTicket().adcBailout)
                        {
                            radiosity.ComputeAmbient(isect.IPoint, -rawnormal, -layNormal, brilliance, ambBackCol, weight * max_Radiosity_Contribution, ray.GetTicket());
                            radiosity_back_done = true;
                        }
                    }

                    // [CLi] moved multiplication with filCol to further below
                    radiosityContribution += (layCol.colour() * ambBackCol) * (att * diffuse);
                }

#if POV_EXPERIMENTAL_BRILLIANCE_OUT
                if((sceneData->radiositySettings.brilliance) && (layer->Finish->BrillianceOut != 1.0))
                    radiosityContribution *= pow(fabs(cos_Angle_Incidence), layer->Finish->BrillianceOut-1.0) * (layer->Finish->BrillianceOut+7.0)/8.0;
#endif

                if (layer->Finish->Fresnel != 0.0)
                {
                    // In diffuse reflections (which includes radiosity), the Fresnel effect is
                    // that we _lose_ the reflected component as the light enters the material
                    // (where it changes direction randomly), and then _again lose_ another
                    // reflected component as the light leaves the material. However, taking into
                    // account the former of these losses requires knowledge of the incoming light
                    // direction, which must be accounted for in radiosity sampling (TODO - not implemented yet),
                    // so we only do the latter here.
                    // NB: One might think that to properly compute the outgoing loss we would have
                    // to compute the Fresnel term for the _internal_ angle of incidence and the
                    // _inverse_ of the relative refractive index; however, the Fresnel formula
                    // is such that we can just as well plug in the external angle of incidence
                    // and straight relative refractive index.
                    double f = layer->Finish->Fresnel * FresnelR(cos_Angle_Incidence, relativeIor);
                    radiosityContribution *= (1.0 - f);
                }

                tmpCol += radiosityContribution;
            }

            // Add emissive ("classic" ambient) contribution.
            // [CLi] moved multiplication with filCol to further below
            MathColour emission = layer->Finish->Emission;
            if (!sceneData->radiositySettings.radiosityEnabled || (sceneData->EffectiveLanguageVersion() < 370))
                // only use "ambient" setting when radiosity is disabled (or in legacy scenes)
                emission += layer->Finish->Ambient * sceneData->ambientLight;
            if (layer->Finish->Fresnel != 0.0)
            {
                // Light emitted from the material itself is subject to the Fresnel effect as it
                // leaves the material _losing_ light reflected at the interface.
                // In diffuse reflections (which includes ambient), the Fresnel effect is
                // that we _lose_ the reflected component as the light enters the material
                // (where it changes direction randomly), and then _again lose_ another
                // reflected component as the light leaves the material. The former of these
                // losses needs to be accounted for when choosing the `ambient` setting,
                // so we only do the latter here.
                // NB: One might think that to properly compute the outgoing loss we would have
                // to compute the Fresnel term for the _internal_ angle of incidence and the
                // _inverse_ of the relative refractive index; however, the Fresnel formula
                // is such that we can just as well plug in the external angle of incidence
                // and straight relative refractive index.
                double f = layer->Finish->Fresnel * FresnelR(cos_Angle_Incidence, relativeIor);
                emission *= (1.0 - f);
            }
            tmpCol += (layCol.colour() * emission * att);

            // set up the "litObjectIgnoresPhotons" flag (thread variable) so that
            // ComputeShadowColour will know whether or not this lit object is
            // ignoring photons, which affects partial-shadowing (i.e. filter and transmit)
            threadData->litObjectIgnoresPhotons = Test_Flag(isect.Object,PH_IGNORE_PHOTONS_FLAG);

            // Add diffuse, phong, specular, and iridescence contribution.
            // (We don't need to do this for (non-radiosity) rays during pretrace, as it does not affect radiosity sampling)
            if(!ray.IsPretraceRay())
            {
                if (((layer->Finish->Diffuse     != 0.0) ||
                     (layer->Finish->DiffuseBack != 0.0) ||
                     (layer->Finish->Specular    != 0.0) ||
                     (layer->Finish->Phong       != 0.0)) &&
                    ((!layer->Finish->AlphaKnockout) ||
                     (att != 0.0)))
                {
                    MathColour classicContribution;

                    ComputeDiffuseLight(layer->Finish, isect.IPoint, ray, layNormal, layCol.colour(), classicContribution, att, isect.Object, relativeIor);

#if POV_EXPERIMENTAL_BRILLIANCE_OUT
                    if(layer->Finish->BrillianceOut != 1.0)
                    {
                        double cos_angle_of_incidence = dot(ray.Direction, layNormal);
                        classicContribution *= pow(fabs(cos_angle_of_incidence), layer->Finish->BrillianceOut-1.0)* (layer->Finish->BrillianceOut+7.0)/8.0;
                    }
#endif

                    tmpCol += classicContribution;
                }
            }

            if(sceneData->photonSettings.photonsEnabled && sceneData->surfacePhotonMap.numPhotons > 0)
            {
                // NK phmap - now do the same for the photons in the area
                if(!Test_Flag(isect.Object, PH_IGNORE_PHOTONS_FLAG))
                {
                    MathColour photonsContribution;

                    ComputePhotonDiffuseLight(layer->Finish, isect.IPoint, ray, layNormal, rawnormal, layCol.colour(), photonsContribution, att, isect.Object, relativeIor, *surfacePhotonGatherer);

#if POV_EXPERIMENTAL_BRILLIANCE_OUT
                    if(layer->Finish->BrillianceOut != 1.0)
                    {
                        double cos_angle_of_incidence = dot(ray.Direction, layNormal);
                        photonsContribution *= pow(fabs(cos_angle_of_incidence), layer->Finish->BrillianceOut-1.0) * (layer->Finish->BrillianceOut+7.0)/8.0;
                    }
#endif

                    tmpCol += photonsContribution;
                }
            }

            tmpCol *= filCol;
            resultColour += tmpCol;
        }

        // Get new filter color.
        if(colour_found)
        {
            filCol *= layMeans.filters ? layMeans.transmitted : layCol.TransmittedColour();

            if(layer->Finish->Conserve_Energy != 0 && listWNRX->empty() == false)
            {
                // adjust filCol based on reflection
                // this would work so much better with r,g,b,rt,gt,bt
                filCol *= (1.0 - listWNRX->back().reflec).ClippedUpper(1.0);
            }
        }

        // Get new remaining translucency.
        // [CLi] changed filCol to RGB, as filter and transmit were always pinned to 1.0 and 0.0, respectively anyway
        // TODO CLARIFY - is this working properly if filCol.greyscale() is negative? (what would be the right thing then?)
        trans = min(1.0, (double)fabs(filCol.Greyscale()));
    }

    if (subsurfaceLayers.Finish())
        ComputeSubsurfaceScattering(subsurfaceLayers, isect, ray, resultColour);

    // Calculate transmitted component.
    //
    // If the surface is translucent a transmitted ray is traced
    // and its contribution is added to the total ResCol after
    // filtering it by filCol.
    tir_occured = false;

    if (((interior = isect.Object->interior.get()) != nullptr) && (trans > ray.GetTicket().adcBailout) && qualityFlags.refractions)
    {
        // [CLi] changed filCol to RGB, as filter and transmit were always pinned to 1.0 and 0.0, respectively anyway
        // TODO CLARIFY - is this working properly if some filCol component is negative? (what would be the right thing then?)
        w1 = filCol.WeightMaxAbs();
        new_Weight = weight * w1;

        // Trace refracted ray.
        double roulette;
        if (SurvivesRadiosityRoulette(ray, isect.IPoint, new_Weight, 0, roulette))
        {
            const double share = ray.GetTicket().radiosityShare;
            ray.GetTicket().radiosityShare = share * roulette;
            tir_occured = ComputeRefraction(texture->Finish, interior, isect.IPoint, ray, topNormal, rawnormal, rfrCol, rfrTransm, new_Weight,
                                            secondaryDiff());
            ray.GetTicket().radiosityShare = share;
            rfrCol *= roulette;
        }
        else
        {
            rfrCol.Clear();
            rfrTransm = 0.0;
        }

        // Get distance based attenuation.
        // TODO - virtually the same code is used in ComputeShadowTexture().
        attCol.Set(interior->Old_Refract);

        if ((interior != nullptr) && ray.IsInterior(interior) == true)
        {
            if(fabs(interior->Fade_Distance) > EPSILON)
            {
                // NK attenuate
                if(interior->Fade_Power >= 1000)
                {
                    double depth = isect.Depth / interior->Fade_Distance;
                    attCol *= Exp(-(1.0 - interior->Fade_Colour) * depth);
                }
                else
                {
                    att = 1.0 + pow(isect.Depth / interior->Fade_Distance, (double)interior->Fade_Power);
                    attCol *= (interior->Fade_Colour + (1.0 - interior->Fade_Colour) / att);
                }
            }
        }

        // If total internal reflection occured the transmitted light is not filtered.
        if(tir_occured)
        {
            resultColour += attCol * rfrCol;
            // NOTE: transm() (alpha channel) stays zero
        }
        else
        {
            if(one_colour_found)
            {
                // [CLi] changed filCol to RGB, as filter and transmit were always pinned to 1.0 and 0.0, respectively anyway
                resultColour += attCol * rfrCol * filCol;
                // We need to know the transmittance value for the alpha channel. [DB]
                resultTransm = attCol.Greyscale() * rfrTransm * trans;
            }
            else
            {
                resultColour += attCol * rfrCol;
                // We need to know the transmittance value for the alpha channel. [DB]
                resultTransm = attCol.Greyscale() * rfrTransm;
            }
        }
    }

    // Calculate reflected component.
    //
    // If total internal reflection occured all reflections using
    // TopNormal are skipped.
    if(qualityFlags.reflections)
    {
        layer = texture;
        for(i = 0; i < layer_number; i++)
        {
            if((!tir_occured) ||
               (fabs(topNormal[X]-(*listWNRX)[i].normal[X]) > EPSILON) ||
               (fabs(topNormal[Y]-(*listWNRX)[i].normal[Y]) > EPSILON) ||
               (fabs(topNormal[Z]-(*listWNRX)[i].normal[Z]) > EPSILON))
            {
                if(!(*listWNRX)[i].reflec.IsZero())
                {
                    rflCol.Clear();
                    double roulette;
                    if (!SurvivesRadiosityRoulette(ray, isect.IPoint, (*listWNRX)[i].weight, i + 1, roulette))
                        roulette = 0.0;
                    else
                    {
                        const double share = ray.GetTicket().radiosityShare;
                        ray.GetTicket().radiosityShare = share * roulette;
                        ComputeReflection(layer->Finish, isect.IPoint, ray, (*listWNRX)[i].normal, rawnormal, rflCol, (*listWNRX)[i].weight,
                                          secondaryDiff());
                        ray.GetTicket().radiosityShare = share;
                    }

                    // the roulette scales the term, not the colour a reflection exponent bends
                    if((*listWNRX)[i].reflex != 1.0)
                    {
                        resultColour += (*listWNRX)[i].reflec * Pow(rflCol, (*listWNRX)[i].reflex) * roulette;
                    }
                    else
                    {
                        resultColour += (*listWNRX)[i].reflec * rflCol * roulette;
                    }
                }
            }
            layer = layer->Next;
        }
    }
}

void Trace::ComputeShadowTexture(MathColour& filtercolour, const TEXTURE *texture, vector<const TEXTURE *>& warps, const Vector3d& ipoint,
                                 const Vector3d& rawnormal, const Ray& ray, Intersection& isect)
{
    Interior *interior = isect.Object->interior.get();
    const TEXTURE *layer;
    double caustics, dotval, k;
    Vector3d layer_Normal;
    MathColour refraction;
    TransColour layer_Pigment_Colour;
    bool one_colour_found, colour_found;

    MathColour tmpCol = MathColour(1.0);

    one_colour_found = false;

    for (layer = texture; layer != nullptr; layer = layer->Next)
    {
        colour_found = Compute_Pigment(layer_Pigment_Colour, layer->Pigment, ipoint, &isect, &ray, threadData);

        if(colour_found)
        {
            one_colour_found = true;

            tmpCol *= layer_Pigment_Colour.TransmittedColour();
        }

        // Get normal for faked caustics (will rewrite later to cache).
        if ((interior != nullptr) && ((caustics = interior->Caustics) != 0.0))
        {
            layer_Normal = rawnormal;

            if (qualityFlags.normals && (layer->Tnormal != nullptr))
            {
                for(vector<const TEXTURE *>::iterator i(warps.begin()); i != warps.end(); i++)
                    Warp_Normal(layer_Normal, layer_Normal, *i, Test_Flag(*i, DONT_SCALE_BUMPS_FLAG));

                Perturb_Normal(layer_Normal, layer->Tnormal, ipoint, &isect, &ray, threadData);

                if((Test_Flag(layer->Tnormal,DONT_SCALE_BUMPS_FLAG)))
                    layer_Normal.normalize();

                // TODO - Reverse iterator may be less performant than forward iterator; we might want to
                //        compare performance with using forward iterators and decrement, or using random access.
                for(vector<const TEXTURE *>::reverse_iterator i(warps.rbegin()); i != warps.rend(); i++)
                    UnWarp_Normal(layer_Normal, layer_Normal, *i, Test_Flag(*i, DONT_SCALE_BUMPS_FLAG));
            }

            // Get new filter/transmit values.
            dotval = dot(layer_Normal, ray.Direction);

            k = (1.0 + pow(fabs(dotval), caustics));

            tmpCol *= k;
        }
    }

    // TODO - [CLi] aren't spatial effects (distance attenuation, media) better handled in Trace::ComputeTextureColour()? We may be doing double work here!

    // Get distance based attenuation.
    // TODO - virtually the same code is used in ComputeLightedTexture().
    refraction = MathColour(1.0);

    if ((interior != nullptr) && (ray.IsInterior(interior) == true))
    {
        if((interior->Fade_Power > 0.0) && (fabs(interior->Fade_Distance) > EPSILON))
        {
            // NK - attenuation
            if(interior->Fade_Power>=1000)
            {
                refraction *= Exp( -(1.0 - interior->Fade_Colour) * (isect.Depth / interior->Fade_Distance) );
            }
            else
            {
                k = 1.0 + pow(isect.Depth / interior->Fade_Distance, (double)interior->Fade_Power);
                refraction *= (interior->Fade_Colour + (1.0 - interior->Fade_Colour) / k);
            }
        }
    }

    // Get distance based attenuation.
    filtercolour = tmpCol * refraction;
}

void Trace::ComputeReflection(const FINISH* finish, const Vector3d& ipoint, Ray& ray, const Vector3d& normal, const Vector3d& rawnormal, MathColour& colour, COLC weight,
                              const SurfaceDifferentials *diff)
{
    Ray nray(ray);
    double n, n2;
    const Vector3d *mirror = &normal;
    bool mirrorAgain = false;

    nray.SetFlags(Ray::ReflectionRay, ray);
    nray.SetKey(ray.NextChildKey(kDrawReflection));

    // The rest of this is essentally what was originally here, with small changes.
    n = -2.0 * dot(ray.Direction, normal);
    nray.Direction = ray.Direction + n * normal;

    // Nathan Kopp & CEY 1998 - Reflection bugfix
    // if the new ray is going the opposite direction as raw normal, we
    // need to fix it.
    n = dot(nray.Direction, rawnormal);

    if(n < 0.0)
    {
        // It needs fixing. Which kind?
        n2 = dot(nray.Direction, normal);

        if(n2 < 0.0)
        {
            // reflected inside rear virtual surface. Reflect Ray using Raw_Normal
            n = -2.0 * dot(ray.Direction, rawnormal);
            nray.Direction = ray.Direction + n * rawnormal;
            mirror = &rawnormal;
        }
        else
        {
            // Double reflect NRay using Raw_Normal
            // n = dot(nray.Direction, rawnormal); - kept the old n around
            n *= -2.0;
            nray.Direction += n * rawnormal;
            mirrorAgain = true;
        }
    }

    if (diff != nullptr)
    {
        const DBL s = ((mirror == &normal) && (dot(normal, rawnormal) < 0.0)) ? -1.0 : 1.0;
        Vector3d dDdx = ReflectDifferential(ray.Direction, ray.dDdx, *mirror, s * diff->dNdx);
        Vector3d dDdy = ReflectDifferential(ray.Direction, ray.dDdy, *mirror, s * diff->dNdy);
        if (mirrorAgain)
        {
            const Vector3d once = ray.Direction - 2.0 * dot(ray.Direction, normal) * normal;
            dDdx = ReflectDifferential(once, dDdx, rawnormal, diff->dNdx);
            dDdy = ReflectDifferential(once, dDdy, rawnormal, diff->dNdy);
        }
        SetRayDifferentials(nray, diff->dPdx, diff->dPdy, dDdx, dDdy);
    }

    nray.Direction.normalize();
    nray.Origin = ipoint;
    threadData->Stats()[Reflected_Rays_Traced]++;

    // Trace reflected ray.
    bool alphaBackground = ray.GetTicket().alphaBackground;
    ray.GetTicket().alphaBackground = false;
    if (!ray.IsPhotonRay() && (finish->Irid > 0.0))
    {
        MathColour tmpCol;
        ColourChannel dummyTransm;
        TraceRay(nray, tmpCol, dummyTransm, weight, false);
        ComputeIridColour(finish, nray.Direction, ray.Direction, normal, ipoint, tmpCol);
        colour += tmpCol;
    }
    else
    {
        ColourChannel dummyTransm;
        TraceRay(nray, colour, dummyTransm, weight, false);
    }
    ray.GetTicket().alphaBackground = alphaBackground;
}

bool Trace::ComputeRefraction(const FINISH* finish, Interior *interior, const Vector3d& ipoint, Ray& ray, const Vector3d& normal, const Vector3d& rawnormal, MathColour& colour, ColourChannel& transm, COLC weight,
                              const SurfaceDifferentials *diff)
{
    Ray nray(ray);
    Vector3d localnormal;
    double n, ior, dispersion;
    unsigned int dispersionelements = interior->Disp_NElems;
    POV_ASSERT(dispersionelements >= DISPERSION_ELEMENTS_MIN);
    bool totalReflection = false;

    nray.SetFlags(Ray::RefractionRay, ray);
    const std::uint64_t refractionKey = ray.NextChildKey(kDrawRefraction);
    nray.SetKey(refractionKey);

    // Set up new ray.
    nray.Origin = ipoint;

    // Get ratio of iors depending on the interiors the ray is traversing.

    // Note:
    // For the purpose of refraction, the space occupied by "nested" objects is considered to be "outside" the containing objects,
    // i.e. when encountering (A (B B) A) we pretend that it's (A A|B B|A A).
    // (Here "(X" and "X)" denote the entering and leaving of object X, and "X|Y" denotes an interface between objects X and Y.)
    // In case of overlapping objects, the intersecting region is considered to be part of whatever object is encountered last,
    // i.e. when encountering (A (B A) B) we pretend that it's (A A|B B|B B).

    if(nray.GetInteriors().empty())
    {
        // The ray is entering from the atmosphere.
        nray.AppendInterior(interior);

        ior = sceneData->atmosphereIOR / interior->IOR;
        dispersion = sceneData->atmosphereDispersion / interior->Dispersion;
    }
    else
    {
        // The ray is currently inside an object.
        if(interior == nray.GetInteriors().back()) // The ray is leaving the "innermost" object
        {
            nray.RemoveInterior(interior);
            if(nray.GetInteriors().empty())
            {
                // The ray is leaving into the atmosphere
                ior = interior->IOR / sceneData->atmosphereIOR;
                dispersion = interior->Dispersion / sceneData->atmosphereDispersion;
            }
            else
            {
                // The ray is leaving into another object, i.e. (A (B B) ...
                // For the purpose of refraction, pretend that we weren't inside that other object,
                // i.e. pretend that we didn't encounter (A (B B) ... but (A A|B B|A ...
                ior = interior->IOR / nray.GetInteriors().back()->IOR;
                dispersion = interior->Dispersion / nray.GetInteriors().back()->Dispersion;
                dispersionelements = max(dispersionelements, (unsigned int)(nray.GetInteriors().back()->Disp_NElems));
            }
        }
        else if(nray.RemoveInterior(interior) == true) // The ray is leaving the intersection of overlapping objects, i.e. (A (B A) ...
        {
            // For the purpose of refraction, pretend that we had already left the other member of the intersection when we entered the overlap,
            // i.e. pretend that we didn't encounter (A (B A) ... but (A A|B B|B ...
            ior = 1.0;
            dispersion = 1.0;
        }
        else
        {
            // The ray is entering a new object.
            // For the purpose of refraction, pretend that we're leaving any containing objects,
            // i.e. pretend that we didn't encounter (A (B ... but (A A|B ...
            ior = nray.GetInteriors().back()->IOR / interior->IOR;
            dispersion = nray.GetInteriors().back()->Dispersion / interior->Dispersion;

            nray.AppendInterior(interior);
        }
    }

    bool haveDispersion = (fabs(dispersion - 1.0) >= EPSILON);

    // Do the two mediums traversed have the same indices of refraction?
    if((fabs(ior - 1.0) < EPSILON) && !haveDispersion)
    {
        // Only transmit the ray.
        nray.Direction = ray.Direction;
        if (diff != nullptr)
            SetRayDifferentials(nray, diff->dPdx, diff->dPdy, ray.dDdx, ray.dDdy);
        // Trace a transmitted ray.
        threadData->Stats()[Transmitted_Rays_Traced]++;

        colour.Clear();
        transm = 0.0;
        TraceRay(nray, colour, transm, weight, true);
    }
    else
    {
        // Refract the ray.
        n = dot(ray.Direction, normal);

        if(n <= 0.0)
        {
            localnormal = normal;
            n = -n;
        }
        else
            localnormal = -normal;


        // TODO FIXME: also for first radiosity pass ? (see line 3272 of v3.6 lighting.cpp)
        if(!haveDispersion) // TODO FIXME - radiosity: || (!isFinalTrace)
            totalReflection = TraceRefractionRay(finish, ipoint, ray, nray, ior, n, normal, rawnormal, localnormal, colour, transm, weight, diff);
        else if(ray.IsMonochromaticRay())
            totalReflection = TraceRefractionRay(finish, ipoint, ray, nray, ray.GetSpectralBand().GetDispersionIOR(ior, dispersion), n, normal, rawnormal, localnormal, colour, transm, weight, diff);
        else
        {
            colour.Clear();
            transm = 0.0;

            for(unsigned int i = 0; i < dispersionelements; i++)
            {
                MathColour tempColour;
                ColourChannel tempTransm;

                // NB setting the dispersion factor also causes the MonochromaticRay flag to be set
                SpectralBand spectralBand(i, dispersionelements);
                nray.SetSpectralBand(spectralBand);
                nray.SetKey(DeriveKey(refractionKey, kDrawRefraction, i));

                (void)TraceRefractionRay(finish, ipoint, ray, nray, spectralBand.GetDispersionIOR(ior, dispersion), n, normal, rawnormal, localnormal, tempColour, tempTransm, weight, diff);

                colour += tempColour * spectralBand.GetHue();
                transm += tempTransm;
            }

            colour /= ColourChannel(dispersionelements);
            transm /= ColourChannel(dispersionelements);
        }
    }

    return totalReflection;
}

bool Trace::TraceRefractionRay(const FINISH* finish, const Vector3d& ipoint, Ray& ray, Ray& nray, double ior, double n, const Vector3d& normal, const Vector3d& rawnormal, const Vector3d& localnormal, MathColour& colour, ColourChannel& transm, COLC weight,
                               const SurfaceDifferentials *diff)
{
    // Compute refrated ray direction using Heckbert's method.
    double t = 1.0 + Sqr(ior) * (Sqr(n) - 1.0);

    if(t < 0.0)
    {
        MathColour tempcolour;

        // Total internal reflection occures.
        threadData->Stats()[Internal_Reflected_Rays_Traced]++;
        ComputeReflection(finish, ipoint, ray, normal, rawnormal, tempcolour, weight, diff);
        colour += tempcolour;

        return true;
    }

    const double root = sqrt(t);
    t = ior * n - root;

    nray.Direction = ior * ray.Direction + t * localnormal;

    // Igehy's refraction differential, with n = -D.N and t = ior n - root
    if ((diff != nullptr) && (root > 1.0e-6))
    {
        const DBL s = (dot(localnormal, rawnormal) < 0.0) ? -1.0 : 1.0;
        auto refract = [&](const Vector3d& dD, const Vector3d& dN) -> Vector3d
        {
            const DBL dn = -(dot(dD, localnormal) + s * dot(ray.Direction, dN));
            return ior * dD + ((ior - Sqr(ior) * n / root) * dn) * localnormal + (t * s) * dN;
        };
        SetRayDifferentials(nray, diff->dPdx, diff->dPdy, refract(ray.dDdx, diff->dNdx), refract(ray.dDdy, diff->dNdy));
    }

    // Trace a refracted ray.
    threadData->Stats()[Refracted_Rays_Traced]++;

    colour.Clear();
    transm = 0.0;
    TraceRay(nray, colour, transm, weight, false);

    return false;
}

// see Diffuse in the v3.6 code (lighting.cpp)
void Trace::ComputeDiffuseLight(const FINISH *finish, const Vector3d& ipoint, const Ray& eye, const Vector3d& layer_normal, const MathColour& layer_pigment_colour,
                                MathColour& colour, double attenuation, ObjectPtr object, double relativeIor)
{
    Vector3d reye;

    // crand draws a random number per evaluation, so its lights are tested one by one as before
    if(eye.IsRadiosityRay() && (finish->Crand <= 0.0) && (threadData->lightSources.size() + object->LLights.size() > 1))
    {
        ComputeSampledDiffuseLight(finish, ipoint, eye, layer_normal, layer_pigment_colour, colour, attenuation, object, relativeIor);
        return;
    }

    // TODO FIXME - [CLi] why is this computed here? Not so exciting, is it?
    if(finish->Specular != 0.0)
        reye = -eye.Direction;

    // global light sources, if not turned off for this object
    if((object->Flags & NO_GLOBAL_LIGHTS_FLAG) != NO_GLOBAL_LIGHTS_FLAG)
    {
        for(int i = 0; i < threadData->lightSources.size(); i++)
            ComputeOneDiffuseLight(*threadData->lightSources[i], reye, finish, ipoint, eye, layer_normal, layer_pigment_colour, colour, attenuation, object, relativeIor, i);
    }

    // local light sources from a light group, if any
    if(!object->LLights.empty())
    {
        for(int i = 0; i < object->LLights.size(); i++)
            ComputeOneDiffuseLight(*object->LLights[i], reye, finish, ipoint, eye, layer_normal, layer_pigment_colour, colour, attenuation, object, relativeIor);
    }

    // Lights seen through portals; a light path crosses at most one portal, so images are of real lights only.
    if(!sceneData->portalLights.empty() && !eye.IsRadiosityRay())
    {
        if((object->Flags & NO_GLOBAL_LIGHTS_FLAG) != NO_GLOBAL_LIGHTS_FLAG)
            for(const LightSource *light : threadData->lightSources)
                for(const LightSource *image : light->portalImages)
                    ComputePortalDiffuseLight(*image, reye, finish, ipoint, eye, layer_normal, layer_pigment_colour, colour, attenuation, object, relativeIor);
        for(const LightSource *light : object->LLights)
            for(const LightSource *image : light->portalImages)
                ComputePortalDiffuseLight(*image, reye, finish, ipoint, eye, layer_normal, layer_pigment_colour, colour, attenuation, object, relativeIor);
    }
}

// A uniform number in [0,1) from the bits of a point, a direction and a salt, the same whatever thread draws it.
static double RouletteDraw(const Vector3d& point, const Vector3d& direction, unsigned int salt)
{
    std::uint64_t h = 0x9E3779B97F4A7C15ull * (salt + 1);
    for (int i = 0; i < 3; i++)
    {
        const double values[2] = { point[i], direction[i] };
        std::uint64_t bits[2];
        std::memcpy(bits, values, sizeof(bits));
        for (std::uint64_t b : bits)
        {
            h ^= b + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
            h ^= h >> 31;
            h *= 0xBF58476D1CE4E5B9ull;
            h ^= h >> 29;
        }
    }
    return double(h >> 11) * (1.0 / 9007199254740992.0);
}

bool Trace::SurvivesRadiosityRoulette(const Ray& ray, const Vector3d& point, double weight, unsigned int salt, double& scale)
{
    scale = 1.0;
    const TraceTicket& ticket = ray.GetTicket();
    const double importance = weight * ticket.radiosityShare;
    // what ADC would cut anyway, and what matters enough to trace, are left alone
    if (!ray.IsRadiosityRay() || (weight < ticket.adcBailout) || (importance >= ticket.adcBailout))
        return true;
    const double p = importance / ticket.adcBailout;
    if (RouletteDraw(point, ray.Direction, salt) >= p)
    {
        threadData->Stats()[ADC_Saves]++;
        return false;
    }
    scale = 1.0 / p;
    return true;
}

// Untested lights carrying at most this fraction of the unshadowed light are scaled by the tested ones' visibility, not drawn.
static const double kUntestedLightFraction = 0.05;

// Lights are shadow-tested brightest first until the untested ones carry at most this fraction of the unshadowed light.
static const double kSampledLightFraction = 0.25;

// Salt for the draw of a radiosity ray's sampled light.
static const unsigned int kLightDrawSalt = 97;

void Trace::ComputeSampledDiffuseLight(const FINISH *finish, const Vector3d& ipoint, const Ray& eye, const Vector3d& layer_normal,
                                       const MathColour& layer_pigment_colour, MathColour& colour, double attenuation, ObjectPtr object, double relativeIor)
{
    const Vector3d reye(-eye.Direction);
    const size_t first = lightCandidates.size();
    Ray lightsourceray(eye);
    lightsourceray.hasDifferentials = false;
    double total = 0.0;
    double faintest = HUGE_VAL;

    auto consider = [&](const LightSource& light, int index)
    {
        if (!qualityFlags.shadows || (light.Light_Type == FILL_LIGHT_SOURCE) || (light.Projected_Through_Object != nullptr))
        {
            ComputeOneDiffuseLight(light, reye, finish, ipoint, eye, layer_normal, layer_pigment_colour, colour, attenuation, object, relativeIor, index);
            return;
        }
        LightCandidate candidate;
        candidate.light = &light;
        candidate.index = index;
        if (!ComputeOneLightReach(light, finish, ipoint, layer_normal, object, candidate.depth, lightsourceray, candidate.colour, candidate.backside))
            return;
        candidate.direction = lightsourceray.Direction;
        ComputeOneLightContribution(light, reye, finish, ipoint, eye, layer_normal, layer_pigment_colour, candidate.potential, attenuation, object, relativeIor,
                                    candidate.depth, lightsourceray, candidate.colour, candidate.backside);
        candidate.weight = candidate.potential.Weight();
        if (candidate.weight > 0.0)
        {
            lightCandidates.push_back(candidate);
            total += candidate.weight;
            faintest = std::min(faintest, candidate.weight);
        }
    };

    if((object->Flags & NO_GLOBAL_LIGHTS_FLAG) != NO_GLOBAL_LIGHTS_FLAG)
    {
        for(int i = 0; i < threadData->lightSources.size(); i++)
            consider(*threadData->lightSources[i], i);
    }
    for(int i = 0; i < object->LLights.size(); i++)
        consider(*object->LLights[i], -1);

    const size_t end = lightCandidates.size();
    const size_t order = lightOrder.size();
    for (size_t i = first; i < end; ++i)
        lightOrder.push_back(i);
    // when even the faintest light is too bright to leave untested, the order does not matter
    if (faintest <= kSampledLightFraction * total)
        std::sort(lightOrder.begin() + order, lightOrder.end(),
                  [this](size_t a, size_t b) { return lightCandidates[a].weight > lightCandidates[b].weight; });

    auto test = [&](const LightCandidate& candidate)
    {
        MathColour lightcolour = candidate.colour;
        lightsourceray.Origin = ipoint;
        lightsourceray.Direction = candidate.direction;
        TestOneLightShadow(*candidate.light, candidate.depth, lightsourceray, ipoint, lightcolour, candidate.index);
        MathColour lit;
        if ((lightcolour - candidate.colour).IsZero())
            lit = candidate.potential;
        else if (!lightcolour.IsNearZero(EPSILON))
            ComputeOneLightContribution(*candidate.light, reye, finish, ipoint, eye, layer_normal, layer_pigment_colour, lit, attenuation, object, relativeIor,
                                        candidate.depth, lightsourceray, lightcolour, candidate.backside);
        return lit;
    };

    double untested = total;
    double testedWeight = 0.0;
    double litWeight = 0.0;
    size_t next = order;
    for (; (next < lightOrder.size()) && (untested > kSampledLightFraction * total); ++next)
    {
        // copied, as a shadow ray's own lighting may grow the stacks
        const LightCandidate candidate = lightCandidates[lightOrder[next]];
        const MathColour lit = test(candidate);
        colour += lit;
        testedWeight += candidate.weight;
        litWeight += lit.Weight();
        untested -= candidate.weight;
    }

    if ((next < lightOrder.size()) && (untested > kUntestedLightFraction * total))
    {
        // hashed from the hit and the ray, so the draw does not depend on thread order
        double draw = RouletteDraw(ipoint, eye.Direction, kLightDrawSalt) * untested;
        size_t pick = next;
        for (; (pick + 1 < lightOrder.size()) && (draw >= lightCandidates[lightOrder[pick]].weight); ++pick)
            draw -= lightCandidates[lightOrder[pick]].weight;
        const LightCandidate candidate = lightCandidates[lightOrder[pick]];
        colour += test(candidate) * (untested / candidate.weight);
    }
    else if (next < lightOrder.size())
    {
        const double visible = litWeight / testedWeight;
        for (; next < lightOrder.size(); ++next)
            colour += lightCandidates[lightOrder[next]].potential * visible;
    }

    lightOrder.resize(order);
    lightCandidates.resize(first);
}

void Trace::ComputePhotonDiffuseLight(const FINISH *Finish, const Vector3d& IPoint, const Ray& Eye, const Vector3d& Layer_Normal, const Vector3d& Raw_Normal,
                                      const MathColour& Layer_Pigment_Colour, MathColour& colour, double Attenuation, ConstObjectPtr Object, double relativeIor, PhotonGatherer& gatherer)
{
    double Cos_Shadow_Angle;
    Vector3d lightDirection;
    MathColour Light_Colour;
    MathColour tmpCol, tmpCol2;
    double r;
    int n;
    int j;
    double thisDensity=0;
    double prevDensity=0.0000000000000001; // avoid div-by-zero error
    int expanded = false;
    double att;  // attenuation for lambertian compensation & filters

    if (!sceneData->photonSettings.photonsEnabled || sceneData->surfacePhotonMap.numPhotons<1)
        return;

    if ((Finish->Diffuse == 0.0) && (Finish->DiffuseBack == 0.0) && (Finish->Specular == 0.0) && (Finish->Phong == 0.0))
        return;

    // statistics
    threadData->Stats()[Gather_Performed_Count]++;

    if(gatherer.gathered)
        r = gatherer.alreadyGatheredRadius;
    else
        r = gatherer.gatherPhotonsAdaptive(&IPoint, &Layer_Normal, true);

    n = gatherer.gatheredPhotons.numFound;
    const std::uint64_t photonKey = DeriveKey(Eye.GetKey(), kDrawPhoton, 0);

    tmpCol.Clear();

    // now go through these photons and add up their contribution
    for(j=0; j<n; j++)
    {
        // double theta,phi;
        int theta,phi;
        bool backside = false;

        // convert small color to normal color
        Light_Colour = ToMathColour(RGBColour(gatherer.gatheredPhotons.photonGatherList[j]->colour));

        // convert theta/phi to vector direction
        // Use a pre-computed array of sin/cos to avoid many calls to the
        // sin() and cos() functions.  These arrays were initialized in
        // InitBacktraceEverything.
        theta = gatherer.gatheredPhotons.photonGatherList[j]->theta+127;
        phi = gatherer.gatheredPhotons.photonGatherList[j]->phi+127;

        lightDirection[Y] = sinCosData.sinTheta[theta];
        lightDirection[X] = sinCosData.cosTheta[theta];

        lightDirection[Z] = lightDirection[X]*sinCosData.sinTheta[phi];
        lightDirection[X] = lightDirection[X]*sinCosData.cosTheta[phi];

        // this compensates for real lambertian (diffuse) lighting (see paper)
        // use raw normal, not layer normal
        // att = dot(Layer_Normal, Light_Source_Ray.Direction);
        att = dot(Raw_Normal, lightDirection);
        if (att>1) att=1.0;
        if (att<.1) att = 0.1; // limit to 10x - otherwise we get bright dots
        att = 1.0 / fabs(att);

        // do gaussian filter
        //att *= 0.918*(1.0-(1.0-exp((-1.953) * gatherer.photonDistances[j])) / (1.0-exp(-1.953)) );
        // do cone filter
        //att *= 1.0-(sqrt(gatherer.photonDistances[j])/(4.0 * r)) / (1.0-2.0/(3.0*4.0));

        Light_Colour *= att;

        // See if light on far side of surface from camera.
        if (!(Test_Flag(Object, DOUBLE_ILLUMINATE_FLAG)))
        {
            Cos_Shadow_Angle = dot(Layer_Normal, lightDirection);
            if (Cos_Shadow_Angle < EPSILON)
            {
                if (Finish->DiffuseBack != 0.0)
                    backside = true;
                else
                    continue;
            }
        }

        // now add diffuse, phong, specular, irid contribution

        tmpCol2.Clear();

        if (!(sceneData->useSubsurface && Finish->UseSubsurface))
            // (Diffuse contribution is not supported in combination with BSSRDF, to emphasize the fact that the BSSRDF
            // model is intended to provide for all the diffuse term by default. If users want to add some additional
            // surface-only diffuse term, they should use layered textures.
            ComputeDiffuseColour(Finish, lightDirection, Eye.Direction, Layer_Normal, tmpCol2, Light_Colour, Layer_Pigment_Colour, relativeIor, Attenuation, backside,
                                 photonKey, j);

        // NK rad - don't compute highlights for radiosity gather rays, since this causes
        // problems with colors being far too bright
        // don't compute highlights for diffuse backside illumination
        if(!Eye.IsRadiosityRay() && !backside) // TODO FIXME radiosity - is this really the right way to do it (speaking of realism)?
        {
            if (Finish->Phong > 0.0)
            {
                ComputePhongColour(Finish, lightDirection, Eye.Direction, Layer_Normal, tmpCol2, Light_Colour, Layer_Pigment_Colour, relativeIor);
            }
            if (Finish->Specular > 0.0)
            {
                ComputeSpecularColour(Finish, lightDirection, -Eye.Direction, Layer_Normal, tmpCol2, Light_Colour, Layer_Pigment_Colour, relativeIor);
            }
        }

        if (Finish->Irid > 0.0)
        {
            ComputeIridColour(Finish, lightDirection, Eye.Direction, Layer_Normal, IPoint, tmpCol2);
        }

        tmpCol += tmpCol2;
    }

    // finish the photons equation
    tmpCol /= M_PI*r*r;

    // add photon contribution to total lighting
    colour += tmpCol;
}

// see Diffuse_One_Light in the v3.6 code (lighting.cpp)
void Trace::ComputeOneDiffuseLight(const LightSource &lightsource, const Vector3d& reye, const FINISH *finish, const Vector3d& ipoint, const Ray& eye, const Vector3d& layer_normal,
                                   const MathColour& layer_pigment_colour, MathColour& colour, double attenuation, ConstObjectPtr object, double relativeIor, int light_index)
{
    double lightsourcedepth;
    Ray lightsourceray(eye);
    lightsourceray.hasDifferentials = false;
    MathColour lightcolour;
    bool backside;

    if (!ComputeOneLightReach(lightsource, finish, ipoint, layer_normal, object, lightsourcedepth, lightsourceray, lightcolour, backside))
        return;

    TestOneLightShadow(lightsource, lightsourcedepth, lightsourceray, ipoint, lightcolour, light_index);

    ComputeOneLightContribution(lightsource, reye, finish, ipoint, eye, layer_normal, layer_pigment_colour, colour, attenuation, object, relativeIor,
                                lightsourcedepth, lightsourceray, lightcolour, backside);
}

bool Trace::ComputeOneLightReach(const LightSource &lightsource, const FINISH *finish, const Vector3d& ipoint, const Vector3d& layer_normal, ConstObjectPtr object,
                                 double& lightsourcedepth, Ray& lightsourceray, MathColour& lightcolour, bool& backside)
{
    double cos_shadow_angle;
    backside = false;

    // Get a colour and a ray.
    ComputeOneLightRay(lightsource, lightsourcedepth, lightsourceray, ipoint, lightcolour);

    // Don't calculate spotlights when outside of the light's cone.
    if(lightcolour.IsNearZero(EPSILON))
        return false;

    // See if light on far side of surface from camera.
    if(!(Test_Flag(object, DOUBLE_ILLUMINATE_FLAG)) // NK 1998 double_illuminate - changed to Test_Flag
       && !lightsource.Use_Full_Area_Lighting) // JN2007: Easiest way of getting rid of sharp shadow lines
    {
        cos_shadow_angle = dot(layer_normal, lightsourceray.Direction);
        if(cos_shadow_angle < EPSILON)
        {
            if (finish->DiffuseBack != 0.0)
                backside = true;
            else
                return false;
        }
    }

    return true;
}

namespace
{
bool SegmentMeetsBox(const Vector3d& origin, const Vector3d& direction, double length, const BoundingBox& box, double grow)
{
    double from = 0.0, to = length;
    for (int i = 0; i < 3; i++)
    {
        const double low = box.lowerLeft[i] - grow, high = box.lowerLeft[i] + box.size[i] + grow;
        if (fabs(direction[i]) < EPSILON)
        {
            if ((origin[i] < low) || (origin[i] > high))
                return false;
            continue;
        }
        double a = (low - origin[i]) / direction[i], b = (high - origin[i]) / direction[i];
        if (a > b)
            std::swap(a, b);
        from = max(from, a);
        to = min(to, b);
        if (from > to)
            return false;
    }
    return true;
}
}

void Trace::ComputePortalDiffuseLight(const LightSource& image, const Vector3d& reye, const FINISH *finish, const Vector3d& ipoint, const Ray& eye,
                                      const Vector3d& layer_normal, const MathColour& layer_pigment_colour, MathColour& colour, double attenuation,
                                      ConstObjectPtr object, double relativeIor)
{
    double depth;
    Ray lightsourceray(eye);
    ComputeOneWhiteLightRay(image, depth, lightsourceray, ipoint);

    // Sample rays fan out to the light's extent; those of tilted parallel or cylinder area lights are not bounded so.
    double spread = 0.0;
    if (image.Area_Light)
        spread = (image.Parallel || (image.Light_Type == CYLINDER_SOURCE)) ? BOUND_HUGE : 2.0 * max(image.Axis1.length(), image.Axis2.length());
    if ((depth <= 0.0) || !SegmentMeetsBox(ipoint, lightsourceray.Direction, depth, image.portal->BBox, spread))
        return;

    bool backside = false;
    if (!Test_Flag(object, DOUBLE_ILLUMINATE_FLAG) && !image.Use_Full_Area_Lighting && (dot(layer_normal, lightsourceray.Direction) < EPSILON))
    {
        if (finish->DiffuseBack == 0.0)
            return;
        backside = true;
    }

    // As for a light seen straight, an area light's points share the fade and cone of its centre, or of each lightlet.
    MathColour lightcolour = image.colour;
    if (image.Area_Light && qualityFlags.areaLights && !image.Use_Full_Area_Lighting)
    {
        Intersection crossing;
        if (FindPortalCrossing(*image.portal, lightsourceray, depth, crossing))
        {
            Vector3d target;
            MTransPoint(target, crossing.IPoint, &image.portal->map);
            Ray beyond(lightsourceray);
            double farDepth;
            ComputeOneWhiteLightRay(*image.imageOf, farDepth, beyond, target);
            lightcolour *= Attenuate_Light(image.imageOf, beyond, crossing.Depth + farDepth);
        }
        else
            lightcolour *= Attenuate_Light(&image, lightsourceray, depth);
    }
    TraceShadowRay(image, depth, lightsourceray, ipoint, lightcolour);
    ComputeOneLightContribution(image, reye, finish, ipoint, eye, layer_normal, layer_pigment_colour, colour, attenuation, object, relativeIor,
                                depth, lightsourceray, lightcolour, backside);
}

bool Trace::FindPortalCrossing(const Portal& portal, const Ray& ray, double reach, Intersection& crossing)
{
    if (!SegmentMeetsBox(ray.Origin, ray.Direction, reach, portal.BBox, 0.0))
        return false;
    IStack stack(threadData->stackPool);
    if (!const_cast<Portal&>(portal).All_Intersections(ray, stack, threadData))
        return false;

    bool found = false;
    crossing.Depth = reach;
    for (; stack->size() > 0; stack->pop())
    {
        Intersection& hit = stack->top();
        if ((hit.Depth <= SMALL_TOLERANCE) || (hit.Depth >= crossing.Depth))
            continue;
        Vector3d normal;
        hit.Object->Normal(normal, &hit, threadData);
        if (Test_Flag(hit.Object, INVERTED_FLAG))
            normal.invert();
        if (portal.Admits(normal, ray.Direction))
        {
            crossing = hit;
            found = true;
        }
    }
    return found;
}

void Trace::TracePortalLightShadowRay(const LightSource &image, double& lightsourcedepth, Ray& lightsourceray, MathColour& lightcolour,
                                      const Vector3d& offset)
{
    const Portal& portal = *image.portal;
    const LightSource& light = *image.imageOf;
    const double reach = lightsourcedepth;
    lightsourcedepth = 0.0;

    Intersection crossing;
    if (!FindPortalCrossing(portal, lightsourceray, reach, crossing) ||
        (portal.exit && (reach - crossing.Depth > portal.Chord(lightsourceray, crossing.IPoint, threadData))))
    {
        lightcolour.Clear();
        return;
    }

    double open = 1.0;
    TransColour opening;
    bool found;
    {
        ActiveTraceScope noTracer(threadData, nullptr, 1.0);
        found = (portal.pigment != nullptr) && Compute_Pigment(opening, portal.pigment, crossing.IPoint, &crossing, &lightsourceray, threadData);
    }
    if (found)
    {
        open = std::max(0.0, std::min(1.0, double(opening.Opacity())));
        lightcolour *= opening.colour();
    }

    Vector3d target, lightOffset;
    MTransPoint(target, crossing.IPoint, &portal.map);
    MTransDirection(lightOffset, offset, &portal.map);
    Ray beyond(lightsourceray);
    double farDepth;
    ComputeOneWhiteLightRay(light, farDepth, beyond, target, lightOffset);
    if ((open <= 0.0) || (farDepth <= 0.0))
    {
        lightcolour.Clear();
        return;
    }
    lightcolour *= open;
    if (!image.Area_Light || !qualityFlags.areaLights)
        lightcolour *= Attenuate_Light(&light, beyond, crossing.Depth + farDepth);
    if (lightcolour.IsNearZero(EPSILON) || !qualityFlags.shadows || (light.Light_Type == FILL_LIGHT_SOURCE))
        return;

    DivertPortalLight(lightsourceray, crossing.Depth, lightcolour, &portal);
    DivertPortalLight(beyond, farDepth, lightcolour, &portal);
    if (lightcolour.IsNearZero(EPSILON))
        return;
    double hereDepth = crossing.Depth;
    Ray here(lightsourceray);
    TracePointLightShadowRay(image, hereDepth, here, lightcolour);
    if (lightcolour.IsNearZero(EPSILON))
        return;
    AttenuatePortalLightPiece(light, here, hereDepth, lightcolour);

    RayInteriorVector containing;
    FindContainingInteriors(target, containing);
    beyond.ResetInteriors();
    beyond.AppendInteriors(containing);
    TracePointLightShadowRay(image, farDepth, beyond, lightcolour);
    AttenuatePortalLightPiece(light, beyond, farDepth, lightcolour);
}

void Trace::AttenuatePortalLightPiece(const LightSource& light, Ray& piece, double depth, MathColour& lightcolour)
{
    if ((depth <= SHADOW_TOLERANCE) || !light.Media_Interaction || !light.Media_Attenuation || lightcolour.IsNearZero(EPSILON))
        return;
    Intersection end;
    end.Depth = depth;
    end.Object = nullptr;
    ComputeShadowMedia(piece, end, lightcolour, true);
}

void Trace::TraceSampleShadowRay(const LightSource &lightsource, double& lightsourcedepth, Ray& lightsourceray, MathColour& lightcolour,
                                 const Vector3d& offset)
{
    if (sceneData->portalMouths.empty())
        TracePointLightShadowRay(lightsource, lightsourcedepth, lightsourceray, lightcolour);
    else if (lightsource.portal != nullptr)
        TracePortalLightShadowRay(lightsource, lightsourcedepth, lightsourceray, lightcolour, offset);
    else
    {
        DivertPortalLight(lightsourceray, lightsourcedepth, lightcolour, nullptr);
        if (!lightcolour.IsNearZero(EPSILON))
            TracePointLightShadowRay(lightsource, lightsourcedepth, lightsourceray, lightcolour);
    }
}

void Trace::DivertPortalLight(const Ray& ray, double reach, MathColour& lightcolour, const Portal *crossed)
{
    for (const Portal *mouth : sceneData->portalMouths)
    {
        if ((crossed != nullptr) && ((mouth == crossed) || (mouth == crossed->partner)))
            continue;
        if (!mouth->Diverts(true) && !mouth->Diverts(false))
            continue;
        if (!SegmentMeetsBox(ray.Origin, ray.Direction, reach, mouth->BBox, 0.0))
            continue;
        IStack stack(threadData->stackPool);
        if (!const_cast<Portal *>(mouth)->All_Intersections(ray, stack, threadData))
            continue;
        for (; stack->size() > 0; stack->pop())
        {
            Intersection& hit = stack->top();
            if ((hit.Depth <= SHADOW_TOLERANCE) || (hit.Depth >= reach - SHADOW_TOLERANCE))
                continue;
            Vector3d normal;
            hit.Object->Normal(normal, &hit, threadData);
            if (Test_Flag(hit.Object, INVERTED_FLAG))
                normal.invert();
            // A shadow ray runs from the lit point to the light, so the light enters by the side the ray leaves.
            if (!mouth->Diverts(mouth->EntersFront(normal, -ray.Direction)))
                continue;
            double open = 1.0;
            TransColour opening;
            bool found;
            {
                ActiveTraceScope noTracer(threadData, nullptr, 1.0);
                found = (mouth->pigment != nullptr) && Compute_Pigment(opening, mouth->pigment, hit.IPoint, &hit, &ray, threadData);
            }
            if (found)
                open = std::max(0.0, std::min(1.0, double(opening.Opacity())));
            lightcolour *= 1.0 - open;
        }
        if (lightcolour.IsNearZero(EPSILON))
        {
            lightcolour.Clear();
            return;
        }
    }
}

void Trace::TestOneLightShadow(const LightSource &lightsource, double lightsourcedepth, Ray& lightsourceray, const Vector3d& ipoint, MathColour& lightcolour, int light_index)
{
    // If light source was not blocked by any intervening object, then
    // calculate it's contribution to the object's overall illumination.
    if (qualityFlags.shadows && ((lightsource.Projected_Through_Object != nullptr) || (lightsource.Light_Type != FILL_LIGHT_SOURCE)))
    {
        if (lightColorCacheIndex != -1 && light_index != -1)
        {
            if (lightColorCache[lightColorCacheIndex][light_index].tested == false)
            {
                // note that lightColorCache may be re-sized during trace, so we don't store a reference to it across the call
                TraceShadowRay(lightsource, lightsourcedepth, lightsourceray, ipoint, lightcolour);
                lightColorCache[lightColorCacheIndex][light_index].tested = true;
                lightColorCache[lightColorCacheIndex][light_index].colour = lightcolour;
            }
            else
                lightcolour = lightColorCache[lightColorCacheIndex][light_index].colour;
        }
        else
            TraceShadowRay(lightsource, lightsourcedepth, lightsourceray, ipoint, lightcolour);
    }
}

void Trace::ComputeOneLightContribution(const LightSource &lightsource, const Vector3d& reye, const FINISH *finish, const Vector3d& ipoint, const Ray& eye,
                                        const Vector3d& layer_normal, const MathColour& layer_pigment_colour, MathColour& colour, double attenuation,
                                        ConstObjectPtr object, double relativeIor, double lightsourcedepth, Ray& lightsourceray,
                                        const MathColour& lightcolour, bool backside)
{
    MathColour tmpCol;

    if(!lightcolour.IsNearZero(EPSILON))
    {
        if(lightsource.Area_Light && lightsource.Use_Full_Area_Lighting &&
            qualityFlags.areaLights) // JN2007: Full area lighting
        {
            ComputeFullAreaDiffuseLight(lightsource, reye, finish, ipoint, eye,
                layer_normal, layer_pigment_colour, colour, attenuation,
                lightsourcedepth, lightsourceray, lightcolour,
                object, relativeIor);
            return;
        }

        if(!(sceneData->useSubsurface && finish->UseSubsurface))
            // (Diffuse contribution is not supported in combination with BSSRDF, to emphasize the fact that the BSSRDF
            // model is intended to provide for all the diffuse term by default. If users want to add some additional
            // surface-only diffuse term, they should use layered textures.
            ComputeDiffuseColour(finish, lightsourceray.Direction, eye.Direction, layer_normal, tmpCol, lightcolour, layer_pigment_colour, relativeIor, attenuation, backside,
                                 eye.GetKey(), LightSlot(lightsource) << 32);

        MathColour tempLightColour = (finish->AlphaKnockout ? lightcolour * attenuation : lightcolour);

        // NK rad - don't compute highlights for radiosity gather rays, since this causes
        // problems with colors being far too bright
        // don't compute highlights for diffuse backside illumination
        if((lightsource.Light_Type != FILL_LIGHT_SOURCE) && !eye.IsRadiosityRay() && !backside) // TODO FIXME radiosity - is this really the right way to do it (speaking of realism)?
        {
            if(finish->Phong > 0.0)
            {
                ComputePhongColour (finish, lightsourceray.Direction, eye.Direction, layer_normal, tmpCol,
                                    tempLightColour, layer_pigment_colour, relativeIor);
            }

            if(finish->Specular > 0.0)
                ComputeSpecularColour (finish, lightsourceray.Direction, -eye.Direction, layer_normal, tmpCol,
                                       tempLightColour, layer_pigment_colour, relativeIor);
        }

        if(finish->Irid > 0.0)
            ComputeIridColour(finish, lightsourceray.Direction, eye.Direction, layer_normal, ipoint, tmpCol);
    }

    colour += tmpCol;
}

// JN2007: Full area lighting:
void Trace::ComputeFullAreaDiffuseLight(const LightSource &lightsource, const Vector3d& reye, const FINISH *finish, const Vector3d& ipoint, const Ray& eye,
                                        const Vector3d& layer_normal, const MathColour& layer_pigment_colour, MathColour& colour, double attenuation,
                                        double lightsourcedepth, Ray& lightsourceray, const MathColour& lightcolour, ConstObjectPtr object, double relativeIor)
{
    Vector3d temp;
    Vector3d axis1Temp, axis2Temp;
    double axis1_Length, cos_shadow_angle;

    axis1Temp = lightsource.Axis1;
    axis2Temp = lightsource.Axis2;

    if(lightsource.Orient == true)
    {
        // Orient the area light to face the intersection point [ENB 9/97]

        // Do Light source to get the correct lightsourceray
        ComputeOneWhiteLightRay(lightsource, lightsourcedepth, lightsourceray, ipoint);

        // Save the lengths of the axises
        axis1_Length = axis1Temp.length();

        // Make axis 1 be perpendicular with the light-ray
        if(fabs(fabs(lightsourceray.Direction[Z]) - 1.0) < 0.01)
            // too close to vertical for comfort, so use cross product with horizon
            temp = Vector3d(0.0, 1.0, 0.0);
        else
            temp = Vector3d(0.0, 0.0, 0.1);

        axis1Temp = cross(lightsourceray.Direction, temp).normalized();

        // Make axis 2 be perpendicular with the light-ray and with Axis1.  A simple cross-product will do the trick.
        axis2Temp = cross(lightsourceray.Direction, axis1Temp).normalized();

        // make it square
        axis1Temp *= axis1_Length;
        axis2Temp *= axis1_Length;
    }

    MathColour sampleLightcolour = lightcolour / (lightsource.Area_Size1 * lightsource.Area_Size2);
    MathColour attenuatedLightcolour;
    const std::uint64_t key = DeriveKey(eye.GetKey(), kDrawFullAreaLight, LightSlot(lightsource));
    if(lightsource.Jitter)
        MarkGrain();

    for(int v = 0; v < lightsource.Area_Size2; ++v)
    {
        for(int u = 0; u < lightsource.Area_Size1; ++u)
        {
            Vector3d jitterAxis1, jitterAxis2;
            Ray lsr(lightsourceray);
            double jitter_u = (double)u;
            double jitter_v = (double)v;
            bool backside = false;
            MathColour tmpCol;

            const std::uint64_t cell = std::uint64_t(v) * lightsource.Area_Size1 + u;
            if(lightsource.Jitter)
            {
                jitter_u += Draw(key, kDrawFullAreaLight, 2 * cell) - 0.5;
                jitter_v += Draw(key, kDrawFullAreaLight, 2 * cell + 1) - 0.5;
            }

            // Create circular are lights [ENB 9/97]
            // First, make jitter_u and jitter_v be numbers from -1 to 1
            // Second, set scaleFactor to the abs max (jitter_u,jitter_v) (for shells)
            // Third, divide scaleFactor by the length of <jitter_u,jitter_v>
            // Fourth, scale jitter_u & jitter_v by scaleFactor
            // Finally scale Axis1 by jitter_u & Axis2 by jitter_v
            if(lightsource.Circular == true)
            {
                jitter_u = jitter_u / (lightsource.Area_Size1 - 1) - 0.5 + 0.001;
                jitter_v = jitter_v / (lightsource.Area_Size2 - 1) - 0.5 + 0.001;
                double scaleFactor = ((fabs(jitter_u) > fabs(jitter_v)) ? fabs(jitter_u) : fabs(jitter_v));
                scaleFactor /= sqrt(jitter_u * jitter_u + jitter_v * jitter_v);
                jitter_u *= scaleFactor;
                jitter_v *= scaleFactor;
                jitterAxis1 = axis1Temp * jitter_u;
                jitterAxis2 = axis2Temp * jitter_v;
            }
            else
            {
                if(lightsource.Area_Size1 > 1)
                {
                    double scaleFactor = jitter_u / (double)(lightsource.Area_Size1 - 1) - 0.5;
                    jitterAxis1 = axis1Temp * scaleFactor;
                }
                else
                    jitterAxis1 = Vector3d(0.0, 0.0, 0.0);

                if(lightsource.Area_Size2 > 1)
                {
                    double scaleFactor = jitter_v / (double)(lightsource.Area_Size2 - 1) - 0.5;
                    jitterAxis2 = axis2Temp * scaleFactor;
                }
                else
                    jitterAxis2 = Vector3d(0.0, 0.0, 0.0);
            }

            // Recalculate the light source ray but not the colour
            ComputeOneWhiteLightRay(lightsource, lightsourcedepth, lsr, ipoint, jitterAxis1 + jitterAxis2);
            // Calculate distance- and angle-based light attenuation
            attenuatedLightcolour = sampleLightcolour * Attenuate_Light(&lightsource, lsr, lightsourcedepth);

            // If not double-illuminated, check if the normal is pointing away:
            if(!Test_Flag(object, DOUBLE_ILLUMINATE_FLAG))
            {
                cos_shadow_angle = dot(layer_normal, lsr.Direction);
                if(cos_shadow_angle < EPSILON)
                {
                    if (finish->DiffuseBack != 0.0)
                        backside = true;
                    else
                        continue;
                }
            }

            if(!(sceneData->useSubsurface && finish->UseSubsurface))
                // (Diffuse contribution is not supported in combination with BSSRDF, to emphasize the fact that the BSSRDF
                // model is intended to provide for all the diffuse term by default. If users want to add some additional
                // surface-only diffuse term, they should use layered textures.
                ComputeDiffuseColour(finish, lsr.Direction, eye.Direction, layer_normal, tmpCol, attenuatedLightcolour, layer_pigment_colour, relativeIor, attenuation, backside,
                                     eye.GetKey(), (LightSlot(lightsource) << 32) + 1 + cell);

            // NK rad - don't compute highlights for radiosity gather rays, since this causes
            // problems with colors being far too bright
            // don't compute highlights for diffuse backside illumination
            if((lightsource.Light_Type != FILL_LIGHT_SOURCE) && !eye.IsRadiosityRay() && !backside) // TODO FIXME radiosity - is this really the right way to do it (speaking of realism)?
            {
                if(finish->Phong > 0.0)
                {
                    ComputePhongColour(finish, lsr.Direction, eye.Direction, layer_normal, tmpCol, attenuatedLightcolour, layer_pigment_colour, relativeIor);
                }

                if(finish->Specular > 0.0)
                    ComputeSpecularColour(finish, lsr.Direction, -eye.Direction, layer_normal, tmpCol, attenuatedLightcolour, layer_pigment_colour, relativeIor);
            }

            if(finish->Irid > 0.0)
                ComputeIridColour(finish, lsr.Direction, eye.Direction, layer_normal, ipoint, tmpCol);

            colour += tmpCol;
        }
    }
}

// see do_light in v3.6's lighting.cpp
void Trace::ComputeOneLightRay(const LightSource &lightsource, double& lightsourcedepth, Ray& lightsourceray, const Vector3d& ipoint, MathColour& lightcolour, bool forceAttenuate)
{
    double attenuation;
    ComputeOneWhiteLightRay(lightsource, lightsourcedepth, lightsourceray, ipoint);

    // Get the light source colour.
    lightcolour = lightsource.colour;

    // Attenuate light source color.
    if (lightsource.Area_Light && lightsource.Use_Full_Area_Lighting && qualityFlags.areaLights && !forceAttenuate)
        // for full area lighting we apply distance- and angle-based attenuation to each "lightlet" individually later
        attenuation = 1.0;
    else
        attenuation = Attenuate_Light(&lightsource, lightsourceray, lightsourcedepth);

    // Now scale the color by the attenuation
    lightcolour *= attenuation;
}

// see block_light_source in the v3.6 source
void Trace::TraceShadowRay(const LightSource &lightsource, double depth, Ray& lightsourceray, const Vector3d& point, MathColour& colour, const Vector2d* areaSample)
{
    // test and set highest level traced. We do it differently than TraceRay() does,
    // for compatibility with the way max_trace_level is tested and reported in v3.6
    // and earlier.
    if(lightsourceray.GetTicket().traceLevel > lightsourceray.GetTicket().maxAllowedTraceLevel)
    {
        colour.Clear();
        return;
    }

    lightsourceray.GetTicket().maxFoundTraceLevel = (unsigned int) max(lightsourceray.GetTicket().maxFoundTraceLevel, lightsourceray.GetTicket().traceLevel);
    lightsourceray.GetTicket().traceLevel++;

    double newdepth;
    Intersection isect;
    Ray newray(lightsourceray);

    // Store current depth and ray because they will be modified.
    newdepth = depth;

    // NOTE: shadow rays are never photon rays, so flag can be hard-coded to false
    newray.SetFlags(Ray::OtherRay, true, false);
    newray.Derive(lightsourceray, kDrawShadow, LightSlot(lightsource));
    newray.ResetMediaErrorBudget();

    // Get shadows from current light source.
    if(lightsource.Area_Light && qualityFlags.areaLights && (areaSample != nullptr))
        TraceAreaLightSampleShadowRay(lightsource, newdepth, newray, point, colour, *areaSample);
    else if(lightsource.Area_Light && qualityFlags.areaLights)
        TraceAreaLightShadowRay(lightsource, newdepth, newray, point, colour);
    else
        TraceSampleShadowRay(lightsource, newdepth, newray, colour, Vector3d(0.0));

    // If there's some distance left for the ray to reach the light source
    // we have to apply atmospheric stuff to this part of the ray.

    if((newdepth > SHADOW_TOLERANCE) && (lightsource.Media_Interaction) && (lightsource.Media_Attenuation))
    {
        isect.Depth = newdepth;
        isect.Object = nullptr;
        ComputeShadowMedia(newray, isect, colour, (lightsource.Media_Interaction) && (lightsource.Media_Attenuation));
    }

    lightsourceray.GetTicket().traceLevel--;
    maxFoundTraceLevel = (unsigned int) max(maxFoundTraceLevel, lightsourceray.GetTicket().maxFoundTraceLevel);
}

// moved this here (was originally inside TracePointLightShadowRay) because
// for some reason the Intel compiler (version W_CC_PC_8.1.027) will fail
// to link the exe, complaining of an unresolved external.
//
// TODO: try moving it back in at some point in the future.
struct NoShadowFlagRayObjectCondition final : public RayObjectCondition
{
    ConstObjectPtr missed = nullptr; // already found not to cross the ray within its reach
    virtual bool operator()(const Ray&, ConstObjectPtr object, double) const override { return (object != missed) && !Test_Flag(object, NO_SHADOW_FLAG); }
};

struct SmallToleranceRayObjectCondition final  : public RayObjectCondition
{
    virtual bool operator()(const Ray&, ConstObjectPtr, double dist) const override { return dist > SMALL_TOLERANCE; }
};

// Any opaque hit in the shadow window blacks the light out, whether or not it is the nearest.
struct OpaqueShadowStopCondition final : public IntersectionStopCondition
{
    double farthest;

    explicit OpaqueShadowStopCondition(double limit) : farthest(limit) {}

    virtual bool operator()(const Intersection& isect) const override
    {
        if (isect.Depth >= farthest)
            return false;
        ConstObjectPtr testObject = (isect.Csg != nullptr ? isect.Csg : isect.Object);
        return Test_Flag(isect.Object, OPAQUE_FLAG) && Test_Flag(testObject, OPAQUE_FLAG);
    }
};

void Trace::TracePointLightShadowRay(const LightSource &lightsource, double& lightsourcedepth, Ray& lightsourceray, MathColour& lightcolour)
{
    lightsourceray.ResetMediaErrorBudget();
    Intersection boundedIntersection;
    ObjectPtr cacheObject = nullptr;
    bool foundTransparentObjects = false;
    bool foundIntersection;

    // Projected through main tests
    double projectedDepth = 0.0;

    if (lightsource.Projected_Through_Object != nullptr)
    {
        Intersection tempIntersection;

        if(FindIntersection(lightsource.Projected_Through_Object, tempIntersection, lightsourceray))
        {
            if((tempIntersection.Depth - lightsourcedepth) < 0.0)
                projectedDepth = lightsourcedepth - fabs(tempIntersection.Depth) + SMALL_TOLERANCE;
            else
            {
                lightcolour.Clear();
                return;
            }
        }
        else
        {
            lightcolour.Clear();
            return;
        }

        // Make sure we don't do shadows for fill light sources.
        // (Note that if Projected_Through_Object is `nullptr`, the test for FILL_LIGHT_SOURCE has happened earlier already)
        if(lightsource.Light_Type == FILL_LIGHT_SOURCE)
            return;
    }

    NoShadowFlagRayObjectCondition precond;
    SmallToleranceRayObjectCondition postcond;

    // check for object in the light source shadow cache (object that fully shadowed during last test) first

    if(lightsource.lightGroupLight == false) // we don't cache for light groups
    {
        if ((lightsourceray.GetTicket().traceLevel == 2) && (lightSourceLevel1ShadowCache[lightsource.index] != nullptr))
            cacheObject = lightSourceLevel1ShadowCache[lightsource.index];
        else if (lightSourceOtherShadowCache[lightsource.index] != nullptr)
            cacheObject = lightSourceOtherShadowCache[lightsource.index];

        // if there was an object in the light source shadow cache, check that first
        if (cacheObject != nullptr)
        {
            // Same window as the walk below, so a thread's earlier rays cannot change what shadows this one.
            const double limit = std::min(lightsourcedepth - SHADOW_TOLERANCE, lightsourcedepth - projectedDepth);
            const IsoShadowWindow window(threadData, SHADOW_TOLERANCE, limit);
            if ((Test_Flag(cacheObject, OPAQUE_FLAG) && cacheObject->Shadow_Hint_Intersection(lightsourceray, &boundedIntersection, threadData) &&
                 (boundedIntersection.Depth > SHADOW_TOLERANCE) && (boundedIntersection.Depth < limit)) ||
                FindIntersection(cacheObject, boundedIntersection, lightsourceray, postcond, limit))
            {
                if(!Test_Flag(boundedIntersection.Object, NO_SHADOW_FLAG))
                {
                    // A part that lets light through is left to the walk below, which filters it once.
                    const MathColour unfiltered = lightcolour;
                    const DBL mediaBudget = lightsourceray.GetMediaErrorBudget();
                    ComputeShadowColour(lightsource, boundedIntersection, lightsourceray, lightcolour);

                    if(lightcolour.IsNearZero(EPSILON) &&
                       (Test_Flag(boundedIntersection.Object, OPAQUE_FLAG)))
                    {
                        threadData->Stats()[Shadow_Ray_Tests]++;
                        threadData->Stats()[Shadow_Rays_Succeeded]++;
                        threadData->Stats()[Shadow_Cache_Hits]++;
                        return;
                    }
                    lightcolour = unfiltered;
                    lightsourceray.SetMediaErrorBudget(mediaBudget);
                }
                cacheObject = nullptr;
            }
            else
            {
                precond.missed = cacheObject;
                cacheObject = nullptr;
            }
        }
    }

    foundTransparentObjects = false;

    while(true)
    {
        boundedIntersection.Object = boundedIntersection.Csg = nullptr;
        boundedIntersection.Depth = lightsourcedepth - projectedDepth;

        threadData->Stats()[Shadow_Ray_Tests]++;

        if (qualityFlags.shadows && (sceneData->boundingMethod == 1) && (sceneData->flatSlabs != nullptr))
        {
            OpaqueShadowStopCondition stop(std::min(lightsourcedepth - SHADOW_TOLERANCE, lightsourcedepth - projectedDepth));
            const IsoShadowWindow window(threadData, SHADOW_TOLERANCE, stop.farthest);
            foundIntersection = Intersect_Flat_BBox_Tree(*sceneData->flatSlabs, lightsourceray, &boundedIntersection, precond, postcond, stop, threadData);
        }
        else
            foundIntersection = FindIntersection(boundedIntersection, lightsourceray, precond, postcond);

        if((foundIntersection == true) &&
           (boundedIntersection.Depth < lightsourcedepth - SHADOW_TOLERANCE) &&
           (lightsourcedepth - boundedIntersection.Depth > projectedDepth) &&
           (boundedIntersection.Depth > SHADOW_TOLERANCE))
        {
            threadData->Stats()[Shadow_Rays_Succeeded]++;

            ComputeShadowColour(lightsource, boundedIntersection, lightsourceray, lightcolour);

            ObjectPtr testObject(boundedIntersection.Csg != nullptr ? boundedIntersection.Csg : boundedIntersection.Object);

            if(lightcolour.IsNearZero(EPSILON) &&
               (Test_Flag(testObject, OPAQUE_FLAG)))
            {
                // Hit a fully opaque object; cache that object, so that next time we can test for it first;
                // don't cache for light groups though (why not??)

                if((lightsource.lightGroupLight == false) && (foundTransparentObjects == false))
                {
                    cacheObject = testObject;

                    if(lightsourceray.GetTicket().traceLevel == 2)
                        lightSourceLevel1ShadowCache[lightsource.index] = cacheObject;
                    else
                        lightSourceOtherShadowCache[lightsource.index] = cacheObject;
                }
                break;
            }

            foundTransparentObjects = true;

            // Move the ray to the point of intersection, plus some
            lightsourcedepth -= boundedIntersection.Depth;

            lightsourceray.Origin = boundedIntersection.IPoint;
        }
        else
            // No further intersections in the direction of the ray.
            break;
    }
}

void Trace::TraceAreaLightShadowRay(const LightSource &lightsource, double& lightsourcedepth, Ray& lightsourceray,
                                    const Vector3d& ipoint, MathColour& lightcolour)
{
    Vector3d axis1Temp, axis2Temp;

    lightGrid.resize(lightsource.Area_Size1 * lightsource.Area_Size2);

    // Flag uncalculated points with a negative value for Red
    /*
    for(i = 0; i < lightsource.Area_Size1; i++)
    {
        for(j = 0; j < lightsource.Area_Size2; j++)
            lightGrid[i * lightsource.Area_Size2 + j].Invalidate();
    }
    */
    for(size_t ind = 0; ind < lightGrid.size(); ++ind)
        lightGrid[ind].Invalidate();

    ComputeAreaLightAxes(lightsource, lightsourcedepth, lightsourceray, ipoint, axis1Temp, axis2Temp);

    TraceAreaLightSubsetShadowRay(lightsource, lightsourcedepth, lightsourceray, ipoint, lightcolour, 0, 0, lightsource.Area_Size1 - 1, lightsource.Area_Size2 - 1, 0, axis1Temp, axis2Temp);

    // Jitter only shapes a shadow the light's points disagree on.
    if(lightsource.Jitter && !grain)
    {
        const MathColour* first = nullptr;
        for(const MathColour& sample : lightGrid)
        {
            if(!sample.IsValid())
                continue;
            if(first == nullptr)
                first = &sample;
            else if(!(sample - *first).IsZero())
            {
                MarkGrain();
                break;
            }
        }
    }
}

void Trace::ComputeAreaLightAxes(const LightSource &lightsource, double& lightsourcedepth, Ray& lightsourceray,
                                 const Vector3d& ipoint, Vector3d& axis1, Vector3d& axis2)
{
    Vector3d temp;
    double axis1_Length;

    axis1 = lightsource.Axis1;
    axis2 = lightsource.Axis2;

    if(lightsource.Orient == true)
    {
        // Orient the area light to face the intersection point [ENB 9/97]

        // Do Light source to get the correct lightsourceray
        ComputeOneWhiteLightRay(lightsource, lightsourcedepth, lightsourceray, ipoint);

        // Save the lengths of the axes
        axis1_Length = axis1.length();

        // Make axis 1 be perpendicular with the light-ray
        if(fabs(fabs(lightsourceray.Direction[Z]) - 1.0) < 0.01)
            // too close to vertical for comfort, so use cross product with horizon
            temp = Vector3d(0.0, 1.0, 0.0);
        else
            temp = Vector3d(0.0, 0.0, 1.0);

        axis1 = cross(lightsourceray.Direction, temp).normalized();

        // Make axis 2 be perpendicular with the light-ray and with Axis1.  A simple cross-product will do the trick.
        axis2 = cross(lightsourceray.Direction, axis1).normalized();

        // make it square
        axis1 *= axis1_Length;
        axis2 *= axis1_Length;
    }
}

void Trace::TraceAreaLightSubsetShadowRay(const LightSource &lightsource, double& lightsourcedepth, Ray& lightsourceray,
                                          const Vector3d& ipoint, MathColour& lightcolour, int u1, int  v1, int  u2, int  v2, int level, const Vector3d& axis1, const Vector3d& axis2)
{
    MathColour sample_Colour[4];
    int i, u, v, new_u1, new_v1, new_u2, new_v2;
    double jitter_u, jitter_v;

    // Sample the four corners of the region
    for(i = 0; i < 4; i++)
    {
        Vector3d center(lightsource.Center);
        Ray lsr(lightsourceray);

        switch(i)
        {
            case 0: u = u1; v = v1; break;
            case 1: u = u2; v = v1; break;
            case 2: u = u1; v = v2; break;
            case 3: u = u2; v = v2; break;
            default: u = v = 0;  // Should never happen!
        }

        if(lightGrid[u * lightsource.Area_Size2 + v].IsValid())
            // We've already calculated this point, reuse it
            sample_Colour[i] = lightGrid[u * lightsource.Area_Size2 + v];
        else
        {
            jitter_u = (double)u;
            jitter_v = (double)v;

            if(lightsource.Jitter)
            {
                const std::uint64_t cell = std::uint64_t(u) * lightsource.Area_Size2 + v;
                jitter_u += Draw(lightsourceray.GetKey(), kDrawAreaLight, 2 * cell) - 0.5;
                jitter_v += Draw(lightsourceray.GetKey(), kDrawAreaLight, 2 * cell + 1) - 0.5;
            }

            // Recalculate the light source ray but not the colour
            const Vector3d offset = AreaLightOffset(lightsource, jitter_u, jitter_v, axis1, axis2);
            ComputeOneWhiteLightRay(lightsource, lightsourcedepth, lsr, ipoint, offset);

            sample_Colour[i] = lightcolour;

            TraceSampleShadowRay(lightsource, lightsourcedepth, lsr, sample_Colour[i], offset);
            lightsourceray.SetMediaErrorBudget(min(lightsourceray.GetMediaErrorBudget(), lsr.GetMediaErrorBudget()));

            lightGrid[u * lightsource.Area_Size2 + v] = sample_Colour[i];
        }
    }

    if((u2 - u1 > 1) || (v2 - v1 > 1))
    {
        if((level < lightsource.Adaptive_Level) ||
           (ColourDistance(sample_Colour[0], sample_Colour[1]) > 0.1) ||
           (ColourDistance(sample_Colour[1], sample_Colour[3]) > 0.1) ||
           (ColourDistance(sample_Colour[3], sample_Colour[2]) > 0.1) ||
           (ColourDistance(sample_Colour[2], sample_Colour[0]) > 0.1))
        {
            Vector3d center(lightsource.Center);

            for (i = 0; i < 4; i++)
            {
                switch (i)
                {
                    case 0:
                        new_u1 = u1;
                        new_v1 = v1;
                        new_u2 = (int)floor((u1 + u2)/2.0);
                        new_v2 = (int)floor((v1 + v2)/2.0);
                        break;
                    case 1:
                        new_u1 = (int)ceil((u1 + u2)/2.0);
                        new_v1 = v1;
                        new_u2 = u2;
                        new_v2 = (int)floor((v1 + v2)/2.0);
                        break;
                    case 2:
                        new_u1 = u1;
                        new_v1 = (int)ceil((v1 + v2)/2.0);
                        new_u2 = (int)floor((u1 + u2)/2.0);
                        new_v2 = v2;
                        break;
                    case 3:
                        new_u1 = (int)ceil((u1 + u2)/2.0);
                        new_v1 = (int)ceil((v1 + v2)/2.0);
                        new_u2 = u2;
                        new_v2 = v2;
                        break;
                    default:  // Should never happen!
                        new_u1 = new_u2 = new_v1 = new_v2 = 0;
                }

                // Recalculate the light source ray but not the colour
                ComputeOneWhiteLightRay(lightsource, lightsourcedepth, lightsourceray, ipoint, center);

                sample_Colour[i] = lightcolour;

                TraceAreaLightSubsetShadowRay(lightsource, lightsourcedepth, lightsourceray,
                                              ipoint, sample_Colour[i], new_u1, new_v1, new_u2, new_v2, level + 1, axis1, axis2);
            }
        }
    }

    // Average up the light contributions
    lightcolour = (sample_Colour[0] + sample_Colour[1] + sample_Colour[2] + sample_Colour[3]) * 0.25;
}

// Maps s in [0,1) to a grid coordinate distributed as the full adaptive grid weights its points: edge points half.
static double AreaGridCoordinate(double s, int size, bool jitter)
{
    const double t = s * (size - 1);
    if(!jitter)
        return floor(t + 0.5);
    if(t < 0.5)
        return 2.0 * t - 0.5;
    if(t > size - 1.5)
        return 2.0 * t - size + 1.5;
    return t;
}

void Trace::TraceAreaLightSampleShadowRay(const LightSource &lightsource, double& lightsourcedepth, Ray& lightsourceray,
                                          const Vector3d& ipoint, MathColour& lightcolour, const Vector2d& sample)
{
    Vector3d axis1, axis2;
    double u, v;

    ComputeAreaLightAxes(lightsource, lightsourcedepth, lightsourceray, ipoint, axis1, axis2);

    u = AreaGridCoordinate(sample[U], lightsource.Area_Size1, lightsource.Jitter);
    v = AreaGridCoordinate(sample[V], lightsource.Area_Size2, lightsource.Jitter);

    const Vector3d offset = AreaLightOffset(lightsource, u, v, axis1, axis2);
    ComputeOneWhiteLightRay(lightsource, lightsourcedepth, lightsourceray, ipoint, offset);
    TraceSampleShadowRay(lightsource, lightsourcedepth, lightsourceray, lightcolour, offset);
}

Vector3d Trace::AreaLightOffset(const LightSource &lightsource, double jitter_u, double jitter_v, const Vector3d& axis1, const Vector3d& axis2)
{
    Vector3d jitterAxis1, jitterAxis2;
    double scaleFactor;

    // Circular lights squash the square grid onto a disc [ENB 9/97].
    if(lightsource.Circular == true)
    {
        jitter_u = jitter_u / (lightsource.Area_Size1 - 1) - 0.5 + 0.001;
        jitter_v = jitter_v / (lightsource.Area_Size2 - 1) - 0.5 + 0.001;
        scaleFactor = ((fabs(jitter_u) > fabs(jitter_v)) ? fabs(jitter_u) : fabs(jitter_v));
        scaleFactor /= sqrt(jitter_u * jitter_u + jitter_v * jitter_v);
        jitter_u *= scaleFactor;
        jitter_v *= scaleFactor;
        jitterAxis1 = axis1 * jitter_u;
        jitterAxis2 = axis2 * jitter_v;
    }
    else
    {
        if(lightsource.Area_Size1 > 1)
        {
            scaleFactor = jitter_u / (double)(lightsource.Area_Size1 - 1) - 0.5;
            jitterAxis1 = axis1 * scaleFactor;
        }
        else
            jitterAxis1 = Vector3d(0.0, 0.0, 0.0);

        if(lightsource.Area_Size2 > 1)
        {
            scaleFactor = jitter_v / (double)(lightsource.Area_Size2 - 1) - 0.5;
            jitterAxis2 = axis2 * scaleFactor;
        }
        else
            jitterAxis2 = Vector3d(0.0, 0.0, 0.0);
    }

    return jitterAxis1 + jitterAxis2;
}

// see filter_shadow_ray in v3.6's lighting.cpp
void Trace::ComputeShadowColour(const LightSource &lightsource, Intersection& isect, Ray& lightsourceray, MathColour& colour)
{
    WeightedTextureVector wtextures;
    Vector3d ipoint;
    Vector3d raw_Normal;
    MathColour fc1;
    ColourChannel ft1;
    MathColour temp_Colour;
    Vector2d uv_Coords;
    double normaldirection;

    // Here's the issue:
    // Imagine "LightA" shoots photons at "GlassSphereB", which refracts light and
    // hits "PlaneC".
    // When computing Diffuse/Phong/etc lighting for PlaneC, if there were no
    // photons, POV would compute a filtered shadow ray from PlaneC through
    // GlassSphereB to LightA.  If photons are used for the combination of objects,
    // this filtered shadow ray should be completely black.  The filtered shadow
    // ray should be forced to black UNLESS any of the following conditions are
    // true (which would indicate that photons were not shot from LightA through
    // GlassSphereB to PlaneC):
    // 1) PlaneC has photon collection set to "off"
    // 2) GlassSphereB is not a photon target
    // 3) GlassSphereB has photon refraction set to "off"
    // 4) LightA has photon refraction set to "off"
    // 5) Neither GlassSphereB nor LightA has photon refraction set to "on"
    if((sceneData->photonSettings.photonsEnabled == true) &&
        (sceneData->surfacePhotonMap.numPhotons > 0) &&
        (!threadData->litObjectIgnoresPhotons) &&
        (Test_Flag(isect.Object,PH_TARGET_FLAG)) &&
        (!Test_Flag(isect.Object,PH_RFR_OFF_FLAG)) &&
        (!Test_Flag(&lightsource,PH_RFR_OFF_FLAG)) &&
        ((Test_Flag(isect.Object,PH_RFR_ON_FLAG) || Test_Flag(&lightsource,PH_RFR_ON_FLAG)))
        )
    {
        // full shadow (except for photon-based illumination)
        colour.Clear();
        return;
    }

    ipoint = isect.IPoint;

    if(!qualityFlags.shadows)
        // no shadow
        return;

    // If the object is opaque there's no need to go any further. [DB 8/94]
    if(Test_Flag(isect.Object, OPAQUE_FLAG))
    {
        // full shadow
        colour.Clear();
        return;
    }

    // Get the normal to the surface
    isect.Object->Normal(raw_Normal, &isect, threadData);

    // I added this to flip the normal if the object is inverted (for CSG).
    // However, I subsequently commented it out for speed reasons - it doesn't
    // make a difference (no pun intended). The preexisting flip code below
    // produces a similar (though more extensive) result. [NK]
    //
    // Actually, we should keep this code to guarantee that normaldirection
    // is set properly. [NK]
    if(Test_Flag(isect.Object, INVERTED_FLAG))
        raw_Normal.invert();

    // If the surface normal points away, flip its direction.
    normaldirection = dot(raw_Normal, lightsourceray.Direction);
    if(normaldirection > 0.0)
        raw_Normal.invert();

    isect.INormal = raw_Normal;
    // and save to intersection -hdf-
    isect.PNormal = raw_Normal;

    // now switch to UV mapping if we need to
    if(Test_Flag(isect.Object, UV_FLAG))
    {
        // TODO FIXME
        //  I think we have a serious problem here regarding bump mapping:
        //  The UV vector doesn't contain any information about the (local) *orientation* of U and V in our XYZ co-ordinate system!
        //  This causes slopes do be applied in the wrong directions.

        // get the UV vect of the intersection
        isect.Object->UVCoord(uv_Coords, &isect);
        // save the normal and UV coords into Intersection
        isect.Iuv = uv_Coords;

        ipoint[X] = uv_Coords[U];
        ipoint[Y] = uv_Coords[V];
        ipoint[Z] = 0;
    }

    // NB the v3.6 code doesn't set the light cache's Tested flags to false after incrementing the level.
    if (++lightColorCacheIndex >= lightColorCache.size())
    {
        lightColorCache.resize(lightColorCacheIndex + 10);
        for (LightColorCacheListList::iterator it = lightColorCache.begin() + lightColorCacheIndex; it != lightColorCache.end(); it++)
            it->resize(lightColorCache[0].size());
    }

    bool isMultiTextured = Test_Flag(isect.Object, MULTITEXTURE_FLAG) ||
                           ((isect.Object->Texture == nullptr) && Test_Flag(isect.Object, CUTAWAY_TEXTURES_FLAG));

    // get textures and weights
    if(isMultiTextured == true)
    {
        isect.Object->Determine_Textures(&isect, normaldirection > 0.0, wtextures, threadData);
    }
    else if (isect.Object->Texture != nullptr)
    {
        if ((normaldirection > 0.0) && (isect.Object->Interior_Texture != nullptr))
            wtextures.push_back(WeightedTexture(1.0, isect.Object->Interior_Texture)); /* Chris Huff: Interior Texture patch */
        else
            wtextures.push_back(WeightedTexture(1.0, isect.Object->Texture));
    }
    else
    {
        // don't need to do anything as the texture list will be empty.
        // TODO: could we perform these tests earlier ? [cjc]
        lightColorCacheIndex--;
        return;
    }

    temp_Colour.Clear();

    for(WeightedTextureVector::iterator i(wtextures.begin()); i != wtextures.end(); i++)
    {
        TextureVector warps(texturePool);
        POV_REFPOOL_ASSERT(warps->empty()); // verify that the TextureVector pulled from the pool is in a cleaned-up condition

        // If contribution of this texture is negligible skip ahead.
        if ((i->weight < lightsourceray.GetTicket().adcBailout) || (i->texture == nullptr))
            continue;

        ComputeOneTextureColour(fc1, ft1, i->texture, *warps, ipoint, raw_Normal, lightsourceray, 0.0, isect, true, false);

        temp_Colour += i->weight * fc1;
    }

    lightColorCacheIndex--;

    if(fabs(temp_Colour.Weight()) < lightsourceray.GetTicket().adcBailout)
    {
        // close enough to full shadow - bail out to avoid media computations
        colour.Clear();
        return;
    }

    // [CLi] moved this here from Trace::ComputeShadowTexture() and Trace::ComputeLightedTexture(), respectively,
    // to avoid media to be computed twice when dealing with averaged textures.
    // TODO - For photon rays we're still potentially doing double work on media.
    // TODO - For shadow rays we're still potentially doing double work on distance-based attenuation.
    // Calculate participating media effects.
    if(qualityFlags.media && (!lightsourceray.GetInteriors().empty()) && (lightsourceray.IsHollowRay() == true))
    {
        ColourChannel dummyTransm;
        media.ComputeMedia(lightsourceray.GetInteriors(), lightsourceray, isect, temp_Colour, dummyTransm);
    }

    colour *= temp_Colour;

    // Get atmospheric attenuation.
    ComputeShadowMedia(lightsourceray, isect, colour, (lightsource.Media_Interaction) && (lightsource.Media_Attenuation));
}

void Trace::ComputeDiffuseColour(const FINISH *finish, const Vector3d& lightDirection, const Vector3d& eyeDirection, const Vector3d& layer_normal, MathColour& colour, const MathColour& light_colour,
                                 const MathColour& layer_pigment_colour, double relativeIor, double attenuation, bool backside, std::uint64_t key, std::uint64_t index)
{
    double cos_angle_of_incidence, intensity;
    double diffuse = (backside? finish->DiffuseBack : finish->Diffuse) * finish->BrillianceAdjust;

    if (diffuse <= 0.0)
        return;

    cos_angle_of_incidence = dot(layer_normal, lightDirection);

    // Brilliance is likely to be 1.0 (default value)
    if(finish->Brilliance != 1.0)
        intensity = pow(fabs(cos_angle_of_incidence), (double) finish->Brilliance);
    else
        intensity = fabs(cos_angle_of_incidence);

    intensity *= diffuse * attenuation;

    if(finish->Crand > 0.0)
    {
        intensity -= Draw(key, kDrawCrand, index) * finish->Crand;
        MarkGrain();
    }

    if (finish->Fresnel != 0.0)
    {
        // In diffuse reflections, the Fresnel effect is that we _lose_ the reflected component
        // as the light enters the material (where it changes direction randomly), and then
        // _again lose_ another reflected component as the light leaves the material.
        // ComputeFresnel() gives us the reflected component.

        double f1 = finish->Fresnel * FresnelR(cos_angle_of_incidence, relativeIor);

        // NB: One might think that to properly compute the outgoing loss we would have
        // to compute the Fresnel term for the _internal_ angle of incidence and the
        // _inverse_ of the relative refractive index; however, the Fresnel formula
        // is such that we can just as well plug in the external angle of incidence
        // and straight relative refractive index.
        cos_angle_of_incidence = -dot(layer_normal, eyeDirection);
        double f2 = finish->Fresnel * FresnelR(cos_angle_of_incidence, relativeIor);

        colour += intensity * layer_pigment_colour * light_colour * (1.0 - f1) * (1.0 - f2);
    }
    else
        colour += intensity * layer_pigment_colour * light_colour;
}

void Trace::ComputeIridColour(const FINISH *finish, const Vector3d& lightDirection, const Vector3d& eyeDirection, const Vector3d& layer_normal, const Vector3d& ipoint, MathColour& colour)
{
    double cos_angle_of_incidence_light, cos_angle_of_incidence_eye, interference;
    double film_thickness;
    double noise;
    TurbulenceWarp turb;

    film_thickness = finish->Irid_Film_Thickness;

    if(finish->Irid_Turb != 0)
    {
        // Uses hardcoded octaves, lambda, omega
        turb.Omega=0.5;
        turb.Lambda=2.0;
        turb.Octaves=5;

        // Turbulence() returns a value from 0..1, so noise will be in order of magnitude 1.0 +/- finish->Irid_Turb
        noise = Turbulence(ipoint, &turb, sceneData->noiseGenerator);
        noise = 2.0 * noise - 1.0;
        noise = 1.0 + noise * finish->Irid_Turb;
        film_thickness *= noise;
    }

    // NOTE: Shouldn't we compute Cos_Angle_Of_Incidence just once?
    cos_angle_of_incidence_light = abs(dot(layer_normal, lightDirection));
    cos_angle_of_incidence_eye   = abs(dot(layer_normal, eyeDirection));

    // Calculate phase offset.
    interference = 2.0 * M_PI * film_thickness * (cos_angle_of_incidence_light + cos_angle_of_incidence_eye);

    // Modify color by phase offset for each wavelength.
    colour *= 1.0 + finish->Irid * Cos(interference / sceneData->iridWavelengths);
}

void Trace::ComputePhongColour(const FINISH *finish, const Vector3d& lightDirection, const Vector3d& eyeDirection, const Vector3d& layer_normal, MathColour& colour, const MathColour& light_colour,
                               const MathColour& layer_pigment_colour, double relativeIor)
{
    double cos_angle_of_incidence, intensity;
    Vector3d reflect_direction;

    cos_angle_of_incidence = -2.0 * dot(eyeDirection, layer_normal);

    reflect_direction = eyeDirection + cos_angle_of_incidence * layer_normal;

    cos_angle_of_incidence = dot(reflect_direction, lightDirection);

    if(cos_angle_of_incidence > 0.0)
    {
        if((finish->Phong_Size < 60) || (cos_angle_of_incidence > 0.0008)) // rgs
        {
            intensity = finish->Phong * pow(cos_angle_of_incidence, (double)finish->Phong_Size);

            if ((finish->Fresnel != 0.0) || (finish->Metallic != 0.0))
            {
                MathColour cs(1.0);
                // NB: The following dot product should technically be done with the vector halfway
                // between the in- and outgoing direction, rather than, the surface normal.
                // But since the Phong model specifically takes shortcuts to not compute that
                // vector, we'll take the surface normal as an approximation. Also, the choice to
                // compute the dot with the light rather than viewing direction is arbitrary.
                double ndotl = dot(layer_normal, lightDirection);
                if (finish->Fresnel != 0.0)
                    // In specular reflections (which includes highlights), the Fresnel effect is
                    // that we only get the reflected component.
                    cs *= finish->Fresnel * FresnelR(ndotl, relativeIor);
                ComputeMetallic(cs, finish->Metallic, layer_pigment_colour, ndotl);
                colour += intensity * light_colour * cs;
            }
            else
                colour += intensity * light_colour;
        }
    }
}

void Trace::ComputeSpecularColour(const FINISH *finish, const Vector3d& lightDirection, const Vector3d& reyeDirection, const Vector3d& layer_normal, MathColour& colour,
                                  const MathColour& light_colour, const MathColour& layer_pigment_colour, double relativeIor)
{
    double cos_angle_of_incidence, intensity, halfway_length;
    Vector3d halfway;

    halfway = (reyeDirection + lightDirection) * 0.5;

    halfway_length = halfway.length();

    if(halfway_length > 0.0)
    {
        cos_angle_of_incidence = dot(halfway, layer_normal) / halfway_length;

        if(cos_angle_of_incidence > 0.0)
        {
            intensity = finish->Specular * pow(cos_angle_of_incidence, (double)finish->Roughness);

            if ((finish->Fresnel != 0.0) || (finish->Metallic != 0.0))
            {
                MathColour cs(1.0);
                double ndotl = dot(halfway, lightDirection) / halfway_length;
                if (finish->Fresnel != 0.0)
                    // In specular reflections (which includes highlights), the Fresnel effect is
                    // that we only get the reflected component.
                    cs *= finish->Fresnel * FresnelR(ndotl, relativeIor);
                ComputeMetallic(cs, finish->Metallic, layer_pigment_colour, ndotl);
                colour += intensity * light_colour * cs;
            }
            else
                colour += intensity * light_colour;
        }
    }
}

void Trace::ComputeRelativeIOR(const Ray& ray, const Interior *interior, double& ior)
{
    // Get ratio of iors depending on the interiors the ray is traversing.
    if (interior == nullptr)
    {
        // TODO VERIFY - is this correct?
        ior = 1.0;
    }
    else
    {
        if(ray.GetInteriors().empty())
            // The ray is entering from the atmosphere.
            ior = interior->IOR / sceneData->atmosphereIOR;
        else
        {
            // The ray is currently inside an object.
            if(ray.IsInterior(interior) == true)
            {
                if(ray.GetInteriors().size() == 1)
                    // The ray is leaving into the atmosphere.
                    ior = sceneData->atmosphereIOR / interior->IOR;
                else
                    // The ray is leaving into another object.
                    ior = ray.GetInteriors().back()->IOR / interior->IOR;
            }
            else
                // The ray is entering a new object.
                ior = interior->IOR / ray.GetInteriors().back()->IOR;
        }
    }
}

void Trace::ComputeReflectivity(double& weight, MathColour& reflectivity, const MathColour& reflection_max, const MathColour& reflection_min,
                                bool fresnel, double reflection_falloff, double cos_angle, double relativeIor)
{
    double temp_Weight_Min, temp_Weight_Max;
    double reflection_Frac;

    if (fresnel == false)
    {
        temp_Weight_Max = reflection_max.WeightMax();
        temp_Weight_Min = reflection_min.WeightMax();
        weight = weight * max(temp_Weight_Max, temp_Weight_Min);

        if(fabs(reflection_falloff - 1.0) > EPSILON)
            reflection_Frac = pow(1.0 - cos_angle, reflection_falloff);
        else
            reflection_Frac = 1.0 - cos_angle;

        if(fabs(reflection_Frac) < EPSILON)
            reflectivity = reflection_min;
        else if (fabs(reflection_Frac - 1.0)<EPSILON)
            reflectivity = reflection_max;
        else
            reflectivity = reflection_Frac * reflection_max + (1.0 - reflection_Frac) * reflection_min;
    }
    else
    {
        ComputeFresnel(reflectivity, reflection_max, reflection_min, cos_angle, relativeIor);
        weight = weight * reflectivity.WeightMax();
    }
}

void Trace::ComputeMetallic(MathColour& colour, double metallic, const MathColour& metallicColour, double cosAngle)
{
    // Calculate the reflected color by interpolating between
    // the light source color and the surface color according
    // to the (empirical) Fresnel reflectivity function. [DB 9/94]
    if(metallic != 0.0)
    {
        double x = fabs(acos(cosAngle)) / M_PI_2;
        double F = 0.014567225 / Sqr(x - 1.12) - 0.011612903;
        F = min(1.0, max(0.0, F));

        colour *= (1.0 + (metallic * (1.0 - F)) * (metallicColour - 1.0));
    }
}

void Trace::ComputeFresnel(MathColour& reflectivity, const MathColour& rMax, const MathColour& rMin, double cos_angle, double ior)
{
    double f = FresnelR(cos_angle, ior);
    reflectivity = f * rMax + (1.0 - f) * rMin;
}

double Trace::FresnelR(double cosTi, double n)
{
    // NB: This is a special case of the Fresnel formula, presuming that incident light is unpolarized.
    //
    // The implemented formula is as follows:
    //
    //      1     / g - cos Ti \ 2    /     / cos Ti (g + cos Ti) - 1 \ 2 \
    // R = --- * ( ------------ )  * ( 1 + ( ------------------------- )   )
    //      2     \ g + cos Ti /      \     \ cos Ti (g - cos Ti) + 1 /   /
    //
    // where
    //
    //        /---------------------------
    // g = -\/ (n1/n2)^2 + (cos Ti)^2 - 1

    double sqrg = Sqr(n) + Sqr(cosTi) - 1.0;

    if(sqrg <= 0.0)
        // Total reflection.
        return 1.0;

    double g = sqrt(sqrg);

    double quot1 = (g - cosTi) / (g + cosTi);
    double quot2 = (cosTi * (g + cosTi) - 1.0) / (cosTi * (g - cosTi) + 1.0);

    double f = 0.5 * Sqr(quot1) * (1.0 + Sqr(quot2));

    return clip(f, 0.0, 1.0);
}

void Trace::ComputeOneWhiteLightRay(const LightSource &lightsource, double& lightsourcedepth, Ray& lightsourceray, const Vector3d& ipoint, const Vector3d& jitter)
{
    Vector3d center = lightsource.Center + jitter;
    double a;
    Vector3d v1;

    // Get the light ray starting at the intersection point and pointing towards the light source.
    lightsourceray.Origin = ipoint;
    // NK 1998 parallel beams for cylinder source - added 'if'
    if(lightsource.Light_Type == CYLINDER_SOURCE)
    {
        double distToPointsAt;
        Vector3d toLightCtr;

        // use new code to get ray direction - use center - points_at for direction
        lightsourceray.Direction = center - lightsource.Points_At;

        // get vector pointing to center of light
        toLightCtr = center - ipoint;

        // project light_ctr-intersect_point onto light_ctr-point_at
        distToPointsAt = lightsourceray.Direction.length();
        lightsourcedepth = dot(toLightCtr, lightsourceray.Direction);

        // lenght of shadow ray is the length of the projection
        lightsourcedepth /= distToPointsAt;
        lightsourceray.Direction.normalize(); // TODO - lightsourceray.Direction /= distToPointsAt  should also work
    }
    else
    {
        // NK 1998 parallel beams for cylinder source - the stuff in this 'else'
        // block used to be all that there was... the first half of the if
        // statement (before the 'else') is new
        lightsourceray.Direction = center - ipoint;
        lightsourcedepth = lightsourceray.Direction.length();
        lightsourceray.Direction /= lightsourcedepth;
    }

    // Attenuate light source color.
    // Attenuation = Attenuate_Light(lightsource, lightsourceray, *Light_Source_Depth);
    // Recalculate for Parallel light sources
    if(lightsource.Parallel)
    {
        if(lightsource.Area_Light)
        {
            v1 = (center - lightsource.Points_At).normalized();
            a = dot(v1, lightsourceray.Direction);
            lightsourcedepth *= a;
            lightsourceray.Direction = v1;
        }
        else
        {
            a = dot(lightsource.Direction, lightsourceray.Direction);
            lightsourcedepth *= (-a);
            lightsourceray.Direction = -lightsource.Direction;
        }
    }
}

void Trace::ComputeSky(const Ray& ray, MathColour& colour, ColourChannel& transm)
{
    if (sceneData->EffectiveLanguageVersion() < 370)
    {
        // this gives the same results regarding sky sphere filter as how v3.6 did it
        // NB the use of the RGBFTColour data type here is intentional, and required to achieve backward compatibility

        ColourChannel filter;
        double att, trans;
        MathColour col;
        TransColour col_Temp;
        MathColour filterc_colour;
        ColourChannel filterc_filter;
        ColourChannel filterc_transm;
        Vector3d p;

        if (ray.GetTicket().alphaBackground)
        {
            // If rendering with alpha channel, just return full transparency.
            // (As we're working with associated alpha internally, the respective color must be black here.)
            colour.Clear();
            transm = 1.0;
            return;
        }

        colour = sceneData->backgroundColour.colour();
        sceneData->backgroundColour.GetFT(filter, transm);

        if (sceneData->skysphere == nullptr)
            return;

        col.Clear();
        filterc_colour = MathColour(1.0);
        filterc_filter = 1.0;
        filterc_transm = 1.0;
        trans = 1.0;

        // Transform point on unit sphere.
        if (sceneData->skysphere->Trans != nullptr)
            MInvTransPoint(p, ray.Direction, sceneData->skysphere->Trans);
        else
            p = ray.Direction;

        // TODO - Reverse iterator may be less performant than forward iterator; we might want to
        //        compare performance with using forward iterators and decrement, or using random access.
        //        Alternatively, reversing the vector after parsing might be another option.
        for(vector<PIGMENT*>::const_reverse_iterator i = sceneData->skysphere->Pigments.rbegin(); i != sceneData->skysphere->Pigments.rend(); ++ i)
        {
            // Compute sky colour from colour map.

            Compute_Pigment(col_Temp, *i, p, nullptr, nullptr, threadData);

            att = trans * col_Temp.Opacity();

            col += col_Temp.colour() * att;

            RGBFTColour col_Temp2 = ToRGBFTColour(col_Temp);
            filterc_colour *= col_Temp.colour();
            filterc_filter *= col_Temp2.filter();
            filterc_transm *= col_Temp2.transm();

            trans = fabs(filterc_filter) + fabs(filterc_transm);
        }

        col *= sceneData->skysphere->Emission;

        MathColour transColour = filterc_colour * filterc_filter + filterc_transm;
        colour = colour * transColour + col;
        transm *= filterc_transm;
    }
    else // i.e. sceneData->EffectiveLanguageVersion() >= 370
    {
        // this gives the same results regarding sky sphere filter as a layered-texture genuine sphere

        MathColour filCol(1.0);
        double att;
        MathColour col;
        TransColour col_Temp;
        Vector3d p;

        col.Clear();

        if (sceneData->skysphere != nullptr)
        {
            // Transform point on unit sphere.
            if (sceneData->skysphere->Trans != nullptr)
                MInvTransPoint(p, ray.Direction, sceneData->skysphere->Trans);
            else
                p = ray.Direction;

            // TODO - Reverse iterator may be less performant than forward iterator; we might want to
            //        compare performance with using forward iterators and decrement, or using random access.
            //        Alternatively, reversing the vector after parsing might be another option.
            for(vector<PIGMENT*>::const_reverse_iterator i = sceneData->skysphere->Pigments.rbegin(); i != sceneData->skysphere->Pigments.rend(); ++ i)
            {
                // Compute sky colour from colour map.
                Compute_Pigment(col_Temp, *i, p, nullptr, nullptr, threadData);

                att = col_Temp.Opacity();

                col += col_Temp.colour() * att * filCol * sceneData->skysphere->Emission;
                filCol *= col_Temp.TransmittedColour();
            }
        }

        // apply background as if it was another sky sphere with uniform pigment
        col_Temp = sceneData->backgroundColour;
        if (!ray.GetTicket().alphaBackground)
        {
            // if rendering without alpha channel, ignore filter and transmit of background color.
            col_Temp.SetFT(0.0, 0.0);
        }

        att = col_Temp.Opacity();

        col += col_Temp.colour() * att * filCol;
        filCol *= col_Temp.TransmittedColour();

        colour = col;
        transm = min(1.0f, fabs(filCol.Greyscale()));
    }
}

void Trace::ComputeFog(const Ray& ray, const Intersection& isect, MathColour& colour, ColourChannel& transm)
{
    double att, width;
    MathColour col_fog;
    ColourChannel filter_fog, transm_fog;
    MathColour sum_att; // total attenuation.
    MathColour sum_col; // total color.

    // Why are we here.
    if (sceneData->fog == nullptr)
        return;

    // Init total attenuation and total color.
    sum_att = MathColour(1.0);
    sum_col = MathColour(0.0);

    // Loop over all fogs.
    for (FOG *fog = sceneData->fog; fog != nullptr; fog = fog->Next)
    {
        // Don't care about fogs with zero distance.
        if(fabs(fog->Distance) > EPSILON)
        {
            width = isect.Depth;

            switch(fog->Type)
            {
                case GROUND_MIST:
                    att = ComputeGroundFogDepth(ray, 0.0, width, fog);
                    break;
                default:
                    att = ComputeConstantFogDepth(ray, 0.0, width, fog);
                    break;
            }

            col_fog = fog->colour.colour();
            fog->colour.GetFT(filter_fog, transm_fog);

            // Check for minimum transmittance.
            if(att < transm_fog)
                att = transm_fog;

            // Get attenuation sum due to filtered/unfiltered translucency.
            // [CLi] removed computation of sum_att.filer() and sum_att.transm(), as they were discarded anyway
            sum_att *= att * ((1.0 - filter_fog) + filter_fog * col_fog);

            if(!ray.IsShadowTestRay())
                sum_col += (1.0 - att) * col_fog;
        }
    }

    // Add light coming from background.
    colour = sum_col + sum_att * colour;
    transm *= sum_att.Greyscale();
}

double Trace::ComputeConstantFogDepth(const Ray &ray, double depth, double width, const FOG *fog)
{
    Vector3d p;
    double k;

    if (fog->Turb != nullptr)
    {
        depth += width / 2.0;

        p = ray.Evaluate(depth);
        p *= fog->Turb->Turbulence;

        // The further away the less influence turbulence has.
        k = exp(-width / fog->Distance);

        width *= (1.0 - k * min(1.0, Turbulence(p, fog->Turb, sceneData->noiseGenerator) * fog->Turb_Depth));
    }

    return (exp(-width / fog->Distance));
}

/*****************************************************************************
*   Here is an ascii graph of the ground fog density, it has a maximum
*   density of 1.0 at Y <= 0, and approaches 0.0 as Y goes up:
*
*   ***********************************
*        |           |            |    ****
*        |           |            |        ***
*        |           |            |           ***
*        |           |            |            | ****
*        |           |            |            |     *****
*        |           |            |            |          *******
*   -----+-----------+------------+------------+-----------+-----
*       Y=-2        Y=-1         Y=0          Y=1         Y=2
*
*   ground fog density is 1 / (Y*Y+1) for Y >= 0 and equals 1.0 for Y <= 0.
*   (It behaves like regular fog for Y <= 0.)
*
*   The integral of the density is atan(Y) (for Y >= 0).
******************************************************************************/

double Trace::ComputeGroundFogDepth(const Ray& ray, double depth, double width, const FOG *fog)
{
    double fog_density, delta;
    double start, end;
    double y1, y2, k;
    Vector3d p, p1, p2;

    // Get start point.
    p1 = ray.Evaluate(depth);

    // Get end point.
    p2 = p1 + ray.Direction * width;

    // Could preform transfomation here to translate Start and End
    // points into ground fog space.
    y1 = dot(p1, fog->Up);
    y2 = dot(p2, fog->Up);

    start = (y1 - fog->Offset) / fog->Alt;
    end   = (y2 - fog->Offset) / fog->Alt;

    // Get integral along y-axis from start to end.
    if(start <= 0.0)
    {
        if(end <= 0.0)
            fog_density = 1.0;
        else
            fog_density = (atan(end) - start) / (end - start);
    }
    else
    {
        if(end <= 0.0)
            fog_density = (atan(start) - end) / (start - end);
        else
        {
            delta = start - end;

            if(fabs(delta) > EPSILON)
                fog_density = (atan(start) - atan(end)) / delta;
            else
                fog_density = 1.0 / (Sqr(start) + 1.0);
        }
    }

    // Apply turbulence.
    if (fog->Turb != nullptr)
    {
        p = (p1 + p2) * 0.5;
        p *= fog->Turb->Turbulence;

        // The further away the less influence turbulence has.
        k = exp(-width / fog->Distance);
        width *= (1.0 - k * min(1.0, Turbulence(p, fog->Turb, sceneData->noiseGenerator) * fog->Turb_Depth));
    }

    return (exp(-width * fog_density / fog->Distance));
}

void Trace::ComputeShadowMedia(Ray& light_source_ray, Intersection& isect, MathColour& resultcolour, bool media_attenuation_and_interaction)
{
    if(resultcolour.IsNearZero(EPSILON))
        return;

    // Calculate participating media effects.
    if (media_attenuation_and_interaction && qualityFlags.media &&
        ((light_source_ray.IsHollowRay() == true) || ((isect.Object != nullptr) && (isect.Object->interior != nullptr))))
    {
        // we're using general-purpose media and fog handling code, which insists on computing a transmissive component (for alpha channel)
        ColourChannel tmpTransm = 0.0;
        media.ComputeMedia(sceneData->atmosphere, light_source_ray, isect, resultcolour, tmpTransm);

        if ((sceneData->fog != nullptr) && (light_source_ray.IsHollowRay() == true) && (light_source_ray.IsPhotonRay() == false))
            ComputeFog(light_source_ray, isect, resultcolour, tmpTransm);

        // discard the transmissive component (alpha channel)
    }

    // If ray is entering from the atmosphere or the ray is currently *not* inside an object add it,
    // but if it is currently inside an object, the ray is leaving the current object and is removed
    if ((isect.Object != nullptr) && ((light_source_ray.GetInteriors().empty()) || (light_source_ray.RemoveInterior(isect.Object->interior.get()) == false)))
        light_source_ray.AppendInterior(isect.Object->interior.get());
}



void Trace::ComputeRainbow(const Ray& ray, const Intersection& isect, MathColour& colour, ColourChannel& transm)
{
    int n;
    double dot1, k, ki, index, x, y, l, angle, fade, f;
    Vector3d Temp;
    TransColour Cr;
    ColourChannel CrFilter, CrTransm;
    MathColour CtColour;
    ColourChannel CtFilter, CtTransm;

    // Why are we here.
    if (sceneData->rainbow == nullptr)
        return;

    // TODO - get rid of the use of the RGBFT colour model

    CtColour = MathColour(0.0);
    CtFilter = 1.0;
    CtTransm = 1.0;

    n = 0;

    std::uint64_t rainbowIndex = 0;
    for (RAINBOW *Rainbow = sceneData->rainbow; Rainbow != nullptr; Rainbow = Rainbow->Next, rainbowIndex++)
    {
        if ((Rainbow->Pigment != nullptr) && (Rainbow->Distance != 0.0) && (Rainbow->Width != 0.0))
        {
            // Get angle between ray direction and rainbow's up vector.
            x = dot(ray.Direction, Rainbow->Right_Vector);
            y = dot(ray.Direction, Rainbow->Up_Vector);

            l = Sqr(x) + Sqr(y);

            if(l > 0.0)
            {
                l = sqrt(l);
                y /= l;
            }

            angle = fabs(acos(y));

            if(angle <= Rainbow->Arc_Angle)
            {
                // Get dot product between ray direction and antisolar vector.
                dot1 = dot(ray.Direction, Rainbow->Antisolar_Vector);

                if(dot1 >= 0.0)
                {
                    // Get index ([0;1]) into rainbow's colour map.
                    index = (acos(dot1) - Rainbow->Angle) / Rainbow->Width;

                    // Jitter index.
                    if(Rainbow->Jitter > 0.0)
                    {
                        index += (2.0 * Draw(ray.GetKey(), kDrawRainbow, rainbowIndex) - 1.0) * Rainbow->Jitter;
                        MarkGrain();
                    }

                    if((index >= 0.0) && (index <= 1.0 - EPSILON))
                    {
                        // Get colour from rainbow's colour map.
                        Temp = Vector3d(index, 0.0, 0.0);
                        Compute_Pigment(Cr, Rainbow->Pigment, Temp, &isect, &ray, threadData);
                        Cr.GetFT(CrFilter, CrTransm);

                        // Get fading value for falloff.
                        if((Rainbow->Falloff_Width > 0.0) && (angle > Rainbow->Falloff_Angle))
                        {
                            fade = (angle - Rainbow->Falloff_Angle) / Rainbow->Falloff_Width;
                            fade = (3.0 - 2.0 * fade) * fade * fade;
                        }
                        else
                            fade = 0.0;

                        // Get attenuation factor due to distance.
                        k = exp(-isect.Depth / Rainbow->Distance);

                        // Colour's transm value is used as minimum attenuation value.
                        k = max(k, fade * (1.0 - CrTransm) + CrTransm);

                        // Now interpolate the colours.
                        ki = 1.0 - k;

                        // Attenuate filter value.
                        f = CrFilter * ki;

                        CtColour += k * colour * ((1.0 - f) + f * Cr.colour()) + ki * Cr.colour();
                        CtFilter *= k * CrFilter;
                        CtTransm *= k * CrTransm;

                        n++;
                    }
                }
            }
        }
    }

    if(n > 0)
    {
        colour =  CtColour / n;
        transm *= CtTransm;
    }
}

bool Trace::TestShadow(const LightSource &lightsource, double& depth, Ray& light_source_ray, const Vector3d& p, MathColour& colour, const Vector2d* areaSample)
{
    ComputeOneLightRay(lightsource, depth, light_source_ray, p, colour);

    // There's no need to test for shadows if no light
    // is coming from the light source.
    //
    // Test for PURE zero, because we only want to skip this if we're out
    // of the range of a spot light or cylinder light.  Very dim lights
    // should not be ignored.

    if(colour.IsNearZero(EPSILON))
    {
        colour.Clear();
        return true;
    }

    // Test for shadows.
    if (qualityFlags.shadows && ((lightsource.Projected_Through_Object != nullptr) || (lightsource.Light_Type != FILL_LIGHT_SOURCE)))
    {
        TraceShadowRay(lightsource, depth, light_source_ray, p, colour, areaSample);

        if(colour.IsNearZero(EPSILON))
        {
            colour.Clear();
            return true;
        }
    }

    return false;
}

bool Trace::IsObjectInCSG(ConstObjectPtr object, ConstObjectPtr parent)
{
    bool found = false;

    if(object == parent)
        return true;

    if(parent->Type & IS_COMPOUND_OBJECT)
    {
        for(vector<ObjectPtr>::const_iterator Sib = (reinterpret_cast<const CSG *>(parent))->children.begin(); Sib != (reinterpret_cast<const CSG *>(parent))->children.end(); Sib++)
        {
            if(IsObjectInCSG(object, *Sib))
                found = true;
        }
    }

    return found;
}

// SSLT code by Sarah Tariq and Lawrence (Lorenzo) Ibarria

double Trace::ComputeFt(double cos_angle, double eta)
{
    double g = sqrt(Sqr(eta) + Sqr(cos_angle) - 1);
    double F = 0.5 * (Sqr(g - cos_angle) / Sqr(g + cos_angle));
    F = F * (1 + Sqr(cos_angle * (g + cos_angle) - 1) / Sqr(cos_angle * (g - cos_angle) + 1));

    return 1.0 - min(1.0,max(0.0,F));
}

void Trace::ComputeSurfaceTangents(const Vector3d& normal, Vector3d& u, Vector3d& v)
{
#if 1
    if (fabs(normal[0]) <= fabs(normal[1]) && fabs(normal[0]) <= fabs(normal[2]))
        // if x co-ordinate is smallest, creating a tangent in the yz plane is a piece of cake;
        // the following code is equivalent to u = cross(normal, Vector3d(1,0,0)).normalized();
        u = Vector3d(0, normal[2], -normal[1]).normalized();
    else if (fabs(normal[1]) <= fabs(normal[2]))
        // if y co-ordinate is smallest, creating a tangent in the xz plane is a piece of cake;
        // the following code is equivalent to u = cross(normal, Vector3d(0,1,0)).normalized();
        u = Vector3d(-normal[2], 0, normal[0]).normalized();
    else
        // if z co-ordinate is smallest, creating a tangent in the xy plane is a piece of cake;
        // the following code is equivalent to u = cross(normal, Vector3d(0,0,1)).normalized();
        u = Vector3d(normal[1], -normal[0], 0).normalized();
#else
    if (fabs(normal[0]) <= fabs(normal[1]) && fabs(normal[0]) <= fabs(normal[2]))
        // if x co-ordinate is smallest, creating a tangent in the yz plane is a piece of cake
        u = cross(normal, Vector3d(1,0,0)).normalized();
    else if (fabs(normal[1]) <= fabs(normal[0]) && fabs(normal[1]) <= fabs(normal[2]))
        // if y co-ordinate is smallest, creating a tangent in the xz plane is a piece of cake
        u = cross(normal, Vector3d(0,1,0)).normalized();
    else
        // if z co-ordinate is smallest, creating a tangent in the xy plane is a piece of cake
        u = cross(normal, Vector3d(0,0,1)).normalized();
#endif

    v = cross(normal, u);
}

void Trace::ComputeSSLTNormal(Intersection& Ray_Intersection)
{
    Vector3d Raw_Normal;

    /* Get the normal to the surface */
    Ray_Intersection.Object->Normal(Raw_Normal, &Ray_Intersection, threadData);
    Ray_Intersection.INormal = Raw_Normal;
    Ray_Intersection.PNormal = Raw_Normal; // TODO FIXME - we should possibly take normal pertubation into account
}

bool Trace::IsSameSSLTObject(ConstObjectPtr obj1, ConstObjectPtr obj2)
{
    // TODO maybe use something smarter
    return (obj1 && obj2 && obj1->interior == obj2->interior);
}

void Trace::ComputeDiffuseSampleBase(Vector3d& basePoint, const Intersection& out, const Vector3d& vOut, double avgFreeDist, TraceTicket& ticket)
{
    Vector3d pOut = out.IPoint;
    Vector3d nOut = out.INormal;

    // make sure to get the normal right; obviously, the observer must be "outside".
    // for algorithm simplicity, we want the normal to point inward
    double cos_phi = dot(nOut, vOut);
    if (cos_phi > 0)
        nOut.invert();

    // typically, place the base point the average free distance below the surface;
    // however, never place it closer to the "back side" than to the front
    Intersection backSide;
    Ray ray(ticket, pOut, nOut); // we're shooting from the surface, so SubsurfaceRay would do us no good (as it would potentially "re-discover" the current surface)
    backSide.Depth = avgFreeDist * 2; // max distance we're looking at
    bool found = FindIntersection(backSide, ray);
    if (found)
    {
        if (IsSameSSLTObject(out.Object, backSide.Object))
            basePoint = pOut + nOut * (backSide.Depth / 2);
        else
            basePoint = pOut + nOut * min(avgFreeDist, backSide.Depth - EPSILON);
    }
    else
        basePoint = pOut + nOut * avgFreeDist;
}

void Trace::ComputeDiffuseSamplePoint(const Vector3d& basePoint, ObjectPtr object, Intersection& in, double& sampleArea, TraceTicket& ticket,
                                      std::uint64_t key, int sample)
{
    // A shading point's samples take successive directions of the pool, from a place its key sets.
    // TODO FIXME - a suitably weighted distribution (oriented according to the surface normal) would possibly be better
    Vector3d v = (*ssltUniformDirections)[(DeriveKey(key, kDrawSubsurface, 0) + sample) % kSubsurfaceDirectionPool];

    Ray ray(ticket, basePoint, v, Ray::SubsurfaceRay);
    bool found = FindIntersection(object, in, ray);

    if (found)
    {
        ComputeSSLTNormal(in);

        Vector3d vDelta = in.IPoint - basePoint;
        double dist = vDelta.length();
        double cos_phi = abs(dot(vDelta / dist, in.INormal));
        if (cos_phi < 0)
        {
            in.INormal.invert();
            cos_phi = -cos_phi;
        }
        if (cos_phi < 0.01)
            cos_phi = 0.01; // TODO FIXME - rather arbitrary limit
        sampleArea = 4.0 * M_PI * Sqr(dist * sceneData->mmPerUnit) / cos_phi;
    }
    else
    {
        sampleArea = 0.0;
    }
}

// Point-cloud method; see doc/PERF.md. Diffusion lengths (1/sigma_tr) summed around a shading point; cell side over the
// finest point spacing; radii, in point spacings, of the core lit as the exit point, of the disc and of the ring.
static const double kCloudReach = 8.0;
static const double kCloudCellSpacings = 128.0;
static const double kCloudCore = 0.5;
static const double kCloudDisc = 1.5;
static const double kCloudRing = 4.0;
// Area-light points per cloud point; least cosine between the exit normal and a disc point's, a ring point's, and in
// a group summed as one; spacings within which a point seen from behind hands over to method 1.
static const int kCloudAreaPoints = 4;
static const double kCloudFlat = 0.5;
static const double kCloudFold = 0.0;
static const double kCloudCone = 0.5;
static const double kCloudCrease = 2.5;
// Most ring area, over a flat ring's, for a flat ring; visibilities this close agree.
static const double kCloudDense = 1.33;
static const float kCloudAgree = 0.02f;
// Coarse clouds (doc/PERF.md): spacing over the larger of a pixel and z_r; how far an exit point's shadow may stray.
static const double kCloudCoarse = 4.0;
static const float kCloudStray = 0.25f;
// Shares of the ring's window above which the surface is whole, and below which an edge is corrected for in full.
static const double kCloudWhole = 0.95, kCloudEdge = 0.85;

static double CloudWindow(double distSqr, double radiusSqr)
{
    return (distSqr < radiusSqr) ? Sqr(1.0 - distSqr / radiusSqr) : 0.0;
}

// The share of the window (1 - r^2/R^2)^2 over a plane inside a straight edge t window radii from its centre.
static double CloudWindowShare(double t)
{
    t = min(1.0, max(0.0, t));
    return 0.5 + 16.0 / (5.0 * M_PI) * (t / 48.0 * (8.0 * Sqr(Sqr(t)) - 26.0 * Sqr(t) + 33.0) * sqrt(1.0 - Sqr(t)) + 5.0 / 16.0 * asin(t));
}

// How far from the window's centre, in window radii, the centroid of that share lies.
static double CloudWindowOffset(double t)
{
    t = min(1.0, max(0.0, t));
    return 16.0 / 105.0 * pow(1.0 - Sqr(t), 3.5) / (CloudWindowShare(t) * M_PI / 3.0);
}

// The edge distance t that leaves the given share of the window, or puts its centroid at the given offset.
static double CloudEdgeDistance(double share, bool byOffset = false)
{
    double lo = 0.0, hi = 1.0;
    for (int i = 0; i < 24; i++)
    {
        double mid = 0.5 * (lo + hi);
        ((byOffset ? (CloudWindowOffset(mid) > share) : (CloudWindowShare(mid) < share)) ? lo : hi) = mid;
    }
    return 0.5 * (lo + hi);
}

static MathColour CloudColour(const float *v)
{
    MathColour colour;
    for (int j = 0; j < MathColour::channels; j++)
        colour[j] = v[j];
    return colour;
}

// One point of an area light per shadow ray; a low-discrepancy (R2) sequence spreads successive rays over the light.
static Vector2d SubsurfaceAreaSample(const Vector2d& shift, int index)
{
    const double su = shift[U] + index * 0.7548776662466927;
    const double sv = shift[V] + index * 0.5698402909980532;
    return Vector2d(su - floor(su), sv - floor(sv));
}

// Shadow rays per sample for each light that reaches any sample, and the share of them split evenly between lights.
static const double kSubsurfaceShadowBudget = 1.0;
static const double kSubsurfaceEvenShare = 0.5;

// Unshadowed light a diffuse sample point receives from one source; rd holds its profile, exit transmittance and weight.
void Trace::ComputeDiffuseCandidate(const LightSource& lightsource, const Intersection& in, const PreciseMathColour& rd, double eta,
                                    SubsurfaceCandidate& candidate, TraceTicket& ticket)
{
    Ray lightsourceray(ticket);
    double lightsourcedepth;
    MathColour lightcolour;
    ComputeOneLightRay(lightsource, lightsourcedepth, lightsourceray, in.IPoint, lightcolour, true);

    // Don't calculate spotlights when outside of the light's cone.
    if(lightcolour.IsNearZero(EPSILON))
        return;

    // Light reaches the point from inside the object, so either side of the surface may face it.
    double cos_in = fabs(dot(in.INormal, lightsourceray.Direction));
    // [CLi] light coming in almost parallel to the surface is a problem though
    if(cos_in < EPSILON)
        return;

    // POV-Ray's light intensities already imply the 1/pi factor, so it is left out here.
    double ft = cos_in * ComputeFt(min(cos_in, 1.0), eta);
    for (int j = 0; j < MathColour::channels; j++)
        candidate.factor[j] = ft * rd[j];
    candidate.SetLight(in.IPoint, lightcolour);
}

// Unshadowed single-scattered light from one source at bend_point; weightOut holds each channel's sampling weight.
void Trace::ComputeSingleScatteringCandidate(const LightSource& lightsource, const Intersection& out, const PreciseMathColour& sigma_t_xo, const PreciseMathColour& sigma_s,
                                             const PreciseMathColour& weightOut, double eta, const Vector3d& bend_point, double ftOut, double cos_out_prime,
                                             SubsurfaceCandidate& candidate, TraceTicket& ticket)
{
    // Do Light source to get the correct lightsourceray
    // (note that for now we're mainly interested in the direction)
    Ray lightsourceray(ticket, Ray::SubsurfaceRay);
    double lightsourcedepth;
    ComputeOneWhiteLightRay(lightsource, lightsourcedepth, lightsourceray, bend_point);

    // Where light from the source enters the object, ignoring refraction; mostly for the surface normal there.
    Intersection xi;
    if (!FindIntersection(SubsurfaceObject(out), xi, lightsourceray))
        return;

    if (!IsSameSSLTObject(xi.Object, out.Object))
        return; // TODO - what if the other object is transparent?

    ComputeSSLTNormal(xi);

    // Get a colour and a ray (also recomputes all the lightsourceray stuff).
    MathColour lightcolour;
    ComputeOneLightRay(lightsource, lightsourcedepth, lightsourceray, xi.IPoint, lightcolour, true);

    // Don't calculate spotlights when outside of the light's cone.
    if(lightcolour.IsNearZero(EPSILON))
        return;

    // Light reaches the point from inside the object, so either side of the surface may face it.
    double cos_in = fabs(dot(xi.INormal, lightsourceray.Direction));
    // [CLi] light coming in almost parallel to the surface is a problem though
    if(cos_in < EPSILON)
        return;

    double cos_in_prime_sqr = 1 - (1 - Sqr(cos_in)) / Sqr(eta);
    if (cos_in_prime_sqr < 0.0)
        return; // total reflection
    double cos_in_prime = sqrt(cos_in_prime_sqr);
    if (cos_in_prime <= EPSILON)
        return; // close enough to total reflection to give us trouble

    double si = (bend_point - xi.IPoint).length() * sceneData->mmPerUnit;
    double s_prime_i = si * cos_in / cos_in_prime;
    double F = ComputeFt(min(cos_in, 1.0), eta) * ftOut;

    // G is only valid for comparatively flat surfaces; sigma_t where the light enters is taken to be that at the exit.
    double G = fabs(cos_out_prime / cos_in_prime);

    // Isotropic phase function, less the 1/pi that POV-Ray's light intensities already imply.
    const double p = 1.0 / 4.0;

    for (int j = 0; j < MathColour::channels; j++)
    {
        double sigma_tc = sigma_t_xo[j] * (1.0 + G);
        double f = (sigma_s[j] * F * p / sigma_tc) * exp(-s_prime_i * sigma_t_xo[j]) * weightOut[j] * cos_in;
        POV_SUBSURFACE_ASSERT((f >= 0.0) && (f <= DBL_MAX)); // verify f is a non-negative, finite value (no #INF, no #IND, no #NAN)
        candidate.factor[j] = min(f, double(FLT_MAX));
    }
    candidate.SetLight(xi.IPoint, lightcolour);
    // Light leaves the object toward its source here, so the outward normal faces the source.
    candidate.normal = (dot(xi.INormal, lightsourceray.Direction) >= 0.0) ? xi.INormal : -xi.INormal;
}

void Trace::SubsurfaceCandidate::SetLight(const Vector3d& p, const MathColour& lightcolour)
{
    point = p;
    unshadowed = lightcolour;
    bound = 0.0;
    for (int j = 0; j < MathColour::channels; j++)
        bound = max(bound, fabs(double(lightcolour[j] * factor[j]))); // magnitude: negative lights count too
}

// Estimates the shadowed sum of one light's candidates from budget shadow rays, drawn systematically in proportion to their bounds.
MathColour Trace::DrawSubsurfaceShadows(const LightSource& lightsource, const SubsurfaceCandidate* candidates, int count, double sum, int budget, TraceTicket& ticket,
                                        std::uint64_t key)
{
    MathColour estimate;
    const std::uint64_t lightKey = DeriveKey(key, kDrawShadow, LightSlot(lightsource));
    double step = sum / budget;
    double next = Draw(lightKey, kDrawSubsurface, 0) * step;
    double end = 0.0;
    int drawn = 0;
    Vector2d shift(Draw(lightKey, kDrawSubsurface, 1), Draw(lightKey, kDrawSubsurface, 2));
    for (int i = 0; (i < count) && (drawn < budget); i++)
    {
        const SubsurfaceCandidate& c = candidates[i];
        end += c.bound;
        int hits = 0;
        for (; (drawn + hits < budget) && (next < end); next += step)
            hits++;
        if (hits == 0)
            continue;

        // A point light's ray is the same each time; an area light gets a new point of the light per draw.
        int rays = (lightsource.Area_Light && qualityFlags.areaLights) ? hits : 1;
        MathColour lit;
        for (int r = 0; r < rays; r++)
        {
            Ray lightsourceray(ticket);
            lightsourceray.SetKey(DeriveKey(lightKey, kDrawSubsurface, 3 + drawn + r));
            double lightsourcedepth;
            MathColour lightcolour;
            ComputeOneLightRay(lightsource, lightsourcedepth, lightsourceray, c.point, lightcolour, true);
            Vector2d areaSample = SubsurfaceAreaSample(shift, drawn + r);
            TraceShadowRay(lightsource, lightsourcedepth, lightsourceray, c.point, lightcolour, &areaSample);
            lit += lightcolour;
        }
        drawn += hits;
        estimate += lit * c.factor * float((hits / double(rays)) * step / c.bound);
    }
    return estimate;
}

// Adds the shadowed light of the candidates, count per light in lights' order; see doc/PERF.md.
void Trace::ShadeSubsurfaceCandidates(const std::vector<const LightSource*>& lights, const SubsurfaceCandidate* candidates, int count, MathColour& total, TraceTicket& ticket,
                                      std::uint64_t key)
{
    std::vector<double> sums(lights.size(), 0.0);
    double sum = 0.0;
    int lit = 0;
    for (int l = 0; l < lights.size(); l++)
    {
        const LightSource& lightsource = *lights[l];
        bool shadows = qualityFlags.shadows && ((lightsource.Projected_Through_Object != nullptr) || (lightsource.Light_Type != FILL_LIGHT_SOURCE));
        for (int i = l * count; i < (l + 1) * count; i++)
        {
            if (shadows)
                sums[l] += candidates[i].bound;
            else if (candidates[i].bound > 0.0)
                total += candidates[i].unshadowed * candidates[i].factor;
        }
        sum += sums[l];
        lit += (sums[l] > 0.0);
    }
    if (!(sum > 0.0))
        return;

    double budgetAll = count * kSubsurfaceShadowBudget * lit;
    for (int l = 0; l < lights.size(); l++)
    {
        if (!(sums[l] > 0.0))
            continue;
        double share = kSubsurfaceEvenShare / lit + (1.0 - kSubsurfaceEvenShare) * sums[l] / sum;
        int budget = max(1, int(budgetAll * share + 0.5));
        total += DrawSubsurfaceShadows(*lights[l], &candidates[l * count], count, sums[l], budget, ticket, key);
    }
}

// The whole object a subsurface intersection belongs to: its outermost CSG, if any.
ObjectPtr Trace::SubsurfaceObject(const Intersection& isect)
{
    return (isect.Csg != nullptr) ? isect.Csg : isect.Object;
}

// The global lights unless the object turns them off, then those of its light group.
void Trace::CollectSubsurfaceLights(ConstObjectPtr object, std::vector<const LightSource*>& lights)
{
    lights.clear();
    if((object->Flags & NO_GLOBAL_LIGHTS_FLAG) != NO_GLOBAL_LIGHTS_FLAG)
        for(int i = 0; i < threadData->lightSources.size(); i++)
            lights.push_back(threadData->lightSources[i]);
    for(int i = 0; i < object->LLights.size(); i++)
        lights.push_back(object->LLights[i]);
}

// Lo is the sum over samples; one bend point per sample serves every channel, drawn from their mixture; see doc/PERF.md.
void Trace::ComputeSingleScatteringContribution(const Intersection& out, double dist, double ftOut, double cos_out_prime, const Vector3d& refractedREye,
                                                const PreciseMathColour& sigma_t_xo, const PreciseMathColour& sigma_s, int numSamples, MathColour& Lo, double eta,
                                                const std::vector<const LightSource*>& lights, const SubsurfaceCloud* cloud, const SubsurfaceFlesh& flesh,
                                                TraceTicket& ticket, std::uint64_t key)
{
    Lo.Clear();
    if (lights.empty())
        return;

    // Where the cloud found the surface flat and whole, a light in front that the disc agrees on needs no samples: its
    // path in is G times the path out, which gives the sampled estimate's expectation in closed form.
    std::vector<char>& closed = ssltScratchClosed;
    closed.assign(lights.size(), 0);
    int sampled = int(lights.size());
    if (cloud != nullptr)
    {
        Vector3d n = out.INormal.normalized();
        if (dot(n, refractedREye) > 0.0)
            n.invert();
        for (int l = 0; l < lights.size(); l++)
        {
            double cos_in = dot(cloud->exitDirection[l], n);
            bool dark = (cloud->exitDirection[l].lengthSqr() == 0.0);
            if (!cloud->exitAgreed[l] || cloud->edge || (!dark && (cos_in < EPSILON)))
                continue;
            closed[l] = 1;
            sampled--;
            if (dark)
                continue;
            double cos_in_prime_sqr = 1.0 - (1.0 - Sqr(cos_in)) / Sqr(eta);
            if (cos_in_prime_sqr <= Sqr(EPSILON))
                continue;
            double G = fabs(cos_out_prime / sqrt(cos_in_prime_sqr));
            double F = ComputeFt(min(cos_in, 1.0), eta) * ftOut;
            MathColour f;
            for (int j = 0; j < MathColour::channels; j++)
            {
                double depth = (dist < HUGE_VAL) ? -expm1(-sigma_t_xo[j] * (2.0 + G) * dist) : 1.0;
                f[j] = sigma_s[j] * F * 0.25 * cos_in / (sigma_t_xo[j] * (1.0 + G)) * depth / (2.0 + G);
            }
            if (flesh.skin != nullptr)
                f *= flesh.exitSkin;
            Lo += cloud->exitLight[l] * f * float(numSamples);
        }
    }
    if (sampled == 0)
        return;

    std::vector<SubsurfaceCandidate> candidates(lights.size() * numSamples);
    int channel = min(int(Draw(key, kDrawSubsurface, 0) * MathColour::channels), MathColour::channels - 1);

    for (int i = 0; i < numSamples; i++, channel = (channel + 1) % MathColour::channels)
    {
        double epsilon = 1.0 - Draw(key, kDrawSubsurface, i + 1); // in (0,1]
        double s_prime_out = fabs(log(epsilon)) / sigma_t_xo[channel];

        if (s_prime_out >= dist)
            continue; // not within the object - this is covered by a "zero scattering" term instead

        PreciseMathColour pdf;
        double pdfMix = 0.0;
        for (int j = 0; j < MathColour::channels; j++)
        {
            pdf[j] = sigma_t_xo[j] * exp(-s_prime_out * sigma_t_xo[j]);
            pdfMix += pdf[j] / MathColour::channels;
        }
        if (!(pdfMix > 0.0))
            continue;

        // Each channel's own sampling density over the mixture's, times its attenuation on the way out.
        PreciseMathColour weightOut;
        for (int j = 0; j < MathColour::channels; j++)
            weightOut[j] = pdf[j] / pdfMix * exp(-s_prime_out * sigma_t_xo[j]);

        Vector3d bend_point = out.IPoint + refractedREye * (s_prime_out / sceneData->mmPerUnit);

        for (int l = 0; l < lights.size(); l++)
            if (!closed[l])
                ComputeSingleScatteringCandidate(*lights[l], out, sigma_t_xo, sigma_s, weightOut, eta, bend_point, ftOut, cos_out_prime,
                                                 candidates[l * numSamples + i], ticket);
    }

    // Light entering elsewhere crosses the skin there.
    if (flesh.skin != nullptr)
        for (SubsurfaceCandidate& c : candidates)
            if (c.bound > 0.0)
            {
                c.factor *= ComputeSubsurfaceSkin(flesh, c.point);
                c.SetLight(c.point, c.unshadowed);
            }

    if ((cloud != nullptr) && !cloud->local)
    {
        // Light entering among cloud points that agree on a light's shadow is shadowed as they are; elsewhere by rays.
        SubsurfaceVisibility& visibility = ssltScratchVisibility;
        for (int l = 0; l < lights.size(); l++)
        {
            for (int i = 0; i < numSamples; i++)
            {
                SubsurfaceCandidate& c = candidates[l * numSamples + i];
                if (!(c.bound > 0.0) || !LookupSubsurfaceVisibility(*cloud, c.point, c.normal, visibility) || !visibility.Agrees(l))
                    continue;
                Lo += c.unshadowed * CloudColour(&visibility.mean[l * MathColour::channels]) * c.factor;
                c.bound = 0.0;
            }
        }
    }
    ShadeSubsurfaceCandidates(lights, candidates.data(), numSamples, Lo, ticket, key);

    // TODO FIXME - radiosity should also be taken into account
}

bool Trace::SSLTComputeRefractedDirection(const Vector3d& v, const Vector3d& n, double eta, Vector3d& refracted)
{
    // Phi: angle between normal and -incoming_ray (REye in this case, since it points to Eye)
    // Theta: angle between -normal and outgoing_ray
    double          cosPhi;
    Vector3d        unitV = v.normalized();
    Vector3d        unitN = n.normalized();

    cosPhi = dot(unitV, unitN);
    if (cosPhi > 0)
        unitN.invert();
    else
        cosPhi = -cosPhi;

    double cosThetaSqr = 1.0 + Sqr(eta) * (Sqr(cosPhi) - 1.0);
    if (cosThetaSqr < 0.0)
        return false;

    double cosTheta = sqrt(cosThetaSqr);
    refracted = (unitV * eta + unitN * (eta * cosPhi - cosTheta)).normalized();

    return true;
}

void Trace::SubsurfaceVisibility::Reset(size_t lights)
{
    size_t n = lights * MathColour::channels;
    lo.assign(n, FLT_MAX);
    mean.assign(n, 0.0f);
    hi.assign(n, -FLT_MAX);
    count = 0;
}

void Trace::SubsurfaceVisibility::Add(const float *visibility)
{
    for (size_t k = 0; k < mean.size(); k++)
    {
        lo[k] = min(lo[k], visibility[k]);
        hi[k] = max(hi[k], visibility[k]);
        mean[k] += visibility[k];
    }
    count++;
}

void Trace::SubsurfaceVisibility::Finish()
{
    if (count > 0)
        for (float& v : mean)
            v /= float(count);
}

bool Trace::SubsurfaceVisibility::Agrees(int light) const
{
    if (count == 0)
        return false;
    for (int k = light * MathColour::channels; k < (light + 1) * MathColour::channels; k++)
        if (hi[k] - lo[k] > kCloudAgree)
            return false;
    return true;
}

// Light entering the surface at point: each light's colour, shadow, cosine and Fresnel transmittance; see doc/PERF.md.
MathColour Trace::ComputeSubsurfaceIrradiance(const Vector3d& point, const Vector3d& normal, const std::vector<const LightSource*>& lights, double eta,
                                             int areaPoints, const Vector2d* areaShift, float* visibility, TraceTicket& ticket, std::uint64_t key)
{
    MathColour irradiance;
    for (int l = 0; l < lights.size(); l++)
    {
        const LightSource& lightsource = *lights[l];
        bool sampled = (areaShift != nullptr) && lightsource.Area_Light && qualityFlags.areaLights;
        int points = sampled ? areaPoints : 1;
        MathColour lit, shadowed, unshadowed;
        for (int k = 0; k < points; k++)
        {
            Ray lightsourceray(ticket);
            lightsourceray.SetKey(DeriveKey(key, kDrawSubsurface, k));
            double lightsourcedepth;
            MathColour lightcolour;
            ComputeOneLightRay(lightsource, lightsourcedepth, lightsourceray, point, lightcolour, true);
            if (lightcolour.IsNearZero(EPSILON))
                break;
            unshadowed += lightcolour;
            if (qualityFlags.shadows && ((lightsource.Projected_Through_Object != nullptr) || (lightsource.Light_Type != FILL_LIGHT_SOURCE)))
            {
                Vector2d areaSample = sampled ? SubsurfaceAreaSample(*areaShift, k * 7 + l) : Vector2d();
                TraceShadowRay(lightsource, lightsourcedepth, lightsourceray, point, lightcolour, sampled ? &areaSample : nullptr);
            }
            shadowed += lightcolour;
            double cos_in = fabs(dot(normal, lightsourceray.Direction));
            if (cos_in >= EPSILON)
                lit += lightcolour * float(cos_in * ComputeFt(min(cos_in, 1.0), eta));
        }
        irradiance += lit / float(points);
        if (visibility != nullptr)
            for (int j = 0; j < MathColour::channels; j++)
                visibility[l * MathColour::channels + j] = (fabs(unshadowed[j]) > EPSILON) ? shadowed[j] / unshadowed[j] : 1.0f;
    }
    return irradiance;
}

// Light entering at the exit point, shadowed as the disc's points agree or else by its own shadow test; keeps each light's
// shadowed colour and direction, and whether its shadow holds around the point, for single scattering; see doc/PERF.md.
MathColour Trace::ComputeCloudExitIrradiance(const Intersection& out, const SubsurfaceVisibility& disc, SubsurfaceCloud& cloud, bool coarse,
                                             TraceTicket& ticket, std::uint64_t key)
{
    MathColour irradiance;
    size_t count = cloud.lights.size();
    cloud.exitMismatch = false;
    cloud.exitLight.assign(count, MathColour());
    cloud.exitDirection.assign(count, Vector3d());
    cloud.exitAgreed.assign(count, 1);
    for (int l = 0; l < count; l++)
    {
        const LightSource& lightsource = *cloud.lights[l];
        Ray lightsourceray(ticket);
        double lightsourcedepth;
        MathColour lightcolour;
        ComputeOneLightRay(lightsource, lightsourcedepth, lightsourceray, out.IPoint, lightcolour, true);
        if (lightcolour.IsNearZero(EPSILON))
            continue;
        cloud.exitDirection[l] = lightsourceray.Direction;
        if (qualityFlags.shadows && ((lightsource.Projected_Through_Object != nullptr) || (lightsource.Light_Type != FILL_LIGHT_SOURCE)))
        {
            if (disc.Agrees(l) && !coarse)
                lightcolour *= CloudColour(&disc.mean[l * MathColour::channels]);
            else
            {
                MathColour unshadowed = lightcolour;
                lightsourceray.SetKey(key);
                TraceShadowRay(lightsource, lightsourcedepth, lightsourceray, out.IPoint, lightcolour, nullptr);
                cloud.exitAgreed[l] = cloud.local;
                for (int j = 0; coarse && (j < MathColour::channels); j++)
                    if ((fabs(unshadowed[j]) > EPSILON) && (fabs(lightcolour[j] / unshadowed[j] - disc.mean[l * MathColour::channels + j]) > kCloudStray))
                        cloud.exitMismatch = true;
            }
        }
        cloud.exitLight[l] = lightcolour;
        double cos_in = fabs(dot(out.INormal, lightsourceray.Direction));
        if (cos_in >= EPSILON)
            irradiance += lightcolour * float(cos_in * ComputeFt(min(cos_in, 1.0), cloud.eta));
    }
    return irradiance;
}

// The radiosity cache's light entering at the exit point (normal n, pointing out) for a cloud that carries it.
MathColour Trace::ComputeCloudExitAmbient(const Intersection& out, const Vector3d& n, const SubsurfaceCloud& cloud, TraceTicket& ticket)
{
    MathColour ambient;
    if (cloud.radiosity)
    {
        radiosity.ComputeAmbient(out.IPoint, n, n, 1.0, ambient, 1.0, ticket);
        ambient *= float(ComputeFt(1.0, cloud.eta));
    }
    return ambient;
}

static Vector3d SubsurfacePhotonNormal(Intersection& hit, TraceThreadData* data)
{
    Vector3d normal;
    if (const Mesh* mesh = dynamic_cast<const Mesh*>(hit.Object))
    {
        normal = mesh->Face_Normal(static_cast<const MESH_TRIANGLE*>(hit.Pointer));
        if (mesh->Trans != nullptr)
            MTransNormal(normal, normal, mesh->Trans);
    }
    else
        hit.Object->Normal(normal, &hit, data);
    return normal;
}

bool Trace::SubsurfacePhotonsEnabled(ConstObjectPtr receiver) const
{
    return sceneData->photonSettings.photonsEnabled && (sceneData->photonSettings.maxGatherCount > 0) &&
           (sceneData->surfacePhotonMap.numPhotons > 0) && !Test_Flag(receiver, PH_IGNORE_PHOTONS_FLAG);
}

bool Trace::UniformSubsurfacePhotonReceiver(ConstObjectPtr receiver, ConstObjectPtr root) const
{
    if ((Test_Flag(receiver, PH_IGNORE_PHOTONS_FLAG) != Test_Flag(root, PH_IGNORE_PHOTONS_FLAG)) ||
        (Test_Flag(receiver, NO_GLOBAL_LIGHTS_FLAG) != Test_Flag(root, NO_GLOBAL_LIGHTS_FLAG)) || (receiver->LLights != root->LLights))
        return false;
    if (receiver->interior != root->interior)
        return false;
    if (const Mesh* mesh = dynamic_cast<const Mesh*>(receiver))
        if (mesh->Data->Normals != nullptr)
            return false;
    if (receiver->Type & IS_COMPOUND_OBJECT)
        for (ObjectPtr child : static_cast<const CSG*>(receiver)->children)
            if (!UniformSubsurfacePhotonReceiver(child, root))
                return false;
    return true;
}

struct SubsurfacePhotonBoundaryProbe final
{
    bool& state;
    bool saved;
    explicit SubsurfacePhotonBoundaryProbe(bool& value) : state(value), saved(value) { state = true; }
    ~SubsurfacePhotonBoundaryProbe() { state = saved; }
};

// An isosurface's roots are only as exact as its accuracy, so a deposit and a later hit on it can differ by a few times that.
static double SubsurfaceRootTolerance(ConstObjectPtr object)
{
    double tolerance = 0.0;
    if (const IsoSurface* iso = dynamic_cast<const IsoSurface*>(object))
        tolerance = 4.0 * iso->accuracy;
    else if (object->Type & IS_COMPOUND_OBJECT)
        for (ObjectPtr child : static_cast<const CSG*>(object)->children)
            tolerance = max(tolerance, SubsurfaceRootTolerance(child));
    return tolerance;
}

static double SubsurfacePhotonTolerance(const Vector3d& location, double rootTolerance)
{
    double scale = max(1.0, max(fabs(location[X]), max(fabs(location[Y]), fabs(location[Z]))));
    return max(max(1e-7, 2.0 * std::numeric_limits<PhotonScalar>::epsilon() * scale), rootTolerance);
}

static double SubsurfacePhotonProbe(ConstObjectPtr receiver, double tolerance, double radius)
{
    double extent = max(double(receiver->BBox.size[X]), max(double(receiver->BBox.size[Y]), double(receiver->BBox.size[Z])));
    return max(1e-3, max(4.0 * tolerance, max(0.05 * radius, min(extent, 1e6) * 1e-4)));
}

bool Trace::RecoverSubsurfacePhotonBoundary(const Vector3d& location, const Vector3d& n, ObjectPtr receiver, double radius,
                                             Vector3d& outward, TraceTicket& ticket)
{
    double tolerance = SubsurfacePhotonTolerance(location, SubsurfaceRootTolerance(receiver));
    if (!(tolerance < 0.01 * radius))
        return false;
    return ProbeSubsurfacePhotonBoundary(location, n, receiver, tolerance, SubsurfacePhotonProbe(receiver, tolerance, radius), outward, ticket);
}

bool Trace::ProbeSubsurfacePhotonBoundary(const Vector3d& location, const Vector3d& n, ObjectPtr receiver, double tolerance,
                                           double probe, Vector3d& outward, TraceTicket& ticket)
{
    double scale = max(1.0, max(fabs(location[X]), max(fabs(location[Y]), fabs(location[Z]))));
    SubsurfacePhotonBoundaryProbe probeState(threadData->subsurfacePhotonBoundaryProbe);
    // Recover the deposit boundary without changing the on-disk photon layout.
    Ray ray(ticket, location + n * probe, -n, Ray::SubsurfaceRay);
    IStack hits(stackPool);
    if (!receiver->All_Intersections(ray, hits, threadData))
        return false;
    Intersection deposit;
    int matches = 0;
    for (; !hits->empty(); hits->pop())
    {
        const Intersection& hit = hits->top();
        if ((hit.Depth > 0.0) && (hit.Depth < 2.0 * probe) && ((hit.IPoint - location).length() <= tolerance))
        {
            if ((matches == 1) && (hit.Object == deposit.Object) && dynamic_cast<const Mesh*>(hit.Object) &&
                ((hit.IPoint - deposit.IPoint).length() <= max(1e-10, 64.0 * std::numeric_limits<double>::epsilon() * scale)))
            {
                Intersection other = hit;
                Vector3d a = SubsurfacePhotonNormal(deposit, threadData), b = SubsurfacePhotonNormal(other, threadData);
                double lengths = a.length() * b.length();
                if ((lengths > 0.0) && (fabs(dot(a, b)) >= (1.0 - 1e-10) * lengths))
                    continue;
            }
            ++matches;
            deposit = hit;
        }
    }
    if ((matches != 1) || ((deposit.IPoint - location).length() > tolerance) ||
        !IsObjectInCSG(deposit.Object, receiver) || Test_Flag(deposit.Object, PH_IGNORE_PHOTONS_FLAG))
        return false;
    deposit.INormal = SubsurfacePhotonNormal(deposit, threadData);
    double depositLength = deposit.INormal.length();
    if (!(depositLength > 0.0) || !std::isfinite(depositLength))
        return false;
    outward = deposit.INormal / depositLength;
    double orientationProbe = 4.0 * tolerance;
    bool ahead = receiver->Inside(deposit.IPoint + outward * orientationProbe, threadData);
    bool behind = receiver->Inside(deposit.IPoint - outward * orientationProbe, threadData);
    if (ahead == behind)
        return false;
    if (ahead)
        outward.invert();
    return true;
}

// One answer per photon and receiver: find the surface along the photon's own incoming ray, then probe along its normal.
bool Trace::SubsurfacePhotonDeposit(const Photon& photon, const Vector3d& incoming, ObjectPtr receiver, double rootTolerance,
                                    Vector3d& outward, TraceTicket& ticket)
{
    SubsurfacePhotonBoundaries* table = threadData->subsurfaceCache ? &threadData->subsurfaceCache->photonBoundaries : nullptr;
    std::uint64_t key = 0;
    bool keyed = table && table->Key(sceneData->surfacePhotonMap, &photon, receiver, key);
    float folded[2];
    if (keyed && table->Find(key, folded))
        threadData->Stats()[Subsurface_Photon_Boundaries_Reused]++;
    else
    {
        threadData->Stats()[Subsurface_Photon_Boundaries_Computed]++;
        Vector3d location(photon.Loc);
        double tolerance = SubsurfacePhotonTolerance(location, rootTolerance);
        double probe = SubsurfacePhotonProbe(receiver, tolerance, sceneData->surfacePhotonMap.minGatherRad);
        Vector3d axis = incoming, found;
        {
            SubsurfacePhotonBoundaryProbe probeState(threadData->subsurfacePhotonBoundaryProbe);
            Ray ray(ticket, location + incoming * probe, -incoming, Ray::SubsurfaceRay);
            IStack hits(stackPool);
            Intersection nearest;
            double best = HUGE_VAL;
            if (receiver->All_Intersections(ray, hits, threadData))
                for (; !hits->empty(); hits->pop())
                    if ((hits->top().Depth > 0.0) && ((hits->top().IPoint - location).length() < best))
                    {
                        nearest = hits->top();
                        best = (nearest.IPoint - location).length();
                    }
            Vector3d surface = (best < HUGE_VAL) ? SubsurfacePhotonNormal(nearest, threadData) : Vector3d(0.0);
            double length = surface.length();
            if ((length > 0.0) && std::isfinite(length))
                axis = surface / length;
        }
        bool valid = ProbeSubsurfacePhotonBoundary(location, axis, receiver, tolerance, probe, found, ticket);
        SubsurfacePhotonBoundaries::Fold(found, valid, folded);
        if (keyed)
            table->Store(key, folded);
    }
    return SubsurfacePhotonBoundaries::Unfold(folded, outward);
}

MathColour Trace::ComputeSubsurfacePhotonIrradiance(const Vector3d& point, const Vector3d& normal, double eta, ObjectPtr receiver,
                                                  PhotonGatherer& gatherer, TraceTicket& ticket, bool cloud)
{
    MathColour irradiance;
    const PhotonMap& map = sceneData->surfacePhotonMap;
    if (!SubsurfacePhotonsEnabled(receiver) || !(eta > 0.0) || !std::isfinite(eta) ||
        !(map.minGatherRad > 0.0) || !std::isfinite(map.minGatherRad) || !(map.gatherRadStep >= 0.0) ||
        !std::isfinite(map.gatherRadStep) || (map.gatherNumSteps < 1))
        return irradiance;
    double length = normal.length();
    if (!(length > 0.0) || !std::isfinite(length))
        return irradiance;
    Vector3d n = normal / length;
    if (cloud && !RecoverSubsurfacePhotonBoundary(point, n, receiver, map.minGatherRad, n, ticket))
        return irradiance;
    threadData->Stats()[cloud ? Subsurface_Photon_Cloud_Gathers : Subsurface_Photon_Sample_Gathers]++;
    // Each entry needs a fresh gather; gathered is only a texture-layer reuse flag.
    double radius = gatherer.gatherPhotonsAdaptive(&point, &n, true);
    threadData->Stats()[Subsurface_Photon_Searches] += gatherer.adaptiveSearches;
    if (!(radius > 0.0) || !std::isfinite(radius))
        return irradiance;
    threadData->Stats()[Subsurface_Photon_Radius_Sum] += radius;
    threadData->Stats()[Subsurface_Photon_Radius_Squared_Sum] += Sqr(radius);
    threadData->Stats()[Subsurface_Photon_Candidates] += gatherer.gatheredPhotons.numFound;
    double rootTolerance = SubsurfaceRootTolerance(receiver);
    for (int i = 0; i < gatherer.gatheredPhotons.numFound; i++)
    {
        const Photon& photon = *gatherer.gatheredPhotons.photonGatherList[i];
        int theta = photon.theta + 127, phi = photon.phi + 127;
        Vector3d incoming(sinCosData.cosTheta[theta] * sinCosData.cosTheta[phi], sinCosData.sinTheta[theta],
                          sinCosData.cosTheta[theta] * sinCosData.sinTheta[phi]);
        double cosine = min(1.0, dot(n, incoming));
        if (!(cosine > EPSILON) || !(Sqr(eta) + Sqr(cosine) > 1.0))
            continue;
        Vector3d outward;
        if (!(SubsurfacePhotonTolerance(Vector3d(photon.Loc), rootTolerance) < 0.01 * radius) ||
            !SubsurfacePhotonDeposit(photon, incoming, receiver, rootTolerance, outward, ticket))
            continue;
        if ((dot(n, outward) < 0.5) || !(dot(outward, incoming) > EPSILON))
            continue;
        double ft = ComputeFt(cosine, eta);
        if (!std::isfinite(ft) || !(ft > 0.0))
            continue;
        MathColour flux = ToMathColour(RGBColour(photon.colour));
        for (int j = 0; j < MathColour::channels; j++)
            if (std::isfinite(flux[j]) && (flux[j] >= 0.0))
                irradiance[j] += flux[j] * ft;
        threadData->Stats()[Subsurface_Photon_Accepted]++;
    }
    // Flux density already contains projected area; rejected deposits leave the aperture unchanged.
    irradiance /= M_PI * Sqr(radius);
    for (int j = 0; j < MathColour::channels; j++)
        if (!std::isfinite(irradiance[j]))
            irradiance[j] = 0.0f;
    return irradiance;
}

// Projects dipole-pole discs onto the boundary visible from the existing interior sample base.
bool Trace::ComputeProjectedSubsurfacePhotons(const Intersection& out, const Vector3d& base, const SubsurfaceProfile& profile,
                                              const SubsurfaceFlesh& flesh, double ftOut, int samples, PhotonGatherer& gatherer,
                                              TraceTicket& ticket, std::uint64_t key, MathColour& diffuse)
{
    ObjectPtr receiver = SubsurfaceObject(out);
    if ((dynamic_cast<const Box*>(receiver) == nullptr) && (dynamic_cast<const Sphere*>(receiver) == nullptr) &&
        (dynamic_cast<const Mesh*>(receiver) == nullptr))
        return false;
    if (const Mesh* mesh = dynamic_cast<const Mesh*>(receiver))
        if (!mesh->has_inside_vector)
            return false;
    const BoundingBox& box = receiver->BBox;
    double scale = 1.0;
    double extent = 0.0;
    for (int a = 0; a < 3; a++)
    {
        if (!(box.size[a] > 0.0) || !(box.size[a] < BOUND_HUGE / 4) || !std::isfinite(box.lowerLeft[a]))
            return false;
        scale = max(scale, max(fabs(double(box.lowerLeft[a])), fabs(double(box.lowerLeft[a] + box.size[a]))));
        extent = max(extent, double(box.size[a]));
    }
    double tolerance = max(1e-10, 64.0 * std::numeric_limits<double>::epsilon() * scale);
    double padding = max(1e-7, 2.0 * std::numeric_limits<PhotonScalar>::epsilon() * scale);
    double originOffset = max(1e-3, max(4.0 * padding, 1e-4 * extent));
    double mm = sceneData->mmPerUnit;
    double heights[2 * MathColour::channels];
    double sigmas[2 * MathColour::channels], weights[2 * MathColour::channels];
    double weightSum = 0.0;
    int poles = 0;
    for (int j = 0; j < MathColour::channels; j++)
        for (double h : { profile.z_r[j] / mm, profile.z_v[j] / mm })
        {
            double sigma = profile.sigma_tr[j] * mm;
            double weight = profile.scale[j] * exp(-sigma * h);
            if ((h > 0.0) && std::isfinite(h) && (sigma >= 0.0) && std::isfinite(sigma) && (weight > 0.0) && std::isfinite(weight))
            {
                heights[poles] = h;
                sigmas[poles] = sigma;
                weights[poles++] = weight;
                weightSum += weight;
            }
        }
    if (poles == 0)
        return false;
    for (int p = 0; p < poles; p++)
        weights[p] /= weightSum;
    Intersection exit = out;
    Vector3d axes[3];
    axes[0] = SubsurfacePhotonNormal(exit, threadData);
    double length = axes[0].length();
    if (!(length > 0.0) || !std::isfinite(length))
        return false;
    axes[0] /= length;
    ComputeSurfaceTangents(axes[0], axes[1], axes[2]);
    double probabilities[3] = { 0.8, 0.1, 0.1 };
    int counts[3] = { samples, 0, 0 };
    if (samples >= 3)
    {
        counts[1] = counts[2] = max(1, samples / 10);
        counts[0] = samples - counts[1] - counts[2];
        for (int a = 0; a < 3; a++)
            probabilities[a] = double(counts[a]) / samples;
    }
    PreciseMathColour sum;
    std::vector<Intersection> crossings;
    std::uint64_t sampleKey = DeriveKey(key, kDrawSubsurface, 6);
    for (int i = 0; i < samples; i++)
    {
        double choice = Draw(sampleKey, kDrawSubsurface, 4 * i);
        int axis = (samples >= 3) ? ((i < counts[0]) ? 0 : (i < counts[0] + counts[1]) ? 1 : 2) : (choice < 0.8) ? 0 : (choice < 0.9) ? 1 : 2;
        int pole = 0;
        double mass = Draw(sampleKey, kDrawSubsurface, 4 * i + 1);
        for (; pole + 1 < poles && mass >= weights[pole]; pole++)
            mass -= weights[pole];
        double u = Draw(sampleKey, kDrawSubsurface, 4 * i + 2);
        double h = heights[pole], sigma = sigmas[pole];
        double t = u / (1.0 - u);
        if (sigma > 0.0)
        {
            double target = -std::log1p(-u);
            double a = sigma * h;
            t = target / (a + 1.0);
            for (int iteration = 0; iteration < 32; iteration++)
            {
                double error = a * t + std::log1p(t) - target;
                if (fabs(error) <= 1e-13 * (1.0 + target))
                    break;
                t = max(0.0, t - error / (a + 1.0 / (1.0 + t)));
            }
        }
        double r = h * sqrt(t * (t + 2.0));
        double phi = 2.0 * M_PI * Draw(sampleKey, kDrawSubsurface, 4 * i + 3);
        Vector3d point = out.IPoint + r * (axes[(axis + 1) % 3] * cos(phi) + axes[(axis + 2) % 3] * sin(phi));
        Vector3d dir = axes[axis];
        double nearDepth = -HUGE_VAL, farDepth = HUGE_VAL;
        for (int a = 0; a < 3; a++)
        {
            double lo = box.lowerLeft[a] - padding, hi = box.lowerLeft[a] + box.size[a] + padding;
            if (dir[a] == 0.0)
            {
                if ((point[a] < lo) || (point[a] > hi))
                    farDepth = -HUGE_VAL;
            }
            else
            {
                double a0 = (lo - point[a]) / dir[a], a1 = (hi - point[a]) / dir[a];
                nearDepth = max(nearDepth, min(a0, a1));
                farDepth = min(farDepth, max(a0, a1));
            }
        }
        if (!(nearDepth < farDepth) || !std::isfinite(nearDepth) || !std::isfinite(farDepth))
            continue;
        crossings.clear();
        Ray projection(ticket, point + dir * (nearDepth - originOffset), dir, Ray::SubsurfaceRay);
        IStack hits(stackPool);
        receiver->All_Intersections(projection, hits, threadData);
        for (; !hits->empty(); hits->pop())
            crossings.push_back(hits->top());
        std::sort(crossings.begin(), crossings.end(), [](const Intersection& a, const Intersection& b) { return a.Depth < b.Depth; });
        Vector3d previous;
        bool havePrevious = false;
        int entryIndex = 0;
        for (Intersection& in : crossings)
        {
            if (havePrevious && ((in.IPoint - previous).lengthSqr() <= Sqr(tolerance)))
                continue;
            previous = in.IPoint;
            havePrevious = true;
            if (!IsSameSSLTObject(in.Object, out.Object) || Test_Flag(in.Object, PH_IGNORE_PHOTONS_FLAG))
                continue;
            Vector3d delta = in.IPoint - base;
            double distance = delta.length();
            if (!(distance > tolerance))
                continue;
            Intersection visible;
            Ray ray(ticket, base, delta / distance, Ray::SubsurfaceRay);
            if (!FindIntersection(receiver, visible, ray, distance + 8.0 * tolerance) ||
                ((visible.IPoint - in.IPoint).lengthSqr() > Sqr(8.0 * tolerance)))
                continue;
            Vector3d normal = SubsurfacePhotonNormal(in, threadData);
            double normalLength = normal.length();
            if (!(normalLength > 0.0) || !std::isfinite(normalLength))
                continue;
            normal /= normalLength;
            if (dot(normal, delta) < 0.0)
                normal.invert();
            Vector3d offset = in.IPoint - out.IPoint;
            double distSqr = offset.lengthSqr(), density = 0.0;
            for (int a = 0; a < 3; a++)
            {
                double radialSqr = max(0.0, distSqr - Sqr(dot(offset, axes[a])));
                double disk = 0.0;
                for (int p = 0; p < poles; p++)
                {
                    double dSqr = radialSqr + Sqr(heights[p]);
                    double distance = sqrt(dSqr);
                    disk += weights[p] * heights[p] * (1.0 + sigmas[p] * distance) *
                            exp(-sigmas[p] * radialSqr / (distance + heights[p])) / (dSqr * distance);
                }
                density += probabilities[a] * fabs(dot(normal, axes[a])) * disk / (2.0 * M_PI);
            }
            if (!(density > 0.0) || !std::isfinite(density))
                continue;
            PreciseMathColour rd = profile.Rd(distSqr * Sqr(mm)) * (ftOut * Sqr(mm) / density);
            if (flesh.PerEntry())
            {
                SubsurfaceEntry entry;
                ComputeSubsurfaceEntry(flesh, in.IPoint, -normal, Draw(DeriveKey(sampleKey, kDrawSubsurface, i), kDrawSubsurface, entryIndex++), entry);
                rd *= PreciseMathColour(entry.factor);
            }
            MathColour photonEntry = ComputeSubsurfacePhotonIrradiance(in.IPoint, normal, out.Object->interior->IOR / sceneData->atmosphereIOR,
                                                                      receiver, gatherer, ticket, false);
            sum += rd * PreciseMathColour(photonEntry);
        }
    }
    diffuse = (samples > 0) ? MathColour(sum / double(samples)) : MathColour();
    return true;
}

struct SubsurfacePhotonReceiver final
{
    bool& state;
    bool saved;
    bool active;
    SubsurfacePhotonReceiver(bool& value, ConstObjectPtr receiver, bool enabled) : state(value), saved(value), active(enabled)
    {
        if (active)
            state = Test_Flag(receiver, PH_IGNORE_PHOTONS_FLAG);
    }
    ~SubsurfacePhotonReceiver() { if (active) state = saved; }
};

// A random number in [0, 1) from a 64-bit state (splitmix64).
static double CloudRandom(uint64_t& state)
{
    uint64_t z = (state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return double((z ^ (z >> 31)) >> 11) * (1.0 / 9007199254740992.0);
}

// A seed from a cell's place and a job's number, so a cell comes out the same whichever threads build it.
static uint64_t CloudSeed(const SubsurfaceCellKey& key, int stage, int job)
{
    return (uint64_t(uint32_t(key.x)) * 0x9E3779B97F4A7C15ull) ^ (uint64_t(uint32_t(key.y)) * 0xC2B2AE3D27D4EB4Full) ^
           (uint64_t(uint32_t(key.z)) * 0x165667B19E3779F9ull) ^ (uint64_t(uint32_t(key.sizeLevel)) * 0xD6E8FEB86659FD93ull) ^
           (uint64_t(stage) << 62) ^ (uint64_t(uint32_t(job)) * 0xD1B54A32D192ED03ull);
}

static const int kCloudPointsPerJob = 256;
// Points a cell may hold, and the finest spacing, in cell sides; past the first a cell is left to method 1. Least
// cosine between the view and an axis that sparser lines along it allow for.
static const size_t kCloudCellPoints = size_t(1) << 18;
static const double kCloudFinest = 1.0 / 1024.0;
static const double kCloudOblique = 1.0 / 4.0;
// Lines across an object's longest side at least, so that one small beside a long diffusion reach is not left a few points.
static const double kCloudObjectLines = 32.0;

// Points on the object inside one cell, where lines along the axes cross it, each lit by every light. The caller builds
// it; other threads that need it meanwhile help with its jobs.
void Trace::BuildSubsurfaceCell(const SubsurfaceCloud& cloud, const SubsurfaceCellKey& key, SubsurfaceCell& cell)
{
    try
    {
        double size = cloud.size;
        Vector3d lo = Vector3d(key.x, key.y, key.z) * size;
        Vector3d hi = lo + Vector3d(size);
        const BoundingBox& box = cloud.object->BBox;
        bool empty = false;
        for (int a = 0; a < 3; a++)
            empty = empty || (box.lowerLeft[a] > hi[a]) || (box.lowerLeft[a] + box.size[a] < lo[a]);

        // Points need be no closer than about a pixel where the cell's part of the object comes nearest the camera;
        // lines along an axis the camera looks across may be sparser, as surfaces facing it are seen obliquely.
        Vector3d toward;
        for (int a = 0; a < 3; a++)
        {
            double from = max(lo[a], double(box.lowerLeft[a])), to = min(hi[a], double(box.lowerLeft[a] + box.size[a]));
            toward[a] = max(0.0, max(from - ssltCameraLocation[a], ssltCameraLocation[a] - to));
        }
        double nearest = toward.length();
        double longest = 0.0;
        for (int a = 0; a < 3; a++)
            if (box.size[a] < BOUND_HUGE / 4)
                longest = max(longest, double(box.size[a]));
        {
            std::lock_guard<std::mutex> lock(cell.mutex);
            cell.jobs = 0;
            for (int a = 0; a < 3; a++)
            {
                double facing = ((nearest > 0.0) && (ssltPixelAngle > 0.0)) ? max(toward[a] / nearest, kCloudOblique) : 1.0;
                double pixel = (ssltPixelSize + nearest * ssltPixelAngle) / sqrt(facing);
                double step = max(size / kCloudCellSpacings, pixel) * sceneData->subsurfaceSpacing;
                if (longest > 0.0)
                    step = min(step, longest / kCloudObjectLines * sceneData->subsurfaceSpacing);
                cell.step[a] = max(step, size * kCloudFinest);
                cell.steps[a] = empty ? 0 : max(1, int(ceil(size / (cell.step[a] * sqrt(1.5)))));
                cell.jobs += cell.steps[a];
            }
            cell.rows.resize(cell.jobs);
        }
        cell.changed.notify_all();
        WorkOnSubsurfaceCell(cloud, key, cell, true);

        size_t count = cell.found;
        bool usable = !cell.failed && (cell.unoriented * 16 <= count);
        if (usable)
        {
            cell.points.reserve(count);
            for (std::vector<SubsurfacePoint>& row : cell.rows)
            {
                cell.points.insert(cell.points.end(), row.begin(), row.end());
                std::vector<SubsurfacePoint>().swap(row);
            }
            cell.lights = int(cloud.lights.size());
            cell.visibility.resize(count * cell.lights * MathColour::channels);
            if (key.medium != nullptr)
                cell.entry.assign(count * 2 * MathColour::channels, 0.0f);
            if (key.photons)
                cell.photonEntry.assign(count * MathColour::channels, 0.0f);
            if (key.radiosity)
                cell.ambient.assign(count * MathColour::channels, std::numeric_limits<float>::quiet_NaN());
            {
                std::lock_guard<std::mutex> lock(cell.mutex);
                cell.stage = SubsurfaceCell::kLighting;
                cell.jobs = int((count + kCloudPointsPerJob - 1) / kCloudPointsPerJob);
                cell.next = cell.done = 0;
            }
            cell.changed.notify_all();
            WorkOnSubsurfaceCell(cloud, key, cell, true);
            usable = !cell.failed;
            if (usable && key.radiosity)
                AddSubsurfaceAmbient(cloud, cell);
            if (usable)
                cell.BuildHierarchy();
        }
        FinishSubsurfaceCell(cell, usable);
    }
    catch (...)
    {
        FinishSubsurfaceCell(cell, false);
        throw;
    }
}

// Marks a cell ready, releasing the budget of one that cannot be used, and wakes the threads waiting on it.
void Trace::FinishSubsurfaceCell(SubsurfaceCell& cell, bool usable)
{
    {
        std::lock_guard<std::mutex> lock(cell.mutex);
        if (cell.stage == SubsurfaceCell::kReady)
            return;
        // Rows still being cast (by a thread the builder's exception left behind) stay until the cell goes.
        if (cell.done == cell.jobs)
            cell.rows.clear();
        cell.usable = usable;
        cell.stage = SubsurfaceCell::kReady;
        if (!usable)
        {
            threadData->subsurfaceCache->Release(cell.reserved);
            cell.reserved = 0;
        }
    }
    cell.changed.notify_all();
}

// Cell jobs run at the quality subsurface needs, whichever trace runs them (a radiosity gather's has no area lights).
struct CellQuality final
{
    QualityFlags& flags;
    QualityFlags saved;
    CellQuality(QualityFlags& f) : flags(f), saved(f) { flags = QualityFlags(9); }
    ~CellQuality() { flags = saved; }
};

// Takes jobs of a cell being built until none are left; then the builder returns once its stage is done, a helper once
// the cell is ready. A job that throws fails the cell.
void Trace::WorkOnSubsurfaceCell(const SubsurfaceCloud& cloud, const SubsurfaceCellKey& key, SubsurfaceCell& cell, bool builder)
{
    std::unique_lock<std::mutex> lock(cell.mutex);
    while (cell.stage != SubsurfaceCell::kReady)
    {
        if (cell.next < cell.jobs)
        {
            SubsurfaceCell::Stage stage = cell.stage;
            int job = cell.next++;
            lock.unlock();
            // A shared cell comes out the same whichever ray needed it first, so its jobs trace from a ticket of their own.
            TraceTicket jobTicket(sceneData->parsedMaxTraceLevel, sceneData->parsedAdcBailout);
            try
            {
                CellQuality quality(qualityFlags);
                if (!cell.failed && (stage == SubsurfaceCell::kCasting))
                    CastSubsurfaceLines(cloud, key, cell, job, jobTicket);
                else if (!cell.failed)
                    LightSubsurfacePoints(cloud, key, cell, job, jobTicket);
            }
            catch (...)
            {
                lock.lock();
                cell.failed = true;
                ++cell.done;
                lock.unlock();
                cell.changed.notify_all();
                throw;
            }
            lock.lock();
            if (++cell.done == cell.jobs)
                cell.changed.notify_all();
        }
        else if (builder && (cell.done == cell.jobs))
            return;
        else if (cell.changed.wait_for(lock, std::chrono::milliseconds(50)) == std::cv_status::timeout)
        {
            lock.unlock();
            cooperate();
            lock.lock();
        }
    }
}

// One row of lines along one axis through a jittered grid of step h_a: a patch with unit normal n meets those lines
// |n_a|/h_a^2 times per unit area, so each crossing stands for 1/sum(|n_a|/h_a^2) of surface.
void Trace::CastSubsurfaceLines(const SubsurfaceCloud& cloud, const SubsurfaceCellKey& key, SubsurfaceCell& cell, int job, TraceTicket& ticket)
{
    double size = cloud.size, mm = sceneData->mmPerUnit;
    int a = 0, i = job;
    while (i >= cell.steps[a])
        i -= cell.steps[a++];
    int steps = cell.steps[a], u = (a + 1) % 3, v = (a + 2) % 3;
    double h = size / steps;
    Vector3d lo = Vector3d(key.x, key.y, key.z) * size;
    double start = lo[a] - h;
    // Normals point out of the object where a step along them leaves it; a crossing that cannot tell is dropped. The
    // step shrinks to a quarter of the wall between a crossing and its neighbours, so thin walls are still told apart.
    const double probeMax = 0.05 * h;
    const double probeMin = max(1e-6, 1e-4 * h);
    const double depth = (cloud.flesh != nullptr) && (cloud.flesh->fleshAtDepth || cloud.flesh->emissionAtDepth) ? cloud.flesh->depth : 0.0;
    const BoundingBox& box = cloud.object->BBox;
    Vector3d dir(0.0);
    dir[a] = 1.0;
    uint64_t state = CloudSeed(key, 0, job);
    std::vector<SubsurfacePoint>& row = cell.rows[job];
    std::vector<Intersection> crossings;
    for (int j = 0; (j < steps) && !cell.failed; j++)
    {
        Vector3d origin;
        origin[a] = start;
        origin[u] = lo[u] + (i + CloudRandom(state)) * h;
        origin[v] = lo[v] + (j + CloudRandom(state)) * h;
        if ((origin[u] < box.lowerLeft[u]) || (origin[u] > box.lowerLeft[u] + box.size[u]) ||
            (origin[v] < box.lowerLeft[v]) || (origin[v] > box.lowerLeft[v] + box.size[v]))
            continue;
        crossings.clear();
        CollectCrossings(cloud.object, origin, dir, h, h + size, crossings, ticket);
        std::sort(crossings.begin(), crossings.end(), [](const Intersection& p, const Intersection& q) { return p.Depth < q.Depth; });
        size_t kept = 0, dropped = 0;
        for (size_t c = 0; c < crossings.size(); c++)
        {
            Intersection& in = crossings[c];
            if (!(in.Depth < h + size))
                continue;
            ComputeSSLTNormal(in);
            double length = in.INormal.length();
            if (!(length > 0.0) || !(length < HUGE_VAL))
                continue;
            Vector3d n = in.INormal / length;
            double gap = HUGE_VAL;
            if (c > 0)
                gap = min(gap, in.Depth - crossings[c - 1].Depth);
            if (c + 1 < crossings.size())
                gap = min(gap, crossings[c + 1].Depth - in.Depth);
            double probe = min(probeMax, 0.25 * gap * fabs(n[a]));
            bool ahead = (probe >= probeMin) && cloud.object->Inside(in.IPoint + n * probe, threadData);
            bool behind = (probe >= probeMin) && cloud.object->Inside(in.IPoint - n * probe, threadData);
            if (ahead == behind)
            {
                dropped++;
                continue;
            }
            if (ahead)
                n.invert();
            if ((depth > 0.0) && (n[a] < 0.0) && (c + 1 < crossings.size()))
            {
                threadData->Stats()[Subsurface_Walls]++;
                if ((crossings[c + 1].Depth - in.Depth) * fabs(n[a]) < depth)
                    threadData->Stats()[Subsurface_Thin_Walls]++;
            }
            SubsurfacePoint point;
            point.position = in.IPoint;
            for (int k = 0; k < 3; k++)
                point.normal[k] = float(n[k]);
            for (int k = 0; k < MathColour::channels; k++)
                point.irradiance[k] = 0.0f;
            double density = 0.0;
            for (int b = 0; b < 3; b++)
                density += fabs(n[b]) * Sqr(cell.steps[b] / size);
            point.area = float(Sqr(mm) / density);
            point.id = 0;
            point.axis = a;
            row.push_back(point);
            kept++;
        }
        if (kept + dropped == 0)
            continue;
        bool reserved = (kept == 0) || threadData->subsurfaceCache->Reserve(kept);
        std::lock_guard<std::mutex> lock(cell.mutex);
        if (reserved && (cell.stage == SubsurfaceCell::kReady))
            threadData->subsurfaceCache->Release(kept);
        else if (reserved)
            cell.reserved += kept;
        cell.found += kept;
        cell.unoriented += dropped;
        if (!reserved || (cell.found > kCloudCellPoints))
            cell.failed = true;
    }
}

// A draw for the depth of a point's flesh lookups: an R3 lattice over the grid squares, so that neighbouring points'
// depths spread evenly over the draw and a few of them together stand for all depths.
static double CloudDepthDraw(const SubsurfaceCellKey& key, const SubsurfaceCell& cell, const SubsurfacePoint& point, double size)
{
    static const double lattice[3] = { 0.8191725133961645, 0.6710436067037893, 0.5497004779019703 };
    uint64_t state = CloudSeed(key, 3, point.axis);
    double u = CloudRandom(state);
    for (int a = 0; a < 3; a++)
        u += lattice[a] * floor(point.position[a] * cell.steps[a] / size);
    return u - floor(u);
}

// The light entering one run of a cell's points, and their shadow from each light.
void Trace::LightSubsurfacePoints(const SubsurfaceCloud& cloud, const SubsurfaceCellKey& key, SubsurfaceCell& cell, int job, TraceTicket& ticket)
{
    int stride = cell.lights * MathColour::channels;
    int first = job * kCloudPointsPerJob, last = min(int(cell.points.size()), first + kCloudPointsPerJob);
    uint64_t state = CloudSeed(key, 1, job);
    SubsurfacePhotonReceiver receiverState(threadData->litObjectIgnoresPhotons, cloud.object, cloud.photons);
    std::unique_ptr<PhotonGatherer> gatherer;
    if (key.photons)
        gatherer.reset(new PhotonGatherer(&sceneData->surfacePhotonMap, sceneData->photonSettings));
    for (int k = first; k < last; k++)
    {
        SubsurfacePoint& point = cell.points[k];
        Vector3d normal(point.normal[0], point.normal[1], point.normal[2]);
        Vector2d areaShift(CloudRandom(state), CloudRandom(state));
        MathColour irradiance = ComputeSubsurfaceIrradiance(point.position, normal, cloud.lights, cloud.eta, kCloudAreaPoints, &areaShift,
                                                            cell.visibility.data() + k * stride, ticket, CloudSeed(key, 2, k));
        MathColour photonEntry;
        if (gatherer)
        {
            photonEntry = ComputeSubsurfacePhotonIrradiance(point.position, normal, cloud.eta, cloud.object, *gatherer, ticket, true);
            irradiance += photonEntry;
        }
        if (key.medium != nullptr)
        {
            SubsurfaceEntry entry;
            ComputeSubsurfaceEntry(*cloud.flesh, point.position, -normal, CloudDepthDraw(key, cell, point, cloud.size), entry);
            irradiance = irradiance * entry.factor + entry.emission;
            if (gatherer)
                photonEntry *= entry.factor;
            for (int j = 0; j < MathColour::channels; j++)
            {
                cell.entry[k * 2 * MathColour::channels + j] = entry.factor[j];
                cell.entry[(k * 2 + 1) * MathColour::channels + j] = entry.emission[j];
            }
        }
        for (int j = 0; j < MathColour::channels; j++)
        {
            point.irradiance[j] = irradiance[j];
            if (gatherer)
                cell.photonEntry[k * MathColour::channels + j] = photonEntry[j];
        }
        point.id = k;
        MathColour ambient;
        if (key.radiosity && radiosity.LookupPretraceAmbient(point.position, normal, ambient))
            for (int j = 0; j < MathColour::channels; j++)
                cell.ambient[k * MathColour::channels + j] = ambient[j];
    }
}

// Adds the radiosity cache's light to a cell's points, entering along the normal as in the sampled method; a point with
// no sample near enough takes the mean of those with one.
void Trace::AddSubsurfaceAmbient(const SubsurfaceCloud& cloud, SubsurfaceCell& cell)
{
    const int channels = MathColour::channels;
    size_t count = cell.points.size(), found = 0;
    std::vector<double> mean(channels, 0.0);
    for (size_t k = 0; k < count; k++)
        if (!std::isnan(cell.ambient[k * channels]))
        {
            found++;
            for (int j = 0; j < channels; j++)
                mean[j] += cell.ambient[k * channels + j];
        }
    float ft = float(ComputeFt(1.0, cloud.eta));
    for (SubsurfacePoint& point : cell.points)
    {
        const float *ambient = &cell.ambient[point.id * channels];
        for (int j = 0; j < channels; j++)
        {
            float light = ft * (!std::isnan(ambient[0]) ? ambient[j] : (found > 0) ? float(mean[j] / found) : 0.0f);
            point.irradiance[j] += cell.entry.empty() ? light : light * cell.entry[point.id * 2 * channels + j];
        }
    }
    std::vector<float>().swap(cell.ambient);
}

// Every crossing of the object along a line within [from, to]. One test of some shapes returns only the nearest
// interval's crossings (a blob's does), so the line is tested again past the farthest crossing found until none is left.
void Trace::CollectCrossings(ObjectPtr object, const Vector3d& origin, const Vector3d& dir, double from, double to, std::vector<Intersection>& hits, TraceTicket& ticket)
{
    double step = max(1e-7, 1e-6 * to);
    double start = 0.0;
    for (int guard = 0; (guard < 256) && (start <= to); guard++)
    {
        Ray ray(ticket, origin + dir * start, dir, Ray::SubsurfaceRay);
        if (!object->Bound.empty() && !Ray_In_Bound(ray, object->Bound, threadData))
            return;
        IStack stack(stackPool);
        if (!object->All_Intersections(ray, stack, threadData))
            return;
        double farthest = 0.0;
        for (; stack->size() > 0; stack->pop())
        {
            Intersection& in = stack->top();
            if (!(in.Depth > 0.0))
                continue;
            farthest = max(farthest, in.Depth);
            in.Depth += start;
            if ((in.Depth >= from) && (in.Depth <= to))
                hits.push_back(in);
        }
        if (!(farthest > 0.0))
            return;
        start += farthest + step;
    }
}

// Fixes the object's cloud and collects the cells within reach of the exit point; false where no cloud can serve.
bool Trace::OpenSubsurfaceCloud(const Intersection& out, const SubsurfaceProfile& profile, const std::vector<const LightSource*>& lights, SubsurfaceCloud& cloud)
{
    cloud.object = SubsurfaceObject(out);
    cloud.edge = false;
    if (cloud.photons && !UniformSubsurfacePhotonReceiver(cloud.object, cloud.object))
        return false;
    if (cloud.object->interior == nullptr)
        return false;
    // Cloud points hold a shadow per light of the whole object, which a part in a light group of its own cannot use.
    CollectSubsurfaceLights(cloud.object, cloud.lights);
    if (cloud.lights != lights)
        return false;
    double mm = sceneData->mmPerUnit;
    double sigmaMin = min(profile.sigma_tr[0], min(profile.sigma_tr[1], profile.sigma_tr[2]));
    if (!(sigmaMin > 0.0))
        return false;
    cloud.reach = kCloudReach / sigmaMin;
    cloud.eta = cloud.object->interior->IOR / sceneData->atmosphereIOR;
    if (!ssltCameraKnown)
        ssltCameraKnown = threadData->subsurfaceCache->GetCamera(ssltCameraLocation, ssltPixelSize, ssltPixelAngle);

    // Diffusion that stays within about a pixel is taken as lit like the exit point.
    double footprint = ssltPixelSize + (out.IPoint - ssltCameraLocation).length() * ssltPixelAngle;
    cloud.footprint = footprint * mm;
    cloud.local = (1.0 / sigmaMin / mm < footprint);
    if (cloud.local)
        return !cloud.photons;

    // Cells are a power of four in size, at least the diffusion's reach, so a texture that varies it uses few sizes.
    cloud.sizeLevel = 2 * int(ceil(0.5 * log2(cloud.reach / mm)));
    cloud.size = ldexp(1.0, cloud.sizeLevel);
    // An unbounded object (a plane) gets no coarse cells, which would reach the horizon and spend the point budget.
    const BoundingBox& box = cloud.object->BBox;
    bool unbounded = false;
    for (int a = 0; a < 3; a++)
        unbounded = unbounded || (box.size[a] >= BOUND_HUGE / 4);
    double zr = max(profile.z_r[0], max(profile.z_r[1], profile.z_r[2]));
    double finest = cloud.size / kCloudCellSpacings * sceneData->subsurfaceSpacing * mm;
    if (unbounded && (finest > kCloudCoarse * max(zr, cloud.footprint)))
        return false;
    if (!GatherSubsurfaceCells(cloud, out.IPoint, cloud.reach / mm))
        return false;
    const SubsurfaceCell *here = FindSubsurfaceCell(cloud, out.IPoint);
    if (here == nullptr)
        return false;
    // The disc and ring are sized by the coarsest cell they reach, so that each holds enough of its points.
    Vector3d normal = out.INormal.normalized();
    cloud.spacing = here->Spacing(normal);
    for (int pass = 0; pass < 2; pass++)
    {
        double ringSqr = Sqr(kCloudRing * cloud.spacing);
        for (int i = 0; i < cloud.cells.size(); i++)
        {
            double distSqr = 0.0;
            for (int a = 0; a < 3; a++)
            {
                double lo = cloud.coords[i][a] * cloud.size;
                distSqr += Sqr(max(0.0, max(lo - out.IPoint[a], out.IPoint[a] - lo - cloud.size)));
            }
            if (distSqr < ringSqr)
                cloud.spacing = max(cloud.spacing, cloud.cells[i]->Spacing(normal));
        }
    }
    // The ring must lie within reach, for its points to be counted.
    double ring = kCloudRing * cloud.spacing * mm;
    if (ring > cloud.reach)
    {
        cloud.reach = ring;
        return GatherSubsurfaceCells(cloud, out.IPoint, cloud.reach / mm);
    }
    return true;
}

// The cells of the cloud that a sphere touches, built as needed.
bool Trace::GatherSubsurfaceCells(SubsurfaceCloud& cloud, const Vector3d& centre, double radius)
{
    cloud.cells.clear();
    cloud.coords.clear();
    int lo[3], hi[3];
    for (int a = 0; a < 3; a++)
    {
        double from = floor((centre[a] - radius) / cloud.size), to = floor((centre[a] + radius) / cloud.size);
        if (!(fabs(from) < 1073741824.0) || !(fabs(to) < 1073741824.0) || (to - from > 8.0))
            return false;
        lo[a] = int(from);
        hi[a] = int(to);
    }
    for (int cx = lo[X]; cx <= hi[X]; cx++)
    for (int cy = lo[Y]; cy <= hi[Y]; cy++)
    for (int cz = lo[Z]; cz <= hi[Z]; cz++)
    {
        SubsurfaceCellKey key = { cloud.object, cloud.sizeLevel, cx, cy, cz, cloud.photons, cloud.radiosity, cloud.medium };
        const SubsurfaceCell *&known = ssltCells[key];
        if (known == nullptr)
        {
            bool build;
            std::shared_ptr<SubsurfaceCell> cell = threadData->subsurfaceCache->Acquire(key, build);
            if (build)
                BuildSubsurfaceCell(cloud, key, *cell);
            else
                WorkOnSubsurfaceCell(cloud, key, *cell, false);
            known = cell.get();
        }
        if (!known->usable)
            return false;
        if (!known->nodes.empty())
        {
            cloud.cells.push_back(known);
            cloud.coords.push_back(Vector3d(cx, cy, cz));
        }
    }
    return true;
}

// The gathered cell holding a point, if any.
const SubsurfaceCell *Trace::FindSubsurfaceCell(const SubsurfaceCloud& cloud, const Vector3d& q)
{
    Vector3d key(floor(q[X] / cloud.size), floor(q[Y] / cloud.size), floor(q[Z] / cloud.size));
    for (int i = 0; i < cloud.cells.size(); i++)
        if ((cloud.coords[i] - key).lengthSqr() < 0.25)
            return cloud.cells[i];
    // A point on a cell's face (an object resting on y = 0) can round into the empty cell beside the one holding its surface.
    const SubsurfaceCell *nearest = nullptr;
    double best = Sqr(1e-6 * cloud.size);
    for (int i = 0; i < cloud.cells.size(); i++)
    {
        double distSqr = 0.0;
        for (int a = 0; a < 3; a++)
        {
            double lo = cloud.coords[i][a] * cloud.size;
            distSqr += Sqr(max(0.0, max(lo - q[a], q[a] - lo - cloud.size)));
        }
        if (distSqr <= best)
        {
            best = distSqr;
            nearest = cloud.cells[i];
        }
    }
    return nearest;
}

// Per-light visibility at a surface point from the leaf of the cloud hierarchy nearest it, among points facing the same way.
bool Trace::LookupSubsurfaceVisibility(const SubsurfaceCloud& cloud, const Vector3d& q, const Vector3d& normal, SubsurfaceVisibility& visibility)
{
    const SubsurfaceCell *cell = FindSubsurfaceCell(cloud, q);
    if (cell == nullptr)
        return false;
    auto boxSqr = [&q](const SubsurfaceNode& node)
    {
        double d = 0.0;
        for (int a = 0; a < 3; a++)
            d += Sqr(max(0.0, max(node.lo[a] - q[a], q[a] - node.hi[a])));
        return d;
    };
    int index = 0;
    while (cell->nodes[index].count == 0)
        index = (boxSqr(cell->nodes[index + 1]) <= boxSqr(cell->nodes[cell->nodes[index].first])) ? index + 1 : cell->nodes[index].first;
    const SubsurfaceNode& leaf = cell->nodes[index];
    Vector3d n = normal.normalized();
    if (boxSqr(leaf) > Sqr(kCloudDisc * cell->Spacing(n)))
        return false;
    int stride = cell->lights * MathColour::channels;
    visibility.Reset(cloud.lights.size());
    for (int i = leaf.first; i < leaf.first + leaf.count; i++)
    {
        const SubsurfacePoint& p = cell->points[i];
        double facing = p.normal[0] * n[X] + p.normal[1] * n[Y] + p.normal[2] * n[Z];
        if (facing >= 0.5)
            visibility.Add(cell->visibility.data() + p.id * stride);
    }
    visibility.Finish();
    return visibility.count > 0;
}

// How far a cloud point would be from the exit point x (normal n) were the surface unfolded flat, taking it as the planes
// through x and the point meeting at a crease; between the straight distance and twice that.
static double UnfoldedDistance(const Vector3d& x, const Vector3d& n, const SubsurfacePoint& p)
{
    Vector3d np(p.normal[0], p.normal[1], p.normal[2]);
    double straight = (p.position - x).length();
    double c = dot(n, np);
    if (!(c < 0.999) || !(c > -0.999))
        return straight;
    Vector3d crease = cross(n, np);
    crease /= crease.length();
    double hx = dot(n, x), hp = dot(np, p.position);
    Vector3d onCrease = (n * (hx - hp * c) + np * (hp - hx * c)) / (1.0 - c * c);
    Vector3d fromX = x - onCrease, fromP = p.position - onCrease;
    double a = (fromX - crease * dot(fromX, crease)).length(), b = (fromP - crease * dot(fromP, crease)).length();
    double along = dot(p.position - x, crease);
    return min(2.0 * straight, max(straight, sqrt(Sqr(a + b) + Sqr(along))));
}

// The diffuse term from the object's cloud around the exit point (core, disc, ring and beyond; see doc/PERF.md); false
// where the disc bends sharply, a crease is close, or a coarse cloud meets a shadow edge, for method 1.
bool Trace::ComputeSubsurfaceCloud(const Intersection& out, const Vector3d& base, const SubsurfaceProfile& profile, double ftOut, SubsurfaceCloud& cloud,
                                   MathColour& diffuse, TraceTicket& ticket, std::uint64_t key)
{
    double mm = sceneData->mmPerUnit;
    double reachSqr = Sqr(cloud.reach);

    Vector3d n = out.INormal.normalized();
    // The exit normal points out of the object, away from the base point below it, as the cloud's normals do.
    if (dot(n, out.IPoint - base) < 0.0)
        n.invert();

    if (cloud.local)
    {
        ssltScratchVisibility.Reset(cloud.lights.size());
        MathColour here = ComputeCloudExitIrradiance(out, ssltScratchVisibility, cloud, false, ticket, key) + ComputeCloudExitAmbient(out, n, cloud, ticket);
        if (cloud.medium != nullptr)
            here = here * cloud.flesh->nearby.factor + cloud.flesh->nearby.emission;
        diffuse = MathColour(profile.RdDisc(cloud.reach) * (M_PI * reachSqr) * PreciseMathColour(here) * ftOut);
        threadData->Stats()[Subsurface_Cloud_Served]++;
        return true;
    }

    double core = kCloudCore * cloud.spacing * mm, disc = kCloudDisc * cloud.spacing * mm, ring = kCloudRing * cloud.spacing * mm;
    double coreSqr = Sqr(core), discSqr = Sqr(disc), ringSqr = Sqr(ring);
    double errorBound = sceneData->subsurfaceErrorBound;
    const Vector3d& x = out.IPoint;
    SubsurfaceVisibility& visibility = ssltScratchVisibility;
    visibility.Reset(cloud.lights.size());
    PreciseMathColour far, discWeight, discLight, ringWeight, ringLight, ringUnfolded, discFactor, discEmission, discPhotons;
    // Points behind the exit point's tangent plane that turn away from it (the far face and the edges of a wall), summed
    // apart from the exit point's own surface.
    PreciseMathColour discLightAcross, ringLightAcross;
    double discFacing = 1.0, ringFacing = 1.0, ringArea = 0.0, covered = 0.0;
    Vector3d moment(0.0);
    double creaseSqr = Sqr(kCloudCrease * cloud.spacing * mm);
    bool hidden = false;
    std::vector<int>& stack = ssltScratchStack;
    // The ring's points are walked first, so that a shading point handed to method 1 does not pay for the far ones.
    auto walk = [&](bool near)
    {
        for (int c = 0; c < cloud.cells.size(); c++)
        {
            const SubsurfaceCell *cell = cloud.cells[c];
            Vector3d cellLo = cloud.coords[c] * cloud.size;
            int stride = cell->lights * MathColour::channels;
            stack.assign(1, 0);
            while (!stack.empty())
            {
                int index = stack.back();
                stack.pop_back();
                const SubsurfaceNode& node = cell->nodes[index];
                double boxSqr = 0.0, outerSqr = 0.0;
                for (int a = 0; a < 3; a++)
                {
                    boxSqr += Sqr(max(0.0, max(node.lo[a] - x[a], x[a] - node.hi[a])));
                    outerSqr += Sqr(max(x[a] - node.lo[a], node.hi[a] - x[a]));
                }
                boxSqr *= Sqr(mm);
                if ((boxSqr > (near ? ringSqr : reachSqr)) || (!near && (outerSqr * Sqr(mm) < ringSqr)))
                    continue;
                if (!near && (boxSqr > ringSqr) && (node.area < errorBound * boxSqr) && (node.cone >= kCloudCone))
                {
                    Vector3d toward = node.centre - base;
                    if (toward[X] * node.normal[0] + toward[Y] * node.normal[1] + toward[Z] * node.normal[2] > 0.0)
                    {
                        double distSqr = (x - node.centre).lengthSqr() * Sqr(mm);
                        PreciseMathColour rd = profile.Rd(distSqr);
                        for (int j = 0; j < MathColour::channels; j++)
                            far[j] += rd[j] * node.irradiance[j];
                    }
                    continue;
                }
                if (node.count == 0)
                {
                    stack.push_back(index + 1);
                    stack.push_back(node.first);
                    continue;
                }
                for (int i = node.first; i < node.first + node.count; i++)
                {
                    const SubsurfacePoint& p = cell->points[i];
                    double distSqr = (x - p.position).lengthSqr() * Sqr(mm);
                    if ((distSqr < ringSqr) != near)
                        continue;
                    // A point the base point sees from behind is reached only through another part of the surface.
                    Vector3d toward = p.position - base;
                    if (toward[X] * p.normal[0] + toward[Y] * p.normal[1] + toward[Z] * p.normal[2] <= 0.0)
                    {
                        hidden = hidden || (distSqr < creaseSqr);
                        continue;
                    }
                    PreciseMathColour rd = profile.Rd(distSqr);
                    if (!near)
                    {
                        for (int j = 0; j < MathColour::channels; j++)
                            far[j] += rd[j] * p.area * p.irradiance[j];
                        continue;
                    }
                    double facing = p.normal[0] * n[X] + p.normal[1] * n[Y] + p.normal[2] * n[Z];
                    bool inDisc = (distSqr < discSqr);
                    if ((facing < kCloudFlat) && (dot(p.position - x, n) < 0.0))
                    {
                        PreciseMathColour& light = inDisc ? discLightAcross : ringLightAcross;
                        for (int j = 0; j < MathColour::channels; j++)
                            light[j] += rd[j] * p.area * p.irradiance[j];
                        continue;
                    }
                    if (inDisc)
                    {
                        discFacing = min(discFacing, facing);
                        visibility.Add(cell->visibility.data() + p.id * stride);
                        for (int j = 0; cloud.photons && (j < MathColour::channels); j++)
                            discPhotons[j] += rd[j] * p.area * cell->photonEntry[p.id * MathColour::channels + j];
                        for (int j = 0; !cell->entry.empty() && (j < MathColour::channels); j++)
                        {
                            discFactor[j] += rd[j] * p.area * cell->entry[p.id * 2 * MathColour::channels + j];
                            discEmission[j] += rd[j] * p.area * cell->entry[(p.id * 2 + 1) * MathColour::channels + j];
                        }
                    }
                    double unfoldedSqr = inDisc ? distSqr : Sqr(UnfoldedDistance(x, n, p) * mm);
                    // The window is taken at the centre of the grid square the point's line crosses, free of its jitter.
                    Vector3d square = p.position;
                    for (int b = 1; b < 3; b++)
                    {
                        int u = (p.axis + b) % 3;
                        double h = cloud.size / cell->steps[p.axis];
                        square[u] = cellLo[u] + (floor((square[u] - cellLo[u]) / h) + 0.5) * h;
                    }
                    double window = p.area * CloudWindow((square - x).lengthSqr() * Sqr(mm) * unfoldedSqr / max(distSqr, 1e-30), ringSqr);
                    covered += window;
                    moment += (square - x) * window;
                    if (!inDisc)
                    {
                        ringFacing = min(ringFacing, facing);
                        ringArea += p.area;
                        PreciseMathColour rdUnfolded = profile.Rd(unfoldedSqr);
                        for (int j = 0; j < MathColour::channels; j++)
                            ringUnfolded[j] += rdUnfolded[j] * p.area;
                    }
                    PreciseMathColour& weight = inDisc ? discWeight : ringWeight;
                    PreciseMathColour& light = inDisc ? discLight : ringLight;
                    for (int j = 0; j < MathColour::channels; j++)
                    {
                        weight[j] += rd[j] * p.area;
                        light[j] += rd[j] * p.area * p.irradiance[j];
                    }
                }
            }
        }
    };
    walk(true);
    double density = ringArea / (M_PI * (ringSqr - discSqr));
    if (hidden)
    {
        threadData->Stats()[Subsurface_Cloud_Hidden]++;
        return false;
    }
    if ((visibility.count == 0) || (discFacing < kCloudFlat))
    {
        threadData->Stats()[Subsurface_Cloud_Bent]++;
        return false;
    }
    if (cloud.photons)
        for (int j = 0; j < MathColour::channels; j++)
            if (!(discWeight[j] > 0.0))
            {
                threadData->Stats()[Subsurface_Cloud_Bent]++;
                return false;
            }
    visibility.Finish();
    // Coarse points cannot place a shadow edge between them: the exit point tests its own, and edges go to method 1.
    double zr = max(profile.z_r[0], max(profile.z_r[1], profile.z_r[2]));
    bool coarse = (cloud.spacing * mm > kCloudCoarse * max(zr, cloud.footprint));
    for (int l = 0; coarse && (l < cloud.lights.size()); l++)
        if (!visibility.Agrees(l))
        {
            threadData->Stats()[Subsurface_Cloud_Shadow_Edge]++;
            return false;
        }

    // Where the surface runs out, the core, disc and ring keep their integrals' share inside one straight edge (an open
    // border) or two either side (a narrow strip), placed by the window's covered share and centroid; see doc/PERF.md.
    PreciseMathColour coreShare(1.0), discShare(1.0), ringShare(1.0);
    double share = covered / (M_PI * ringSqr / 3.0);
    cloud.edge = (share < kCloudWhole);
    if (cloud.edge)
    {
        double offset = (moment - n * dot(moment, n)).length() / max(covered, 1e-30) / (ring / mm);
        double side = CloudEdgeDistance(offset, true), strip = CloudEdgeDistance(0.5 * (1.0 + share)), scale = min(1.0, 2.0 * share);
        double oneSided = min(1.0, offset / CloudWindowOffset(CloudEdgeDistance(share)));
        double full = min(1.0, (kCloudWhole - share) / (kCloudWhole - kCloudEdge));
        auto regionShare = [&](double inner, double outer)
        {
            PreciseMathColour oneEdge = profile.RdEdgeShare(inner, outer, side * ring) * scale;
            PreciseMathColour twoEdges = profile.RdEdgeShare(inner, outer, strip * ring);
            PreciseMathColour blend;
            for (int j = 0; j < MathColour::channels; j++)
                blend[j] = 1.0 - full * (1.0 - (oneEdge[j] * oneSided + max(0.0, 2.0 * twoEdges[j] - 1.0) * (1.0 - oneSided)));
            return blend;
        };
        coreShare = regionShare(0.0, core);
        discShare = regionShare(core, disc);
        ringShare = regionShare(disc, ring);
    }

    SubsurfacePhotonReceiver receiverState(threadData->litObjectIgnoresPhotons, cloud.object, cloud.photons);
    MathColour here = ComputeCloudExitIrradiance(out, visibility, cloud, coarse, ticket, key);
    if (cloud.exitMismatch)
    {
        threadData->Stats()[Subsurface_Cloud_Exit_Mismatch]++;
        return false;
    }
    walk(false);
    here += ComputeCloudExitAmbient(out, n, cloud, ticket);
    // The core takes the flesh and emission of the disc's points, not those under the exit point.
    if (cloud.medium != nullptr)
        for (int j = 0; j < MathColour::channels; j++)
            here[j] = (discWeight[j] > 0.0) ? here[j] * discFactor[j] / discWeight[j] + discEmission[j] / discWeight[j]
                                            : here[j] * cloud.flesh->nearby.factor[j] + cloud.flesh->nearby.emission[j];
    if (cloud.photons)
        for (int j = 0; j < MathColour::channels; j++)
            here[j] += discPhotons[j] / discWeight[j];
    PreciseMathColour coreRd = profile.RdDisc(core) * (M_PI * coreSqr);
    PreciseMathColour discRd = profile.RdDisc(disc) * (M_PI * discSqr);
    PreciseMathColour sum = far + coreRd * coreShare * PreciseMathColour(here);
    for (int j = 0; j < MathColour::channels; j++)
        sum[j] += (discRd[j] - coreRd[j]) * discShare[j] * ((discWeight[j] > 0.0) ? discLight[j] / discWeight[j] : here[j]);
    // The ring's integral is a flat ring's, times how much nearer its points are than they would be unfolded flat: 1 on a
    // flat surface, and more across a fold, where points around the edge are nearer through the object.
    if ((ringFacing >= kCloudFold) && (density <= kCloudDense))
    {
        PreciseMathColour ringRd = profile.RdDisc(ring) * (M_PI * ringSqr) - discRd;
        for (int j = 0; j < MathColour::channels; j++)
            if ((ringWeight[j] > 0.0) && (ringUnfolded[j] > 0.0))
                sum[j] += ringRd[j] * ringShare[j] * (ringWeight[j] / ringUnfolded[j]) * (ringLight[j] / ringWeight[j]);
    }
    else
        sum += ringLight;
    // The far face of a thin wall is a surface of its own, summed from its points: their distance runs through the wall.
    sum += discLightAcross + ringLightAcross;
    diffuse = MathColour(sum * ftOut);
    threadData->Stats()[Subsurface_Cloud_Served]++;
    return true;
}

// The mean of Rd over a disc of radius (mm) centred on the exit point, in closed form.
PreciseMathColour Trace::SubsurfaceProfile::RdDisc(double radius) const
{
    PreciseMathColour rd;
    for (int j = 0; j < MathColour::channels; j++)
    {
        double d_r = sqrt(Sqr(z_r[j]) + Sqr(radius));
        double d_v = sqrt(Sqr(z_v[j]) + Sqr(radius));
        double integral = exp(-sigma_tr[j] * z_r[j]) - z_r[j] * exp(-sigma_tr[j] * d_r) / d_r
                        + exp(-sigma_tr[j] * z_v[j]) - z_v[j] * exp(-sigma_tr[j] * d_v) / d_v;
        rd[j] = scale[j] * 2.0 * M_PI * integral / (M_PI * Sqr(radius));
    }
    return rd;
}

// The integral of Rd over the whole plane, in closed form.
PreciseMathColour Trace::SubsurfaceProfile::RdTotal() const
{
    PreciseMathColour rd;
    for (int j = 0; j < MathColour::channels; j++)
        rd[j] = scale[j] * 2.0 * M_PI * (exp(-sigma_tr[j] * z_r[j]) + exp(-sigma_tr[j] * z_v[j]));
    return rd;
}

// The share of Rd's integral over a flat annulus (radii in mm) inside a straight edge d mm from its centre.
PreciseMathColour Trace::SubsurfaceProfile::RdEdgeShare(double inner, double outer, double d) const
{
    PreciseMathColour share(1.0);
    if (!(d < outer))
        return share;
    PreciseMathColour whole = RdDisc(outer) * (M_PI * Sqr(outer));
    if (inner > 0.0)
        whole -= RdDisc(inner) * (M_PI * Sqr(inner));
    double from = max(inner, d), span = outer - from;
    const int steps = 8;
    PreciseMathColour cut;
    for (int i = 0; i < steps; i++)
    {
        double s = (i + 0.5) / steps, r = from + span * Sqr(s);
        cut += Rd(Sqr(r)) * (2.0 * r * acos(min(1.0, d / r)) * 2.0 * span * s / steps);
    }
    for (int j = 0; j < MathColour::channels; j++)
        share[j] = (whole[j] > 0.0) ? max(0.0, 1.0 - cut[j] / whole[j]) : 1.0;
    return share;
}

// Jensen's dipole diffusion profile Rd, per channel, at squared distance distSqr (mm^2) from the exit point.
PreciseMathColour Trace::SubsurfaceProfile::Rd(double distSqr) const
{
    PreciseMathColour rd;
    for (int j = 0; j < MathColour::channels; j++)
    {
        double dSqr_r = Sqr(z_r[j]) + distSqr;
        double d_r    = sqrt(dSqr_r);
        double dSqr_v = Sqr(z_v[j]) + distSqr;
        double d_v    = sqrt(dSqr_v);
        double r_term = z_r[j] * (sigma_tr[j] + 1.0/d_r) * exp(-sigma_tr[j] * d_r) / dSqr_r; // dimension 1/area
        double v_term = z_v[j] * (sigma_tr[j] + 1.0/d_v) * exp(-sigma_tr[j] * d_v) / dSqr_v; // dimension 1/area
        rd[j] = scale[j] * (r_term + v_term);
    }
    return rd;
}

void Trace::ComputeDiffuseAmbientContribution1(const Intersection& in, const PreciseMathColour& rd, MathColour& Total_Colour, double eta, double weight, TraceTicket& ticket)
{
    MathColour ambientcolour;
    // TODO FIXME - should support pertubed normals
    radiosity.ComputeAmbient(in.IPoint, in.INormal, in.INormal, 1.0 /* TODO - brilliance */, ambientcolour, weight, ticket);
    // Radiosity data is already cosine-weighted, so the light is taken as arriving along the normal.
    double ft = ComputeFt(1.0, eta);
    for (int j = 0; j < MathColour::channels; j++)
    {
        ambientcolour[j] *= ft * rd[j];
        POV_SUBSURFACE_ASSERT(ambientcolour[j] >= 0);
        Total_Colour[j] += ambientcolour[j];
    }
}

void Trace::SubsurfaceLayers::Add(const TEXTURE *layer, const MathColour& pigment, const MathColour& visibility)
{
    reflectance += visibility * (pigment * layer->Finish->Diffuse);
    tint += visibility * pigment;
    weight += visibility;
    if (top == nullptr)
        top = layer;
}

bool Trace::SubsurfaceLayers::Finish()
{
    if ((top == nullptr) || !(weight.WeightMax() > 0.0))
        return false;
    for (int j = 0; j < MathColour::channels; j++)
    {
        reflectance[j] = (weight[j] > 0.0) ? reflectance[j] / weight[j] : 0.0f;
        tint[j] = (weight[j] > 0.0) ? tint[j] / weight[j] : 0.0f;
    }
    return true;
}

// Flesh lookups per shading point for its reference colour and its nearby entry values, and how far they spread
// sideways, in mean depths.
static const int kFleshSamples = 4;
static const double kFleshSpread = 2.0;
static const double kFleshFloor = 1e-3;

// Inverse of the standard normal distribution function (Acklam's rational approximation, relative error about 1e-9).
static double InverseNormal(double p)
{
    static const double a[] = { -3.969683028665376e+01, 2.209460984245205e+02, -2.759285104469687e+02, 1.383577518672690e+02, -3.066479806614716e+01, 2.506628277459239e+00 };
    static const double b[] = { -5.447609879822406e+01, 1.615858368580409e+02, -1.556989798598866e+02, 6.680131188771972e+01, -1.328068155288572e+01 };
    static const double c[] = { -7.784894002430293e-03, -3.223964580411365e-01, -2.400758277161838e+00, -2.549732539343734e+00, 4.374664141464968e+00, 2.938163982698783e+00 };
    static const double d[] = { 7.784695709041462e-03, 3.224671290700398e-01, 2.445134137142996e+00, 3.754408661907416e+00 };
    const double tail = 0.02425;
    if (p < tail)
    {
        double q = sqrt(-2.0 * log(p));
        return (((((c[0] * q + c[1]) * q + c[2]) * q + c[3]) * q + c[4]) * q + c[5]) / ((((d[0] * q + d[1]) * q + d[2]) * q + d[3]) * q + 1.0);
    }
    if (p > 1.0 - tail)
    {
        double q = sqrt(-2.0 * log(1.0 - p));
        return -(((((c[0] * q + c[1]) * q + c[2]) * q + c[3]) * q + c[4]) * q + c[5]) / ((((d[0] * q + d[1]) * q + d[2]) * q + d[3]) * q + 1.0);
    }
    double q = p - 0.5, r = q * q;
    return (((((a[0] * r + a[1]) * r + a[2]) * r + a[3]) * r + a[4]) * r + a[5]) * q / (((((b[0] * r + b[1]) * r + b[2]) * r + b[3]) * r + b[4]) * r + 1.0);
}

// A depth below the surface from a draw in [0, 1): normal about depth, spread wide, cut off at the surface by drawing
// only from the share of the curve below it.
double Trace::SubsurfaceFlesh::DepthAt(double draw) const
{
    if (!(spread > 0.0))
        return depth;
    double p = outside + draw * (1.0 - outside);
    return max(0.0, depth + spread * InverseNormal(min(max(p, 1e-12), 1.0 - 1e-12)));
}

// The skin's transmittance at a surface point: its tint to the power of half its relative thickness, one crossing's share.
MathColour Trace::ComputeSubsurfaceSkin(const SubsurfaceFlesh& flesh, const Vector3d& point)
{
    TransColour colour;
    Compute_Pigment(colour, flesh.skin, point, nullptr, nullptr, threadData);
    double thickness = flesh.finish->SubsurfaceThickness;
    if (flesh.finish->SubsurfaceThicknessPigment != nullptr)
    {
        TransColour grey;
        Compute_Pigment(grey, flesh.finish->SubsurfaceThicknessPigment, point, nullptr, nullptr, threadData);
        thickness = grey.colour().Greyscale();
    }
    return Pow(colour.colour().ClippedLower(0.0), ColourChannel(0.5 * max(0.0, thickness)));
}

// What light entering the flesh at a surface point is multiplied by, and the flesh's own light there; inward is the unit
// normal into the object, and draw sets the depth of the flesh's lookup.
void Trace::ComputeSubsurfaceEntry(const SubsurfaceFlesh& flesh, const Vector3d& point, const Vector3d& inward, double draw, SubsurfaceEntry& entry)
{
    entry.flesh = MathColour(1.0);
    entry.emission.Clear();
    if (flesh.fleshAtDepth || flesh.emissionAtDepth)
    {
        Vector3d q = point + inward * flesh.DepthAt(draw);
        TransColour colour;
        if (flesh.fleshAtDepth)
        {
            Compute_Pigment(colour, flesh.finish->SubsurfacePigment, q, nullptr, nullptr, threadData);
            entry.flesh = colour.colour().ClippedLower(0.0);
        }
        if (flesh.emissionAtDepth && (flesh.finish->SubsurfaceEmissionPigment != nullptr))
        {
            Compute_Pigment(colour, flesh.finish->SubsurfaceEmissionPigment, q, nullptr, nullptr, threadData);
            entry.emission = colour.colour() * entry.flesh;
        }
        else if (flesh.emissionAtDepth)
            entry.emission = flesh.finish->SubsurfaceEmission * entry.flesh;
    }
    entry.factor = entry.flesh;
    if (flesh.skin != nullptr)
        entry.factor *= ComputeSubsurfaceSkin(flesh, point);
}

// The flesh and skin at a shading point; the flesh's reference and nearby values are means of a few lookups spread
// around it, so that nothing is read from under the exit point alone.
void Trace::SetUpSubsurfaceFlesh(SubsurfaceFlesh& flesh, const SubsurfaceLayers& layers, const Intersection& out, const Vector3d& inward,
                                 std::uint64_t key)
{
    const FINISH *finish = layers.top->Finish;
    flesh.finish = finish;
    if (!finish->SubsurfaceHasColour && !finish->SubsurfaceEmits)
        return;
    double mean = finish->SubsurfaceTranslucency.Greyscale();
    flesh.depth = ((finish->SubsurfaceDepth > 0.0) ? double(finish->SubsurfaceDepth) : 0.5 * mean) / sceneData->mmPerUnit;
    flesh.spread = ((finish->SubsurfaceSpread > 0.0) ? double(finish->SubsurfaceSpread) : 0.5 * mean) / sceneData->mmPerUnit;
    flesh.outside = (flesh.spread > 0.0) ? 0.5 * erfc(flesh.depth / (flesh.spread * sqrt(2.0))) : 0.0;
    flesh.fleshAtDepth = finish->SubsurfaceVolume && (finish->SubsurfacePigment != nullptr);
    flesh.emissionAtDepth = finish->SubsurfaceEmits && finish->SubsurfaceVolume && ((finish->SubsurfaceEmissionPigment != nullptr) || flesh.fleshAtDepth);
    bool skin = finish->SubsurfaceHasColour && ((finish->SubsurfaceThicknessPigment != nullptr) || (finish->SubsurfaceThickness != 0.0));
    if (skin && (finish->SubsurfaceThicknessSet || flesh.fleshAtDepth || flesh.emissionAtDepth))
        flesh.skin = layers.top->Pigment;

    TransColour colour;
    if (skin)
    {
        MathColour tint = layers.tint.ClippedLower(0.0);
        double thickness = finish->SubsurfaceThickness;
        if (finish->SubsurfaceThicknessPigment != nullptr)
        {
            Compute_Pigment(colour, finish->SubsurfaceThicknessPigment, out.IPoint, &out, nullptr, threadData);
            thickness = colour.colour().Greyscale();
        }
        flesh.exitSkin = Pow(tint, ColourChannel(0.5 * max(0.0, thickness)));
        if (flesh.skin == nullptr)
            flesh.entrySkin = flesh.exitSkin;
    }
    if (finish->SubsurfaceEmits && !flesh.emissionAtDepth)
    {
        flesh.emission = finish->SubsurfaceEmission;
        if (finish->SubsurfaceEmissionPigment != nullptr)
        {
            Compute_Pigment(colour, finish->SubsurfaceEmissionPigment, out.IPoint, &out, nullptr, threadData);
            flesh.emission = colour.colour();
        }
    }
    if (finish->SubsurfaceHasColour && (finish->SubsurfacePigment == nullptr))
        flesh.reference = PreciseMathColour(finish->SubsurfaceColour);
    else if (finish->SubsurfaceHasColour && !flesh.fleshAtDepth)
    {
        Compute_Pigment(colour, finish->SubsurfacePigment, out.IPoint, &out, nullptr, threadData);
        flesh.reference = PreciseMathColour(colour.colour().ClippedLower(0.0));
    }
    if (!flesh.PerEntry())
        return;

    Vector3d u, v;
    ComputeSurfaceTangents(inward, u, v);
    SubsurfaceEntry sum, entry;
    sum.factor.Clear();
    sum.flesh.Clear();
    for (int k = 0; k < kFleshSamples; k++)
    {
        double r = kFleshSpread * (flesh.depth + flesh.spread) * sqrt(Draw(key, kDrawSubsurface, 3 * k)), phi = 2.0 * M_PI * Draw(key, kDrawSubsurface, 3 * k + 1);
        ComputeSubsurfaceEntry(flesh, out.IPoint + (u * cos(phi) + v * sin(phi)) * r, inward, Draw(key, kDrawSubsurface, 3 * k + 2), entry);
        sum.flesh += entry.flesh;
        sum.factor += entry.factor;
        sum.emission += entry.emission;
    }
    flesh.nearby.flesh = sum.flesh / float(kFleshSamples);
    flesh.nearby.factor = sum.factor / float(kFleshSamples);
    flesh.nearby.emission = sum.emission / float(kFleshSamples);
    if (flesh.fleshAtDepth)
        flesh.reference = PreciseMathColour(flesh.nearby.flesh);
}

void Trace::ComputeSubsurfaceScattering(const SubsurfaceLayers& layers, const Intersection& out, Ray& Eye, MathColour& Final_Colour)
{
    const FINISH *Finish = layers.top->Finish;
    int NumSamplesDiffuse = sceneData->subsurfaceSamplesDiffuse;
    int NumSamplesSingle  = sceneData->subsurfaceSamplesSingle;

    // TODO FIXME - this is hard-coded for now
    if (Eye.GetTicket().subsurfaceRecursionDepth >= 2)
        return;
    else if (Eye.GetTicket().subsurfaceRecursionDepth == 1)
    {
        NumSamplesDiffuse = 1;
        NumSamplesSingle  = 1;
    }

    Eye.GetTicket().subsurfaceRecursionDepth++;
    const std::uint64_t key = Eye.NextChildKey(kDrawSubsurface);
    MarkGrain();

    Vector3d vOut = -Eye.Direction;

    MathColour Total_Colour;

    double eta;

    ComputeRelativeIOR(Eye, out.Object->interior.get(), eta);

    // The profile's reflectance is the flesh's where it has a colour of its own, otherwise the layers'.
    Vector3d inward = out.INormal.normalized();
    if (dot(inward, vOut) > 0.0)
        inward.invert();
    SubsurfaceFlesh flesh;
    SetUpSubsurfaceFlesh(flesh, layers, out, inward, DeriveKey(key, kDrawSubsurface, 5));
    MathColour reflectance = Finish->SubsurfaceHasColour ? MathColour(flesh.reference) * Finish->Diffuse : layers.reflectance;

    // user setting specifies reduced scattering coefficient
    PreciseMathColour   alpha_prime     = out.Object->interior->subsurface->GetReducedAlbedo(reflectance);
    PreciseMathColour   sigma_prime_s   = 1.0 / PreciseMathColour(Finish->SubsurfaceTranslucency);

    PreciseMathColour   sigma_prime_t   = sigma_prime_s / alpha_prime;
    PreciseMathColour   sigma_a         = sigma_prime_t - sigma_prime_s;

    PreciseMathColour   g(0.0); // the mean cosine of the scattering angle; for isotropic scattering, g = 0
    PreciseMathColour   sigma_t_xo      = sigma_prime_t / (1.0-g);
    PreciseMathColour   sigma_s         = sigma_prime_s / (1.0-g);

    double F_dr   = FresnelDiffuseReflectance(eta);
    double Aconst = ((1 + F_dr) / (1 - F_dr));
    SubsurfaceProfile profile;
    for (int j = 0; j < MathColour::channels; j++)
    {
        double spt = sigma_prime_s[j] + sigma_a[j];
        profile.scale[j]    = (sigma_prime_s[j] / spt) / (4.0 * M_PI);
        profile.sigma_tr[j] = sqrt(3 * sigma_a[j] * spt);
        profile.z_r[j]      = 1.0 / spt;
        profile.z_v[j]      = profile.z_r[j] * (1.0 + Aconst * 4.0/3.0);
    }

    double cos_out = clip(dot(vOut, out.INormal), -1.0, 1.0); // (clip values to not run into trouble due to petty precision issues)
    double ftOut   = ComputeFt(cos_out, eta);

    // colour dependent diffuse contribution

    double      sampleArea;
    double      sigma_a_mean        = sigma_a.Greyscale(); // TODO FIXME - use a "fair" average of all three color channels
    double      sigma_prime_s_mean  = sigma_prime_s.Greyscale(); // TODO FIXME - use a "fair" average of all three color channels
    double      sigma_prime_t_mean  = sigma_a_mean + sigma_prime_s_mean;

    bool radiosity_needed = (sceneData->radiositySettings.radiosityEnabled == true) && qualityFlags.radiosity &&
                            (sceneData->subsurfaceUseRadiosity == true) &&
                            (radiosity.CheckRadiosityTraceLevel(Eye.GetTicket()) == true) &&
                            (Test_Flag(out.Object, IGNORE_RADIOSITY_FLAG) == false);

    std::vector<const LightSource*> lights;
    CollectSubsurfaceLights(out.Object, lights);

    // Shading points the cloud cannot serve, and radiosity in the pretrace (the cache still filling), use method 1.
    int method = (Finish->SubsurfaceMethod != 0) ? Finish->SubsurfaceMethod : sceneData->subsurfaceMethod;
    bool cloudRadiosity = radiosity_needed && radiosity.IsFinalTrace();
    Vector3d sampleBase;
    ComputeDiffuseSampleBase(sampleBase, out, vOut, 1.0 / (sigma_prime_t_mean * sceneData->mmPerUnit), Eye.GetTicket());

    SubsurfaceCloud& cloudData = ssltClouds[Eye.GetTicket().subsurfaceRecursionDepth - 1];
    cloudData.radiosity = cloudRadiosity;
    cloudData.photons = SubsurfacePhotonsEnabled(out.Object);
    cloudData.flesh = &flesh;
    cloudData.medium = flesh.PerEntry() ? layers.top : nullptr;
    MathColour cloudDiffuse;
    if ((method == kSubsurfaceMethodPointCloud) && (cloudRadiosity || !radiosity_needed))
        threadData->Stats()[Subsurface_Cloud_Attempts]++;
    bool cloud = (method == kSubsurfaceMethodPointCloud) && (cloudRadiosity || !radiosity_needed) && OpenSubsurfaceCloud(out, profile, lights, cloudData) &&
                 ComputeSubsurfaceCloud(out, sampleBase, profile, ftOut, cloudData, cloudDiffuse, Eye.GetTicket(), DeriveKey(key, kDrawSubsurface, 1));
    const bool photonSamples = cloudData.photons && !cloud;
    if (photonSamples && (method == kSubsurfaceMethodPointCloud) && (cloudRadiosity || !radiosity_needed))
    {
        cloudData.photons = false;
        cloud = OpenSubsurfaceCloud(out, profile, lights, cloudData) &&
                ComputeSubsurfaceCloud(out, sampleBase, profile, ftOut, cloudData, cloudDiffuse, Eye.GetTicket(), DeriveKey(key, kDrawSubsurface, 1));
    }
    const bool photonsOnly = cloud && photonSamples;
    if (!cloud || photonSamples)
    {
        std::vector<SubsurfaceCandidate> candidates(photonsOnly ? 0 : lights.size() * NumSamplesDiffuse);
        const std::uint64_t fleshKey = DeriveKey(key, kDrawSubsurface, 4);
        std::unique_ptr<PhotonGatherer>& gatherer = ssltPhotonGatherers[Eye.GetTicket().subsurfaceRecursionDepth - 1];
        MathColour projectedDiffuse;
        bool projectedPhotons = false;
        if (photonSamples)
        {
            if (!gatherer)
                gatherer.reset(new PhotonGatherer(&sceneData->surfacePhotonMap, sceneData->photonSettings));
            if (method == kSubsurfaceMethodPointCloud)
                threadData->Stats()[Subsurface_Photon_Fallbacks]++;
            projectedPhotons = ComputeProjectedSubsurfacePhotons(out, sampleBase, profile, flesh, ftOut, sceneData->subsurfaceSamplesDiffuse, *gatherer,
                                                                 Eye.GetTicket(), key, projectedDiffuse);
        }

        for (int i = 0; (!photonsOnly || !projectedPhotons) && (i < NumSamplesDiffuse); i++)
        {
            Intersection in;
            ComputeDiffuseSamplePoint(sampleBase, SubsurfaceObject(out), in, sampleArea, Eye.GetTicket(), key, i);

            // avoid pathological cases
            if (sampleArea == 0)
                continue;

            if (!IsSameSSLTObject(in.Object, out.Object))
                continue; // TODO - what's the proper thing to do?

            double weight = sampleArea;
            double distSqr = (in.IPoint - out.IPoint).lengthSqr() * Sqr(sceneData->mmPerUnit);
            PreciseMathColour rd = profile.Rd(distSqr) * (ftOut * weight);
            if (flesh.PerEntry())
            {
                Vector3d n = in.INormal.normalized();
                if (dot(n, sampleBase - in.IPoint) < 0.0)
                    n.invert();
                SubsurfaceEntry entry;
                ComputeSubsurfaceEntry(flesh, in.IPoint, n, Draw(fleshKey, kDrawSubsurface, i), entry);
                if (!photonsOnly)
                    Total_Colour += MathColour(rd * PreciseMathColour(entry.emission));
                rd *= PreciseMathColour(entry.factor);
            }

            if (photonSamples && !projectedPhotons && !Test_Flag(in.Object, PH_IGNORE_PHOTONS_FLAG))
            {
                Vector3d n = SubsurfacePhotonNormal(in, threadData).normalized();
                if (dot(n, in.IPoint - sampleBase) < 0.0)
                    n.invert();
                MathColour photonEntry = ComputeSubsurfacePhotonIrradiance(in.IPoint, n, out.Object->interior->IOR / sceneData->atmosphereIOR, SubsurfaceObject(out), *gatherer, Eye.GetTicket(), false);
                Total_Colour += MathColour(rd * PreciseMathColour(photonEntry));
            }

            // radiosity-alike ambient illumination
            if (radiosity_needed && !photonsOnly)
                // shoot just one random ray to account for ambient illumination (we're averaging stuff anyway)
                ComputeDiffuseAmbientContribution1(in, rd, Total_Colour, eta, weight, Eye.GetTicket());

            for (int l = 0; !photonsOnly && (l < lights.size()); l++)
                ComputeDiffuseCandidate(*lights[l], in, rd, eta, candidates[l * NumSamplesDiffuse + i], Eye.GetTicket());
        }
        if (!photonsOnly)
            ShadeSubsurfaceCandidates(lights, candidates.data(), NumSamplesDiffuse, Total_Colour, Eye.GetTicket(), DeriveKey(key, kDrawSubsurface, 2));
        // Rays that leave without meeting the object count as samples of nothing, or open surfaces get twice their light.
        if (NumSamplesDiffuse > 0)
            Total_Colour /= NumSamplesDiffuse;
        if (projectedPhotons)
            Total_Colour += projectedDiffuse;
    }
    if (cloud)
        Total_Colour += cloudDiffuse;
    // Looked-up flesh is relative to the reference; emission the same everywhere enters over the whole profile.
    for (int j = 0; flesh.fleshAtDepth && (j < MathColour::channels); j++)
        Total_Colour[j] /= max(flesh.reference[j], kFleshFloor);
    if (Finish->SubsurfaceHasColour)
        Total_Colour *= flesh.entrySkin;
    if (Finish->SubsurfaceEmits && !flesh.emissionAtDepth)
        Total_Colour += MathColour(profile.RdTotal() * PreciseMathColour(flesh.emission) * ftOut);

    Vector3d refractedEye;
    if (SSLTComputeRefractedDirection(Eye.Direction, out.INormal, 1.0/eta, refractedEye))
    {
        Ray refractedEyeRay(Eye.GetTicket(), out.IPoint, refractedEye);
        refractedEyeRay.SetKey(DeriveKey(key, kDrawRefraction, 0));
        Intersection unscatteredIn;

        double dist;

        // find the intersection of the refracted ray with the object
        // find the distance to this intersection
        bool found = FindIntersection(unscatteredIn, refractedEyeRay);
        if (found)
            dist = (out.IPoint - unscatteredIn.IPoint).length() * sceneData->mmPerUnit;
        else
            dist = HUGE_VAL;

        double cos_out_prime = sqrt(1 - ((Sqr(1.0 / eta)) * (1 - Sqr(cos_out))));

        // colour dependent single scattering contribution

        if (NumSamplesSingle > 0)
        {
            MathColour singleColour;
            ComputeSingleScatteringContribution(out, dist, ftOut, cos_out_prime, refractedEye, sigma_t_xo, sigma_s, NumSamplesSingle, singleColour, eta, lights,
                                                cloud ? &cloudData : nullptr, flesh, Eye.GetTicket(), DeriveKey(key, kDrawSubsurface, 3));
            if (Finish->SubsurfaceHasColour)
                singleColour *= flesh.entrySkin;
            Total_Colour += singleColour / NumSamplesSingle;
        }

        // colour dependent unscattered contribution

        // Trace refracted ray.
        MathColour tempColour;
        ColourChannel tempTransm;

        // TODO FIXME - account for fresnel attenuation at interfaces
        PreciseMathColour att = Exp(-sigma_prime_t * dist); // TODO should be sigma_t
        double weight = att.WeightMax();
        if (weight > Eye.GetTicket().adcBailout)
        {
            if (!found)
            {
                // TODO - trace the ray to the background?
            }
            else if (IsSameSSLTObject(unscatteredIn.Object, out.Object))
            {
                unscatteredIn.Object->Normal(unscatteredIn.INormal, &unscatteredIn, threadData);
                if (dot(refractedEye, unscatteredIn.INormal) > 0)
                    unscatteredIn.INormal.invert();
                Vector3d doubleRefractedEye;
                if (SSLTComputeRefractedDirection(refractedEye, unscatteredIn.INormal, eta, doubleRefractedEye))
                {
                    Ray doubleRefractedEyeRay(refractedEyeRay);
                    doubleRefractedEyeRay.SetKey(DeriveKey(key, kDrawRefraction, 1));
                    doubleRefractedEyeRay.SetFlags(Ray::RefractionRay, refractedEyeRay);
                    doubleRefractedEyeRay.Origin = unscatteredIn.IPoint;
                    doubleRefractedEyeRay.Direction = doubleRefractedEye;
                    TraceRay(doubleRefractedEyeRay, tempColour, tempTransm, weight, false);
                    MathColour unscattered = MathColour(PreciseMathColour(tempColour) * att);
                    if (Finish->SubsurfaceHasColour)
                        unscattered *= (flesh.skin != nullptr) ? flesh.exitSkin : flesh.entrySkin;
                    Total_Colour += unscattered;
                }
            }
            else
            {
                // TODO - trace the ray into that object (if it is transparent)
            }
        }
    }

    if (Finish->SubsurfaceHasColour)
        Total_Colour *= flesh.exitSkin;
    Final_Colour += Total_Colour * layers.weight;

    Eye.GetTicket().subsurfaceRecursionDepth--;
}

}
// end of namespace pov
