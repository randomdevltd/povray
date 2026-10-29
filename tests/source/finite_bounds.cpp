#include "core/bounding/boundingbox.h"
#include <cassert>
#include <iostream>

using namespace pov;

struct LegacyBox { BBoxVector3d low, size; };
struct FloatBox { float low[3], size[3]; bool finite; };
struct DoubleBox { double low[3], size[3]; bool finite; };

int main()
{
    BoundingBox box;
    assert(!UsableFiniteBounds(box));
    for (double radius : {1e-8, 1.0, 1.7374e6, 6.957e8, 1e13, 1e25})
        for (double distance : {0.0, radius * 215.0, radius * -100000.0})
        {
            Make_BBox(box, distance - radius, -radius, -radius, 2 * radius, 2 * radius, 2 * radius);
            assert(UsableFiniteBounds(box));
            assert(double(box.lowerLeft[0]) <= distance - radius);
            assert(double(box.lowerLeft[0]) + double(box.size[0]) >= distance + radius);
            BoundingBox copy = box;
            MATRIX matrix = {};
            matrix[0][0] = 2;
            matrix[1][0] = 0.25;
            matrix[1][1] = 3;
            matrix[2][1] = 0.5;
            matrix[2][2] = 0.5;
            matrix[3][0] = distance;
            matrix[3][1] = -distance;
            matrix[3][2] = distance;
            matrix[3][3] = 1;
            Make_Transformed_BBox(copy, matrix);
            assert(UsableFiniteBounds(copy));
            for (double x : {distance - radius, distance + radius})
                for (double y : {-radius, radius})
                    for (double z : {-radius, radius})
                    {
                        const Vector3d point(2 * x + 0.25 * y + distance, 3 * y + 0.5 * z - distance, 0.5 * z + distance);
                        for (int axis = 0; axis < 3; ++axis)
                            assert(double(copy.lowerLeft[axis]) <= point[axis] &&
                                   double(copy.lowerLeft[axis]) + double(copy.size[axis]) >= point[axis]);
                    }
        }
    Make_BBox(box, -BOUND_HUGE / 2, -BOUND_HUGE / 2, -BOUND_HUGE / 2, BOUND_HUGE, BOUND_HUGE, BOUND_HUGE);
    assert(UsableFiniteBounds(box));
    Make_BBox(box, -BOUND_HUGE / 2, -BOUND_HUGE / 2, -BOUND_HUGE / 2, BOUND_HUGE, BOUND_HUGE, BOUND_HUGE, false);
    assert(!UsableFiniteBounds(box));
    MATRIX identity = {};
    for (int axis = 0; axis < 4; ++axis)
        identity[axis][axis] = 1;
    Make_Transformed_BBox(box, identity);
    assert(!box.finite);
    Make_BBox(box, 1e30, 0, 0, 1, 1, 1);
    assert(!UsableFiniteBounds(box));
    Make_BBox(box, 0, 0, 0, 1, 1, 1);
    box.finiteInterior = false;
    Make_Transformed_BBox(box, identity);
    assert(box.finite && !box.finiteInterior);
    box.size[0] = 0;
    assert(!UsableFiniteBounds(box));
    box.size[0] = std::numeric_limits<BBoxScalar>::infinity();
    assert(!UsableFiniteBounds(box));
    box.size[0] = std::numeric_limits<BBoxScalar>::quiet_NaN();
    assert(!UsableFiniteBounds(box));
    Make_BBox_from_min_max(box, Vector3d(1, 0, 0), Vector3d(1 + 1e-15, 1e-40, 1e-40));
    assert(UsableFiniteBounds(box));
    assert(double(box.lowerLeft[0]) + double(box.size[0]) >= 1 + 1e-15);
    assert(double(box.size[1]) >= 1e-40);
    Make_BBox_from_min_max(box, Vector3d(-std::numeric_limits<double>::denorm_min(), 0, 0), Vector3d(1));
    assert(box.lowerLeft[0] < 0 && UsableFiniteBounds(box));
    Make_BBox(box, 0, 0, 0, 0, 1, 1);
    assert(box.finite && !UsableFiniteBounds(box));
    std::cout << "finite bounds checks passed; BoundingBox " << sizeof(LegacyBox) << " -> " << sizeof(BoundingBox)
              << " bytes; float layout " << sizeof(FloatBox) << "; double layout " << sizeof(DoubleBox)
              << "; tree node " << sizeof(BBox_Tree_Struct) << "\n";
}
