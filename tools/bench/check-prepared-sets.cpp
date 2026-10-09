#include "core/scene/scenedata.h"
#include "core/material/pattern.h"
#include "core/shape/csg.h"
#include "core/shape/sphere.h"

#include <stdexcept>

using namespace pov;

static void Check(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}

int main()
{
    SceneData scene;
    Sphere *red = new Sphere();
    red->tags = {"red"};
    Sphere *blue = new Sphere();
    blue->tags = {"blue"};
    CSGUnion *group = new CSGUnion();
    group->tags = {"group"};
    LightSource *local = new LightSource();
    local->tags = {"child"};
    local->index = 0;
    local->lightGroupLight = true;
    group->children.push_back(local);
    Sphere *inside = new Sphere();
    group->children.push_back(inside);
    LightSource *global = new LightSource();
    global->tags = {"red"};
    global->index = 0;
    global->lightGroupLight = false;
    scene.objects = {red, blue, group, global};
    scene.lightSources = {global};
    scene.lightGroupLightSources = {local};

    const TagFilter first = ParseTagFilter("\"red\"");
    const TagFilter alias = ParseTagFilter("\"red\" | \"missing\"");
    Check(!(first == alias), "test expressions must be distinct");
    const PreparedSetId redId = scene.RegisterPreparedSet(first);
    const PreparedSetId aliasId = scene.RegisterPreparedSet(alias);
    Check(redId == aliasId && scene.preparedSets.size() == 1, "equal membership must reuse one prepared set");
    Check(scene.FindPreparedSet(alias) == redId, "alias lookup must retain the shared identity");
    PreparedSet& redSet = scene.GetPreparedSet(redId);
    Check(redSet.objects == std::vector<ObjectPtr>({red, global}), "root membership must preserve scene order");
    Check(redSet.lights == std::vector<LightSource*>({global}), "light membership must match the selected roots");
    redSet.nodes = 17;
    Check(scene.GetPreparedSet(aliasId).nodes == 17, "aliases must share bounds state");
    Check(&redSet.surfacePhotonMap == &scene.GetPreparedSet(aliasId).surfacePhotonMap, "aliases must share photon storage");

    const PreparedSetId empty = scene.RegisterPreparedSet(ParseTagFilter("none"));
    Check(empty == scene.RegisterPreparedSet(ParseTagFilter("\"missing\"")), "empty results must share one set");
    Check(scene.GetPreparedSet(empty).objects.empty() && scene.GetPreparedSet(empty).lights.empty(), "empty set must contain nothing");
    const PreparedSetId groupId = scene.RegisterPreparedSet(ParseTagFilter("\"group\""));
    Check(scene.LightInPreparedSet(*local, groupId), "a selected root's children carry its tags");
    Check(!scene.LightInPreparedSet(*local, redId), "local lights must not leak into unrelated sets");
    Check(scene.GetPreparedSet(groupId).lights == std::vector<LightSource*>({local}), "selected descendant lights must be prepared");

    LightSource clone;
    clone.index = global->index;
    clone.lightGroupLight = false;
    Check(scene.LightInPreparedSet(clone, redId), "thread light copies must resolve to their scene membership");
    Check(!scene.LightInPreparedSet(clone, empty), "thread light copies must remain excluded from empty sets");
    Check(redSet.globalLights == std::vector<unsigned int>({0}), "a set lists its global lights by scene index");
    Check(scene.GetPreparedSet(empty).globalLights.empty() && scene.GetPreparedSet(groupId).globalLights.empty(),
          "sets without global lights list none, so render loops need no test");
    Check(scene.preparedSets.size() == 3, "only distinct memberships should allocate prepared sets");

    const PreparedSetId partial = scene.RegisterPreparedSet(ParseTagFilter("\"group\" & !\"child\""));
    const PreparedSet& partialSet = scene.GetPreparedSet(partial);
    Check(!scene.LightInPreparedSet(*local, partial) && partialSet.groupLightsFiltered, "a child rejected by its own tags must leave the view");
    Check(partialSet.objects.size() == 1 && partialSet.objects[0] != group && partialSet.views.size() == 1,
          "a partly kept group must be a view of it");
    Check(static_cast<CompoundObject *>(partialSet.objects[0])->children == std::vector<ObjectPtr>({inside}),
          "a view must share the children it keeps");
    Check(partial == scene.RegisterPreparedSet(ParseTagFilter("!\"child\" & \"group\"")), "equal views must share one set");
    Check(scene.preparedSets.size() == 4, "a partial view is a distinct membership");

    ScreenPattern kept;
    {
        ScreenPattern declared;
        declared.pProjection = std::make_shared<Camera>();
        scene.screenCameraCandidates.push_back(declared.pProjection);
        kept.pProjection = declared.pProjection;
        scene.screenCameraCandidates.push_back(kept.pProjection);
        ScreenPattern rejected;
        rejected.pProjection = std::make_shared<Camera>();
        scene.screenCameraCandidates.push_back(rejected.pProjection);
    }
    scene.FinalizeScreenCameras();
    Check(scene.screenCameras.size() == 1, "only surviving screen cameras should be prepared once");
    Check(scene.screenCameras.front() == kept.pProjection, "shared retained screen ownership must survive declaration cleanup");
    Check(scene.screenCameraCandidates.empty(), "parse-time screen candidates must be released after finalization");
}
