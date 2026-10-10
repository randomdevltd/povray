//******************************************************************************
///
/// @file backend/lighting/photonshootingtask.cpp
///
/// This module implements Photon Mapping.
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

// Unit header file must be the first file included within POV-Ray *.cpp files (pulls in config)
#include "backend/lighting/photonshootingtask.h"

// C++ variants of C standard header files
//  (none at the moment)

// C++ standard header files
#include <vector>

// POV-Ray header files (base module)
//  (none at the moment)

// POV-Ray header files (core module)
#include "core/bounding/boundingbox.h"
#include "core/lighting/lightgroup.h"
#include "core/lighting/lightsource.h"
#include "core/math/matrix.h"
#include "core/render/ray.h"
#include "core/scene/object.h"
#include "core/shape/csg.h"
#include "core/support/octree.h"
#include "core/support/statistics.h"

// POV-Ray header files (POVMS module)
#include "povms/povmscpp.h"
#include "povms/povmsid.h"
#include "povms/povmsutil.h"

// POV-Ray header files (backend module)
#include "backend/lighting/photonshootingstrategy.h"
#include "backend/scene/backendscenedata.h"
#include "backend/scene/view.h"
#include "backend/scene/viewthreaddata.h"
#include "core/lighting/emitter.h"
#include "core/shape/csg.h"

// this must be the last file included
#include "base/povdebug.h"

namespace pov
{

PhotonShootingTask::PhotonShootingTask(ViewData *vd, PhotonShootingStrategy* strategy, size_t seed, int pass, unsigned int shard, unsigned int workers) :
    RenderTask(vd, seed, "Photon"),
    trace(vd->GetSceneData(), GetViewDataPtr(), vd->GetQualityFeatureFlags(), cooperate),
    strategy(strategy),
    cooperate(*this),
    maxTraceLevel(vd->GetSceneData()->photonSettings.Max_Trace_Level),
    adcBailout(vd->GetSceneData()->photonSettings.adcBailout),
    progressivePass(pass), progressiveShard(shard), progressiveWorkers(workers)
{
}

PhotonShootingTask::~PhotonShootingTask()
{
}


void PhotonShootingTask::SendProgress(void)
{
    if (timer.ElapsedRealTime() > 1000)
    {
        // TODO FIXME PHOTONS
        // with multiple threads shooting photons, the stats messages get confusing on the front-end.
        // this is because each thread sends its own count, and so varying numbers get displayed.
        // the totals should be combined and sent from a single thread.
        timer.Reset();
        POVMS_Object obj(kPOVObjectClass_PhotonProgress);
        int count = 0;
        for (PreparedSetId id = 0; id < GetViewDataPtr()->PhotonMapSetCount(); ++id)
        {
            PhotonMap* surface = GetViewDataPtr()->FindSurfacePhotonMap(id);
            PhotonMap* media = GetViewDataPtr()->FindMediaPhotonMap(id);
            count += surface ? surface->numPhotons : 0;
            count += media ? media->numPhotons : 0;
        }
        obj.SetInt(kPOVAttrib_CurrentPhotonCount, count);
        RenderBackend::SendViewOutput(GetViewData()->GetViewId(), GetSceneData()->frontendAddress, kPOVMsgIdent_Progress, obj);
    }
}



void PhotonShootingTask::Run()
{
    // quit right away if photons not enabled
    if (!GetSceneData()->photonSettings.photonsEnabled) return;

    if (progressivePass >= 0)
    {
        auto* data = GetViewDataPtr();
        for (unsigned int shard = progressiveShard; shard < ProgressivePhotonBudget::shards; shard += progressiveWorkers)
        {
            const size_t surfaceBegin = data->progressiveSurface.size(), mediaBegin = data->progressiveMedia.size();
            ShootProgressive(progressivePass, shard);
            data->progressiveBatches.push_back({shard, surfaceBegin, data->progressiveSurface.size(), mediaBegin, data->progressiveMedia.size()});
        }
        return;
    }

    Cooperate();

    PhotonShootingUnit* unit = strategy->getNextUnit();
    while(unit)
    {
        strategy->beginUnit(*unit, GetViewDataPtr());
        ShootPhotonsAtObject(*unit);
        unit = strategy->getNextUnit();
    }


    // good idea to make sure all warnings and errors arrive frontend now [trf]
    SendProgress();
    Cooperate();
}

namespace
{
class SurfaceLightEmitter final : public Emitter
{
    public:
        SurfaceLightEmitter(const LightSource& light, const std::vector<ObjectPtr>& targets, const std::vector<double>& probability) :
            light(light), targets(targets), probability(probability) {}
        const MathColour& Intensity() const override { return light.colour; }
        const Vector3d& Centre() const override { return light.Center; }
        double Radius() const override { return (light.Axis1.length() + light.Axis2.length()) * 0.5; }
        double NearDistance() const override { return EPSILON; }
        EmitterSample Sample(double s, std::uint64_t key) const override
        {
            Vector3d point = light.Center;
            if (light.Parallel)
            {
                size_t selected = 0;
                while (selected + 1 < targets.size() && s >= probability[selected])
                    s -= probability[selected++];
                auto centre = [&](ObjectPtr target) {
                    const Vector3d c = Vector3d(target->BBox.lowerLeft) + Vector3d(target->BBox.size) * 0.5;
                    return c - light.Direction * dot(c - light.Center, light.Direction);
                };
                const Vector3d axis = light.Direction;
                const Vector3d u = cross(std::abs(axis[Y]) < 0.9 ? Vector3d(0, 1, 0) : Vector3d(1, 0, 0), axis).normalized();
                const Vector3d v = cross(axis, u);
                const double radius = Vector3d(targets[selected]->BBox.size).length() * 0.5;
                const double r = radius * std::sqrt(Draw(key, kDrawPhoton, 7));
                const double angle = 2.0 * M_PI * Draw(key, kDrawPhoton, 8);
                point = centre(targets[selected]) + u * (r * std::cos(angle)) + v * (r * std::sin(angle));
                double pdf = 0.0;
                for (size_t i = 0; i < targets.size(); ++i)
                {
                    const double r2 = Vector3d(targets[i]->BBox.size).lengthSqr() * 0.25;
                    if (i == selected || (point - centre(targets[i])).lengthSqr() <= r2)
                        pdf += probability[i] / (M_PI * r2);
                }
                return {point, MathColour(1.0 / pdf), pdf};
            }
            if (light.Area_Light)
            {
                double u = s - 0.5, v = Draw(key, kDrawPhoton, 7) - 0.5;
                if (light.Circular)
                {
                    const double radius = 0.5 * std::sqrt(s), angle = 2.0 * M_PI * (v + 0.5);
                    u = radius * std::cos(angle);
                    v = radius * std::sin(angle);
                }
                point += light.Axis1 * u + light.Axis2 * v;
            }
            return {point, MathColour(1.0), 1.0};
        }
        Vector3d Direction(const EmitterSample& sample, double u, double v, double& pdf) const override
        {
            if (light.Parallel)
            {
                pdf = 1.0;
                return light.Direction;
            }
            return Emitter::Direction(sample, u, v, pdf);
        }
    private:
        const LightSource& light;
        const std::vector<ObjectPtr>& targets;
        const std::vector<double>& probability;
};

void ProgressiveTargets(const std::vector<ObjectPtr>& objects, const LightSource* light, std::vector<ObjectPtr>& targets)
{
    for (ObjectPtr object : objects)
    {
        if (Test_Flag(object, PH_TARGET_FLAG) && !(object->Type & LIGHT_SOURCE_OBJECT))
        {
            if (Vector3d(object->BBox.size).lengthSqr() <= 0.0)
                continue;
            const auto flags = light->Flags | object->Flags;
            if (!(((flags & PH_RFR_ON_FLAG) && !(flags & PH_RFR_OFF_FLAG)) ||
                  ((flags & PH_RFL_ON_FLAG) && !(flags & PH_RFL_OFF_FLAG))))
                continue;
            if (light->Parallel && Test_Flag(object, INFINITE_FLAG))
                throw POV_EXCEPTION(kParamErr, "Parallel photon emission needs bounded targets.");
            if (PhotonLightAffectsObject(light, object))
                targets.push_back(object);
        }
        else if (object->Type & IS_COMPOUND_OBJECT)
            ProgressiveTargets(static_cast<CSG*>(object)->children, light, targets);
    }
}
}

void PhotonShootingTask::ShootProgressive(unsigned int pass, unsigned int shard)
{
    auto* data = GetViewDataPtr();
    constexpr unsigned int batch = ProgressivePhotonBudget::batchSize;
    for (PreparedSetId setId = 0; setId < GetSceneData()->preparedSets.size(); ++setId)
    {
        auto& set = GetSceneData()->GetPreparedSet(setId);
        std::vector<LightSource*> lights;
        std::vector<std::vector<ObjectPtr>> targets;
        for (LightSource* light : set.lights)
        {
            if (light->Light_Type == FILL_LIGHT_SOURCE || light->colour.IsZero() ||
                (Test_Flag(light, PH_RFR_OFF_FLAG) && Test_Flag(light, PH_RFL_OFF_FLAG)))
                continue;
            std::vector<ObjectPtr> selected;
            ProgressiveTargets(set.objects, light, selected);
            if (selected.empty())
                continue;
            if (light->lightGroupLight && GetSceneData()->interiorMedia && GetSceneData()->photonSettings.maxMediaSteps > 0)
                throw POV_EXCEPTION(kParamErr, "Photon method 2 does not yet support volumetric photons from light groups.");
            lights.push_back(light);
            targets.push_back(std::move(selected));
        }
        if (lights.empty())
            continue;
        std::vector<RayInteriorVector> pointInteriors(lights.size());
        for (size_t li = 0; li < lights.size(); ++li)
            if (!lights[li]->emitter && !lights[li]->Area_Light && !lights[li]->Parallel)
                trace.FindContainingInteriors(lights[li]->Center, pointInteriors[li], setId);
        std::vector<std::vector<double>> probability(lights.size());
        std::vector<size_t> feedbackOffset;
        for (size_t li = 0; li < lights.size(); ++li)
        {
            feedbackOffset.push_back(data->progressiveFeedback.size());
            double total = 0.0;
            for (ObjectPtr target : targets[li])
            {
                double score = 1.0;
                for (const auto& previous : set.progressiveFeedback)
                    if (previous.light == lights[li] && previous.target == target && previous.attempted)
                        score = 0.05 + std::sqrt(double(previous.hit) / previous.attempted);
                probability[li].push_back(score);
                total += score;
                data->progressiveFeedback.push_back({lights[li], target, setId});
            }
            for (double& score : probability[li])
                score /= total;
        }
        std::vector<Vector3d> axes;
        std::vector<double> widths;
        RayInteriorVector originInteriors;
        for (unsigned int index = shard; index < batch; index += ProgressivePhotonBudget::shards)
        {
            Cooperate();
            const auto key = DeriveKey(DeriveKey(data->stochasticRandomSeedBase, kDrawPhoton, setId),
                                       kDrawPhoton, std::uint64_t(pass) * batch + index);
            const size_t li = std::min(lights.size() - 1, size_t(Draw(key, kDrawPhoton, 0) * lights.size()));
            LightSource* light = lights[li];
            SurfaceLightEmitter fallback(*light, targets[li], probability[li]);
            const Emitter& emitter = light->emitter ? *light->emitter : fallback;
            const EmitterSample origin = emitter.Sample(Draw(key, kDrawPhoton, 1), key);
            axes.clear();
            widths.clear();
            for (ObjectPtr target : targets[li])
            {
                const Vector3d centre = Vector3d(target->BBox.lowerLeft) + Vector3d(target->BBox.size) * 0.5;
                const Vector3d delta = centre - origin.position;
                const double distance = delta.length();
                const double radius = Vector3d(target->BBox.size).length() * 0.5;
                axes.push_back(distance > EPSILON ? delta / distance : Vector3d(0, 0, 1));
                const double sine2 = distance > radius ? radius * radius / (distance * distance) : 1.0;
                widths.push_back(distance > radius ? sine2 / (1.0 + std::sqrt(std::max(0.0, 1.0 - sine2))) : 2.0);
            }
            double pdf;
            const double uSample = (index + Draw(key, kDrawPhoton, 2)) / batch;
            Vector3d direction = emitter.Direction(origin, uSample, Draw(key, kDrawPhoton, 3), pdf);
            const bool targeted = !light->Parallel && Draw(key, kDrawPhoton, 4) < 0.9;
            size_t selectedTarget = 0;
            if (targeted)
            {
                double selector = Draw(key, kDrawPhoton, 5);
                while (selectedTarget + 1 < axes.size() && selector >= probability[li][selectedTarget])
                    selector -= probability[li][selectedTarget++];
                const size_t ti = selectedTarget;
                const Vector3d axis = axes[ti];
                Vector3d u = cross(std::abs(axis[Y]) < 0.9 ? Vector3d(0, 1, 0) : Vector3d(1, 0, 0), axis).normalized();
                const Vector3d v = cross(axis, u);
                const double deficit = uSample * widths[ti];
                const double z = 1.0 - deficit;
                const double r = std::sqrt(std::max(0.0, deficit * (2.0 - deficit)));
                const double angle = 2.0 * M_PI * Draw(key, kDrawPhoton, 3);
                direction = axis * z + u * (r * std::cos(angle)) + v * (r * std::sin(angle));
            }
            if (!light->Parallel)
            {
                pdf = 0.1 / (4.0 * M_PI);
                for (size_t ti = 0; ti < axes.size(); ++ti)
                {
                    const double projection = dot(direction, axes[ti]);
                    const bool inside = widths[ti] >= 1.0 ? projection >= 1.0 - widths[ti] :
                        projection >= 0.0 && (direction - axes[ti] * projection).lengthSqr() <= widths[ti] * (2.0 - widths[ti]);
                    if ((targeted && selectedTarget == ti) || inside)
                        pdf += 0.9 * probability[li][ti] / (2.0 * M_PI * widths[ti]);
                }
            }
            TraceTicket ticket(maxTraceLevel, adcBailout);
            Ray ray(ticket);
            ray.Origin = origin.position;
            ray.Direction = direction;
            ray.SetPreparedSetId(setId);
            ray.SetKey(key);
            ray.SetFlags(Ray::PrimaryRay, false, true);
            if (!light->emitter && !light->Area_Light && !light->Parallel)
            {
                for (Interior* interior : pointInteriors[li])
                    ray.AppendInterior(interior);
            }
            else
            {
                originInteriors.clear();
                trace.FindContainingInteriors(ray.Origin, originInteriors, setId);
                for (Interior* interior : originInteriors)
                    ray.AppendInterior(interior);
            }
            data->photonSourceLight = light;
            data->photonTargetObject = targets[li].front();
            data->Light_Is_Global = !light->lightGroupLight;
            data->passThruPrev = true;
            data->passThruThis = false;
            data->photonDepth = 0.0;
            data->photonSpread = std::max(1.0e-8, Vector3d(targets[li].front()->BBox.size).length() * 0.025) *
                                 ProgressivePhotonBudget::RadiusScale(pass, 3);
            MathColour flux = emitter.Intensity() * origin.weight * (lights.size() / (batch * pdf));
            flux *= computeAttenuation(light, ray, (origin.position - light->Center).length());
            data->Stats()[Number_Of_Photons_Shot]++;
            ColourChannel unused;
            const size_t before = data->progressiveSurface.size() + data->progressiveMedia.size();
            trace.TraceRay(ray, flux, unused, 1.0, false);
            if (targeted)
            {
                auto& feedback = data->progressiveFeedback[feedbackOffset[li] + selectedTarget];
                feedback.attempted++;
                feedback.hit += data->progressiveSurface.size() + data->progressiveMedia.size() > before;
            }
        }
    }
}

void PhotonShootingTask::Stopped()
{
    // nothing to do for now [trf]
}

void PhotonShootingTask::Finish()
{
    GetViewDataPtr()->timeType = TraceThreadData::kPhotonTime;
    GetViewDataPtr()->realTime = ConsumedRealTime();
    GetViewDataPtr()->cpuTime = ConsumedCPUTime();
}






void PhotonShootingTask::ShootPhotonsAtObject(PhotonShootingUnit& unit)
{
    LightTargetCombo& combo = unit.lightAndObject;
    MathColour colour;             /* light color */
    MathColour photonColour;       /* photon color */
    ColourChannel dummyTransm;
    int i;                         /* counter */
    DBL theta, phi;                /* rotation angles */
    DBL dphi;              /* deltas for theta and phi */
    DBL jittheta, jitphi;          /* jittered versions of theta and phi */
    DBL minphi,maxphi;
                                   /* these are minimum and maximum for theta and
                                       phi for the spiral shooting */
    DBL Attenuation;               /* light attenuation for spotlight */
    TRANSFORM Trans;               /* transformation for rotation */
    int mergedFlags=0;             /* merged flags to see if we should shoot photons */
    int notComputed=true;          /* have the ray containers been computed for this point yet?*/
    int hitAtLeastOnce = false;    /* have we hit the object at least once - for autostop stuff */
    ViewThreadData *renderDataPtr = GetViewDataPtr();

    /* get the light source colour */
    colour = combo.light->colour;

    /* set global variable stuff */
    renderDataPtr->photonSourceLight = combo.light;
    renderDataPtr->photonTargetObject = combo.target;
    renderDataPtr->Light_Is_Global = !combo.light->lightGroupLight;

    /* first, check on various flags... make sure all is a go for this ObjectPtr */
    mergedFlags = combo.computeMergedFlags();

    if (!( ((mergedFlags & PH_RFR_ON_FLAG) && !(mergedFlags & PH_RFR_OFF_FLAG)) ||
           ((mergedFlags & PH_RFL_ON_FLAG) && !(mergedFlags & PH_RFL_OFF_FLAG)) ))
        /* it is a no-go for this object... bail out now */
        return;

    renderDataPtr->photonSpread = combo.photonSpread;

    /* ---------------------------------------------
           main ray-shooting loop
       --------------------------------------------- */
    i = 0;
    notComputed = true;
    std::vector<Interior*> originInteriors;
    Vector3d lastOrigin;
    bool haveOrigin = false;
    const std::uint64_t comboKey = DeriveKey(renderDataPtr->stochasticRandomSeedBase, kDrawPhoton, combo.serial);
    std::uint64_t thetaIndex = combo.thetaIndexBase;
    for(theta=combo.mintheta; theta<combo.maxtheta; theta+=combo.dtheta, thetaIndex++)
    {
        if (combo.parallelChunk && strategy->pastCutoff(combo.serial, thetaIndex))
            break;
        const std::uint64_t thetaKey = DeriveKey(comboKey, kDrawPhoton, thetaIndex);
        Cooperate();
        SendProgress();
        renderDataPtr->hitObject = false;
        if (combo.parallelChunk)
            strategy->beginRing(unit, renderDataPtr);

        if (theta<EPSILON)
        {
            dphi=2*M_PI;
        }
        else
        {
            /* remember that for area lights, "theta" really means "radius" */
            if (combo.light->Parallel)
            {
                dphi = combo.dtheta / theta;
            }
            else
            {
                dphi=combo.dtheta/sin(theta);
            }
        }

        // FIXME: should copy from previously computed shootingdirection
        ShootingDirection shootingDirection(combo.light,combo.target);
        shootingDirection.compute();

        minphi = -M_PI + dphi*Draw(thetaKey, kDrawPhoton, 0)*0.5;
        maxphi = M_PI - dphi/2 + (minphi+M_PI);
        std::uint64_t phiIndex = 0;
        for(phi=minphi; phi<maxphi; phi+=dphi, phiIndex++)
        {
            const std::uint64_t photonKey = DeriveKey(thetaKey, kDrawPhoton, phiIndex + 1);
            int x_samples,y_samples;
            int area_x, area_y;
            /* ------------------- shoot one photon ------------------ */

            /* jitter theta & phi */
            jitphi = phi + (dphi)*(Draw(photonKey, kDrawPhoton, 0) - 0.5)*1.0*GetSceneData()->photonSettings.jitter;
            jittheta = theta + (combo.dtheta)*(Draw(photonKey, kDrawPhoton, 1) - 0.5)*1.0*GetSceneData()->photonSettings.jitter;

            /* actually, shoot multiple samples for area light */
            if(combo.light->Area_Light && combo.light->Photon_Area_Light && !combo.light->Parallel)
            {
                x_samples = combo.light->Area_Size1;
                y_samples = combo.light->Area_Size2;
            }
            else
            {
                x_samples = 1;
                y_samples = 1;
            }

            for(area_x=0; area_x<x_samples; area_x++)
            {
                for(area_y=0; area_y<y_samples; area_y++)
                {
                    TraceTicket ticket(maxTraceLevel, adcBailout);
                    Ray ray(ticket);
                    ray.SetPreparedSetId(unit.preparedSetId);
                    ray.SetKey(DeriveKey(photonKey, kDrawPhoton, 2 + std::uint64_t(area_x) * y_samples + area_y));

                    ray.Origin = combo.light->Center;

                    if (combo.light->Area_Light && combo.light->Photon_Area_Light && !combo.light->Parallel)
                    {
                        shootingDirection.recomputeForAreaLight(ray,area_x,area_y);
                        /* we must recompute the media containers (new start point) */
                        notComputed = true;
                    }

                    DBL dist_of_initial_from_center;

                    if (combo.light->Parallel)
                    {
                        DBL a;
                        Vector3d v;
                        /* assign the direction */
                        ray.Direction = combo.light->Direction;

                        /* project ctr onto plane defined by Direction & light location */

                        a = dot(ray.Direction, shootingDirection.toctr);
                        v = ray.Direction * (-a*shootingDirection.dist); /* MAYBE NEEDS TO BE NEGATIVE! */

                        ray.Origin = shootingDirection.ctr + v;

                        /* move point along "left" distance theta (remember theta means rad) */
                        v = shootingDirection.left * jittheta;

                        /* rotate pt around ray.Direction by phi */
                        /* use POV funcitons... slower but easy */
                        Compute_Axis_Rotation_Transform(&Trans,combo.light->Direction,jitphi);
                        MTransPoint(v, v, &Trans);

                        ray.Origin += v;

                        // compute the length of "v" if we're going to use it
                        if (combo.light->Light_Type == CYLINDER_SOURCE)
                        {
                            Vector3d initial_from_center;
                            initial_from_center = ray.Origin - combo.light->Center;
                            dist_of_initial_from_center = initial_from_center.length();
                        }
                    }
                    else
                    {
                        DBL st,ct;                     /* cos(theta) & sin(theta) for rotation */
                        /* rotate toctr by theta around up */
                        st = sin(jittheta);
                        ct = cos(jittheta);
                        /* use fast rotation */
                        Vector3d v = -st * shootingDirection.left + ct * shootingDirection.toctr;

                        /* then rotate by phi around toctr */
                        /* use POV funcitons... slower but easy */
                        Compute_Axis_Rotation_Transform(&Trans,shootingDirection.toctr,jitphi);
                        MTransPoint(ray.Direction, v, &Trans);
                    }

                    /* ------ attenuation for spot/cylinder (copied from point.c) ---- */
                    Attenuation = computeAttenuation(combo.light, ray, dist_of_initial_from_center);

                    /* set up defaults for reflection, refraction */
                    renderDataPtr->passThruPrev = true;
                    renderDataPtr->passThruThis = false;

                    renderDataPtr->photonDepth = 0.0;
                    // GetViewDataPtr()->Trace_Level = 0;
                    // Total_Depth = 0.0;
                    renderDataPtr->Stats()[Number_Of_Photons_Shot]++;

                    /* attenuate for area light extra samples */
                    Attenuation/=(x_samples*y_samples);

                    /* compute photon color from light source & attenuation */

                    photonColour = colour * Attenuation;

                    if (Attenuation<0.00001) continue;

                    /* handle the projected_through object if it exists */
                    if (combo.light->Projected_Through_Object != nullptr)
                    {
                        /* try to intersect ray with projected-through ObjectPtr */
                        Intersection Intersect;

                        Intersect.Object = nullptr;
                        if ( trace.FindIntersection(combo.light->Projected_Through_Object, Intersect, ray) )
                        {
                            /* we must recompute the media containers (new start point) */
                            notComputed = true;

                            /* we did hit it, so find the 'real' starting point of the ray */
                            /* find the farthest intersection */
                            ray.Origin += (Intersect.Depth+EPSILON) * ray.Direction;
                            renderDataPtr->photonDepth += Intersect.Depth+EPSILON;
                            while(trace.FindIntersection( combo.light->Projected_Through_Object, Intersect, ray) )
                            {
                                ray.Origin += (Intersect.Depth+EPSILON) * ray.Direction;
                                renderDataPtr->photonDepth += Intersect.Depth+EPSILON;
                            }
                        }
                        else
                        {
                            /* we didn't hit it, so stop now */
                            continue;
                        }

                    }

                    /* As mike said, "fire photon torpedo!" */
                    //Initialize_Ray_Containers(&ray);
                    ray.ClearInteriors ();

                    // Photons leaving the same point start inside the same objects, so test each origin once.
                    if (!haveOrigin || ray.Origin[X] != lastOrigin[X] || ray.Origin[Y] != lastOrigin[Y] || ray.Origin[Z] != lastOrigin[Z])
                    {
                        originInteriors.clear();
                        PreparedSet& view = GetSceneData()->GetPreparedSet(unit.preparedSetId);
                        for(std::vector<ObjectPtr>::iterator object = view.objects.begin(); object != view.objects.end(); object++)
                        {
                            if ((*object)->Inside(ray.Origin, renderDataPtr) && ((*object)->interior != nullptr))
                                originInteriors.push_back((*object)->interior.get());
                        }
                        lastOrigin = ray.Origin;
                        haveOrigin = true;
                    }
                    for (Interior* interior : originInteriors)
                        ray.AppendInterior(interior);

                    notComputed = false;
                    //disp_elem = 0;   /* for dispersion */
                    //disp_nelems = 0; /* for dispersion */

                    ray.SetFlags(Ray::PrimaryRay, false, true);
                    trace.TraceRay(ray, photonColour, dummyTransm, 1.0, false);

                    /* display here */
                    if ((i++%100) == 0)
                    {
                        Cooperate();
                        SendProgress();
                    }

                } // for(area_y...)
            } // for(area_x...)
        }

        /* if we didn't hit anything and we're past the autostop angle, then
             we should stop

             as per suggestion from Smellenberg, changed autostop to a percentage
             of the object's bounding sphere. */

        /* suggested by Pabs, we only use autostop if we have it it once */
        if (renderDataPtr->hitObject) hitAtLeastOnce=true;

        if (combo.parallelChunk)
            strategy->recordRing(unit, renderDataPtr);
        if (!combo.parallelChunk && hitAtLeastOnce && !renderDataPtr->hitObject && renderDataPtr->photonTargetObject)
            if (theta > GetSceneData()->photonSettings.autoStopPercent*combo.maxtheta)
                break;
    } /* end of rays loop */
}

DBL PhotonShootingTask::computeAttenuation(const LightSource* Light, const Ray& ray, DBL dist_of_initial_from_center)
{
    DBL costheta_spot;
    DBL Attenuation = 1.0;

    /* ---------- spot light --------- */
    if (Light->Light_Type == SPOT_SOURCE)
    {
        costheta_spot = dot(ray.Direction, Light->Direction);

        if (costheta_spot > 0.0)
        {
            Attenuation = pow(costheta_spot, Light->Coeff);

            if (Light->Radius > 0.0)
                Attenuation *= cubic_spline(Light->Falloff, Light->Radius, costheta_spot);

        }
        else
            Attenuation = 0.0;
    }
    /* ---------- cylinder light ----------- */
    else if (Light->Light_Type == CYLINDER_SOURCE)
    {
        DBL k, len;

        k = dot(ray.Direction, Light->Direction);

        if (k > 0.0)
        {
            len = dist_of_initial_from_center;

            if (len < Light->Falloff)
            {
                DBL dist = 1.0 - len / Light->Falloff;
                Attenuation = pow(dist, Light->Coeff);

                if (Light->Radius > 0.0 && len > Light->Radius)
                    Attenuation *= cubic_spline(0.0, 1.0 - Light->Radius / Light->Falloff, dist);

            }
            else
                Attenuation = 0.0;
        }
        else
            Attenuation = 0.0;
    }
    return Attenuation;
}

}
// end of namespace pov
