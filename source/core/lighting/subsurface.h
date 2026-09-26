//******************************************************************************
///
/// @file core/lighting/subsurface.h
///
/// Declarations related to subsurface light transport.
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

#ifndef POVRAY_CORE_SUBSURFACE_H
#define POVRAY_CORE_SUBSURFACE_H

// Module config header file must be the first file included within POV-Ray unit header files
#include "core/configcore.h"

// C++ variants of C standard header files
// C++ standard header files
//  (none at the moment)

// Boost header files
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include <boost/flyweight.hpp>
#include <boost/flyweight/key_value.hpp>

// POV-Ray header files (base module)
//  (none at the moment)

// POV-Ray header files (core module)
#include "core/coretypes.h"

namespace pov
{

//##############################################################################
///
/// @defgroup PovCoreLightingSubsurface Subsurface Scattering
/// @ingroup PovCore
///
/// @{

using boost::flyweights::flyweight;
using boost::flyweights::key_value;

/// Class storing SSLT data precomputed based on index of refraction.
class SubsurfaceInterior final
{

    public:

        SubsurfaceInterior(double ior);
        PreciseMathColour GetReducedAlbedo(const MathColour& diffuseReflectance) const;

    protected:

        static const int ReducedAlbedoSamples = 100;

        // precomputed reduced albedo for selected values of diffuse reflectance
        struct PrecomputedReducedAlbedo final
        {
            float reducedAlbedo[ReducedAlbedoSamples+1];
            PrecomputedReducedAlbedo(float ior);
            PreciseColourChannel operator()(PreciseColourChannel diffuseReflectance) const;
        };

        flyweight<key_value<float,PrecomputedReducedAlbedo>> precomputedReducedAlbedo;
};

/// A point of a subsurface irradiance cloud: the light entering the surface there, and the area it stands for (mm^2).
struct SubsurfacePoint final
{
    Vector3d position;
    float normal[3];
    float irradiance[3];
    float area;
    int id; ///< index into the cell's visibility, which stays in build order
};

/// A node of a cell's point hierarchy: its box, area-weighted centre, total area and area-weighted irradiance.
struct SubsurfaceNode final
{
    Vector3d lo, hi, centre;
    float area;
    float irradiance[3];
    int first, count; ///< points of a leaf; for an inner node, count is 0 and first the second child
};

/// The irradiance cloud of one object in one cube of space, with its point hierarchy; see doc/PERF.md.
struct SubsurfaceCell final
{
    std::vector<SubsurfacePoint> points;
    std::vector<SubsurfaceNode> nodes;
    std::vector<float> visibility; ///< per point, per light, per channel: shadowed over unshadowed light
    int lights = 0;
    bool ready = false;
    bool usable = false;
    void BuildHierarchy();
};

/// Where a cell sits: the object, the cell and point spacing levels, and the cell's integer position.
struct SubsurfaceCellKey final
{
    const void *object;
    int sizeLevel, spacingLevel;
    int x, y, z;
    bool operator==(const SubsurfaceCellKey& o) const
    {
        return object == o.object && sizeLevel == o.sizeLevel && spacingLevel == o.spacingLevel && x == o.x && y == o.y && z == o.z;
    }
};

struct SubsurfaceCellKeyHash final
{
    size_t operator()(const SubsurfaceCellKey& k) const;
};

/// Irradiance clouds built on demand and shared by all render threads, up to a point budget.
class SubsurfaceCache final
{
    public:
        /// The cell for key, and whether the caller must build it (and then call Publish); waits while another thread builds it.
        std::shared_ptr<SubsurfaceCell> Acquire(const SubsurfaceCellKey& key, bool& build);
        void Publish(const std::shared_ptr<SubsurfaceCell>& cell, bool usable);
        /// What the first shading point to ask learnt about an object's shape.
        struct Plan
        {
            int sizeLevel, spacingLevel;
            bool usable, stepped;
        };
        bool FindPlan(const void *object, Plan& plan);
        Plan StorePlan(const void *object, const Plan& plan);
        /// Reserves room for points; false once the budget is spent.
        bool Reserve(size_t points);
        ~SubsurfaceCache();

    private:
        std::mutex mutex;
        std::condition_variable built;
        std::unordered_map<SubsurfaceCellKey, std::shared_ptr<SubsurfaceCell>, SubsurfaceCellKeyHash> cells;
        std::atomic<size_t> reserved{0};
        std::unordered_map<const void*, Plan> plans;
};

/// Approximation to the Fresnel diffuse reflectance.
inline double FresnelDiffuseReflectance(double eta)
{
#if 0
    // This is the original formula as per the 2001 Jensen et al. paper;
    // however, this breaks down for large values of eta, or values < 1.0,
    // and comes with some other bogosities.
    return clip( -1.440/Sqr(eta) + 0.710/eta + 0.668 + 0.0636*eta, 0.0, 1.0-EPSILON );
#else
    // My own approximation; maybe it's utterly wrong, but at least it is stable.
    if (eta < 1.0)
        return Sqr(eta)-pow(eta,2.25);
    else
        return ( (1.0-1.0/eta) + 3*pow(1.0-1.0/eta,4.5) ) / 4.0;
#endif
}

/// @}
///
//##############################################################################

}
// end of namespace pov

#endif // POVRAY_CORE_SUBSURFACE_H
