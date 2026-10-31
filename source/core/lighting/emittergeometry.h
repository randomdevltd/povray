#ifndef POVRAY_CORE_EMITTERGEOMETRY_H
#define POVRAY_CORE_EMITTERGEOMETRY_H

#include "core/configcore.h"

#include <cmath>

#include "core/math/matrix.h"
#include "core/shape/box.h"
#include "core/shape/sphere.h"

namespace pov
{

class PrimitiveEmitterGeometry final
{
    public:
        bool Initialize(const ObjectBase* object)
        {
            kind = None;
            volume = 0.0;
            if (object == nullptr || !object->Clip.empty() || Test_Flag(object, INVERTED_FLAG))
                return false;
            Vector3d edges[3];
            if (const Box* box = dynamic_cast<const Box*>(object))
            {
                Vector3d extent = box->bounds[1] - box->bounds[0];
                for (int i = 0; i < 3; ++i)
                {
                    edges[i] = Vector3d(0.0);
                    edges[i][i] = extent[i];
                    if (box->Trans != nullptr)
                        MTransDirection(edges[i], edges[i], box->Trans);
                }
                kind = Parallelepiped;
            }
            else if (const Sphere* sphere = dynamic_cast<const Sphere*>(object))
            {
                if (sphere->Spherical())
                {
                    double radius = std::fabs(sphere->Radius);
                    volume = 4.0 * M_PI * radius * radius * radius / 3.0;
                    constantArea = M_PI * radius * radius;
                    kind = Ball;
                    return std::isfinite(volume) && volume > 0.0;
                }
                if (sphere->Trans == nullptr)
                    return false;
                for (int i = 0; i < 3; ++i)
                {
                    edges[i] = Vector3d(0.0);
                    edges[i][i] = sphere->Radius;
                    MTransDirection(edges[i], edges[i], sphere->Trans);
                }
                kind = Ellipsoid;
            }
            else
                return false;
            faceVectors[0] = cross(edges[1], edges[2]);
            faceVectors[1] = cross(edges[2], edges[0]);
            faceVectors[2] = cross(edges[0], edges[1]);
            double determinant = dot(edges[0], faceVectors[0]);
            if (!std::isfinite(determinant) || determinant == 0.0)
            {
                kind = None;
                return false;
            }
            volume = std::fabs(determinant);
            if (kind == Ellipsoid)
            {
                ellipsoidAreaScale = M_PI * volume;
                volume *= 4.0 * M_PI / 3.0;
                for (int i = 0; i < 3; ++i)
                    faceVectors[i] /= determinant;
            }
            return std::isfinite(volume) && volume > 0.0;
        }

        double Volume() const { return volume; }

        double MeanProjectedArea() const
        {
            if (kind == Ball)
                return constantArea;
            if (kind == Parallelepiped)
                return 0.5 * (faceVectors[0].length() + faceVectors[1].length() + faceVectors[2].length());
            return 0.0;
        }

        // The direction is a unit vector in the same coordinates as the source geometry.
        double ProjectedArea(const Vector3d& direction) const
        {
            if (kind == Ball)
                return constantArea;
            double components[3];
            for (int i = 0; i < 3; ++i)
                components[i] = dot(direction, faceVectors[i]);
            if (kind == Ellipsoid)
                return ellipsoidAreaScale * std::hypot(std::hypot(components[0], components[1]), components[2]);
            if (kind == Parallelepiped)
                return std::fabs(components[0]) + std::fabs(components[1]) + std::fabs(components[2]);
            return 0.0;
        }

    private:
        enum Kind { None, Ball, Ellipsoid, Parallelepiped } kind = None;
        Vector3d faceVectors[3];
        double volume = 0.0;
        double constantArea = 0.0;
        double ellipsoidAreaScale = 0.0;
};

}

#endif
