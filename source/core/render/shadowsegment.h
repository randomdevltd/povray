#ifndef POVRAY_CORE_SHADOWSEGMENT_H
#define POVRAY_CORE_SHADOWSEGMENT_H

#include "core/render/ray.h"
#include "core/math/intersectiondepth.h"
#include <algorithm>
#include <cmath>
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
    ConstObjectPtr Selected() const { return selected; }
    double SelectedDepth() const { return selectedDepth; }
    // Whether a returned depth is the selected root, as two tests of one surface may round it a few ulps apart.
    bool Coincides(double depth) const
    {
        if (!selected)
            return false;
        double low = std::min(depth, selectedDepth);
        const double high = std::max(depth, selectedDepth);
        for (int step = 0; (step < kCoincidentUlps) && (low < high); step++)
            low = std::nextafter(low, HUGE_VAL);
        return low == high;
    }
    // Crosses the returned boundary as the selected root's; when they do not coincide it is stepped past and false returned.
    bool Commit(double depth)
    {
        const double at = std::max(depth, selectedDepth);
        if (!Coincides(depth) || !Admit(selected, at))
        {
            cursor = std::nextafter(std::max(depth, cursor), HUGE_VAL);
            crossed.clear();
            return false;
        }
        Pass(selected, at);
        return true;
    }
    // Crosses one object's boundary at depth without its being returned.
    void Pass(ConstObjectPtr object, double depth)
    {
        if (depth > cursor)
            crossed.clear();
        cursor = std::max(cursor, depth);
        crossed.push_back(object);
    }
    double Cursor() const { return cursor; }
    double End() const { return end; }
    double Remaining() const { return endpoint - cursor; }
private:
    static const int kCoincidentUlps = 4;
    double endpoint, end, start, cursor = 0.0;
    const Ray* ray;
    std::vector<ConstObjectPtr> crossed;
    mutable ConstObjectPtr selected = nullptr;
    mutable double selectedDepth = HUGE_VAL;
};

}
#endif
