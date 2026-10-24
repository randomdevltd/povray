//******************************************************************************
///
/// @file backend/bounding/boundingtask.cpp
///
/// @todo   What's in here?
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
#include "backend/bounding/boundingtask.h"

// C++ variants of C standard header files
//  (none at the moment)

// C++ standard header files
#include <cstdlib>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Boost header files
#include <boost/bind.hpp>

// POV-Ray header files (base module)
#include "base/timer.h"

// POV-Ray header files (core module)
#include "core/bounding/bsptree.h"
#include "core/math/matrix.h"
#include "core/scene/camera.h"
#include "core/scene/object.h"
#include "core/scene/tracethreaddata.h"
#include "core/shape/csg.h"
#include "core/material/media.h"
#include "core/shape/mesh.h"
#include "core/support/parallel.h"

// POV-Ray header files (POVMS module)
#include "povms/povmsid.h"

// POV-Ray header files (backend module)
#include "backend/control/messagefactory.h"
#include "backend/scene/backendscenedata.h"
#include "backend/support/task.h"

// this must be the last file included
#include "base/povdebug.h"

namespace pov
{

using std::vector;

class SceneObjects final : public BSPTree::Objects
{
    public:
        vector<ObjectPtr> infinite;
        vector<ObjectPtr> finite;
        unsigned int numLights;

        SceneObjects(vector<ObjectPtr>& objects)
        {
            numLights = 0;
            for(vector<ObjectPtr>::iterator i(objects.begin()); i != objects.end(); i++)
            {
                if(Test_Flag((*i), INFINITE_FLAG))
                {
                    infinite.push_back(*i);
                    if (((*i)->Type & LIGHT_SOURCE_OBJECT) != 0)
                        numLights++;
                }
                else
                    finite.push_back(*i);
            }
        }

        virtual ~SceneObjects() override
        {
            // nothing to do
        }

        virtual unsigned int size() const override
        {
            return finite.size();
        }

        virtual float GetMin(unsigned int axis, unsigned int i) const override
        {
            return finite[i]->BBox.lowerLeft[axis];
        }

        virtual float GetMax(unsigned int axis, unsigned int i) const override
        {
            return (finite[i]->BBox.lowerLeft[axis] + finite[i]->BBox.size[axis]);
        }
};

class BSPProgress final : public BSPTree::Progress
{
    public:
        BSPProgress(RenderBackend::SceneId sid, POVMSAddress addr, Task& task) :
            mTask(task),
            sceneId(sid),
            frontendAddress(addr),
            lastProgressTime(0)
        {
        }

        virtual void operator()(unsigned int nodes) const override
        {
            if((timer.ElapsedRealTime() - lastProgressTime) > 1000) // update progress at most every second
            {
                POVMS_Object obj(kPOVObjectClass_BoundingProgress);
                obj.SetLong(kPOVAttrib_RealTime, timer.ElapsedRealTime());
                obj.SetLong(kPOVAttrib_CurrentNodeCount, nodes);
                RenderBackend::SendSceneOutput(sceneId, frontendAddress, kPOVMsgIdent_Progress, obj);

                mTask.Cooperate();

                lastProgressTime = timer.ElapsedRealTime();
            }
        }
    private:
        Task& mTask;
        RenderBackend::SceneId sceneId;
        POVMSAddress frontendAddress;
        Timer timer;
        mutable POV_LONG lastProgressTime;

        BSPProgress() = delete;
};

BoundingTask::BoundingTask(std::shared_ptr<BackendSceneData> sd, unsigned int bt, size_t seed, size_t view) :
    SceneTask(new TraceThreadData(std::dynamic_pointer_cast<SceneData>(sd), seed), boost::bind(&BoundingTask::SendFatalError, this, _1), "Bounding", sd),
    sceneData(sd),
    boundingThreshold(bt),
    preparedSetId(view)
{
}

BoundingTask::~BoundingTask()
{
}

void BoundingTask::AppendObject(ObjectPtr p)
{
    sceneData->objects.push_back(p);
}

void BoundingTask::Run()
{
    PreparedSet& view = sceneData->GetPreparedSet(preparedSetId);
    view.boundingMethod = sceneData->boundingMethod;
    if((view.objects.size() < boundingThreshold) || (view.boundingMethod == 0))
    {
        SceneObjects objects(view.objects);
        view.boundingMethod = 0;
        view.numberOfFiniteObjects = objects.finite.size();
        view.numberOfInfiniteObjects = objects.infinite.size() - objects.numLights;
        return;
    }

    switch(view.boundingMethod)
    {
        case 2:
        {
            // new BSP tree code
            SceneObjects objects(view.objects);
            BSPProgress progress(sceneData->sceneId, sceneData->frontendAddress, *this);

            view.objects.clear();
            view.objects.insert(view.objects.end(), objects.finite.begin(), objects.finite.end());
            view.objects.insert(view.objects.end(), objects.infinite.begin(), objects.infinite.end());
            view.numberOfFiniteObjects = objects.finite.size();
            view.numberOfInfiniteObjects = objects.infinite.size() - objects.numLights;
            view.tree = new BSPTree(sceneData->bspMaxDepth, sceneData->bspObjectIsectCost, sceneData->bspBaseAccessCost, sceneData->bspChildAccessCost, sceneData->bspMissChance);
            view.tree->build(progress, objects,
                             view.nodes, view.splitNodes, view.objectNodes, view.emptyNodes,
                             view.maxObjects, view.averageObjects, view.maxDepth, view.averageDepth,
                             view.aborts, view.averageAborts, view.averageAbortObjects, sceneData->inputFile);
            break;
        }
        case 1:
        {
            // old bounding box code
            unsigned int numberOfLightSources;

            Build_Bounding_Slabs(&(view.boundingSlabs), view.objects, view.numberOfFiniteObjects,
                                 view.numberOfInfiniteObjects, numberOfLightSources);
            delete view.flatSlabs;
            view.flatSlabs = Build_Flat_BBox_Tree(view.boundingSlabs);
            break;
        }
    }
}

void BoundingTask::Stopped()
{
}

void BoundingTask::Finish()
{
    GetSceneDataPtr()->timeType = TraceThreadData::kBoundingTime;
    GetSceneDataPtr()->realTime = ConsumedRealTime();
    GetSceneDataPtr()->cpuTime = ConsumedCPUTime();
}

void BoundingTask::SendFatalError(Exception& e)
{
    // if the front-end has been told about this exception already, we don't tell it again
    if (e.frontendnotified(true))
        return;

    POVMS_Message msg(kPOVObjectClass_ControlData, kPOVMsgClass_SceneOutput, kPOVMsgIdent_Error);

    msg.SetString(kPOVAttrib_EnglishText, e.what());
    msg.SetInt(kPOVAttrib_Error, 0);
    msg.SetInt(kPOVAttrib_SceneId, sceneData->sceneId);
    msg.SetSourceAddress(sceneData->backendAddress);
    msg.SetDestinationAddress(sceneData->frontendAddress);

    POVMS_SendMessage(msg);
}


MeshBuildTask::MeshBuildTask(std::shared_ptr<BackendSceneData> sd, size_t sd_seed, unsigned int t, TraceThreadData *pd) :
    SceneTask(new TraceThreadData(std::dynamic_pointer_cast<SceneData>(sd), sd_seed, false), boost::bind(&MeshBuildTask::SendFatalError, this, _1), "Mesh", sd),
    sceneData(sd),
    seed(sd_seed),
    threads(t),
    parseData(pd),
    buildCpuTime(0)
{
}

MeshBuildTask::~MeshBuildTask()
{
}

void MeshBuildTask::Run()
{
    Timer phase;
    std::vector<std::shared_ptr<DeferredMeshState>> needed;
    std::unordered_set<const DeferredMeshState *> queued;
    std::unordered_set<ConstObjectPtr> visited;
    std::unordered_set<const void *> caches;
    std::function<void(ObjectPtr)> collect = [&](ObjectPtr object)
    {
        if ((object == nullptr) || !visited.insert(object).second)
            return;
        CollectPreparedCaches(object, caches);
        if (Mesh *mesh = dynamic_cast<Mesh *>(object))
        {
            if (mesh->Pending() && !mesh->Deferred()->reported && queued.insert(mesh->Deferred().get()).second)
                needed.push_back(mesh->Deferred());
            return;
        }
        if (LightSource *light = dynamic_cast<LightSource *>(object))
            collect(light->Projected_Through_Object);
        if (CompoundObject *compound = dynamic_cast<CompoundObject *>(object))
            for (ObjectPtr child : compound->children)
                collect(child);
    };
    for (const std::unique_ptr<PreparedSet>& set : sceneData->preparedSets)
        for (ObjectPtr object : set->objects)
            collect(object);
    // a look_at target is measured built, so its meshes are built with the scene's
    for (Camera *camera : FramedCameras(*sceneData))
        collect(camera->frameTarget.get());

    DeferredMeshPool *pool = sceneData->meshPool.get();
    unsigned int discarded = 0;
    POV_LONG wasted = 0;
    std::vector<std::unique_ptr<TraceThreadData>> workers;
    const size_t count = std::max<size_t>(1, std::min<size_t>(threads, needed.size()));
    for (size_t i = 0; i < count; ++i)
        workers.emplace_back(new TraceThreadData(std::dynamic_pointer_cast<SceneData>(sceneData), seed + i + 1, false));
    try
    {
        const auto cooperate = [&]()
        {
            try
            {
                Cooperate();
            }
            catch (...)
            {
                sceneData->meshBuildCancelled->store(true, std::memory_order_relaxed);
                throw;
            }
        };
        if (pool != nullptr)
        {
            // what no set keeps is never built: queued builds are skipped and running ones stop
            for (const std::weak_ptr<DeferredMeshState>& recorded : sceneData->deferredMeshes)
                if (std::shared_ptr<DeferredMeshState> state = recorded.lock())
                    if (!state->reported && (queued.count(state.get()) == 0))
                        state->Discard();
            // the rest of a media grid is filled below on every thread, not on one eager thread each
            for (const auto& media : sceneData->eagerMediaFields)
            {
                StopEagerField(*media.second);
                if (!EagerFieldKept(*media.second, caches))
                    media.first->Discard();
            }
            const size_t helpers = std::min<size_t>(count, (threads > pool->ThreadCount()) ? threads - pool->ThreadCount() : 1);
            std::vector<POV_LONG> cpu(helpers, 0);
            ParallelFor(needed.size(), helpers, [&](size_t i, size_t worker)
            {
                Timer timer;
                needed[i]->TryBuild(workers[worker].get(), -(int(worker) + 1));
                if (timer.HasValidThreadCPUTime())
                    cpu[worker] += timer.ElapsedThreadCPUTime();
            }, cooperate);
            for (size_t worker = (helpers > 1) ? 0 : 1; worker < helpers; ++worker)
                buildCpuTime += cpu[worker];
            pool->Finish(cooperate);

            for (const std::weak_ptr<DeferredMeshState>& recorded : sceneData->deferredMeshes)
                if (std::shared_ptr<DeferredMeshState> state = recorded.lock())
                    if (state->discarded && (state->startedAt != std::chrono::steady_clock::time_point()))
                    {
                        ++discarded;
                        wasted += state->cpuTime;
                    }
            bool sceneFilters = false;
            for (const auto& filter : sceneData->preparedSetFilters)
                sceneFilters = sceneFilters || filter.first.specified;
            if ((wasted >= kWastedEagerWarning) && sceneFilters)
                mpMessageFactory->Warning(kWarningGeneral, "Eager preparation built %u mesh(es) that the scene's tag filters left out, spending "
                                          "%.1f CPU-seconds for nothing. Give the filter as Filter_Tags on the command line or in an INI file, "
                                          "so builds start only when a kept object places them, or turn eager preparation off "
                                          "(Eager_Preparation=off, -WE).", discarded, wasted / 1000.0);
            else if (wasted >= kWastedEagerWarning)
                mpMessageFactory->Warning(kWarningGeneral, "Eager preparation built %u mesh(es) declared but never placed, spending %.1f "
                                          "CPU-seconds for nothing. Remove the unused declarations, or turn eager preparation off "
                                          "(Eager_Preparation=off, -WE).", discarded, wasted / 1000.0);
        }
        std::vector<POV_LONG> cpu(count, 0);
        ParallelFor(needed.size(), count, [&](size_t i, size_t worker)
        {
            Timer timer;
            needed[i]->Build(workers[worker].get(), -(int(worker) + 1));
            if (timer.HasValidThreadCPUTime())
                cpu[worker] += timer.ElapsedThreadCPUTime();
        }, cooperate);
        // A single build runs on this task's own thread, whose time is counted already.
        for (size_t worker = (count > 1) ? 0 : 1; worker < count; ++worker)
            buildCpuTime += cpu[worker];
        if (!sceneData->eagerMediaFields.empty() || !sceneData->eagerMediaUnsafe.empty())
            FinishMediaFields(*workers.front(), caches, cooperate);
    }
    catch (const StopThreadException&)
    {
        if (pool != nullptr)
        {
            pool->Stop();
            pool->Dispose();
        }
        throw;
    }
    catch (...)
    {
        sceneData->meshBuildCancelled->store(true, std::memory_order_relaxed);
        if (pool != nullptr)
        {
            pool->Stop();
            pool->Dispose();
        }
        for (const std::weak_ptr<DeferredMeshState>& recorded : sceneData->deferredMeshes)
            if (std::shared_ptr<DeferredMeshState> state = recorded.lock())
            if (std::exception_ptr failure = state->Failure())
            {
                try
                {
                    std::rethrow_exception(failure);
                }
                catch (const std::exception& error)
                {
                    mpMessageFactory->ErrorAt(state->source, "%s", error.what());
                }
            }
        throw;
    }
    for (const std::unique_ptr<TraceThreadData>& worker : workers)
        parseData->Stats() += worker->Stats();
    for (const std::shared_ptr<DeferredMeshState>& state : needed)
        state->Release();
    POV_LONG eagerCpuTime = 0;
    unsigned int eagerThreads = 0;
    if (pool != nullptr)
    {
        eagerThreads = pool->Started();
        for (const std::unique_ptr<TraceThreadData>& data : pool->Dispose())
            parseData->Stats() += data->Stats();
        eagerCpuTime = pool->CpuTime();
        buildCpuTime += eagerCpuTime;
    }
    std::vector<std::pair<std::shared_ptr<DeferredMeshState>, std::shared_ptr<Media::EagerField>>>().swap(sceneData->eagerMediaFields);
    std::vector<std::weak_ptr<Media::FastCache>>().swap(sceneData->eagerMediaUnsafe);

    unsigned int built = 0, builtAhead = 0, rebuilds = 0;
    for (const std::weak_ptr<DeferredMeshState>& recorded : sceneData->deferredMeshes)
    {
        std::shared_ptr<DeferredMeshState> state = recorded.lock();
        if (state == nullptr)
            continue;
        rebuilds += state->rebuilds;
        if (state->reported || state->discarded || !state->Built())
            continue;
        state->reported = true;
        ++built;
        if (state->lane > 0)
            ++builtAhead;
        for (const std::string& warning : state->warnings)
            mpMessageFactory->WarningAt(kWarningGeneral, state->source, "%s", warning.c_str());
        if (!state->debug.empty())
            mpMessageFactory->UserDebug(state->debug.c_str());
    }

    // A pending mesh's box was a placeholder, and a set's views have no box yet.
    std::unordered_set<ConstObjectPtr> views;
    for (const std::unique_ptr<PreparedSet>& set : sceneData->preparedSets)
        views.insert(set->views.begin(), set->views.end());
    std::unordered_map<ConstObjectPtr, bool> rebounded;
    std::function<bool(ObjectPtr)> rebound = [&](ObjectPtr object) -> bool
    {
        auto found = rebounded.find(object);
        if (found != rebounded.end())
            return found->second;
        bool changed = false;
        if (Mesh *mesh = dynamic_cast<Mesh *>(object))
            changed = mesh->Resolve();
        else if (CompoundObject *compound = dynamic_cast<CompoundObject *>(object))
        {
            if (LightSource *light = dynamic_cast<LightSource *>(object))
                if (light->Projected_Through_Object != nullptr)
                    rebound(light->Projected_Through_Object);
            for (ObjectPtr child : compound->children)
                changed = rebound(child) || changed;
            CSG *csg = dynamic_cast<CSG *>(object);
            if ((csg != nullptr) && (changed || (views.count(object) != 0)) && csg->Bound.empty())
            {
                Make_BBox(csg->BBox, -BOUND_HUGE/2, -BOUND_HUGE/2, -BOUND_HUGE/2, BOUND_HUGE, BOUND_HUGE, BOUND_HUGE);
                csg->Compute_BBox();
                Update_Infinite_Flag(csg);
                changed = true;
            }
        }
        rebounded[object] = changed;
        return changed;
    };
    for (const std::unique_ptr<PreparedSet>& set : sceneData->preparedSets)
        for (ObjectPtr object : set->objects)
            rebound(object);

    const size_t recorded = sceneData->deferredMeshes.size();
    if (recorded > 0)
    {
        const unsigned int queries = sceneData->deferredMeshesBuiltForQueries;
        mpMessageFactory->Info("Deferred meshes: %u recorded, %u built for parse-time queries, %u built for the scene, %u not needed.",
                               unsigned(recorded), queries, built, unsigned(recorded - queries - built));
        const POV_LONG phaseCpu = (phase.HasValidThreadCPUTime() ? phase.ElapsedThreadCPUTime() : 0) + buildCpuTime - eagerCpuTime;
        mpMessageFactory->Info("Mesh build phase: %.3f s wall, %.3f s CPU on %u thread(s); parse-time query builds: %.3f s wall, %.3f s CPU.",
                               phase.ElapsedRealTime() / 1000.0, phaseCpu / 1000.0, unsigned(count),
                               sceneData->meshQueryRealTime / 1000.0, sceneData->meshQueryCpuTime / 1000.0);
        if (pool != nullptr)
            mpMessageFactory->Info("Eager preparation: %u of %u queried meshes and %u of %u scene meshes built ahead; %u build(s) "
                                   "discarded unused (%.3f s CPU), %u redone after a global setting changed, %u left to the end of the parse "
                                   "because a function tests an object or runs a function a snapshot cannot take; %.3f s CPU on %u thread(s).",
                                   sceneData->deferredMeshesQueriedBuiltAhead, queries, builtAhead, built, discarded, wasted / 1000.0, rebuilds,
                                   sceneData->deferredMeshesLeftToTask, eagerCpuTime / 1000.0, eagerThreads);
    }
    std::vector<std::weak_ptr<DeferredMeshState>>().swap(sceneData->deferredMeshes);
}

// below this many CPU milliseconds discarded eager builds go unreported but in the summary
const POV_LONG MeshBuildTask::kWastedEagerWarning = 1000;

void MeshBuildTask::FinishMediaFields(const TraceThreadData& final, const std::unordered_set<const void *>& caches, const std::function<void()>& cooperate)
{
    const auto kept = [&caches](const std::vector<std::weak_ptr<Media::FastCache>>& list)
    {
        unsigned int count = 0;
        for (const std::weak_ptr<Media::FastCache>& cache : list)
            if (const std::shared_ptr<Media::FastCache> live = cache.lock())
                count += (caches.count(live.get()) != 0) ? 1 : 0;
        return count;
    };
    const unsigned int unsafe = kept(sceneData->eagerMediaUnsafe);
    const MeshBuildGlobals now = MeshBuildGlobals::Of(final);
    std::vector<std::unique_ptr<TraceThreadData>> data;
    std::vector<TraceThreadData *> helpers;
    for (unsigned int i = 0; i < threads; ++i)
    {
        data.emplace_back(new TraceThreadData(std::dynamic_pointer_cast<SceneData>(sceneData), seed + i + 1, false));
        helpers.push_back(data.back().get());
    }
    Timer finishing;
    unsigned int prepared = 0;
    int claimed = 0, slabs = 0;
    POV_LONG cpu = 0;
    std::unordered_set<const Media::EagerField *> fields;
    for (const auto& media : sceneData->eagerMediaFields)
    {
        if (!fields.insert(media.second.get()).second || media.first->discarded)
            continue;
        int before, total;
        EagerFieldSlabs(*media.second, before, total);
        claimed += before;
        slabs += total;
        const MeshBuildGlobals& recorded = media.first->recordedGlobals;
        for (TraceThreadData *helper : helpers)
            helper->FreezeGlobals(recorded.noiseGenerator, recorded.workingGamma, recorded.numberOfWaves);
        if (FinishEagerField(media.second, sceneData->EffectivePreparedMediaCells(), helpers,
                             [&](unsigned char reads) { return recorded.Differs(now, reads); }, cooperate))
            ++prepared;
        cpu += EagerFieldCpuTime(*media.second);
    }
    for (const std::unique_ptr<TraceThreadData>& helper : data)
        parseData->Stats() += helper->Stats();
    mpMessageFactory->Info("Eager media grids: %u of %u prepared before the render, %u more left to it by a density that tests an object or "
                           "runs a function a snapshot cannot take; %d of their %d slabs built during the parse; %.3f s CPU in all, %.3f s "
                           "finishing after it.", prepared, unsigned(fields.size()), unsafe, claimed, slabs, cpu / 1000.0,
                           finishing.ElapsedRealTime() / 1000.0);
}

std::vector<Camera *> FramedCameras(SceneData& sceneData)
{
    std::vector<Camera *> framed;
    std::unordered_set<const Camera *> seen;
    auto frames = [&](const Camera& camera) {
        if (camera.frameBounds && camera.frameTarget && seen.insert(&camera).second)
            framed.push_back(const_cast<Camera *>(&camera));
    };
    frames(sceneData.parsedCamera);
    for (const Camera& camera : sceneData.cameras)
        frames(camera);
    for (const std::shared_ptr<const Camera>& camera : sceneData.screenCameras)
        frames(*camera);
    return framed;
}

CameraFramingTask::CameraFramingTask(std::shared_ptr<BackendSceneData> sd, size_t sd_seed, TraceThreadData *pd) :
    SceneTask(new TraceThreadData(std::dynamic_pointer_cast<SceneData>(sd), sd_seed, false), boost::bind(&CameraFramingTask::SendFatalError, this, _1), "Parse", sd),
    sceneData(sd),
    parseData(pd)
{
}

CameraFramingTask::~CameraFramingTask()
{
}

// A target's meshes were built with the scene's; its boxes are redone above them, as for a prepared set.
static bool ResolveTarget(ObjectPtr object)
{
    if (Mesh *mesh = dynamic_cast<Mesh *>(object))
        return mesh->Resolve();
    CompoundObject *compound = dynamic_cast<CompoundObject *>(object);
    if (compound == nullptr)
        return false;
    if (LightSource *light = dynamic_cast<LightSource *>(object))
        if (light->Projected_Through_Object != nullptr)
            ResolveTarget(light->Projected_Through_Object);
    bool changed = false;
    for (ObjectPtr child : compound->children)
        changed = ResolveTarget(child) || changed;
    CSG *csg = dynamic_cast<CSG *>(object);
    if (changed && (csg != nullptr) && csg->Bound.empty())
    {
        Make_BBox(csg->BBox, -BOUND_HUGE/2, -BOUND_HUGE/2, -BOUND_HUGE/2, BOUND_HUGE, BOUND_HUGE, BOUND_HUGE);
        csg->Compute_BBox();
        Update_Infinite_Flag(csg);
    }
    return changed;
}

void CameraFramingTask::Run()
{
    for (Camera *camera : FramedCameras(*sceneData))
        Frame(*camera);
}

void CameraFramingTask::Frame(Camera& camera)
{
    ResolveTarget(camera.frameTarget.get());
    unsigned int unbounded = 0;
    const bool found = FrameTargetBounds(camera.frameTarget.get(), camera.frameLow, camera.frameHigh, unbounded);
    camera.frameTarget.reset();
    const SourceInfo& source = camera.frameSource;
    Exception failed = POV_EXCEPTION_CODE(kParseErr);
    if (!found)
        mpMessageFactory->ErrorAt(failed, source, camera.frameSelection ? "look_at tagged selects no object with finite non-empty bounds."
                                                                : "look_at object framing needs finite non-empty bounds.");
    if (unbounded > 0)
        mpMessageFactory->WarningAt(kWarningGeneral, source, camera.frameSelection
            ? "look_at tagged skipped %u unbounded object(s), such as planes, and frames the rest."
            : "look_at skipped %u unbounded part(s) of this object, such as planes, and frames the rest.", unbounded);
    switch (camera.Frame(camera.frameLow, camera.frameHigh, static_cast<TraceThreadData *>(GetDataPtr()), [this]() { Cooperate(); }))
    {
        case FrameResult::Failed:
            if (camera.Type == MESH_CAMERA)
                mpMessageFactory->ErrorAt(failed, source, "look_at cannot frame a target with a mesh_camera; use a point look_at.");
            if (camera.Type == USER_DEFINED_CAMERA)
                mpMessageFactory->ErrorAt(failed, source, "look_at found no ray of this user_defined camera that reaches the target's bounds.");
            mpMessageFactory->ErrorAt(failed, source, "look_at could not frame these bounds with this camera projection.");
            break;
        case FrameResult::Aimed:
            if ((camera.Type != PANORAMIC_CAMERA) && (camera.Type != OMNIMAX_CAMERA))
                mpMessageFactory->WarningAt(kWarningGeneral, source, "look_at target does not fit the camera's field from its "
                                            "location; the camera points at its centre without zooming out.");
            break;
        default:
            break;
    }
}

void CameraFramingTask::Stopped()
{
}

void CameraFramingTask::Finish()
{
    parseData->realTime += ConsumedRealTime();
    if (parseData->cpuTime >= 0)
        parseData->cpuTime += ConsumedCPUTime();
}

void CameraFramingTask::SendFatalError(Exception& e)
{
    if (e.frontendnotified(true))
        return;

    POVMS_Message msg(kPOVObjectClass_ControlData, kPOVMsgClass_SceneOutput, kPOVMsgIdent_Error);

    msg.SetString(kPOVAttrib_EnglishText, e.what());
    msg.SetInt(kPOVAttrib_Error, 0);
    msg.SetInt(kPOVAttrib_SceneId, sceneData->sceneId);
    msg.SetSourceAddress(sceneData->backendAddress);
    msg.SetDestinationAddress(sceneData->frontendAddress);

    POVMS_SendMessage(msg);
}

void MeshBuildTask::Stopped()
{
    sceneData->meshBuildCancelled->store(true, std::memory_order_relaxed);
}

void MeshBuildTask::Finish()
{
    parseData->realTime += ConsumedRealTime();
    if (parseData->cpuTime >= 0)
        parseData->cpuTime += ConsumedCPUTime() + buildCpuTime;
}

void MeshBuildTask::SendFatalError(Exception& e)
{
    if (e.frontendnotified(true))
        return;

    POVMS_Message msg(kPOVObjectClass_ControlData, kPOVMsgClass_SceneOutput, kPOVMsgIdent_Error);

    msg.SetString(kPOVAttrib_EnglishText, e.what());
    msg.SetInt(kPOVAttrib_Error, 0);
    msg.SetInt(kPOVAttrib_SceneId, sceneData->sceneId);
    msg.SetSourceAddress(sceneData->backendAddress);
    msg.SetDestinationAddress(sceneData->frontendAddress);

    POVMS_SendMessage(msg);
}

}
// end of namespace pov
