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
#include <functional>
#include <memory>
#include <set>
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
#include "core/scene/object.h"
#include "core/scene/tracethreaddata.h"
#include "core/shape/csg.h"
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
    SceneTask(new TraceThreadData(std::dynamic_pointer_cast<SceneData>(sd), sd_seed), boost::bind(&MeshBuildTask::SendFatalError, this, _1), "Mesh", sd),
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
    std::vector<std::shared_ptr<DeferredMeshState>> needed;
    std::unordered_set<const DeferredMeshState *> queued;
    std::unordered_set<ConstObjectPtr> visited;
    std::function<void(ObjectPtr)> collect = [&](ObjectPtr object)
    {
        if ((object == nullptr) || !visited.insert(object).second)
            return;
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

    std::vector<std::unique_ptr<TraceThreadData>> workers;
    const size_t count = std::max<size_t>(1, std::min<size_t>(threads, needed.size()));
    for (size_t i = 0; i < count; ++i)
        workers.emplace_back(new TraceThreadData(std::dynamic_pointer_cast<SceneData>(sceneData), seed + i + 1, false));
    try
    {
        std::vector<POV_LONG> cpu(count, 0);
        ParallelFor(needed.size(), count, [&](size_t i, size_t worker)
        {
            Timer timer;
            needed[i]->Build(workers[worker].get());
            if (timer.HasValidThreadCPUTime())
                cpu[worker] += timer.ElapsedThreadCPUTime();
        }, [&]()
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
        });
        // A single build runs on this task's own thread, whose time is counted already.
        for (size_t worker = (count > 1) ? 0 : 1; worker < count; ++worker)
            buildCpuTime += cpu[worker];
    }
    catch (const StopThreadException&)
    {
        throw;
    }
    catch (...)
    {
        sceneData->meshBuildCancelled->store(true, std::memory_order_relaxed);
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

    unsigned int built = 0;
    for (const std::weak_ptr<DeferredMeshState>& recorded : sceneData->deferredMeshes)
    {
        std::shared_ptr<DeferredMeshState> state = recorded.lock();
        if ((state == nullptr) || state->reported || !state->Built())
            continue;
        state->reported = true;
        ++built;
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
        mpMessageFactory->Info("Deferred meshes: %u recorded, %u built for parse-time queries, %u built for the scene, %u not needed.",
                               unsigned(recorded), sceneData->deferredMeshesBuiltForQueries, built,
                               unsigned(recorded - sceneData->deferredMeshesBuiltForQueries - built));
    std::vector<std::weak_ptr<DeferredMeshState>>().swap(sceneData->deferredMeshes);
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
