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
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

// Boost header files
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

/// How subsurface light is found: rays under every shading point, or a point cloud shared by them; see doc/PERF.md.
enum SubsurfaceMethod
{
    kSubsurfaceMethodSampled = 1,
    kSubsurfaceMethodPointCloud = 2,
};

/// A point of a subsurface irradiance cloud: the light entering the surface there, and the area it stands for (mm^2).
struct SubsurfacePoint final
{
    Vector3d position;
    float normal[3]; ///< pointing out of the object
    float irradiance[MathColour::channels];
    float area;
    int id; ///< index into the cell's visibility, which stays in build order
};

/// A node of a cell's point hierarchy: its box, area-weighted centre, total area, area-weighted irradiance, and the
/// mean normal of its points with the least cosine between it and theirs.
struct SubsurfaceNode final
{
    Vector3d lo, hi, centre;
    float area;
    float irradiance[MathColour::channels];
    float normal[3];
    float cone;
    int first, count; ///< points of a leaf; for an inner node, count is 0 and first the second child
};

/// The irradiance cloud of one object in one cube of space, with its point hierarchy; see doc/PERF.md.
struct SubsurfaceCell final
{
    std::vector<SubsurfacePoint> points;
    std::vector<SubsurfaceNode> nodes;
    std::vector<float> visibility; ///< per point, per light, per channel: shadowed over unshadowed light
    double step[3] = {}; ///< between points on a surface facing each axis, in scene units
    int lights = 0;
    bool usable = false;
    void BuildHierarchy();
    double Spacing(const Vector3d& normal) const; ///< between points on a surface with this unit normal

    /// Building goes in stages, each split into jobs that every thread needing the cell meanwhile takes a share of.
    /// A cell that fails (too many points, a crossing that cannot be oriented, an exception) ends ready but unusable.
    enum Stage { kCasting, kLighting, kReady };
    std::mutex mutex;
    std::condition_variable changed;
    Stage stage = kCasting;
    int jobs = 0, next = 0, done = 0, steps[3] = {}; ///< steps: lines per row along each axis
    std::atomic<bool> failed{false};
    size_t found = 0, reserved = 0, unoriented = 0; ///< while casting: points kept, points reserved, crossings dropped
    std::vector<std::vector<SubsurfacePoint>> rows; ///< points found by each row of lines while casting
};

/// Where a cell sits: the object, the cell size level (its side is 2^level scene units) and the cell's integer position.
struct SubsurfaceCellKey final
{
    const void *object;
    int sizeLevel;
    int x, y, z;
    bool operator==(const SubsurfaceCellKey& o) const
    {
        return object == o.object && sizeLevel == o.sizeLevel && x == o.x && y == o.y && z == o.z;
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
        /// The cell for key, and whether the caller is the first to ask and so must build it.
        std::shared_ptr<SubsurfaceCell> Acquire(const SubsurfaceCellKey& key, bool& build);
        /// Reserves room for points; false once the budget is spent.
        bool Reserve(size_t points);
        void Release(size_t points);
        /// The camera that cells space their points for: the first view to set it, so every thread builds alike.
        void SetCamera(const Vector3d& location, double pixelSize, double pixelAngle);
        bool GetCamera(Vector3d& location, double& pixelSize, double& pixelAngle) const;

    private:
        mutable std::mutex mutex;
        std::unordered_map<SubsurfaceCellKey, std::shared_ptr<SubsurfaceCell>, SubsurfaceCellKeyHash> cells;
        size_t reserved = 0;
        bool cameraSet = false;
        Vector3d cameraLocation;
        double cameraPixelSize = 0.0, cameraPixelAngle = 0.0;
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
