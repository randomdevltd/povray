//******************************************************************************
///
/// @file core/scene/tracethreaddata.h
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

#ifndef POVRAY_CORE_TRACETHREADDATA_H
#define POVRAY_CORE_TRACETHREADDATA_H

// Module config header file must be the first file included within POV-Ray unit header files
#include "core/configcore.h"

// C++ variants of C standard header files
//  (none at the moment)

// C++ standard header files
#include <cstdint>
#include <memory>
#include <stack>
#include <vector>

// POV-Ray header files (base module)
#include "base/types.h"
#include "base/colour.h"

// POV-Ray header files (core module)
#include "core/coretypes.h"
#include "core/bounding/boundingcylinder.h"
#include "core/math/randomsequence_fwd.h"
#include "core/math/vector.h"
#include "core/scene/scenedata_fwd.h"
#include "core/support/cracklecache_fwd.h"
#include "core/support/statistics_fwd.h"

namespace pov
{

//##############################################################################
///
/// @addtogroup PovCore
///
/// @{

/// A mesh triangle as its ray test and smooth normal use it, kept per thread in slot `index % MESH_DECODES`.
struct MeshTriangleDecode final
{
    std::uint64_t mesh; ///< The mesh data's serial; 0 while the slot is empty.
    std::int32_t index;
    std::int32_t axis;  ///< The triangle's dominant axis.
    Vector3d p1, p2, p3;
    Vector3d n;         ///< Unnormalised face normal, cross(p3 - p1, p2 - p1).
    double nn;          ///< Its squared length.
    Vector3d perp;      ///< Smoothing frame, once `framed`.
    int vaxis;
    bool framed;
};

/// The triangle of a mesh that last blocked a shadow ray heading into one octant of directions.
struct MeshShadowHint final
{
    const void *mesh;
    std::int32_t octant;
    std::int32_t triangle;
};

using namespace pov_base;

class PhotonMap;
struct Blob_Interval_Struct;

/// Class holding parser thread specific data.
/// Sets the shadow window of @ref TraceThreadData for a scope, and restores it after.
struct IsoShadowWindow;

class TraceThreadData : public ThreadData
{
    public:

        /// Create thread local data.
        /// @param  sd      Scene data defining scene attributes.
        /// @param  seed    Seed for the stochastic random number generator;
        ///                 should be unique for each render.
        TraceThreadData(std::shared_ptr<SceneData> sd, size_t seed);

        /// Destructor.
        virtual ~TraceThreadData() override;

        /// Get the statistics.
        /// @return     Reference to statistic counters.
        RenderStatistics& Stats(void) { return *mpRenderStats; }

        /// Depths between which an opaque blocker ends a shadow ray, so an opaque isosurface may report any root
        /// there rather than the first; both zero where the first is needed.
        double isoShadowFrom;
        double isoShadowTo;

        DBL *Fractal_IStack[4];
        void **Blob_Queue;
        unsigned int Max_Blob_Queue_Size;
        DBL *Blob_Coefficients;
        Blob_Interval_Struct *Blob_Intervals;
        int Blob_Coefficient_Count;
        int Blob_Interval_Count;
        std::vector<BCYL_INT> BCyl_Intervals;
        std::vector<BCYL_INT> BCyl_RInt;
        std::vector<BCYL_INT> BCyl_HInt;
        IStackPool stackPool;
        std::vector<GenericFunctionContextPtr> functionContextPool;
        int Facets_Last_Seed;
        int Facets_CVC;
        Vector3d Facets_Cube[81];

        /// Salt of every path key, so that renders with different seeds draw independently.
        size_t stochasticRandomSeedBase;

        /// The tracer currently evaluating a surface pigment, for patterns that trace views (`screen`); null elsewhere.
        Trace *activeTrace = nullptr;
        /// Importance of that pigment's surface, which the views it traces carry on.
        double activeWeight = 1.0;
        /// Number of `screen` views currently being traced, one inside the other.
        unsigned int screenDepth = 0;
        /// Trace levels the rays that met those screens had reached, summed.
        unsigned int screenTraceLevels = 0;

        // TODO FIXME - thread-local copy of lightsources. we need this
        // because various parts of the lighting code seem to make changes
        // to the lightsource object passed to them (this is not confined
        // just to the area light shadow code). This code ought to be fixed
        // to treat the lightsource as const, after which this can go away.
        std::vector<LightSource*> lightSources;

        // all of these are for photons
        // most of them should be refactored into parameters, return values, or other objects
        LightSource *photonSourceLight;
        ObjectPtr photonTargetObject;
        bool litObjectIgnoresPhotons;
        MathColour GFilCol;
        int hitObject;    // did we hit the target object? (for autostop)
        DBL photonSpread; // photon spread (in radians)
        DBL photonDepth;  // total distance from light to intersection
        int passThruThis;           // is this a pass-through object encountered before the target?
        int passThruPrev;           // was the previous object a pass-through object encountered before the target?
        bool Light_Is_Global;       // is the current light global? (not part of a light_group?)
        PhotonMap* surfacePhotonMap;
        PhotonMap* mediaPhotonMap;

        CrackleCache* mpCrackleCache;

        static const int MESH_DECODES = 256;
        MeshTriangleDecode meshDecodes[MESH_DECODES];
        static const int MESH_SHADOW_HINTS = 64;
        MeshShadowHint meshShadowHints[MESH_SHADOW_HINTS];

        // data for waves and ripples pattern
        unsigned int numberOfWaves;
        std::vector<double> waveFrequencies;
        std::vector<Vector3d> waveSources;

        /// Called after a rectangle is finished.
        /// Used for crackle cache expiry.
        void AfterTile();

        /// Used by the crackle pattern to indicate age of cache entries.
        /// @return     The index of the current rectangle rendered.
        inline size_t ProgressIndex() const { return progress_index; }

        enum TimeType
        {
            kUnknownTime,
            kParseTime,
            kBoundingTime,
            kPhotonTime,
            kRadiosityTime,
            kRenderTime,
            kMaxTimeType
        };

        TimeType timeType;
        POV_LONG cpuTime;
        POV_LONG realTime;
        QualityFlags qualityFlags; // TODO FIXME - remove again

        inline std::shared_ptr<const SceneData> GetSceneData() const { return sceneData; }

    protected:
        /// scene data
        std::shared_ptr<SceneData> sceneData;
        /// render statistics
        RenderStatistics* mpRenderStats;

    private:

        TraceThreadData() = delete;
        TraceThreadData(const TraceThreadData&) = delete;
        TraceThreadData& operator=(const TraceThreadData&) = delete;

        /// current tile index (for crackle cache expiry)
        size_t progress_index;
};

/// Makes a tracer, or none, the one `screen` patterns trace through, for the scope of a pigment evaluation.
struct ActiveTraceScope final
{
    TraceThreadData *thread;
    Trace *trace;
    double weight;
    ActiveTraceScope(TraceThreadData *t, Trace *active, double w) : thread(t), trace(t->activeTrace), weight(t->activeWeight)
    {
        t->activeTrace = active;
        t->activeWeight = w;
    }
    ~ActiveTraceScope() { thread->activeTrace = trace; thread->activeWeight = weight; }
};

struct IsoShadowWindow final
{
    TraceThreadData *thread;
    double from;
    double to;
    IsoShadowWindow(TraceThreadData *t, double f, double u) : thread(t), from(t->isoShadowFrom), to(t->isoShadowTo)
    {
        t->isoShadowFrom = f;
        t->isoShadowTo = u;
    }
    ~IsoShadowWindow() { thread->isoShadowFrom = from; thread->isoShadowTo = to; }
};

/// @}
///
//##############################################################################

}
// end of namespace pov

#endif // POVRAY_CORE_TRACETHREADDATA_H
