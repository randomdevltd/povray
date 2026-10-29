#ifndef POVRAY_CORE_SHADOWSEGMENT_H
#define POVRAY_CORE_SHADOWSEGMENT_H

#include "core/render/ray.h"
#include "core/math/intersectiondepth.h"
#include <algorithm>
#include <vector>

namespace pov
{

class ShadowSegmentWalker final : public RayRootQuery, public RayObjectCondition
{
public:
    ShadowSegmentWalker(double endpoint, double start, double limit = HUGE_VAL, const Ray* ray = nullptr) : endpoint(endpoint), end(std::min(endpoint, limit)), start(start), ray(ray) {}
    bool Admit(ConstObjectPtr object, double depth) const override
    {
        return (!ray || (!ray->MatchesSurfaceEndpoint(object, depth) && !ray->MatchesSurfaceOrigin(object, depth))) && IsFiniteIntersectionDepth(depth) && depth > start && depth < end && depth >= cursor &&
            (depth != cursor || std::find(crossed.begin(), crossed.end(), object) == crossed.end());
    }
    bool operator()(const Ray&, ConstObjectPtr object, double depth) const override
    {
        if (!Admit(object, depth))
            return false;
        if (depth < selectedDepth)
        {
            selected = object;
            selectedDepth = depth;
        }
        return true;
    }
    void Reset() const { selected = nullptr; selectedDepth = HUGE_VAL; }
    bool Commit(double depth)
    {
        if (!selected || depth != selectedDepth || !Admit(selected, depth))
            return false;
        if (depth > cursor)
            crossed.clear();
        cursor = depth;
        crossed.push_back(selected);
        return true;
    }
    double Cursor() const { return cursor; }
    double End() const { return end; }
    double Remaining() const { return endpoint - cursor; }
private:
    double endpoint, end, start, cursor = 0.0;
    const Ray* ray;
    std::vector<ConstObjectPtr> crossed;
    mutable ConstObjectPtr selected = nullptr;
    mutable double selectedDepth = HUGE_VAL;
};

}
#endif
