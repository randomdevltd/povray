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
#include <cstdint>

// C++ standard header files
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>
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

class PhotonMap;
struct Photon;

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

/// Memory all subsurface clouds may take together unless global_settings { subsurface { memory MB } } says otherwise.
const double kSubsurfaceMemoryMB = 1024.0;

/// Why a shading point that asked for the point cloud got method 1; the statistics count each from Subsurface_Cloud_Unoriented.
enum SubsurfaceCloudFailure
{
    kCloudServed = 0,
    kCloudUnoriented, ///< over 1 in 16 of a cell's crossings could not be told inside from outside
    kCloudUnbounded,  ///< an unbounded object whose cells would be too coarse
    kCloudWide,       ///< the reach spans more cells than a cloud gathers
    kCloudLights,     ///< part of the object has lights of its own (a light group)
    kCloudMissing,    ///< no cell holds the exit point
    kCloudOther,      ///< no interior or diffusion, photons the cloud cannot take, or a cell whose build threw
    kCloudFailures
};

/// Which rule set a cell's point step: the diffusion (a 128th of the cell), the pixel, the object's size, or the floor.
enum SubsurfaceStepTerm { kStepDiffusion, kStepPixel, kStepObject, kStepFinest, kStepTerms };

/// The clouds' totals for the render statistics.
struct SubsurfaceCloudStatistics final
{
    double spacing = 1.0, peakBytes = 0.0, chargedBytes = 0.0, cells = 0.0, points = 0.0, read = 0.0;
    double termCells[kStepTerms] = {}, termPoints[kStepTerms] = {};
};

/// A point of a subsurface irradiance cloud: the light entering the surface there, and the area it stands for (mm^2).
struct SubsurfacePoint final
{
    Vector3d position;
    float normal[3]; ///< pointing out of the object
    float irradiance[MathColour::channels];
    float area;
    unsigned id : 30; ///< index into the cell's visibility, which stays in build order
    unsigned axis : 2; ///< that of the line that found it
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
    std::vector<float> ambient; ///< while lighting a radiosity cell: per point and channel, the cache's light, or NaN if none
    std::vector<float> photonEntry; ///< entry-filtered photons, in point id order, for core reconstruction
    std::vector<float> entry; ///< with a medium: per point, the flesh times skin its light enters by, then its emission, per channel
    double step[3] = {}; ///< between points on a surface facing each axis, in scene units
    int lights = 0;
    bool usable = false;
    SubsurfaceCloudFailure failure = kCloudServed; ///< why it is unusable
    SubsurfaceStepTerm term = kStepDiffusion; ///< the rule that set its finest step
    size_t charged = 0, staged = 0; ///< bytes of its finished arrays counted against the cap; of its rows while casting
    bool settled = false; ///< charged or refunded, after which its rows no longer count
    std::unique_ptr<std::atomic<std::uint32_t>[]> read; ///< a bit per node whose points a gather has used, alone or summed
    void BuildHierarchy();
    double Spacing(const Vector3d& normal) const; ///< between points on a surface with this unit normal
    size_t Bytes() const; ///< held by the cell and its arrays
    void Release(); ///< frees its arrays, for a cell that cannot be used
    void MarkRead(int node) const;
    size_t PointsRead() const; ///< points under a node a gather has used

    /// Building goes in stages, each split into jobs that every thread needing the cell meanwhile takes a share of.
    /// A cell that fails (a crossing that cannot be oriented, an exception) ends ready but unusable.
    enum Stage { kCasting, kLighting, kReady };
    std::mutex mutex;
    std::condition_variable changed;
    Stage stage = kCasting;
    int jobs = 0, next = 0, done = 0, steps[3] = {}; ///< steps: lines per row along each axis
    std::atomic<bool> failed{false};
    size_t found = 0, unoriented = 0; ///< while casting: points kept, crossings dropped
    std::vector<std::vector<SubsurfacePoint>> rows; ///< points found by each row of lines while casting
};

/// Where a cell sits: the object, the cell size level (its side is 2^level scene units) and the cell's integer position.
struct SubsurfaceCellKey final
{
    const void *object;
    int sizeLevel;
    int x, y, z;
    bool photons;
    bool radiosity; ///< whether its points carry the radiosity cache's light as well as the lights'
    const void *medium; ///< the texture layer whose flesh and skin its points look up where light enters, or null
    size_t preparedSetId;
    bool operator==(const SubsurfaceCellKey& o) const
    {
        return object == o.object && sizeLevel == o.sizeLevel && x == o.x && y == o.y && z == o.z && radiosity == o.radiosity &&
               medium == o.medium && photons == o.photons && preparedSetId == o.preparedSetId;
    }
};

struct SubsurfaceCellKeyHash final
{
    size_t operator()(const SubsurfaceCellKey& k) const;
};

/// Each photon's recovered deposit boundary on each receiver, shared by all render threads without locks.
/// Normals are kept folded onto the unit octahedron in two floats, and a photon off the receiver's boundary keeps none.
class SubsurfacePhotonBoundaries final
{
    public:
        /// The photon's index in the map and the receiver as one key; false when the table cannot hold it.
        bool Key(const PhotonMap& map, const Photon* photon, const void* receiver, std::uint64_t& key);
        bool Find(std::uint64_t key, float folded[2]) const;
        /// Stores the first boundary computed for key; a slot another thread holds, or a full table, keeps nothing.
        void Store(std::uint64_t key, const float folded[2]);
        static void Fold(const Vector3d& normal, bool valid, float folded[2]);
        static bool Unfold(const float folded[2], Vector3d& normal);

    private:
        struct Slot final
        {
            std::atomic<std::uint64_t> word{0}; ///< 0 empty, else key * 2, plus 1 once folded is written
            float folded[2];
        };
        static const int kReceivers = 64;
        std::once_flag once;
        std::unique_ptr<Slot[]> slots;
        std::uint64_t mask = 0;
        unsigned int blockSize = 0;
        std::vector<std::pair<std::uintptr_t, std::uint32_t>> blocks; ///< block start and id, by address
        std::atomic<const void*> receivers[kReceivers] = {};
        void Allocate(const PhotonMap& map);
};

/// Irradiance clouds built on demand and shared by all render threads, within a memory cap that stops the render.
class SubsurfaceCache final
{
    public:
        explicit SubsurfaceCache(double capMB = kSubsurfaceMemoryMB, double spacing = 1.0);
        SubsurfacePhotonBoundaries& PhotonBoundaries(const PhotonMap& map);
        /// The cell for key, and whether the caller is the first to ask and so must build it.
        std::shared_ptr<SubsurfaceCell> Acquire(const SubsurfaceCellKey& key, bool& build);
        /// Counts a finished cell's bytes against the cap, so that whether a render stops depends on the scene alone; throws
        /// the fatal error once the clouds pass it.
        void Charge(SubsurfaceCell& cell);
        /// Rows a cell casts into, counted only in the peak.
        void Stage(SubsurfaceCell& cell, size_t bytes);
        /// Whether one cell's own points fit the cap; past it, the stop is recorded, as no total could then fit.
        bool CellFits(size_t bytes);
        void Refund(SubsurfaceCell& cell);
        /// Throws the fatal error once any cell has passed the cap.
        void CheckCap() const;
        /// The share of the image done; once past a quarter, whether finished cells at that rate would pass the cap.
        bool Progress(double fraction, double& heldMB, double& projectedMB, double& capMB);
        SubsurfaceCloudStatistics Statistics() const;
        /// The camera that cells space their points for: the first view to set it, so every thread builds alike.
        void SetCamera(const Vector3d& location, double pixelSize, double pixelAngle);
        bool GetCamera(Vector3d& location, double& pixelSize, double& pixelAngle) const;

    private:
        mutable std::mutex mutex;
        std::unordered_map<SubsurfaceCellKey, std::shared_ptr<SubsurfaceCell>, SubsurfaceCellKeyHash> cells;
        std::mutex photonBoundariesMutex;
        std::unordered_map<const PhotonMap*, std::unique_ptr<SubsurfacePhotonBoundaries>> photonBoundaries;
        size_t cap, charged = 0, staged = 0, peak = 0, refused = 0;
        double progress = 0.0, refusedAt = 0.0;
        bool projected = false;
        double spacing;
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
