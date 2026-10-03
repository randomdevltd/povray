//******************************************************************************
///
/// @file core/shape/portal.h
///
/// Declarations related to the portal object.
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

#ifndef POVRAY_CORE_PORTAL_H
#define POVRAY_CORE_PORTAL_H

// Module config header file must be the first file included within POV-Ray unit header files
#include "core/configcore.h"

// C++ standard header files
#include <string>

// POV-Ray header files (core module)
#include "core/math/matrix.h"
#include "core/scene/object.h"

namespace pov
{

/// @addtogroup PovCoreShape
///
/// @{

/// A body whose front faces lead elsewhere: rays entering them continue from the mapped point, in the mapped direction.
/// The portal renders nothing itself; shadow and photon rays pass it by.
class Portal final : public ObjectBase
{
    public:

        ObjectPtr body;         ///< the surface that leads elsewhere
        TRANSFORM map;          ///< world to world, from the body as placed to its image; outer transforms move only the body
        PIGMENT *pigment;       ///< opacity is how open, colour tints the view; null is fully open
        PIGMENT *fallback;      ///< the view where portals nest too deep; null leaves the portal absent there
        TNORMAL *perturb;       ///< bends the view by its difference from the geometric normal
        double perturbAmount;
        unsigned int maxDepth;  ///< deepest nesting of portal views; 0 takes the trace level
        bool exit;              ///< whether a view that finds nothing across the body resumes beyond it
        bool front;             ///< whether a view entering the front side goes through
        bool back;              ///< whether a view entering the back side goes through; a volume's back faces are met from inside
        bool reversed;          ///< whether the front faces away from the normal, as an image of a surface faces where views come out
        bool farMouth;          ///< whether the body's image is a mouth leading back
        bool farFront;
        bool farBack;
        PIGMENT *farPigment;
        const Portal *partner;  ///< the other mouth, once made
        std::string origin;     ///< where it was written, for messages

        Portal();
        virtual ~Portal() override;

        virtual ObjectPtr Copy() override;

        virtual bool All_Intersections(const Ray&, IStack&, TraceThreadData *) override;
        virtual bool Inside(const Vector3d&, TraceThreadData *) const override;
        virtual void Normal(Vector3d&, Intersection *, TraceThreadData *) const override;
        virtual void Translate(const Vector3d&, const TRANSFORM *) override;
        virtual void Rotate(const Vector3d&, const TRANSFORM *) override;
        virtual void Scale(const Vector3d&, const TRANSFORM *) override;
        virtual void Transform(const TRANSFORM *) override;
        virtual ObjectPtr Invert() override;
        virtual void Compute_BBox() override;
        virtual bool IsOpaque() const override { return false; }

        /// The far mouth: the body under the map, leading back by the inverse map.
        Portal *MakeImage() const;
        /// The distance from a front-face hit to where the ray leaves the body again, or a negative value if it does not.
        double Chord(const Ray& ray, const Vector3d& entry, TraceThreadData *Thread) const;
        /// Whether a ray along `direction` meeting a face of geometric normal `normal` goes through, by the side it enters.
        bool Admits(const Vector3d& normal, const Vector3d& direction) const { return EntersFront(normal, direction) ? front : back; }
        bool EntersFront(const Vector3d& normal, const Vector3d& direction) const
        {
            const double facing = reversed ? -dot(normal, direction) : dot(normal, direction);
            return (facing < 0.0);
        }
        bool AnySideOpen() const { return front || back; }
};

/// Whether an object's inside test describes a finite closed solid.
bool IsClosedSolid(ConstObjectPtr object);

/// @}

}
// end of namespace pov

#endif // POVRAY_CORE_PORTAL_H
