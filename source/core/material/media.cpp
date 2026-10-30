//******************************************************************************
///
/// @file core/material/media.cpp
///
/// Implementations related to participating media.
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

// Unit header file must be the first file included within POV-Ray *.cpp files (pulls in config)
#include "core/material/media.h"

// C++ variants of C standard header files
//  (none at the moment)

// C++ standard header files
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <limits>
#include <mutex>
#include <unordered_set>

// POV-Ray header files (base module)
#include "base/pov_err.h"

// POV-Ray header files (core module)
#include "base/timer.h"

#include "core/lighting/emitter.h"
#include "core/material/prepareddirectionalsource.h"
#include "core/lighting/surfaceemitter.h"
#include "core/lighting/emitteraabb.h"
#include "core/material/preparedtransfer.h"
#include "core/material/physicalphase.h"
#include "core/material/interiorfield.h"
#include "core/math/finite.h"
#include "core/lighting/lightgroup.h"
#include "core/lighting/lightsource.h"
#include "core/lighting/photons.h"
#include "core/lighting/progressivephoton.h"
#include "core/material/pattern.h"
#include "core/material/pigment.h"
#include "core/material/preparedlightaxis.h"
#include "core/material/preparedsourceselector.h"
#include "core/math/chi2.h"
#include "core/math/matrix.h"
#include "core/render/ray.h"
#include "core/scene/object.h"
#include "core/scene/scenedata.h"
#include "core/scene/tracethreaddata.h"
#include "core/support/parallel.h"
#include "core/support/preparationprogress.h"
#include "core/support/statistics.h"

// this must be the last file included
#include "base/povdebug.h"

namespace pov
{

using std::min;
using std::max;
using std::vector;

enum FastMediaWarning
{
    kFastField = 1, kFastGrid = 2, kFastContainer = 4,
    kFastMixed = 8, kFastPhoton = 32, kFastCoarse = 2048
};

static void WarnFastMedia(TraceThreadData* td, const Media& media, unsigned reason,
                          const char* detail, ObjectPtr container = nullptr)
{
    const bool fieldFailure = (reason == kFastField) || (reason == kFastGrid);
    if (!td->mediaMessages || (!fieldFailure &&
        ((td->GetSceneData()->mediaWarningFlags.load(std::memory_order_relaxed) & reason) ||
         (td->GetSceneData()->mediaWarningFlags.fetch_or(reason, std::memory_order_relaxed) & reason))))
        return;
    Vector3d low, high;
    if (container)
        Make_min_max_from_BBox(low, high, container->BBox);
    const char *lead, *tail;
    if (fieldFailure)
    {
        lead = "Media method 4 is using classic sampling";
        tail = "This can be much slower. This warning is reported once for this prepared field.";
    }
    else if (reason == kFastCoarse)
    {
        lead = "Media method 4 coarsened its preparation";
        tail = "Further warnings for this reason are suppressed.";
    }
    else
    {
        lead = "Media method 4 is using classic sampling";
        tail = "This can be much slower. Further warnings for this reason are suppressed.";
    }
    td->mediaMessages->Warning(kWarningGeneral,
        "%s: %s Resolution %.6g; container bounds <%.6g,%.6g,%.6g> to <%.6g,%.6g,%.6g>%s. %s",
        lead, detail, media.FastResolution, low[X], low[Y], low[Z], high[X], high[Y], high[Z],
        container ? "" : " (unavailable)", tail);
}

static bool UsablePreparedContainer(ConstObjectPtr container)
{
    return container && !Test_Flag(container, INVERTED_FLAG) && container->BBox.finiteInterior &&
           UsableFiniteBounds(container->BBox);
}

static bool ConstantDensity(const Media& medium);
static DBL ChannelMean(const MathColour& colour);

class Media::FastCache final
{
    public:
        FastCache(ObjectPtr object, DBL resolution) : container(object), step(resolution) {}
        ObjectPtr container;
        DBL step;
        /// the requested step and the cells it needed, when the cell limit coarsened the grid; 0 when it did not
        DBL coarseStep = 0.0, coarseCells = 0.0;
        bool fieldReady = false;
        bool constantDensity = false;
        MathColour constantValue = MathColour(1.0);
        std::shared_ptr<const PreparedGridLayout> layout;
        bool published = false;
        unsigned failureReason = kFastField;
        std::string failure = "The density pattern or coefficients cannot be prepared; check for unsupported patterns or negative/non-finite values.";
        Vector3d low, size;
        int nx = 0, ny = 0, nz = 0;
        vector<MathColour> density;
        vector<unsigned char> rough;

        struct Optical final
        {
            Vector3d u, v, w;
            DBL lo[3], ds[3];
            int n[3];
            vector<MathColour> prefix;

            size_t Index(int x, int y, int z) const { return (size_t(z) * n[1] + y) * n[0] + x; }
            bool Depth(const Vector3d& point, MathColour& result) const;
        };

        /// one light's grid, keyed by the light's scene index: the LightSource a ray carries is a per-thread copy
        struct LightGrid final
        {
            size_t index = 0;
            bool group = false;
            std::shared_ptr<const Optical> grid;
        };
        static constexpr int kLightGrids = 4;
        int lightCount = 0;
        LightGrid lights[kLightGrids];

        /// a grid the eager threads finished before the render; the first EnsureField takes it instead of building one
        std::shared_ptr<EagerField> prepared;
        Vector3d cell;
        DBL effect = 0.0, cellWidth = 0.0;

        bool EnsureField(Media& media, TraceThreadData *ttd);
        void Finalize(Media& media, TraceThreadData *td, const std::function<void()>& cooperate);
        /// Checks the medium and fits the grid to the bounds and cell limit, sizing its arrays; false where BuildField fails first.
        bool SizeGrid(const Media& media, const BoundingBox& bounds, DBL cellLimit);
        /// Fills one z slab of cells; false if a sample is negative or not finite.
        bool FillSlab(Media& media, int z, TraceThreadData *td, const std::atomic<bool>& unusable);
        bool Segment(const Ray& ray, DBL& from, DBL& to) const;
        bool DensityAt(const Vector3d& point, MathColour& result) const;
        bool Contains(const Vector3d& point) const;
        bool CameraDensityAt(const Vector3d& point, MathColour& result) const;
        void PrepareOptical(Media& media, const LightSource& light, TraceThreadData *td, const std::function<void()>& cooperate);
        bool OpticalDepth(Media& media, const LightSource& light, const Vector3d& a, const Vector3d& b,
                          MathColour& result, TraceThreadData *ttd);
    private:
        bool TakePrepared();
        std::shared_ptr<const Optical> BuildOptical(const Media& media, const LightSource& light, TraceThreadData *ttd, const std::function<void()>& cooperate) const;
};

class Media::EagerField final
{
    public:
        EagerField(const std::shared_ptr<FastCache>& cache, const std::shared_ptr<Media>& copy, DBL limit) :
            target(cache), medium(copy), grid(nullptr, cache->step), requestedStep(cache->step), cellLimit(limit) {}
        std::weak_ptr<FastCache> target;
        /// what the build evaluates: a copy of the medium whose functions are the eager threads' own
        std::shared_ptr<Media> medium;
        FastCache grid;
        DBL requestedStep, cellLimit;
        std::atomic<int> next{0};
        std::atomic<bool> unusable{false}, stopped{false};
        std::atomic<unsigned char> reads{0};
        std::atomic<POV_LONG> cpuTime{0};
};

std::shared_ptr<Media::EagerField> Media::PlanEagerField(const std::shared_ptr<Media>& copy, DBL cellLimit) const
{
    if (!fastCache || (Sample_Method != 4) || fastCache->constantDensity || fastCache->prepared ||
        !UsablePreparedContainer(fastCache->container))
        return nullptr;
    std::shared_ptr<EagerField> field(new EagerField(fastCache, copy, cellLimit));
    field->grid.coarseStep = fastCache->coarseStep;
    field->grid.coarseCells = fastCache->coarseCells;
    if (!field->grid.SizeGrid(*copy, fastCache->container->BBox, cellLimit))
        return nullptr;
    return field;
}

unsigned char WorkEagerField(Media::EagerField& field, TraceThreadData *thread, const std::function<bool()>& stop)
{
    Timer timer;
    thread->globalsRead = 0;
    while (!field.unusable.load(std::memory_order_relaxed) && !(stop && (field.stopped.load(std::memory_order_relaxed) || stop())))
    {
        const int z = field.next.fetch_add(1, std::memory_order_relaxed);
        if (z >= field.grid.nz)
            break;
        bool filled = false;
        try
        {
            filled = field.grid.FillSlab(*field.medium, z, thread, field.unusable);
        }
        catch (...)
        {
            // a claimed slab left unfilled must never be handed to the render
        }
        if (!filled)
            field.unusable.store(true, std::memory_order_relaxed);
    }
    const unsigned char reads = thread->globalsRead;
    field.reads.fetch_or(reads, std::memory_order_relaxed);
    if (timer.HasValidThreadCPUTime())
        field.cpuTime.fetch_add(timer.ElapsedThreadCPUTime(), std::memory_order_relaxed);
    return reads;
}

void EagerFieldSlabs(const Media::EagerField& field, int& claimed, int& total)
{
    total = field.grid.nz;
    claimed = std::min(field.next.load(std::memory_order_relaxed), total);
}

POV_LONG EagerFieldCpuTime(const Media::EagerField& field)
{
    return field.cpuTime.load(std::memory_order_relaxed);
}

void StopEagerField(Media::EagerField& field)
{
    field.stopped.store(true, std::memory_order_relaxed);
}

void CollectPreparedCaches(ConstObjectPtr object, std::unordered_set<const void *>& caches)
{
    if (object->interior != nullptr)
        for (const Media& medium : object->interior->media)
            for (const auto& binding : medium.preparedBindings)
                if (binding->container == object)
                    caches.insert(binding.get());
}

bool EagerFieldKept(const Media::EagerField& field, const std::unordered_set<const void *>& caches)
{
    const std::shared_ptr<Media::FastCache> target = field.target.lock();
    return target && (caches.count(target.get()) != 0);
}

bool FinishEagerField(const std::shared_ptr<Media::EagerField>& field, DBL cellLimit, const std::vector<TraceThreadData *>& threads,
                      const std::function<bool(unsigned char)>& changed, const std::function<void()>& cooperate)
{
    const std::shared_ptr<Media::FastCache> target = field->target.lock();
    if (!target || target->prepared || !UsablePreparedContainer(target->container) ||
        (target->step != field->requestedStep) || (cellLimit != field->cellLimit))
        return false;
    Vector3d lo, hi;
    Make_min_max_from_BBox(lo, hi, target->container->BBox);
    const Vector3d size = hi - lo;
    for (int axis = 0; axis < 3; ++axis)
        if ((lo[axis] != field->grid.low[axis]) || (size[axis] != field->grid.size[axis]))
            return false;
    if (field->next.load(std::memory_order_relaxed) < field->grid.nz)
        ParallelFor(threads.size(), threads.size(), [&](size_t, size_t worker)
        {
            WorkEagerField(*field, threads[worker], std::function<bool()>());
        }, cooperate);
    if (field->unusable.load(std::memory_order_relaxed) || changed(field->reads.load(std::memory_order_relaxed)))
        return false;
    field->medium.reset();
    target->prepared = field;
    return true;
}

bool Media::FastCache::TakePrepared()
{
    FastCache& grid = prepared->grid;
    low = grid.low;
    size = grid.size;
    step = grid.step;
    coarseStep = grid.coarseStep;
    coarseCells = grid.coarseCells;
    nx = grid.nx;
    ny = grid.ny;
    nz = grid.nz;
    cell = grid.cell;
    effect = grid.effect;
    cellWidth = grid.cellWidth;
    density.swap(grid.density);
    rough.swap(grid.rough);
    prepared.reset();
    return true;
}

bool Media::FastCache::Segment(const Ray& ray, DBL& from, DBL& to) const
{
    Vector3d a, b;
    Make_min_max_from_BBox(a, b, container->BBox);
    for (int axis = 0; axis < 3; axis++)
    {
        if (ray.Direction[axis] == 0.0)
        {
            if (ray.Origin[axis] < a[axis] || ray.Origin[axis] > b[axis])
                return false;
            continue;
        }
        DBL first = (a[axis] - ray.Origin[axis]) / ray.Direction[axis];
        DBL last = (b[axis] - ray.Origin[axis]) / ray.Direction[axis];
        if (first > last)
            std::swap(first, last);
        from = std::max(from, first);
        to = std::min(to, last);
        if (to <= from)
            return false;
    }
    return true;
}

/// The widest grid step whose cell count fits the limit: `step` itself when it already fits, else a bisection
/// over [step, longest side]; cellsNeeded reports the count at the requested step.
static DBL FitGridStep(const Vector3d& size, DBL step, DBL limit, DBL& cellsNeeded)
{
    cellsNeeded = ceil(size[X] / step) * ceil(size[Y] / step) * ceil(size[Z] / step);
    if (cellsNeeded <= limit)
        return step;
    DBL lower = step, upper = std::max(size[X], std::max(size[Y], size[Z]));
    for (int i = 0; i < 48; i++)
    {
        const DBL candidate = (lower + upper) * 0.5;
        if (ceil(size[X] / candidate) * ceil(size[Y] / candidate) * ceil(size[Z] / candidate) > limit)
            lower = candidate;
        else
            upper = candidate;
    }
    return upper * 1.00001;
}

/// The coarsened-grid message shared by every path that fits a prepared grid to the cell limit.
static void CoarseGridDetail(char *detail, size_t length, const char *what, DBL requested, DBL needed, DBL limit, DBL fitted)
{
    std::snprintf(detail, length,
        "The %s at the requested resolution %.6g needs %.0f cells, above the limit of %.0f, so it is built at %.6g instead. "
        "Set prepared_media_cells to at least %.0f to avoid this coarsening.",
        what, requested, needed, limit, fitted, needed);
}

bool Media::FastCache::EnsureField(Media&, TraceThreadData *)
{
    if (!published || !fieldReady)
        throw POV_EXCEPTION_STRING("Prepared field was queried before successful publication.");
    return true;
}

void Media::FastCache::Finalize(Media& media, TraceThreadData *td, const std::function<void()>& cooperate)
{
    if (cooperate)
        cooperate();
    if (constantDensity)
    {
        Evaluate_Density_Pigment(media.Density, Vector3d(0.0), constantValue, td);
        fieldReady = true;
        for (int ch = 0; ch < MathColour::channels; ch++)
            fieldReady = fieldReady && IsFiniteNumber(constantValue[ch]) &&
                constantValue[ch] >= 0.0;
        if (media.Sample_Method == 4)
            for (int ch = 0; ch < MathColour::channels; ch++)
                fieldReady = fieldReady && IsFiniteNumber(media.Extinction[ch]) && (media.Extinction[ch] >= 0.0) &&
                             IsFiniteNumber(media.Emission[ch]) && IsFiniteNumber(media.Scattering[ch]);
        if (!fieldReady)
            throw POV_EXCEPTION_STRING("Requested method 4 has invalid constant coefficients.");
        if (cooperate)
            cooperate();
        published = true;
        return;
    }
    if (container == nullptr)
        throw POV_EXCEPTION_STRING("Prepared varying density requires a finite region.");
    for (const PIGMENT *pigment : media.Density)
        if (PigmentRequiresRayContext(pigment))
            throw POV_EXCEPTION_STRING("Prepared density field requires ray context and cannot be published.");
    Vector3d lo, hi;
    Make_min_max_from_BBox(lo, hi, container->BBox);
    const Vector3d extent = hi - lo;
    if (!UsablePreparedContainer(container))
        throw POV_EXCEPTION_STRING("Prepared varying density requires finite usable bounds.");
    if (prepared)
        fieldReady = TakePrepared();
    if (fieldReady && layout)
        step = cellWidth;
    bool sameBounds = fieldReady;
    for (int axis = 0; axis < 3; axis++)
        sameBounds = sameBounds && (low[axis] == lo[axis]) && (size[axis] == extent[axis]);
    if (media.fastCache && media.fastCache.get() == this)
    {
        const auto layout = PrepareInteriorFieldLayout(container, td);
        if (layout)
            sameBounds = sameBounds && nx == int(layout->counts[X]) && ny == int(layout->counts[Y]) && nz == int(layout->counts[Z]);
    }
    if (!sameBounds)
    {
        step = media.FastResolution;
        density.clear();
        rough.clear();
        if (!SizeGrid(media, container->BBox, td->GetSceneData()->EffectivePreparedMediaCells()))
            throw POV_EXCEPTION_STRING("Prepared density cannot be published for this configuration.");
        std::atomic<bool> unusable(false);
        const size_t count = std::max<size_t>(1, std::min<size_t>(nz, td->GetSceneData()->workThreads));
        std::vector<std::unique_ptr<TraceThreadData>> helpers;
        for (size_t i = 1; i < count; i++)
            helpers.emplace_back(new TraceThreadData(std::const_pointer_cast<SceneData>(td->GetSceneData()), i, false));
        ParallelFor(size_t(nz), count, [&](size_t z, size_t worker) {
            if ((count == 1) && cooperate)
                cooperate();
            if (!FillSlab(media, int(z), worker ? helpers[worker - 1].get() : td, unusable))
                unusable.store(true, std::memory_order_relaxed);
        }, cooperate);
        for (const auto& helper : helpers)
            td->Stats() += helper->Stats();
        if (unusable.load(std::memory_order_relaxed))
            throw POV_EXCEPTION_STRING("Prepared density publication produced an invalid value.");
        fieldReady = true;
    }
    if (coarseCells > 0.0)
    {
        char detail[512];
        DBL needed;
        const DBL limit = td->GetSceneData()->EffectivePreparedMediaCells();
        // the resolution to write to get this grid, not the layout's cell width it was built on
        CoarseGridDetail(detail, sizeof(detail), "density grid", coarseStep, coarseCells, limit,
                         FitGridStep(size, coarseStep, limit, needed));
        WarnFastMedia(td, media, kFastCoarse, detail, container);
    }
    if (cooperate)
        cooperate();
    published = true;
}

bool Media::FastCache::SizeGrid(const Media& media, const BoundingBox& bounds, DBL cellLimit)
{
    if (!bounds.finiteInterior || !UsableFiniteBounds(bounds))
        return false;
    for (int ch = 0; ch < MathColour::channels; ch++)
        if (!IsFiniteNumber(media.Extinction[ch]) || (media.Extinction[ch] < 0.0))
            return false;
    for (const PIGMENT *pigment : media.Density)
        if ((pigment->Type != PLAIN_PATTERN) &&
            ((pigment->Type <= LAST_SPECIAL_PATTERN) ||
             (dynamic_cast<const ContinuousPattern*>(pigment->pattern.get()) == nullptr)))
            return false;
    Vector3d hi;
    Make_min_max_from_BBox(low, hi, bounds);
    size = hi - low;
    // The grid is coarsened to the widest step that fits, never dropped to classic sampling; the parse-time fit
    // normally leaves nothing to do here, so this covers bounds that grew after the medium was prepared.
    if (step > 0.0)
    {
        DBL needed;
        const DBL fitted = FitGridStep(size, step, cellLimit, needed);
        if (fitted != step)
        {
            coarseStep = step;
            coarseCells = needed;
            step = fitted;
        }
    }
    const DBL sx = ceil(size[X] / step), sy = ceil(size[Y] / step), sz = ceil(size[Z] / step);
    if ((sx < 1) || (sy < 1) || (sz < 1))
    {
        failureReason = kFastGrid;
        failure = "The density grid has no usable cell size; check the container's bounds and the media resolution.";
        return false;
    }
    nx = layout ? int(layout->counts[X]) : int(sx);
    ny = layout ? int(layout->counts[Y]) : int(sy);
    nz = layout ? int(layout->counts[Z]) : int(sz);
    cell = Vector3d(size[X] / nx, size[Y] / ny, size[Z] / nz);
    density.resize(size_t(nx) * ny * nz);
    rough.clear();
    effect = 0.0;
    cellWidth = std::max(cell[X], std::max(cell[Y], cell[Z]));
    if (layout)
        step = cellWidth;
    return true;
}

bool Media::FastCache::FillSlab(Media& media, int z, TraceThreadData *td, const std::atomic<bool>& unusable)
{
    for (int y = 0; (y < ny) && !unusable.load(std::memory_order_relaxed); y++)
        for (int x = 0; x < nx; x++)
        {
            MathColour& value = density[(size_t(z) * ny + y) * nx + x];
            for (int dz = 0; dz < 2; dz++)
                for (int dy = 0; dy < 2; dy++)
                    for (int dx = 0; dx < 2; dx++)
                    {
                        const Vector3d point = low + Vector3d((x + 0.25 + 0.5 * dx) * cell[X],
                                                               (y + 0.25 + 0.5 * dy) * cell[Y],
                                                               (z + 0.25 + 0.5 * dz) * cell[Z]);
                        MathColour sample;
                        Evaluate_Density_Pigment(media.Density, point, sample, td);
                        for (int ch = 0; ch < MathColour::channels; ch++)
                            if (!IsFiniteNumber(sample[ch]) || (sample[ch] < 0.0))
                                return false;
                        value += sample;

                    }
            value *= 0.125;

        }
    return true;
}


bool Media::FastCache::DensityAt(const Vector3d& point, MathColour& result) const
{
    if ((point[X] < low[X]) || (point[Y] < low[Y]) || (point[Z] < low[Z]) ||
        (point[X] > low[X] + size[X]) || (point[Y] > low[Y] + size[Y]) || (point[Z] > low[Z] + size[Z]))
        return false;
    if (constantDensity)
    {
        result = constantValue;
        return fieldReady;
    }
    int base[3];
    DBL blend[3];
    const int n[3] = { nx, ny, nz };
    for (int axis = 0; axis < 3; axis++)
    {
        const DBL cell = (point[axis] - low[axis]) * n[axis] / size[axis] - 0.5;
        base[axis] = std::max(0, std::min(n[axis] - 2, int(floor(cell))));
        blend[axis] = std::max(0.0, std::min(1.0, cell - base[axis]));
        if (n[axis] == 1) { base[axis] = 0; blend[axis] = 0.0; }
    }
    result.Clear();
    for (int z = 0; z < 2; z++)
        for (int y = 0; y < 2; y++)
            for (int x = 0; x < 2; x++)
            {
                const DBL weight = (x ? blend[0] : 1.0 - blend[0]) *
                                   (y ? blend[1] : 1.0 - blend[1]) * (z ? blend[2] : 1.0 - blend[2]);
                result += density[(size_t(base[2] + z * (nz > 1)) * ny + base[1] + y * (ny > 1)) * nx +
                                  base[0] + x * (nx > 1)] * weight;
            }
    return true;
}

bool Media::FastCache::Contains(const Vector3d& point) const
{
    for (int axis = 0; axis < 3; axis++)
        if ((point[axis] < low[axis]) || (point[axis] > low[axis] + size[axis]))
            return false;
    return true;
}

bool Media::FastCache::CameraDensityAt(const Vector3d& point, MathColour& result) const
{
    if (!published || !fieldReady)
        throw POV_EXCEPTION_STRING("Prepared field was queried before successful publication.");
    if (constantDensity)
    {
        result = constantValue;
        return true;
    }
    if (!DensityAt(point, result))
        result.Clear();
    return true;
}

bool Media::FastCache::Optical::Depth(const Vector3d& point, MathColour& result) const
{
    const DBL coord[3] = { dot(point, u), dot(point, v), dot(point, w) };
    int base[3];
    DBL blend[3];
    for (int axis = 0; axis < 3; axis++)
    {
        if ((coord[axis] < lo[axis] - 1e-5) || (coord[axis] > lo[axis] + ds[axis] * n[axis] + 1e-5))
            return false;
        const DBL cell = (coord[axis] - lo[axis]) / ds[axis] - (axis == 2 ? 0.0 : 0.5);
        base[axis] = std::max(0, std::min(n[axis] - (axis == 2 ? 1 : 2), int(floor(cell))));
        blend[axis] = std::max(0.0, std::min(1.0, cell - base[axis]));
        if ((axis != 2) && (n[axis] == 1)) { base[axis] = 0; blend[axis] = 0.0; }
    }
    result.Clear();
    for (int z = 0; z < 2; z++)
        for (int y = 0; y < 2; y++)
            for (int x = 0; x < 2; x++)
                result += prefix[Index(base[0] + x * (n[0] > 1), base[1] + y * (n[1] > 1), base[2] + z)] *
                          (x ? blend[0] : 1.0 - blend[0]) * (y ? blend[1] : 1.0 - blend[1]) *
                          (z ? blend[2] : 1.0 - blend[2]);
    return true;
}

std::shared_ptr<const Media::FastCache::Optical> Media::FastCache::BuildOptical(const Media& media,
                                                     const LightSource& light, TraceThreadData *ttd, const std::function<void()>& cooperate) const
{
    if (!light.Parallel || light.Area_Light || (light.Light_Type == CYLINDER_SOURCE) ||
        !light.Media_Attenuation || !light.Media_Interaction || (light.portal != nullptr))
        return nullptr;
    std::shared_ptr<Optical> grid(new Optical());
    grid->w = -light.Direction;
    grid->w.normalize();
    grid->u = cross(grid->w, fabs(grid->w[Y]) < 0.9 ? Vector3d(0, 1, 0) : Vector3d(1, 0, 0)).normalized();
    grid->v = cross(grid->w, grid->u);
    const Vector3d basis[3] = { grid->u, grid->v, grid->w };
    DBL span[3];
    for (int axis = 0; axis < 3; axis++)
    {
        grid->lo[axis] = std::numeric_limits<DBL>::infinity();
        DBL hi = -grid->lo[axis];
        for (int z = 0; z < 2; z++)
            for (int y = 0; y < 2; y++)
                for (int x = 0; x < 2; x++)
                {
                    const DBL value = dot(low + Vector3d(x * size[X], y * size[Y], z * size[Z]), basis[axis]);
                    grid->lo[axis] = std::min(grid->lo[axis], value);
                    hi = std::max(hi, value);
                }
        span[axis] = hi - grid->lo[axis];
    }
    // A single uniform scale coarsens ds until every count and the total fit, so the table is built, not dropped.
    const DBL limit = ttd->GetSceneData()->EffectivePreparedMediaCells();
    DBL total = 1.0;
    for (int axis = 0; axis < 3; axis++)
        total *= std::max(1.0, ceil(span[axis] / step));
    const DBL needed = total;
    DBL scale = 1.0;
    while (total > limit)
    {
        scale = (scale > 1.0) ? scale * 1.05 : 1.05;
        total = 1.0;
        for (int axis = 0; axis < 3; axis++)
            total *= std::max(1.0, ceil(span[axis] / (step * scale)));
    }
    for (int axis = 0; axis < 3; axis++)
    {
        const DBL count = std::max(1.0, ceil(span[axis] / (step * scale)));
        grid->n[axis] = int(count);
        grid->ds[axis] = span[axis] / grid->n[axis];
    }
    if (scale > 1.0)
    {
        char detail[512];
        CoarseGridDetail(detail, sizeof(detail), "optical-depth grid", step, needed, limit, step * scale);
        WarnFastMedia(ttd, media, kFastCoarse, detail, container);
    }
    grid->prefix.resize(size_t(grid->n[0]) * grid->n[1] * (grid->n[2] + 1));
    // Columns along the light are independent, so rows of them are shared out like the density grid's slabs.
    const size_t cells = size_t(grid->n[0]) * grid->n[1] * grid->n[2];
    const size_t workers = (cells >= 4096) ? std::max<size_t>(1, std::min<size_t>(grid->n[1], ttd->GetSceneData()->workThreads)) : 1;
    std::vector<std::unique_ptr<TraceThreadData>> helpers;
    for (size_t i = 1; i < workers; ++i)
        helpers.emplace_back(new TraceThreadData(std::const_pointer_cast<SceneData>(ttd->GetSceneData()), i, false));
    ParallelFor(size_t(grid->n[1]), workers, [&](size_t row, size_t worker)
    {
        TraceThreadData *td = (worker == 0) ? ttd : helpers[worker - 1].get();
        const int y = int(row);
        for (int x = 0; x < grid->n[0]; x++)
        {
            if ((workers == 1) && cooperate)
                cooperate();
            for (int z = 0; z < grid->n[2]; z++)
            {
                const Vector3d point = grid->u * (grid->lo[0] + (x + 0.5) * grid->ds[0]) +
                                       grid->v * (grid->lo[1] + (y + 0.5) * grid->ds[1]) +
                                       grid->w * (grid->lo[2] + (z + 0.5) * grid->ds[2]);
                MathColour value;
                if (!Inside_Object(point, container, td) || !DensityAt(point, value))
                    value.Clear();
                grid->prefix[grid->Index(x, y, z + 1)] = grid->prefix[grid->Index(x, y, z)] +
                                                          value * media.Extinction * grid->ds[2];
            }
        }
    }, cooperate);
    for (const auto& helper : helpers)
        ttd->Stats() += helper->Stats();
    return grid;
}

void Media::FastCache::PrepareOptical(Media& media, const LightSource& light, TraceThreadData *td, const std::function<void()>& cooperate)
{
    if (constantDensity || !light.Parallel || light.Area_Light || (light.Light_Type == CYLINDER_SOURCE) ||
        !light.Media_Attenuation || !light.Media_Interaction || light.portal ||
        !td->GetSceneData()->portalMouths.empty())
        return;
    for (int i = 0; i < lightCount; i++)
        if (lights[i].index == light.index && lights[i].group == light.lightGroupLight)
            return;
    if (lightCount >= kLightGrids)
        return;
    LightGrid& slot = lights[lightCount++];
    slot.index = light.index;
    slot.group = light.lightGroupLight;
    slot.grid = BuildOptical(media, light, td, cooperate);
}

bool Media::FastCache::OpticalDepth(Media& media, const LightSource& light, const Vector3d& a,
                                    const Vector3d& b, MathColour& result, TraceThreadData *td)
{
    EnsureField(media, td);
    const Optical *grid = nullptr;
    for (int i = 0; i < lightCount; i++)
        if (lights[i].index == light.index && lights[i].group == light.lightGroupLight)
            grid = lights[i].grid.get();
    MathColour first, last;
    if (!grid || !grid->Depth(a, first) || !grid->Depth(b, last))
        return false;
    result = last - first;
    for (int ch = 0; ch < MathColour::channels; ch++)
        if (result[ch] < 0.0)
            result[ch] = 0.0;
    return true;
}

RefractionSpec::RefractionSpec(const RefractionSpec& source) : detail(source.detail),
    transform(Copy_Transform(source.transform))
{
    for (const auto function : source.functions)
        functions.push_back(function->Clone());
}

RefractionSpec::~RefractionSpec()
{
    for (auto function : functions)
        delete function;
    Destroy_Transform(transform);
}

void RefractionSpec::Transform(const TRANSFORM *mapping)
{
    if (!transform)
        transform = Copy_Transform(mapping);
    else
        Compose_Transforms(transform, mapping);
}

Media::Media()
{
    Type = ISOTROPIC_SCATTERING;

    Intervals      = 10;
    Min_Samples    = 1;
    Max_Samples    = 1;
    Eccentricity   = 0.0;

    Absorption.Clear();
    Emission.Clear();
    Extinction.Clear();
    Scattering.Clear();

    is_constant = false;

    use_absorption = false;
    use_emission   = false;
    use_extinction = false;
    use_scattering = false;

    ignore_photons = false;

    sc_ext     = 1.0;
    Ratio      = 0.9;
    Confidence = 0.9;
    Variance   = 1.0 / 128.0;

    Sample_Threshold = nullptr;

    Sample_Method = 1;
    AA_Threshold = 0.1;
    AA_Level = 3;
    Jitter = 0.0;
    FastResolution = -1.0;
    mix = kMediaBlendAuto;
    priority = 0;
}

Media::Media(const Media& source)
{
    Sample_Threshold = nullptr;

    *this = source;
}

Media::~Media()
{
    if (Sample_Threshold != nullptr)
        delete[] Sample_Threshold;

    for (vector<PIGMENT*>::iterator i = Density.begin(); i != Density.end(); ++ i)
        Destroy_Pigment(*i);
}

Media& Media::operator=(const Media& source)
{
    if(&source != this)
    {
        Type = source.Type;
        Intervals = source.Intervals;
        Min_Samples = source.Min_Samples;
        Max_Samples = source.Max_Samples;
        Sample_Method = source.Sample_Method;
        is_constant = source.is_constant;
        use_absorption = source.use_absorption;
        use_emission = source.use_emission;
        use_extinction = source.use_extinction;
        use_scattering = source.use_scattering;
        ignore_photons = source.ignore_photons;
        Jitter = source.Jitter;
        Eccentricity = source.Eccentricity;
        sc_ext = source.sc_ext;
        Absorption = source.Absorption;
        Emission = source.Emission;
        Extinction = source.Extinction;
        Scattering = source.Scattering;
        Ratio = source.Ratio;
        Confidence = source.Confidence;
        Variance = source.Variance;
        AA_Threshold = source.AA_Threshold;
        AA_Level = source.AA_Level;
        FastResolution = source.FastResolution;
        requestedFastResolution = source.requestedFastResolution;
        fastResolutionRecorded = source.fastResolutionRecorded;
        mix = source.mix;
        priority = source.priority;
        where = source.where;
        fastCache.reset();
        preparedBindings.clear();

        if (Sample_Threshold != nullptr)
            delete[] Sample_Threshold;
        Sample_Threshold = nullptr;

        for (vector<PIGMENT*>::iterator i = Density.begin(); i != Density.end(); ++ i)
            Destroy_Pigment(*i);
        Density.resize(0);
        Density.reserve(source.Density.size());
        for (vector<PIGMENT*>::const_iterator i = source.Density.begin(); i != source.Density.end(); ++ i)
            Density.push_back(Copy_Pigment(*i));

        if (source.Sample_Threshold != nullptr)
        {
            if(Intervals > 0)
            {
                Sample_Threshold = new DBL[Intervals];

                for(int i = 0; i < Intervals; i++)
                    Sample_Threshold[i] =  source.Sample_Threshold[i];
            }
        }
    }

    return *this;
}

void Media::Transform(const TRANSFORM *Trans)
{
    Transform_Density(Density, Trans);
    fastCache.reset();
    preparedBindings.clear();
}

// The prepared grid's width for a container when none is given: 0 for an unbounded one.
static DBL AutomaticResolution(ObjectPtr object)
{
    Vector3d low, high;
    Make_min_max_from_BBox(low, high, object->BBox);
    const Vector3d size = high - low;
    const DBL volume = size[X] * size[Y] * size[Z];
    if (!IsFiniteNumber(volume) || (size[X] <= 0.0) || (size[Y] <= 0.0) || (size[Z] <= 0.0))
        return 0.0;
    return std::max(std::max(size[X], std::max(size[Y], size[Z])) / 128.0, std::cbrt(volume / 500000.0));
}

// A container's bounding-box size, the extent measure the detail rules share.
static Vector3d ContainerSize(ObjectPtr object)
{
    Vector3d low, high;
    Make_min_max_from_BBox(low, high, object->BBox);
    return high - low;
}

std::shared_ptr<const PreparedGridLayout> PrepareInteriorFieldLayout(ObjectPtr owner, TraceThreadData *td)
{
    if (!owner || !owner->interior)
        throw POV_EXCEPTION_STRING("A prepared interior field requires its geometry binding.");
    Vector3d low, high;
    Make_min_max_from_BBox(low, high, owner->BBox);
    const Vector3d size = high - low;
    const bool bounded = UsablePreparedContainer(owner);
    std::shared_ptr<InteriorField> existing;
    for (const auto& field : owner->interior->fields)
        if (field->owner == owner)
        {
            bool sameBounds = bounded && bool(field->layout);
            for (int axis = 0; sameBounds && axis < 3; ++axis)
                sameBounds = field->layout->low[axis] == low[axis] && field->layout->size[axis] == size[axis];
            if (sameBounds || (!bounded && !field->layout))
                return field->layout;
            if (field->mediaReady || field->lightReady)
                throw POV_EXCEPTION_STRING("Prepared geometry bounds changed after publication.");
            existing = field;
            field->layout.reset();
            field->lights.clear();
            break;
        }
    const auto scene = td->GetSceneData();
    DBL requested = HUGE_VAL;
    bool needed = owner->interior->light != nullptr;
    if (needed)
        requested = AutomaticResolution(owner);
    if (owner->interior->refraction && !owner->interior->refraction->functions.empty())
    {
        needed = true;
        const DBL detail = owner->interior->refraction->detail;
        requested = std::min(requested, detail > 0.0 ? detail * 0.5 : AutomaticResolution(owner));
    }
    // a constant medium owns no grid, so its step sets the layout only when nothing that varies does
    DBL constantStep = HUGE_VAL;
    for (const Media& medium : owner->interior->media)
    {
        if (medium.Sample_Method != 4)
            continue;
        needed = true;
        const DBL authored = medium.fastResolutionRecorded ? medium.requestedFastResolution : medium.FastResolution;
        const DBL step = authored > 0.0 ? authored : AutomaticResolution(owner);
        DBL& target = ConstantDensity(medium) ? constantStep : requested;
        if (step > 0.0)
            target = std::min(target, step);
    }
    if (!(requested < HUGE_VAL))
        requested = constantStep;
    if (!needed)
        return nullptr;
    std::shared_ptr<const PreparedGridLayout> layout;
    if (bounded)
    {
        if (!(requested > 0.0) || !IsFiniteNumber(requested))
            throw POV_EXCEPTION_STRING("Prepared interior grid has no usable sampling step.");
        DBL neededCells = 0.0;
        const DBL step = FitGridStep(size, requested, scene->EffectivePreparedMediaCells(), neededCells);
        std::array<size_t, 3> counts;
        for (int axis = 0; axis < 3; axis++)
            counts[axis] = size_t(std::max(1.0, std::ceil(size[axis] / step)));
        layout = std::make_shared<const PreparedGridLayout>(low, size, counts);
    }
    if (existing)
        existing->layout = layout;
    else
        owner->interior->fields.push_back(std::make_shared<InteriorField>(owner, layout));
    return layout;
}

void Media::SetFastContainer(ObjectPtr object, DBL cellLimit)
{
    if (Sample_Method != 4)
        return;
    if (!fastResolutionRecorded)
    {
        requestedFastResolution = FastResolution;
        fastResolutionRecorded = true;
    }
    FastResolution = requestedFastResolution;
    if (FastResolution == -1.0)
        FastResolution = AutomaticResolution(object);
    if ((FastResolution <= 0.0) || !IsFiniteNumber(FastResolution))
        return;
    for (const auto& binding : preparedBindings)
        if (binding->container == object)
        {
            fastCache = binding;
            return;
        }
    if (fastCache && (fastCache->container != object))
        fastCache.reset();
    if (!fastCache)
    {
        // The grid is fitted here, at parse time, so every thread sees one resolution: a step whose grid would
        // exceed the cell limit becomes the widest step that fits, reported when the field is first built.
        DBL needed = 0.0;
        const DBL requested = FastResolution;
        if (!ConstantDensity(*this) && (cellLimit > 0.0))
            FastResolution = FitGridStep(ContainerSize(object), FastResolution, cellLimit, needed);
        fastCache.reset(new FastCache(object, FastResolution));
        if (ConstantDensity(*this))
        {
            fastCache->constantDensity = true;
            Vector3d high;
            Make_min_max_from_BBox(fastCache->low, high, object->BBox);
            fastCache->size = high - fastCache->low;
            bool ready = UsablePreparedContainer(object);
            for (int ch = 0; ch < MathColour::channels; ch++)
                ready = ready && IsFiniteNumber(Extinction[ch]) && (Extinction[ch] >= 0.0) &&
                        IsFiniteNumber(Emission[ch]) && IsFiniteNumber(Scattering[ch]);
            fastCache->fieldReady = ready;
            fastCache->failure = "The constant medium has no finite usable layout or has invalid coefficients.";
        }
        if (FastResolution != requested)
        {
            fastCache->coarseStep = requested;
            fastCache->coarseCells = needed;
        }
    }
    if (std::find(preparedBindings.begin(), preparedBindings.end(), fastCache) == preparedBindings.end())
        preparedBindings.push_back(fastCache);
}

bool Media::PreparedFor(ConstObjectPtr object) const
{
    for (const auto& binding : preparedBindings)
        if (binding->container == object)
            return true;
    return fastCache && (fastCache->container == object);
}

void Media::FinalizePreparedField(ObjectPtr owner, TraceThreadData *td, const std::function<void()>& cooperate)
{
    if (Sample_Method != 4)
        return;
    if (owner != nullptr)
    {
        const auto scene = td->GetSceneData();
        SetFastContainer(owner, scene->EffectivePreparedMediaCells());
    }
    else if (!fastCache)
    {
        fastCache.reset(new FastCache(nullptr, FastResolution));
        fastCache->constantDensity = ConstantDensity(*this);
        preparedBindings.push_back(fastCache);
    }
    if (!fastCache || (fastCache->container != owner))
        throw POV_EXCEPTION_STRING("Prepared spatial field has no valid placement binding.");
    if (owner)
        fastCache->layout = PrepareInteriorFieldLayout(owner, td);
    fastCache->Finalize(*this, td, cooperate);
    if (Sample_Method == 4)
    {
        for (const LightSource *light : td->GetSceneData()->lightSources)
        {
            if (cooperate)
                cooperate();
            fastCache->PrepareOptical(*this, *light, td, cooperate);
        }
        for (const LightSource *light : td->GetSceneData()->lightGroupLightSources)
        {
            if (cooperate)
                cooperate();
            fastCache->PrepareOptical(*this, *light, td, cooperate);
        }
    }
}

void Media::RequirePreparedField() const
{
    for (const auto& binding : preparedBindings)
        if (binding->published && binding->fieldReady)
            return;
    throw POV_EXCEPTION_STRING("Requested prepared field has not been published.");
}

void Media::PreparedDensityAt(const Vector3d& point, MathColour& result) const
{
    RequirePreparedField();
    for (const auto& binding : preparedBindings)
        if (binding->published && binding->fieldReady && binding->constantDensity)
        {
            result = binding->constantValue;
            return;
        }
    const FastCache *chosen = nullptr;
    for (const auto& binding : preparedBindings)
        if (binding->published && binding->fieldReady && binding->Contains(point) &&
            (!chosen || binding->step < chosen->step))
            chosen = binding.get();
    if (!chosen || !chosen->DensityAt(point, result))
        result.Clear();
}

DBL Media::PreparedStep(const Ray& ray, DBL from, DBL to) const
{
    RequirePreparedField();
    DBL step = HUGE_VAL;
    for (const auto& binding : preparedBindings)
    {
        if (!binding->published || !binding->fieldReady)
            continue;
        DBL first = from, last = to;
        if ((binding->container == nullptr) || binding->Segment(ray, first, last))
            if (binding->step > 0.0)
                step = std::min(step, binding->step);
    }
    return step;
}

bool Media::PreparedRange(const Ray& ray, DBL& from, DBL& to) const
{
    RequirePreparedField();
    DBL first = to, last = from;
    bool found = false;
    for (const auto& binding : preparedBindings)
    {
        if (!binding->published || !binding->fieldReady)
            continue;
        DBL a = from, b = to;
        if ((binding->container == nullptr) || binding->Segment(ray, a, b))
        {
            first = std::min(first, a);
            last = std::max(last, b);
            found = true;
        }
    }
    if (found)
    {
        from = first;
        to = last;
    }
    return found;
}

void Media::PreparedCuts(const Ray& ray, DBL from, DBL to, std::vector<DBL>& cuts) const
{
    RequirePreparedField();
    for (const auto& binding : preparedBindings)
    {
        if (!binding->published || !binding->fieldReady || (binding->container == nullptr))
            continue;
        DBL a = from, b = to;
        if (binding->Segment(ray, a, b))
        {
            cuts.push_back(a);
            cuts.push_back(b);
        }
    }
}

Media::FastCache *Media::SinglePreparedBinding() const
{
    FastCache *result = nullptr;
    for (const auto& binding : preparedBindings)
        if (binding->published && binding->fieldReady)
        {
            if (result)
                return nullptr;
            result = binding.get();
        }
    return result;
}

static void ValidateNativePreparedPhotonSources(ObjectPtr owner, TraceThreadData *td)
{
    const auto scene = td->GetSceneData();
    if (!scene->photonSettings.photonsEnabled || scene->photonSettings.Progressive() || scene->photonSettings.maxMediaSteps <= 0)
        return;
    bool prepared = false, scattering = false;
    for (const Media& medium : owner->interior->media)
    {
        prepared = prepared || (medium.Sample_Method == 4 &&
            (medium.use_extinction || medium.use_scattering || medium.use_emission));
        scattering = scattering || (medium.use_scattering && !medium.ignore_photons);
    }
    if (!scattering)
        return;
    Vector3d lo, hi;
    Make_min_max_from_BBox(lo, hi, owner->BBox);
    auto validate = [&](const LightSource *light) {
        if (light->Parallel || !light->Media_Interaction || (!prepared && !light->emitter))
            return;
        if (Test_Flag(light, PH_RFL_OFF_FLAG) && Test_Flag(light, PH_RFR_OFF_FLAG))
            return;
        bool overlaps = false;
        if (light->emitter)
        {
            if (!light->emitterContainer)
                throw POV_EXCEPTION_STRING("Native photon emission has no certified source-origin geometry.");
            Vector3d sourceLow, sourceHigh;
            Make_min_max_from_BBox(sourceLow, sourceHigh, light->emitterContainer->BBox);
            overlaps = true;
            for (int axis = 0; axis < 3; axis++)
                overlaps = overlaps && sourceHigh[axis] >= lo[axis] && sourceLow[axis] <= hi[axis];
        }
        else
            overlaps = Inside_Object(light->Center, owner, td);
        if (overlaps)
        {
            if (light->emitter)
                throw POV_EXCEPTION_STRING("Native photon method 1 requires certified source-origin bounds disjoint from prepared scattering media; use photon method 2.");
            throw POV_EXCEPTION_STRING("Native photon method 1 requires source-origin support outside prepared scattering media; use photon method 2.");
        }
    };
    for (const LightSource *light : scene->lightSources)
        validate(light);
    for (const LightSource *light : scene->lightGroupLightSources)
        validate(light);
}

class PreparedPlacedScalarField final : public PreparedScalarField
{
    public:
        struct Entry final
        {
            std::shared_ptr<const PreparedGridLayout> layout;
            std::shared_ptr<const PreparedScalarField> field;
            DBL step;
        };
        explicit PreparedPlacedScalarField(std::vector<Entry> entries) : entries(std::move(entries)) {}
        PreparedScalarSample Sample(const Vector3d& point) const override
        {
            const Entry *chosen = nullptr;
            for (const Entry& entry : entries)
            {
                bool contains = true;
                for (int axis = 0; axis < 3; axis++)
                    contains = contains && point[axis] >= entry.layout->low[axis] &&
                               point[axis] <= entry.layout->low[axis] + entry.layout->size[axis];
                if (contains && (!chosen || entry.step < chosen->step))
                    chosen = &entry;
            }
            return chosen ? chosen->field->Sample(point) : PreparedScalarSample();
        }
    private:
        const std::vector<Entry> entries;
};

static std::shared_ptr<const PreparedScalarField> PrepareRefraction(const RefractionSpec& spec,
    const Interior& interior, TraceThreadData *td, const std::function<void()>& cooperate)
{
    if (!IsFiniteNumber(spec.detail) || spec.detail < 0.0)
        throw POV_EXCEPTION_STRING("Refraction detail must be finite and nonnegative.");
    if (spec.functions.empty())
        return std::make_shared<const PreparedConstantScalarField>(0.0);
    for (const auto function : spec.functions)
        if (!function || function->RequiresRayContext())
            throw POV_EXCEPTION_STRING("Refraction function requires ray context and cannot be prepared.");
    auto valueAt = [&](const Vector3d& world, TraceThreadData *thread) {
        Vector3d point = world;
        if (spec.transform)
            MInvTransPoint(point, world, spec.transform);
        DBL value = 0.0;
        for (const auto function : spec.functions)
        {
            const DBL contribution = GenericScalarFunctionInstance(function, thread).Evaluate(point);
            if (!IsFiniteNumber(contribution))
                throw POV_EXCEPTION_STRING("Refraction function produced a non-finite scalar value.");
            value += contribution;
        }
        if (!IsFiniteNumber(value))
            throw POV_EXCEPTION_STRING("Refraction functions produced a non-finite sum.");
        return value;
    };
    std::vector<PreparedPlacedScalarField::Entry> entries;
    for (const auto& binding : interior.fields)
    {
        if (!binding->layout)
            throw POV_EXCEPTION_STRING("Varying refraction requires a finite prepared region.");
        const auto& layout = binding->layout;
        std::vector<DBL> values(layout->ValueCount());
        const size_t workers = std::max<size_t>(1, std::min<size_t>(layout->counts[Z], td->GetSceneData()->workThreads));
        std::vector<std::unique_ptr<TraceThreadData>> helpers;
        for (size_t worker = 1; worker < workers; worker++)
            helpers.emplace_back(new TraceThreadData(std::const_pointer_cast<SceneData>(td->GetSceneData()), worker, false));
        ParallelFor(layout->counts[Z], workers, [&](size_t z, size_t worker) {
            if (workers == 1 && cooperate)
                cooperate();
            TraceThreadData *thread = worker ? helpers[worker - 1].get() : td;
            for (size_t y = 0; y < layout->counts[Y]; y++)
                for (size_t x = 0; x < layout->counts[X]; x++)
                {
                    const Vector3d point = layout->low + Vector3d((x + 0.5) * layout->size[X] / layout->counts[X],
                        (y + 0.5) * layout->size[Y] / layout->counts[Y], (z + 0.5) * layout->size[Z] / layout->counts[Z]);
                    values[(z * layout->counts[Y] + y) * layout->counts[X] + x] = valueAt(point, thread);
                }
        }, cooperate);
        for (const auto& helper : helpers)
            td->Stats() += helper->Stats();
        const DBL step = std::max(layout->size[X] / layout->counts[X],
            std::max(layout->size[Y] / layout->counts[Y], layout->size[Z] / layout->counts[Z]));
        entries.push_back({layout, std::make_shared<const PreparedScalarGrid>(layout, std::move(values)), step});
    }
    if (entries.empty())
        throw POV_EXCEPTION_STRING("Refraction has no prepared geometry binding.");
    return std::make_shared<const PreparedPlacedScalarField>(std::move(entries));
}

static std::vector<Interior *> RefractionInteriors(const SceneData& scene)
{
    std::unordered_set<ConstObjectPtr> seen;
    std::unordered_set<Interior *> interiors;
    std::function<void(ObjectPtr)> visit = [&](ObjectPtr object) {
        if (!object || !seen.insert(object).second)
            return;
        if (object->interior)
            interiors.insert(object->interior.get());
        if (auto compound = dynamic_cast<CompoundObject *>(object))
            for (ObjectPtr child : compound->children)
                visit(child);
    };
    for (const auto& set : scene.preparedSets)
        for (ObjectPtr object : set->objects)
            visit(object);
    return std::vector<Interior *>(interiors.begin(), interiors.end());
}

std::size_t PreparedLightUnits(const SceneData& scene)
{
    return RefractionInteriors(scene).size() + scene.mediaLights.size();
}

static void PrepareRefractionDefinitions(const std::vector<Interior *>& interiors, TraceThreadData *td,
                                         const std::function<void()>& cooperate)
{
    for (Interior *interior : interiors)
    {
        if (cooperate)
            cooperate();
        StepPreparation(td->preparationProgress, "refraction");
        if (!interior->refraction)
        {
            interior->preparedRefraction.reset();
            AdvancePreparation(td->preparationProgress);
            continue;
        }
        auto definition = std::make_shared<PreparedRefractionDefinition>();
        const RefractionSpec& spec = *interior->refraction;
        definition->field = PrepareRefraction(spec, *interior, td, cooperate);
        definition->varying = !spec.functions.empty();
        definition->detail = spec.detail > 0.0 ? 0.5 * spec.detail : HUGE_VAL;
        for (const auto& field : interior->fields)
            if (field->layout)
                definition->layouts.push_back(field->layout);
        interior->preparedRefraction = definition;
        AdvancePreparation(td->preparationProgress);
    }
}

void PrepareInteriorFields(ObjectPtr owner, TraceThreadData *td, const std::function<void()>& cooperate)
{
    if (owner == nullptr || owner->interior == nullptr)
        return;
    const auto sharedLayout = PrepareInteriorFieldLayout(owner, td);
    ValidateNativePreparedPhotonSources(owner, td);
    for (Media& medium : owner->interior->media)
        medium.FinalizePreparedField(owner, td, cooperate);
    for (const auto& field : owner->interior->fields)
        if (field->owner == owner)
            field->mediaReady = true;
    if (!UsablePreparedContainer(owner))
    {
        bool prepared = false, varyingLegacy = false;
        for (const Media& medium : owner->interior->media)
        {
            prepared = prepared || medium.Sample_Method == 4;
            varyingLegacy = varyingLegacy || (medium.Sample_Method != 4 && !ConstantDensity(medium));
        }
        if (prepared && varyingLegacy)
            throw POV_EXCEPTION_STRING("Prepared mixtures with varying legacy coefficients require an explicit finite region.");
    }
}

void PrepareAtmosphereFields(TraceThreadData *td, const std::function<void()>& cooperate)
{
    const auto scene = std::const_pointer_cast<SceneData>(td->GetSceneData());
    for (Media& medium : scene->atmosphere)
    {
        medium.FinalizePreparedField(nullptr, td, cooperate);
        AdvancePreparation(td->preparationProgress);
    }
    bool prepared = false, varying = false;
    MathColour extinction, emission;
    for (Media& medium : scene->atmosphere)
    {
        prepared = prepared || medium.Sample_Method == 4;
        varying = varying || !ConstantDensity(medium);
        MathColour density;
        if (ConstantDensity(medium))
        {
            if (medium.Sample_Method == 4)
                medium.PreparedDensityAt(Vector3d(0.0), density);
            else
                Evaluate_Density_Pigment(medium.Density, Vector3d(0.0), density, td);
            extinction += density * medium.Extinction;
            emission += density * medium.Emission;
        }
    }
    if (prepared && varying)
        throw POV_EXCEPTION_STRING("Prepared atmospheric mixtures require constant coefficients or an explicit finite region.");
    if (prepared)
        for (int ch = 0; ch < MathColour::channels; ch++)
            if (extinction[ch] == 0.0 && emission[ch] != 0.0)
                throw POV_EXCEPTION_STRING("Unbounded prepared emission without extinction has a divergent integral.");
}

void PrepareLightFields(TraceThreadData *td, const std::function<void()>& cooperate)
{
    const auto scene = std::const_pointer_cast<SceneData>(td->GetSceneData());
    const std::vector<Interior *> refracting = RefractionInteriors(*scene);
    PreparationProgress *const progress = (refracting.size() + scene->mediaLights.size() > 0) ? td->preparationProgress : nullptr;
    if (progress != nullptr)
        progress->Begin("lights", refracting.size() + scene->mediaLights.size());
    PrepareRefractionDefinitions(refracting, td, cooperate);
    std::vector<ConstObjectPtr> sourceOwners;
    for (const LightSource* light : scene->mediaLights)
        if (light->emitter && !light->emitter->IsSurface() && light->emitterContainer)
            sourceOwners.push_back(light->emitterContainer);
    std::map<ConstObjectPtr, std::shared_ptr<const PreparedSourceDomain>> sourceDomains;
    for (const auto& group : PrepareSourceDomains(sourceOwners))
        for (ConstObjectPtr owner : group.owners)
            sourceDomains.emplace(owner, group.domain);
    for (LightSource *light : scene->mediaLights)
    {
        if (!light->emitter || !light->emitterContainer)
            throw POV_EXCEPTION_STRING("Interior light has no prepared geometry binding.");
        if (light->portal)
            throw POV_EXCEPTION_STRING("Prepared volume light paths through portals are not supported.");
        light->emitter->ResolveGeometry();
    }
    for (size_t index = 0; index < scene->mediaLights.size(); ++index)
    {
        LightSource *light = scene->mediaLights[index];
        if (cooperate)
            cooperate();
        {
            const bool surface = light->emitter->IsSurface();
            StepPreparation(td->preparationProgress, "l" + std::to_string(index + 1) + "/" +
                            std::to_string(scene->mediaLights.size()) + (surface ? " surface" : " volume"));
            std::shared_ptr<const PreparedSourceDomain> layout;
            if (!surface)
                layout = sourceDomains.at(light->emitterContainer);
            if (!surface && !layout)
                throw POV_EXCEPTION_STRING("A varying interior light requires finite prepared coordinates.");
            light->emitter->PrepareAxis(layout, td, cooperate);
            if (!surface && !light->emitter->PreparedAxis())
                throw POV_EXCEPTION_STRING("Interior light did not publish its prepared emission field.");
            if (!surface && td->mediaMessages)
                td->mediaMessages->Info("Prepared interior light grid: %u x %u x %u; source %u%s.",
                    unsigned(layout->Counts()[X]), unsigned(layout->Counts()[Y]), unsigned(layout->Counts()[Z]),
                    unsigned(light->index), light->lightGroupLight ? " in light group" : "");
            light->colour = light->emitter->Intensity() / M_PI;
            light->Center = light->emitter->Centre();
            light->lawRadius = light->emitter->Radius();
        }
        AdvancePreparation(td->preparationProgress);
    }
    // Shared interior fields gain their lights on this thread, in scene order.
    for (LightSource *light : scene->mediaLights)
        if (!light->emitter->IsSurface())
            for (const auto& field : light->emitterContainer->interior->fields)
                if (field->owner == light->emitterContainer)
                {
                    field->lights.push_back(PreparedLightComponent{light, light->emitter->PreparedAxis()});
                    field->lightReady = true;
                }
    if (progress != nullptr)
        progress->End();
}

void Media::PostProcess()
{
    int i;
    DBL t;

    // Get extinction coefficient.
    Extinction = Absorption + sc_ext * Scattering;

    // Determine used effects.

    is_constant = Density.empty();

    use_absorption = !Absorption.IsZero();
    use_emission   = !Emission.IsZero();
    use_scattering = !Scattering.IsZero();
    use_extinction = use_absorption || use_scattering;

    // Init sample threshold array.
    if (Sample_Threshold != nullptr)
        delete[] Sample_Threshold;

    // Create list of thresholds for confidence test.
    Sample_Threshold = new DBL[Max_Samples];

    if(Max_Samples > 1)
    {
        t = chdtri((DBL)(Max_Samples-1), Confidence);

        if(t > 0.0)
            t = Variance / t;
        else
            t = Variance * EPSILON;

        for(i = 0; i < Max_Samples; i++)
            Sample_Threshold[i] = t * chdtri((DBL)(i+1), Confidence);
    }
    else
        Sample_Threshold[0] = 0.0;

    for (vector<PIGMENT*>::iterator i = Density.begin(); i != Density.end(); ++ i)
        Post_Pigment(*i);
}

void Transform_Density(vector<PIGMENT*>& Density, const TRANSFORM *Trans)
{
    for (vector<PIGMENT*>::iterator i = Density.begin(); i != Density.end(); ++ i)
        Transform_Tpattern(*i, Trans);
}

MediaLight::MediaLight(const MediaLight& source) :
    pigment(source.pigment ? Copy_Pigment(source.pigment) : nullptr),
    bouncePasses(source.bouncePasses), useEmission(source.useEmission), brightness(source.brightness), lumens(source.lumens),
    attenuationScale(source.attenuationScale), attenuationPower(source.attenuationPower), cutoff(source.cutoff),
    samples(source.samples),
    shadowless(source.shadowless), mediaInteraction(source.mediaInteraction), mediaAttenuation(source.mediaAttenuation),
    photonReflection(source.photonReflection), photonRefraction(source.photonRefraction),
    photonOptions(source.photonOptions), where(source.where)
{
}

MediaLight::~MediaLight()
{
    if (pigment != nullptr)
        Destroy_Pigment(pigment);
}

namespace
{

class OwningEmissionInput final
{
    public:
        explicit OwningEmissionInput(Interior& interior);
        MathColour Sample(const Vector3d& point, TraceThreadData *td) const;
        bool Constant() const;
    private:
        MediaVector media;
        MediaModifierVector modifiers;
};

/// Lobes an emitter aims draws at: 2^4, enough to follow a thin or forked source.
const int kEmitterLobeDepth = 4;
const size_t kEmitterLobes = size_t(1) << kEmitterLobeDepth;
const std::uint8_t kNoLobe = 0xff;
/// The share of a receiver's position draws that ignore its distance to each lobe.
const double kPowerPositionShare = 0.25;

// The uniform proposal component preserves support of the prepared interpolated light field.
class VolumeEmitter final : public Emitter
{
    public:
        ObjectPtr container = nullptr, geometry = nullptr;
        std::shared_ptr<const MediaLight> sourceSettings;
        std::shared_ptr<const PreparedDirectionalSourceField> bounceField;
        std::shared_ptr<const PreparedLightAxis> authoredAxis;
        std::shared_ptr<const PreparedLightAxis> axis;
        mutable std::mutex prepareMutex;
        Vector3d low, size, centre;
        MathColour coefficient, intensity;
        double radius = 0.0, boxVolume = 0.0;
        bool volumeEmission = true;
        /// the share of each prepared cell inside the geometry, so position draws find thin parts
        std::shared_ptr<const vector<DBL>> occupancy;
        vector<EmitterLobe> lobes;
        vector<size_t> lobeBegin;            ///< each lobe's run in lobeCells, then the end
        vector<std::uint32_t> lobeCells;     ///< emitting cells grouped by lobe
        vector<DBL> lobeCdf;                 ///< cumulative share of each of those cells within its lobe
        vector<std::uint8_t> cellLobe;       ///< each cell's lobe, or kNoLobe
        vector<DBL> cellShare;               ///< each cell's share of its lobe's power

        ~VolumeEmitter() override { Destroy_Object(geometry); }
        const vector<EmitterLobe>& Lobes() const override { return lobes; }

        /// Each lobe's power over its squared distance from `from`, widened by its spread; returns their sum.
        double LobeWeights(const Vector3d& from, double *weights) const
        {
            double total = 0.0;
            for (size_t k = 0; k < lobes.size(); k++)
                total += weights[k] = lobes[k].share / ((lobes[k].centre - from).lengthSqr() + lobes[k].spread2);
            return total;
        }

        /// The density of Toward()'s draws from `from` at `point`.
        DBL TowardPdf(const Vector3d& from, const Vector3d& point) const
        {
            const DBL power = axis->PositionPdf(point);
            if (lobes.empty())
                return power;
            const auto stencil = axis->Domain()->SampleStencil(point);
            DBL nearby = 0.0;
            if (stencil.valid && cellLobe[stencil.cell] != kNoLobe)
            {
                double weights[kEmitterLobes];
                const double total = LobeWeights(from, weights);
                nearby = weights[cellLobe[stencil.cell]] / total * cellShare[stencil.cell] / axis->Domain()->CellVolume(stencil.cell);
            }
            return kPowerPositionShare * power + (1.0 - kPowerPositionShare) * nearby;
        }

        /// A position by power, or mostly by a lobe chosen for its power over its distance from `from`; u in [0,1) stratifies.
        PreparedLightPositionSample Toward(const Vector3d& from, double u, const Vector3d& offset) const
        {
            PreparedLightPositionSample sample;
            if (lobes.empty() || u < kPowerPositionShare)
                sample = axis->SamplePosition(lobes.empty() ? u : u / kPowerPositionShare, offset);
            else
            {
                double weights[kEmitterLobes];
                double v = (u - kPowerPositionShare) / (1.0 - kPowerPositionShare) * LobeWeights(from, weights);
                size_t k = 0;
                while (k + 1 < lobes.size() && v >= weights[k])
                    v -= weights[k++];
                v = min(max(v / weights[k], 0.0), std::nextafter(1.0, 0.0));
                const auto first = lobeCdf.begin() + lobeBegin[k], last = lobeCdf.begin() + lobeBegin[k + 1];
                const size_t i = min(size_t(std::upper_bound(first, last, v) - first), lobeBegin[k + 1] - lobeBegin[k] - 1);
                sample = axis->Domain()->SamplePosition(lobeCells[lobeBegin[k] + i], offset);
            }
            sample.pdf = TowardPdf(from, sample.position);
            return sample;
        }

        void BuildLobes(const PreparedSourceDomain& layout, const vector<DBL>& masses);

        using Parallel = std::function<void(size_t, const std::function<void(size_t, TraceThreadData *, size_t)>&)>;
        /// Occupied length along 2x2 lines per cell row on each axis, as a share of the cell, or its inside corners' share.
        std::shared_ptr<const vector<DBL>> Occupancy(const PreparedSourceDomain& layout, size_t workers,
                                                     const Parallel& parallel) const
        {
            const std::array<size_t, 3>& n = layout.Counts();
            const Vector3d low = layout.Low(), size = layout.Size();
            vector<DBL> axes[3];
            std::vector<vector<std::pair<double, double>>> pieces(workers);
            for (int a = X; a <= Z; a++)
            {
                const int b = (a + 1) % 3, c = (a + 2) % 3;
                const double step = size[a] / n[a];
                axes[a].assign(layout.CellCount(), 0.0);
                Vector3d dir(0.0);
                dir[a] = 1.0;
                parallel(n[b] * n[c], [&](size_t row, TraceThreadData *worker, size_t slot) {
                    const size_t jb = row % n[b], jc = row / n[b];
                    for (int sub = 0; sub < 4; sub++)
                    {
                        Vector3d from = low;
                        from[b] += (jb + 0.25 + 0.5 * (sub & 1)) * size[b] / n[b];
                        from[c] += (jc + 0.25 + 0.5 * (sub >> 1)) * size[c] / n[c];
                        Chord(from, dir, size[a], worker, pieces[slot]);
                        for (const auto& piece : pieces[slot])
                        {
                            const size_t first = size_t(std::max(0.0, std::min(double(n[a] - 1), floor(piece.first / step))));
                            for (size_t i = first; (i < n[a]) && (i * step < piece.second); i++)
                            {
                                const double overlap = std::min(piece.second, (i + 1) * step) - std::max(piece.first, i * step);
                                if (!(overlap > 0.0))
                                    continue;
                                size_t index[3];
                                index[a] = i;
                                index[b] = jb;
                                index[c] = jc;
                                axes[a][(index[Z] * n[Y] + index[Y]) * n[X] + index[X]] += 0.25 * overlap / step;
                            }
                        }
                    }
                });
            }
            // a corner inside keeps cells whose occupied corner the lines pass by
            vector<unsigned char> inside(layout.NodeCount());
            parallel(layout.NodeCount(), [&](size_t node, TraceThreadData *worker, size_t) {
                inside[node] = Inside_Object((layout.Node(node) - centre) / radius, geometry, worker);
            });
            auto result = std::make_shared<vector<DBL>>(layout.CellCount());
            for (size_t cell = 0; cell < result->size(); cell++)
            {
                int corners = 0;
                for (size_t node : layout.CellNodes(cell))
                    corners += inside[node];
                (*result)[cell] = std::min(1.0, std::max(corners / 8.0, (axes[X][cell] + axes[Y][cell] + axes[Z][cell]) / 3.0));
            }
            return result;
        }
        void PrepareAxis(std::shared_ptr<const PreparedSourceDomain> layout, TraceThreadData *td,
                         const std::function<void()>& cooperate) const override;

        struct ScaledColour final : PreparedColourField
        {
            std::shared_ptr<const PreparedColourField> field;
            double scale;
            ScaledColour(std::shared_ptr<const PreparedColourField> field, double scale) : field(std::move(field)), scale(scale) {}
            MathColour Sample(const Vector3d& p) const override { return field->Sample(p) * scale; }
        };

        struct CombinedColour final : PreparedColourField
        {
            std::shared_ptr<const PreparedLightAxis> authored;
            std::shared_ptr<const PreparedDirectionalSourceField> bounced;
            CombinedColour(std::shared_ptr<const PreparedLightAxis> authored, std::shared_ptr<const PreparedDirectionalSourceField> bounced) :
                authored(std::move(authored)), bounced(std::move(bounced)) {}
            MathColour Sample(const Vector3d& p) const override
            { return authored->ColourAt(p) + bounced->AngularAverage(p); }
        };

        std::shared_ptr<const PreparedLightAxis> PreparedAxis() const override { return axis; }
        const MathColour& Intensity() const override { return intensity; }
        const Vector3d& Centre() const override { return centre; }
        double Radius() const override { return radius; }
        double Reach(const Vector3d& from) const override { return BoxReach(from, low, size); }

        bool HasVolumeEmission() const override { return volumeEmission; }

        MathColour EmissionAt(const Vector3d& p, TraceThreadData *td, const Vector3d *outgoing = nullptr) const
        {
            if (!axis)
                throw POV_EXCEPTION_STRING("Interior light axis was not published before tracing.");
            if (!Inside_Object((p - centre) / radius, geometry, td))
                return MathColour();
            if (bounceField)
            {
                MathColour result = outgoing ? bounceField->Evaluate(p, *outgoing) : bounceField->AngularAverage(p);
                if (authoredAxis)
                    result += authoredAxis->ColourAt(p);
                return result;
            }
            return axis->ColourAt(p);
        }

        MathColour PhotonEmissionWeight(const EmitterSample& sample, const Vector3d& outgoing) const override
        {
            if (!bounceField)
                return Emitter::PhotonEmissionWeight(sample, outgoing);
            if (!(sample.pdf > 0.0) || !(sample.weight.Weight() > 0.0))
                return MathColour();
            MathColour result = bounceField->Evaluate(sample.position, outgoing);
            if (authoredAxis)
                result += authoredAxis->ColourAt(sample.position);
            return result / sample.pdf;
        }

        double PositionPdf(const Vector3d& p) const
        {
            if (!axis)
                throw POV_EXCEPTION_STRING("Interior light axis was not published before tracing.");
            return axis->PositionPdf(p);
        }

        EmitterSample Sample(double s, std::uint64_t key, TraceThreadData *td = nullptr) const override
        {
            return SamplePosition(EmitterDraw(s, Draw(key, kDrawEmitter, 0), Draw(key, kDrawEmitter, 1), Draw(key, kDrawEmitter, 2), DeriveKey(key, kDrawEmitter, UINT64_C(0x706f736974696f6e))), td);
        }

        EmitterSample SamplePosition(const EmitterDraw& draw, TraceThreadData *td) const
        {
            if (!axis)
                throw POV_EXCEPTION_STRING("Interior light axis was not published before tracing.");
            const auto position = axis->SamplePosition(draw.choose, Vector3d(draw.x, draw.y, draw.z));
            EmitterSample sample;
            sample.position = position.position;
            sample.pdf = position.pdf;
            sample.weight = EmissionAt(sample.position, td) / sample.pdf;
            return sample;
        }

        void CloseOccupancyBoundaries(ObjectPtr object) const
        {
            Set_Flag(object, CLOSED_FLAG);
            if (object->Type & IS_COMPOUND_OBJECT)
                for (ObjectPtr child : static_cast<CompoundObject *>(object)->children)
                    CloseOccupancyBoundaries(child);
            for (ObjectPtr clip : object->Clip)
                CloseOccupancyBoundaries(clip);
        }

        void Boundaries(ObjectPtr object, const Ray& ray, double start, double end, TraceThreadData *td,
                        vector<double>& cuts) const
        {
            if (object->Type & IS_COMPOUND_OBJECT)
            {
                for (ObjectPtr child : static_cast<CompoundObject *>(object)->children)
                    Boundaries(child, ray, start, end, td, cuts);
            }
            else
            {
                IStack stack(td->stackPool);
                if (object->All_Intersections(ray, stack, td))
                    for (; stack->size() > 0; stack->pop())
                    {
                        const double depth = stack->top().Depth;
                        if ((depth > start) && (depth < end))
                            cuts.push_back(depth);
                    }
            }
            for (ObjectPtr clip : object->Clip)
                Boundaries(clip, ray, start, end, td, cuts);
        }

        double Chord(const Vector3d& from, const Vector3d& dir, double reach, TraceThreadData *td,
                     vector<std::pair<double, double>>& pieces) const
        {
            const Vector3d relative = (from - centre) / radius;
            const double offset = -dot(relative, dir) - 2.0;
            const Vector3d origin = cross(dir, cross(relative, dir)) - dir * 2.0;
            const double start = -offset, end = start + reach / radius;
            TraceTicket ticket(1, 0.0);
            Ray ray(ticket, origin, dir);
            thread_local vector<double> cuts;
            cuts.assign(1, start);
            Boundaries(geometry, ray, start, end, td, cuts);
            cuts.push_back(end);
            std::sort(cuts.begin(), cuts.end());
            pieces.clear();
            double length = 0.0;
            bool previousInside = false;
            for (size_t i = 1; i < cuts.size(); i++)
            {
                const double a = cuts[i - 1], b = cuts[i];
                if (!(b > a))
                    continue;
                const bool inside = Inside_Object(origin + dir * (a + 0.5 * (b - a)), geometry, td);
                if (inside)
                {
                    const double low = (a + offset) * radius, high = (b + offset) * radius;
                    if (previousInside)
                        pieces.back().second = high;
                    else
                        pieces.emplace_back(low, high);
                    length += (b - a) * radius;
                }
                previousInside = inside;
            }
            return length;
        }

        bool SampleToward(const Vector3d& from, const EmitterDraw& draw, TraceThreadData *td,
                          EmitterSample& sample) const override
        {
            if (!axis)
                throw POV_EXCEPTION_STRING("Interior light axis was not published before tracing.");
            const double s = draw.choose, t = draw.x;
            const Vector3d toCentre = centre - from;
            const double d = toCentre.length();
            const double chordShare = 0.5 * min(1.0, max(0.0, 2.0 - 0.5 * d / radius));
            if (chordShare == 0.0)
            {
                const auto position = Toward(from, draw.choose, Vector3d(draw.x, draw.y, draw.z));
                if (!bounceField)
                {
                    sample.position = position.position;
                    sample.pdf = position.pdf;
                    const double r2 = (sample.position - from).lengthSqr();
                    if (r2 > 0.0)
                        sample.weight = EmissionAt(sample.position, td) / (r2 * sample.pdf);
                    else
                        sample.weight.Clear();
                    return true;
                }
                sample.position = position.position;
                sample.pdf = position.pdf;
                const double r2 = (sample.position - from).lengthSqr();
                if (r2 > 0.0)
                {
                    const Vector3d outgoing = (from - sample.position) / std::sqrt(r2);
                    sample.weight = EmissionAt(sample.position, td, &outgoing) / (r2 * sample.pdf);
                }
                else
                    sample.weight.Clear();
                return true;
            }
            // A receiver's draws share their proposal over the emitter's box, built once per receiver point and box.
            thread_local struct { Vector3d key[3]; EmitterAabbProposal proposal; } recent =
                { { Vector3d(std::numeric_limits<double>::quiet_NaN()), Vector3d(), Vector3d() },
                  EmitterAabbProposal(Vector3d(), Vector3d(-1.0), Vector3d(2.0)) };
            const Vector3d key[3] = { from, low, size };
            bool same = true;
            for (int k = 0; k < 3; k++)
                same = same && (recent.key[k][X] == key[k][X]) && (recent.key[k][Y] == key[k][Y]) && (recent.key[k][Z] == key[k][Z]);
            if (!same)
                recent = { { from, low, size }, EmitterAabbProposal(from, low, size) };
            const EmitterAabbProposal& angular = recent.proposal;
            thread_local vector<std::pair<double, double>> pieces;
            double length, depth, positionPdf = 0.0;
            bool positionKnown = false;
            if (s < chordShare)
            {
                const Vector3d dir = angular.Sample(s / chordShare, t);
                length = Chord(from, dir, d + radius, td, pieces);
                sample.position = centre;
                sample.pdf = 0.0;
                sample.weight = MathColour();
                if (!(length > 0.0))
                    return true;
                double at = draw.y * length;
                depth = pieces.back().second;
                for (const auto& piece : pieces)
                {
                    const double span = piece.second - piece.first;
                    if (at <= span)
                    {
                        depth = piece.first + at;
                        break;
                    }
                    at -= span;
                }
                sample.position = from + dir * depth;
            }
            else
            {
                const auto position = Toward(from, (s - chordShare) / (1.0 - chordShare), Vector3d(draw.x, draw.y, draw.z));
                sample.position = position.position;
                sample.pdf = positionPdf = position.pdf;
                positionKnown = true;
                const Vector3d delta = sample.position - from;
                depth = delta.length();
                if (!(depth > 0.0))
                {
                    sample.weight.Clear();
                    return true;
                }
                length = Chord(from, delta / depth, d + radius, td, pieces);
            }
            if (!(depth > 0.0))
            {
                sample.weight.Clear();
                sample.pdf = 0.0;
                return true;
            }
            bool inChord = false;
            for (const auto& piece : pieces)
                inChord = inChord || ((depth >= piece.first) && (depth <= piece.second));
            const double r2 = depth * depth;
            const double angularPdf = angular.Pdf((sample.position - from) / depth);
            const double chordPdf = (inChord && (length > 0.0)) ? angularPdf / (length * r2) : 0.0;
            sample.pdf = chordShare * chordPdf + (1.0 - chordShare) * (positionKnown ? positionPdf : TowardPdf(from, sample.position));
            const Vector3d outgoing = (from - sample.position) / depth;
            sample.weight = EmissionAt(sample.position, td, &outgoing) / (r2 * sample.pdf);
            return true;
        }

};

}

namespace
{

struct LobePoint final
{
    Vector3d position;
    DBL mass, spread2;
    std::uint32_t cell;
};

/// Splits power-weighted points at the weighted median of their longest extent until `depth` runs out.
void SplitLobes(vector<LobePoint>& points, size_t begin, size_t end, int depth, DBL total, vector<EmitterLobe>& lobes,
                vector<std::pair<size_t, size_t>>& ranges)
{
    DBL mass = 0.0;
    Vector3d centre, low(HUGE_VAL), high(-HUGE_VAL);
    for (size_t i = begin; i < end; i++)
    {
        mass += points[i].mass;
        centre += points[i].position * points[i].mass;
        for (int a = 0; a < 3; a++)
        {
            low[a] = std::min(low[a], points[i].position[a]);
            high[a] = std::max(high[a], points[i].position[a]);
        }
    }
    if (!(mass > 0.0))
        return;
    centre /= mass;
    if (depth == 0 || end - begin < 2)
    {
        DBL spread2 = 0.0;
        for (size_t i = begin; i < end; i++)
            spread2 += points[i].mass * ((points[i].position - centre).lengthSqr() + points[i].spread2);
        lobes.push_back(EmitterLobe{centre, spread2 / mass, mass / total});
        ranges.emplace_back(begin, end);
        return;
    }
    const Vector3d extent = high - low;
    const int axis = extent[X] >= extent[Y] ? (extent[X] >= extent[Z] ? X : Z) : (extent[Y] >= extent[Z] ? Y : Z);
    std::stable_sort(points.begin() + begin, points.begin() + end, [axis](const LobePoint& a, const LobePoint& b) {
        return a.position[axis] < b.position[axis]; });
    size_t middle = begin + 1;
    for (DBL running = points[begin].mass; middle + 1 < end && running + points[middle].mass <= 0.5 * mass; middle++)
        running += points[middle].mass;
    SplitLobes(points, begin, middle, depth - 1, total, lobes, ranges);
    SplitLobes(points, middle, end, depth - 1, total, lobes, ranges);
}

}

void VolumeEmitter::BuildLobes(const PreparedSourceDomain& layout, const vector<DBL>& masses)
{
    vector<LobePoint> points;
    DBL total = 0.0;
    for (size_t cell = 0; cell < masses.size(); cell++)
        if (masses[cell] > 0.0)
        {
            const auto corners = layout.CornerQuadrature(cell);
            Vector3d middle;
            for (const auto& q : corners)
                middle += q.position / DBL(corners.size());
            DBL spread2 = 0.0;
            for (const auto& q : corners)
                spread2 += (q.position - middle).lengthSqr() / DBL(corners.size());
            points.push_back(LobePoint{middle, masses[cell], spread2, std::uint32_t(cell)});
            total += masses[cell];
        }
    vector<std::pair<size_t, size_t>> ranges;
    if (total > 0.0)
        SplitLobes(points, 0, points.size(), kEmitterLobeDepth, total, lobes, ranges);
    cellLobe.assign(masses.size(), kNoLobe);
    cellShare.assign(masses.size(), 0.0);
    for (size_t k = 0; k < ranges.size(); k++)
    {
        lobeBegin.push_back(lobeCells.size());
        DBL lobeMass = 0.0, running = 0.0;
        for (size_t i = ranges[k].first; i < ranges[k].second; i++)
            lobeMass += points[i].mass;
        for (size_t i = ranges[k].first; i < ranges[k].second; i++)
        {
            running += points[i].mass;
            lobeCells.push_back(points[i].cell);
            lobeCdf.push_back(running / lobeMass);
            cellLobe[points[i].cell] = std::uint8_t(k);
            cellShare[points[i].cell] = points[i].mass / lobeMass;
        }
        lobeCdf.back() = 1.0;
    }
    lobeBegin.push_back(lobeCells.size());
}

// The corner rule's integral of f over a prepared cell.
template<typename Function>
static DBL CellIntegral(const PreparedSourceDomain& layout, size_t cell, const Function& f)
{
    DBL sum = 0.0;
    for (const auto& q : layout.CornerQuadrature(cell))
        sum += f(q.position) * q.weight;
    return sum;
}

void VolumeEmitter::PrepareAxis(std::shared_ptr<const PreparedSourceDomain> layout, TraceThreadData *td,
                                const std::function<void()>& cooperate) const
{
    std::lock_guard<std::mutex> lock(prepareMutex);
    if (axis)
        return;
    if (!layout)
        throw POV_EXCEPTION_STRING("Interior light requires its prepared field layout.");
    VolumeEmitter *emitter = const_cast<VolumeEmitter*>(this);
    const MediaLight& settings = *sourceSettings;
    if (settings.pigment && PigmentRequiresRayContext(settings.pigment))
        throw POV_EXCEPTION_STRING("Interior light requires ray context and cannot be published.");
    if (!UsablePreparedContainer(container))
    {
        throw POV_EXCEPTION_STRING("Interior light requires a finite container.");
    }
    Vector3d high;
    Make_min_max_from_BBox(emitter->low, high, container->BBox);
    emitter->size = high - emitter->low;
    emitter->boxVolume = emitter->size[X] * emitter->size[Y] * emitter->size[Z];
    if (!(emitter->boxVolume > 0.0) || !IsFiniteNumber(emitter->boxVolume))
    {
        throw POV_EXCEPTION_STRING("Interior light has no finite volume bounds.");
    }
    emitter->centre = emitter->low + emitter->size * 0.5;
    emitter->radius = emitter->size.length() * 0.5;
    Destroy_Object(emitter->geometry);
    emitter->geometry = nullptr;
    emitter->geometry = Copy_Object(container);
    emitter->CloseOccupancyBoundaries(emitter->geometry);
    TRANSFORM translation, scaling;
    const Vector3d displacement = -emitter->centre, scale(1.0 / emitter->radius);
    Compute_Translation_Transform(&translation, displacement);
    Translate_Object(emitter->geometry, displacement, &translation);
    Compute_Scaling_Transform(&scaling, scale);
    Scale_Object(emitter->geometry, scale, &scaling);
    emitter->coefficient = MathColour(settings.lumens < 0.0 && settings.brightness == 0.0 ? 0.0 : 1.0);
    // every serial estimate below is gathered per index on the workers, then summed in index order
    const size_t workers = std::max<size_t>(1, td->GetSceneData()->workThreads);
    std::vector<std::unique_ptr<TraceThreadData>> helpers;
    for (size_t i = 1; i < workers; i++)
        helpers.emplace_back(new TraceThreadData(std::const_pointer_cast<SceneData>(td->GetSceneData()), i, false));
    const auto poll = [&cooperate]() { if (cooperate) cooperate(); };
    const auto thread = [&](size_t worker) { return worker ? helpers[worker - 1].get() : td; };
    const auto parallel = [&](size_t count, const std::function<void(size_t, TraceThreadData *, size_t)>& work) {
        ParallelFor(count, workers, [&](size_t i, size_t worker) {
            if ((workers == 1) && (i % 1024 == 0))
                poll();
            work(i, thread(worker), worker);
        }, poll);
    };
    if (settings.lumens < 0.0 && settings.brightness != 0.0)
    {
        double meanChord = 0.0, discrepancy = HUGE_VAL;
        std::vector<vector<std::pair<double, double>>> pieces(workers);
        std::vector<double> lineLength;
        std::vector<size_t> lineCrossings;
        for (int lines : {1024, 4096, 16384})
        {
            double replicate[4], sumLength = 0.0;
            size_t sumPieces = 0;
            lineLength.assign(size_t(4) * lines, 0.0);
            lineCrossings.assign(size_t(4) * lines, 0);
            parallel(size_t(4) * lines, [&](size_t index, TraceThreadData *worker, size_t slot) {
                const int r = int(index / lines), k = int(index % lines);
                const std::uint64_t scramble = DeriveKey(0x63686f726473ull, kDrawEmitter, r);
                const double a = Draw(scramble, kDrawEmitter, 0), b = Draw(scramble, kDrawEmitter, 1);
                const double c = Draw(scramble, kDrawEmitter, 2);
                {
                    const std::uint64_t key = DeriveKey(scramble, kDrawEmitter, k);
                    const double su = a + k * 0.7548776662466927, sv = b + k * 0.5698402909980532;
                    const double z = 1.0 - 2.0 * (su - floor(su)), azimuth = 2.0 * M_PI * (sv - floor(sv));
                    const double radial = std::sqrt(max(0.0, 1.0 - z * z));
                    const Vector3d w(radial * std::cos(azimuth), radial * std::sin(azimuth), z);
                    const Vector3d u = ((fabs(w[X]) > 0.5) ? cross(Vector3d(0.0, 1.0, 0.0), w) : cross(Vector3d(1.0, 0.0, 0.0), w)).normalized();
                    const double t = c + k * 0.4142135623730950, phi = 2.0 * M_PI * (t - floor(t));
                    const double rho = emitter->radius * std::sqrt((k + Draw(key, kDrawEmitter, 3)) / lines);
                    const Vector3d origin = emitter->centre + (u * std::cos(phi) + cross(w, u) * std::sin(phi)) * rho - w * emitter->radius;
                    lineLength[index] = emitter->Chord(origin, w, 2.0 * emitter->radius, worker, pieces[slot]);
                    lineCrossings[index] = pieces[slot].size();
                }
            });
            for (int r = 0; r < 4; r++)
            {
                double length = 0.0;
                size_t crossings = 0;
                for (int k = 0; k < lines; k++)
                {
                    length += lineLength[size_t(r) * lines + k];
                    crossings += lineCrossings[size_t(r) * lines + k];
                }
                replicate[r] = crossings ? length / crossings : 0.0;
                sumLength += length;
                sumPieces += crossings;
            }
            meanChord = sumPieces ? sumLength / sumPieces : 0.0;
            discrepancy = 0.0;
            for (double estimate : replicate)
                discrepancy = max(discrepancy, fabs(estimate - meanChord) / max(meanChord, 1.0e-300));
            if ((meanChord > 0.0) && (discrepancy <= 0.005))
                break;
        }
        if (!(meanChord > 0.0))
        {
            throw POV_EXCEPTION_STRING("Interior light geometric mean-chord normalization is unresolved or empty.");
        }
        if ((discrepancy > 0.005) && td->mediaMessages)
            td->mediaMessages->Warning(kWarningGeneral, "Interior light's geometric mean-chord normalization has %.3g relative sampling discrepancy.", discrepancy);
        emitter->coefficient = MathColour(settings.brightness / meanChord);
    }
    const size_t count = layout->NodeCount();
    std::unique_ptr<OwningEmissionInput> emissionInput;
    if (settings.useEmission)
        emissionInput.reset(new OwningEmissionInput(*container->interior));
    const bool solid = emissionInput ? emissionInput->Constant() :
        (!settings.pigment || settings.pigment->Type == PLAIN_PATTERN);
    auto colourAt = [&](const Vector3d& p, TraceThreadData *worker) {
        TransColour colour(MathColour(1.0));
        if (settings.pigment)
            Compute_Pigment(colour, settings.pigment, p, nullptr, nullptr, worker);
        const MathColour value = emissionInput ? emissionInput->Sample(p, worker) : colour.colour();
        for (int ch = 0; ch < MathColour::channels; ch++)
            if (!IsFiniteNumber(value[ch]) || value[ch] < 0.0)
                throw POV_EXCEPTION_STRING("Interior light pigment is negative or not finite.");
        return value;
    };
    std::shared_ptr<const PreparedColourField> field;
    std::atomic<bool> positiveField(false);
    if (solid)
    {
        const MathColour value = colourAt(emitter->centre, td);
        positiveField.store(value.Weight() > 0.0, std::memory_order_relaxed);
        field = std::make_shared<PreparedConstantColourField>(value);
    }
    else
    {
        vector<MathColour> values(count);
        parallel(count, [&](size_t node, TraceThreadData *worker, size_t) {
            const MathColour value = colourAt(layout->Node(node), worker);
            values[node] = value;
            if (value.Weight() > 0.0)
                positiveField.store(true, std::memory_order_relaxed);
        });
        field = std::make_shared<PreparedSourceColourField>(layout, std::move(values));
    }
    auto preparedEmission = [&](const Vector3d& p, TraceThreadData *worker) {
        return Inside_Object((p - emitter->centre) / emitter->radius, emitter->geometry, worker) ?
            field->Sample(p) * emitter->coefficient : MathColour();
    };
    MathColour integral;
    double relativeDiscrepancy = 0.0;
    {
        for (int bins : {16, 24, 32})
        {
            if (!positiveField.load(std::memory_order_relaxed) || !(emitter->coefficient.Weight() > 0.0))
                break;
            MathColour replicate[4];
            const size_t draws = size_t(bins) * bins * bins;
            vector<MathColour> drawn(4 * draws);
            parallel(4 * draws, [&](size_t index, TraceThreadData *worker, size_t) {
                const size_t r = index / draws, k = index % draws;
                {
                    const std::uint64_t key = DeriveKey(0x766f6c756d65ull, kDrawEmitter, size_t(r) * draws + k);
                    const Vector3d q((k % bins + Draw(key, kDrawEmitter, 0)) / bins,
                                     ((k / bins) % bins + Draw(key, kDrawEmitter, 1)) / bins,
                                     (k / (size_t(bins) * bins) + Draw(key, kDrawEmitter, 2)) / bins);
                    const Vector3d p = emitter->low + Vector3d(q[X] * emitter->size[X], q[Y] * emitter->size[Y], q[Z] * emitter->size[Z]);
                    drawn[index] = preparedEmission(p, worker);
                }
            });
            for (int r = 0; r < 4; r++)
                for (size_t k = 0; k < draws; k++)
                {
                    const MathColour& j = drawn[r * draws + k];
                    if (!IsFiniteNumber(j.Weight()) || (j.Min() < 0.0))
                    {
                        throw POV_EXCEPTION_STRING("Interior light prepared pigment is negative or not finite.");
                    }
                    replicate[r] += j * (emitter->boxVolume / draws);
                }
            integral.Clear();
            for (const MathColour& estimate : replicate)
                integral += estimate * 0.25;
            const double mean = integral.Greyscale();
            relativeDiscrepancy = 0.0;
            for (const MathColour& estimate : replicate)
                relativeDiscrepancy = max(relativeDiscrepancy, fabs(estimate.Greyscale() - mean) / max(mean, 1.0e-300));
            if ((mean > 0.0) && (relativeDiscrepancy <= 0.0025))
                break;
        }
        if ((settings.lumens > 0.0) && !(integral.Greyscale() > 0.0))
            throw POV_EXCEPTION_STRING("Interior light generated-lumens integral is unresolved or zero.");
        if ((settings.lumens < 0.0) && emitter->coefficient.Weight() > 0.0 &&
            positiveField.load(std::memory_order_relaxed) && !(integral.Greyscale() > 0.0))
            throw POV_EXCEPTION_STRING("Interior light prepared power integral is unresolved; refine its field layout.");
        if ((relativeDiscrepancy > 0.0025) && td->mediaMessages)
            td->mediaMessages->Warning(kWarningGeneral, "Interior light's generated-lumens normalization has %.3g relative sampling discrepancy.", relativeDiscrepancy);
        const double scale = settings.lumens >= 0.0 ?
            (settings.lumens == 0.0 ? 0.0 : settings.lumens / (4.0 * M_PI * integral.Greyscale())) : 1.0;
        emitter->coefficient *= scale;
        emitter->intensity = integral * scale;
    }
    emitter->volumeEmission = positiveField.load(std::memory_order_relaxed) && emitter->coefficient.Weight() > 0.0;
    field = std::make_shared<ScaledColour>(field, emitter->coefficient[0]);
    vector<DBL> masses;
    if (emitter->volumeEmission)
    {
        emitter->occupancy = emitter->Occupancy(*layout, workers, parallel);
        masses.resize(layout->CellCount());
        parallel(layout->CellCount(), [&](size_t cell, TraceThreadData *, size_t) {
            masses[cell] = emitter->occupancy->at(cell) * CellIntegral(*layout, cell, [&](const Vector3d& p) {
                return field->Sample(p).Weight(); });
        });
    }
    for (const auto& helper : helpers)
        td->Stats() += helper->Stats();
    emitter->BuildLobes(*layout, masses);
    emitter->axis = std::make_shared<PreparedLightAxis>(std::move(layout), std::move(field), std::move(masses));
}

std::shared_ptr<const Emitter> MakeBouncedInteriorEmitter(const Emitter& primary,
    std::shared_ptr<const PreparedDirectionalSourceField> field, TraceThreadData *td, bool includePrimary,
    std::shared_ptr<const PreparedSourceDistribution> sharedProposal)
{
    const VolumeEmitter *original = dynamic_cast<const VolumeEmitter*>(&primary);
    if (!original || !original->axis || !original->geometry || !field || (includePrimary && original->bounceField))
        throw POV_EXCEPTION_STRING("Bounced interior light requires a prepared volume emitter and field.");
    std::shared_ptr<VolumeEmitter> emitter(new VolumeEmitter());
    emitter->container = original->container;
    emitter->geometry = Copy_Object(original->geometry);
    emitter->sourceSettings = original->sourceSettings;
    emitter->bounceField = field;
    emitter->volumeEmission = includePrimary && original->HasVolumeEmission();
    std::shared_ptr<const PreparedColourField> proposal = field;
    if (includePrimary)
    {
        emitter->authoredAxis = original->axis;
        emitter->intensity = original->intensity;
        proposal = std::make_shared<VolumeEmitter::CombinedColour>(original->axis, field);
    }
    emitter->low = original->low;
    emitter->size = original->size;
    emitter->centre = original->centre;
    emitter->radius = original->radius;
    emitter->boxVolume = original->boxVolume;
    emitter->coefficient = MathColour(1.0);
    const auto layout = field->Domain();
    if (sharedProposal && sharedProposal->Domain() != layout)
        throw POV_EXCEPTION_STRING("Bounced interior light proposal must match its source domain.");
    vector<DBL> masses;
    if (!sharedProposal)
        masses.resize(layout->CellCount());
    for (size_t node = 0; node < layout->NodeCount(); ++node)
        emitter->volumeEmission = emitter->volumeEmission || proposal->Sample(layout->Node(node)).Weight() > 0.0;
    emitter->occupancy = (original->occupancy && (original->axis->Domain() == layout)) ? original->occupancy :
        emitter->Occupancy(*layout, 1, [&](size_t count, const std::function<void(size_t, TraceThreadData *, size_t)>& work) {
            for (size_t i = 0; i < count; ++i)
                work(i, td, 0);
        });
    for (size_t cell = 0; cell < layout->CellCount(); ++cell)
    {
        const DBL occupied = emitter->occupancy->at(cell);
        if (!(occupied > 0.0))
            continue;
        if (!sharedProposal)
            masses[cell] = occupied * CellIntegral(*layout, cell, [&](const Vector3d& p) { return proposal->Sample(p).Weight(); });
        for (const auto& q : layout->CornerQuadrature(cell))
            emitter->intensity += field->AngularAverage(q.position) * (q.weight * occupied);
    }
    emitter->axis = sharedProposal ? std::make_shared<PreparedLightAxis>(layout, proposal, std::move(sharedProposal)) :
        std::make_shared<PreparedLightAxis>(layout, proposal, std::move(masses));
    return emitter;
}

std::shared_ptr<const Emitter> MakeInteriorEmitter(const Interior& interior, ObjectPtr container, TraceThreadData*, std::string&)
{
    std::shared_ptr<VolumeEmitter> emitter(new VolumeEmitter());
    emitter->container = container;
    emitter->sourceSettings = interior.light;
    Vector3d high;
    Make_min_max_from_BBox(emitter->low, high, container->BBox);
    emitter->size = high - emitter->low;
    emitter->centre = emitter->low + emitter->size * 0.5;
    emitter->radius = emitter->size.length() * 0.5;
    emitter->intensity = MathColour(1.0);
    return emitter;
}

namespace
{

/// One medium on a ray, or an interior without media that clears the media beneath it (medium nullptr).
struct Layer final
{
    int priority;
    size_t order;
    Interior *interior;
    Media *medium;
};

/// Whether a's mix acts on b's media where both hold a point: priority, then placement, then order in the interior.
bool Outranks(const Layer& a, const Layer& b)
{
    if (a.priority != b.priority)
        return a.priority > b.priority;
    if (a.interior->precedence != b.interior->precedence)
        return a.interior->precedence > b.interior->precedence;
    return a.order > b.order;
}

void SubtractFromSum(MathColour *values, size_t count, const MathColour& amount)
{
    MathColour sum;
    for (size_t i = 0; i < count; i++)
        sum += values[i];
    MathColour scale;
    for (int ch = 0; ch < MathColour::channels; ch++)
        scale[ch] = (sum[ch] > amount[ch]) ? (sum[ch] - amount[ch]) / sum[ch] : 0.0;
    for (size_t i = 0; i < count; i++)
        values[i] *= scale;
}

void ApplyCoefficientModifier(MathColour *values, size_t count, const MathColour& density,
                              const MathColour& coefficient, int mix)
{
    if (mix == kMediaBlendMultiply)
        for (size_t i = 0; i < count; i++)
            values[i] *= density;
    else
        SubtractFromSum(values, count, density * coefficient);
}

}

void CollectInteriorMedia(const RayInteriorVector& interiors, MediaVector& medias, MediaModifierVector *modifiers)
{
    if (modifiers == nullptr)
    {
        for (size_t i = 0; i < interiors.size(); i++)
            for (Media& medium : interiors[i]->media)
                medias.push_back(&medium);
        return;
    }
    std::vector<Layer> layers;
    for (Interior *interior : interiors)
    {
        if (interior->clears)
            layers.push_back(Layer{ 0, 0, interior, nullptr });
        for (size_t i = 0; i < interior->media.size(); i++)
            layers.push_back(Layer{ interior->media[i].priority, i, interior, &interior->media[i] });
    }
    std::stable_sort(layers.begin(), layers.end(), [](const Layer& a, const Layer& b) { return Outranks(b, a); });
    size_t first = 0;
    for (size_t i = 0; i < layers.size(); i++)
        if ((layers[i].medium == nullptr) || (layers[i].medium->mix == kMediaBlendReplace))
            first = i;
    for (size_t i = first; i < layers.size(); i++)
    {
        Media *medium = layers[i].medium;
        if (medium == nullptr)
            continue;
        if ((medium->mix == kMediaBlendSubtract) || (medium->mix == kMediaBlendMultiply))
        {
            if (!medias.empty())
                modifiers->push_back(MediaModifier{ medium, layers[i].interior, medias.size() });
        }
        else if (medium->use_absorption || medium->use_emission || medium->use_scattering)
            medias.push_back(medium);
    }
}

std::vector<BounceScatteringComponent> EvaluateOwnerBounceScattering(
    const RayInteriorVector& interiors, const Interior& owner, const Vector3d& point, TraceThreadData *td)
{
    MediaVector media;
    MediaModifierVector modifiers;
    CollectInteriorMedia(interiors, media, &modifiers);
    const auto densityAt = [&](Media& medium) {
        MathColour density;
        if (medium.Sample_Method == 4)
        {
            td->Stats()[Media_Prepared4_Queries]++;
            medium.PreparedDensityAt(point, density);
        }
        else
        {
            if (medium.Sample_Method == 3)
                td->Stats()[Media_Legacy3_Queries]++;
            Evaluate_Density_Pigment(medium.Density, point, density, td);
        }
        return density;
    };
    std::vector<MathColour> scattered(media.size());
    for (size_t i = 0; i < media.size(); ++i)
        scattered[i] = densityAt(*media[i]) * media[i]->Scattering;
    for (const MediaModifier& modifier : modifiers)
    {
        Media& medium = *modifier.medium;
        if (medium.mix == kMediaBlendMultiply || medium.use_scattering)
            ApplyCoefficientModifier(scattered.data(), modifier.below, densityAt(medium), medium.Scattering, medium.mix);
    }
    std::vector<BounceScatteringComponent> result;
    for (size_t i = 0; i < media.size(); ++i)
        for (const Media& owned : owner.media)
            if (media[i] == &owned)
            {
                if (!scattered[i].IsZero())
                    result.push_back(BounceScatteringComponent{owned.Type, owned.Eccentricity, scattered[i]});
                break;
            }
    return result;
}

namespace
{

OwningEmissionInput::OwningEmissionInput(Interior& interior)
{
    RayInteriorVector interiors;
    interiors.push_back(&interior);
    CollectInteriorMedia(interiors, media, &modifiers);
    auto validate = [](const Media& medium) {
        for (const PIGMENT *density : medium.Density)
            if (PigmentRequiresRayContext(density))
                throw POV_EXCEPTION_STRING("Interior light emission density requires ray context and cannot be published.");
    };
    for (const Media *medium : media)
        if (medium->use_emission)
            validate(*medium);
    for (const MediaModifier& modifier : modifiers)
        if (modifier.medium->mix == kMediaBlendMultiply || modifier.medium->use_emission)
            validate(*modifier.medium);
}

bool OwningEmissionInput::Constant() const
{
    for (const Media *medium : media)
        if (medium->use_emission && !ConstantDensity(*medium))
            return false;
    for (const MediaModifier& modifier : modifiers)
        if ((modifier.medium->mix == kMediaBlendMultiply || modifier.medium->use_emission) &&
            !ConstantDensity(*modifier.medium))
            return false;
    return true;
}

MathColour OwningEmissionInput::Sample(const Vector3d& point, TraceThreadData *td) const
{
    std::vector<MathColour> emitted(media.size());
    for (size_t i = 0; i < media.size(); i++)
    {
        if (!media[i]->use_emission)
            continue;
        MathColour density;
        Evaluate_Density_Pigment(media[i]->Density, point, density, td);
        emitted[i] = density * media[i]->Emission;
    }
    for (const MediaModifier& modifier : modifiers)
    {
        if (modifier.medium->mix != kMediaBlendMultiply && !modifier.medium->use_emission)
            continue;
        MathColour density;
        Evaluate_Density_Pigment(modifier.medium->Density, point, density, td);
        ApplyCoefficientModifier(emitted.data(), modifier.below, density, modifier.medium->Emission, modifier.medium->mix);
    }
    MathColour result;
    for (const MathColour& value : emitted)
        result += value;
    return result;
}

}

static bool ConstantDensity(const Media& medium)
{
    for (const PIGMENT *pigment : medium.Density)
        if (pigment->Type != PLAIN_PATTERN)
            return false;
    return true;
}

static DBL ChannelMean(const MathColour& colour)
{
    DBL sum = 0.0;
    for (int ch = 0; ch < MathColour::channels; ch++)
        sum += colour[ch];
    return sum / MathColour::channels;
}

RefractionField::RefractionField(const RayInteriorVector& interiors) :
    maxStep(HUGE_VAL), varies(false), bounded(true)
{
    std::vector<std::shared_ptr<const PreparedScalarField>> payloads;
    for (const Interior *interior : interiors)
    {
        if (!interior->preparedRefraction)
        {
            if (interior->refracting)
                throw POV_EXCEPTION_STRING("Refraction axis was queried before publication.");
            continue;
        }
        const PreparedRefractionDefinition& field = *interior->preparedRefraction;
        payloads.push_back(field.field);
        fields.push_back(&field);
        if (!field.varying)
            continue;
        varies = true;
        if (field.layouts.empty())
            bounded = false;
        for (const auto& layout : field.layouts)
        {
            const DBL extent = std::max(layout->size[X], std::max(layout->size[Y], layout->size[Z]));
            const DBL cell = std::max(layout->size[X] / layout->counts[X],
                std::max(layout->size[Y] / layout->counts[Y], layout->size[Z] / layout->counts[Z]));
            maxStep = std::min(maxStep, std::min(field.detail < HUGE_VAL ? field.detail : extent / 64.0, 2.0 * cell));
        }
    }
    axis.reset(new PreparedRefractionAxis(std::move(payloads)));
    if (!(maxStep < HUGE_VAL))
        maxStep = 1.0;
}

DBL RefractionField::Offset(const Vector3d& point)
{
    return axis->Sample(point).value;
}

DBL RefractionField::Offset(const Vector3d& point, Vector3d& gradient)
{
    const PreparedScalarSample sample = axis->Sample(point);
    gradient = sample.gradient;
    return sample.value;
}

DBL RefractionField::MaxStep(const Vector3d& point) const
{
    DBL limit = HUGE_VAL;
    for (const PreparedRefractionDefinition *field : fields)
    {
        if (!field->varying)
            continue;
        for (const auto& layout : field->layouts)
        {
            bool contains = true;
            for (int coordinate = 0; coordinate < 3; coordinate++)
                contains = contains && point[coordinate] >= layout->low[coordinate] &&
                           point[coordinate] <= layout->low[coordinate] + layout->size[coordinate];
            if (!contains)
                continue;
            const DBL extent = std::max(layout->size[X], std::max(layout->size[Y], layout->size[Z]));
            const DBL cell = std::max(layout->size[X] / layout->counts[X],
                std::max(layout->size[Y] / layout->counts[Y], layout->size[Z] / layout->counts[Z]));
            limit = std::min(limit, std::min(field->detail < HUGE_VAL ? field->detail : extent / 64.0, 2.0 * cell));
        }
    }
    return limit < HUGE_VAL ? limit : maxStep;
}

bool RefractionField::Ahead(const Vector3d& origin, const Vector3d& direction, DBL& entry) const
{
    entry = HUGE_VAL;
    if (!bounded)
    {
        entry = 0.0;
        return true;
    }
    bool found = false;
    for (const PreparedRefractionDefinition *field : fields)
    {
        if (!field->varying)
            continue;
        for (const auto& layout : field->layouts)
        {
            DBL from = 0.0, to = HUGE_VAL;
            bool intersects = true;
            for (int coordinate = 0; coordinate < 3; coordinate++)
            {
                const DBL a = layout->low[coordinate] - maxStep;
                const DBL b = layout->low[coordinate] + layout->size[coordinate] + maxStep;
                if (direction[coordinate] == 0.0)
                {
                    if (origin[coordinate] < a || origin[coordinate] > b)
                        intersects = false;
                    continue;
                }
                DBL first = (a - origin[coordinate]) / direction[coordinate];
                DBL last = (b - origin[coordinate]) / direction[coordinate];
                if (first > last)
                    std::swap(first, last);
                from = std::max(from, first);
                to = std::min(to, last);
                intersects = intersects && to >= from;
            }
            if (intersects)
            {
                found = true;
                entry = std::min(entry, from);
            }
        }
    }
    return found;
}

static bool MediaAffectsTransport(const Media& medium, const Ray& ray)
{
    const bool appearance = !ray.IsShadowTestRay() && !ray.IsRadiosityRay() && !ray.IsPhotonRay();
    return medium.use_extinction || (!ray.IsShadowTestRay() && medium.use_scattering) ||
           (appearance && medium.use_emission);
}

static bool ModifierAffectsTransport(const Media& medium, bool emission, bool scattering)
{
    return (medium.mix == kMediaBlendMultiply) || medium.use_extinction ||
           (emission && medium.use_emission) || (scattering && medium.use_scattering);
}

static bool ModifierAffectsTransport(const Media& medium, const Ray& ray)
{
    return ModifierAffectsTransport(medium, !ray.IsShadowTestRay() && !ray.IsRadiosityRay() && !ray.IsPhotonRay(),
                                   !ray.IsShadowTestRay());
}

DBL MediaFunction::ModifierResolution(const Ray& ray, DBL from, DBL to) const
{
    DBL resolution = HUGE_VAL;
    if (modifiers)
        for (const MediaModifier& modifier : *modifiers)
            if ((modifier.medium->Sample_Method == 4) && !modifier.medium->Density.empty() && ModifierAffectsTransport(*modifier.medium, ray))
                resolution = std::min(resolution, modifier.medium->PreparedStep(ray, from, to));
    return resolution;
}

void MediaFunction::DensityForDeclaredMode(Media& medium, const Vector3d& point, MathColour& density)
{
    if (medium.Sample_Method == 4)
    {
        threadData->Stats()[Media_Prepared4_Queries]++;
        medium.PreparedDensityAt(point, density);
    }
    else
    {
        if (medium.Sample_Method == 3)
            threadData->Stats()[Media_Legacy3_Queries]++;
        Evaluate_Density_Pigment(medium.Density, point, density, threadData);
    }
}

void MediaFunction::ModifierDensity(Media& medium, const Vector3d& point, ModifierSource, MathColour& local)
{
    DensityForDeclaredMode(medium, point, local);
}

void MediaFunction::AddModifiedCoefficients(MediaVector& medias, const MathColour *density, const Vector3d& point, ModifierSource source,
                                            MathColour& extinction, MathColour *emission, MathColour *scattering, const DBL *phaseCosine)
{
    const size_t n = medias.size();
    modifierScratch.resize(3 * n);
    MathColour *absorbing = &modifierScratch[0], *scattered = absorbing + n, *emitted = scattered + n;
    for (size_t i = 0; i < n; i++)
    {
        absorbing[i] = density[i] * medias[i]->Absorption;
        scattered[i] = density[i] * medias[i]->Scattering;
        emitted[i] = density[i] * medias[i]->Emission;
    }
    for (const MediaModifier& modifier : *modifiers)
    {
        Media& medium = *modifier.medium;
        if (!ModifierAffectsTransport(medium, emission != nullptr, scattering != nullptr))
            continue;
        MathColour local;
        ModifierDensity(medium, point, source, local);
        ApplyCoefficientModifier(absorbing, modifier.below, local, medium.Absorption, medium.mix);
        ApplyCoefficientModifier(scattered, modifier.below, local, medium.Scattering, medium.mix);
        ApplyCoefficientModifier(emitted, modifier.below, local, medium.Emission, medium.mix);
    }
    for (size_t i = 0; i < n; i++)
    {
        extinction += absorbing[i] + scattered[i] * medias[i]->sc_ext;
        if (emission != nullptr)
            *emission += emitted[i];
        if (scattering != nullptr)
            *scattering += scattered[i] * (phaseCosine ? PhysicalPhaseFactor(medias[i]->Type, medias[i]->Eccentricity, *phaseCosine) : 1.0);
    }
}

// Points of each area light tested per media sample; see doc/PERF.md.
static const int kMediaAreaLightPoints = 4;

// Where the draws picking the step a stride of prepared steps takes its light at start.
static const std::uint64_t kMediaLightDraw = std::uint64_t(1) << 40;

struct MediaFunction::FixedLightPath final : public Trace::MediaFunctor::Path
{
    struct Domain final
    {
        MediaVector medias;
        LightSourceEntryVector lights;
        Ray ray;
        TraceTicket ticket;
        DBL offset;
        MediaModifierVector modifiers;
        bool allLights = false, infinite = false, physicalOrdinary = false;
        int ordinarySamples = 1;
        Domain(const MediaVector& m, const LightSourceEntryVector& l, const Ray& r, DBL at) :
            medias(m), lights(l), ray(r), ticket(r.GetTicket()), offset(at) {}
    };
    struct Cell final
    {
        size_t domain, firstLight, lastLight;
        DBL at, width;
        MathColour scattering, transfer;
        size_t mark = 0;
        bool continuous = false, dynamic = false;
        DBL start = 0.0, end = 0.0;
        MathColour extinction;
    };
    struct Storage final
    {
        vector<MathColour> transfers, suffix;
        vector<Domain> domains;
        vector<Cell> cells;
        vector<size_t> order, selected;
        vector<LightSource *> sources;
        vector<DBL> cdf, mass, base, lit;
        vector<EmitterLobe> lobes;
    };

    MediaFunction& owner;
    FixedLightPath *previous;
    size_t depth;
    Storage& storage;
    std::uint64_t key;
    DBL offset = 0.0;
    vector<MathColour>& transfers;
    vector<Domain>& domains;
    vector<Cell>& cells;
    size_t temporaryBytes = 0;
    bool collecting = true;

    static Storage& StorageAt(MediaFunction& f, size_t depth)
    {
        while (f.pathStorage.size() <= depth)
            f.pathStorage.push_back(std::make_shared<Storage>());
        return *static_cast<Storage *>(f.pathStorage[depth].get());
    }
    FixedLightPath(MediaFunction& f, const Ray& ray) : owner(f), previous(f.currentPath),
        depth(f.currentPath ? f.currentPath->depth + 1 : 0), storage(StorageAt(f, depth)),
        key(DeriveKey(ray.GetKey(), kDrawMedia, kMediaLightDraw)),
        transfers(storage.transfers), domains(storage.domains), cells(storage.cells) { owner.currentPath = this; }
    ~FixedLightPath() override
    {
        transfers.clear();
        domains.clear();
        cells.clear();
        owner.currentPath = previous;
    }

    void Transfer(const MathColour& depth, size_t first)
    {
        // New cells own this call's transmission; only earlier contributions cross it in full.
        if (first > 0)
            transfers.push_back(Exp(-depth));
        for (size_t i = first; i < cells.size(); i++)
            cells[i].mark = transfers.size();
    }

    void Scale(const MathColour& factor)
    {
        if (!cells.empty())
            transfers.push_back(factor);
    }

    bool Affected(const Cell& cell, const LightSource *light) const
    {
        const Domain& domain = domains[cell.domain];
        if (!light->emitter && !domain.allLights)
            return false;
        const auto& lights = domain.lights;
        for (size_t i = cell.firstLight; i <= cell.lastLight && i < lights.size(); i++)
            if (lights[i].light == light)
            {
                if (cell.continuous ? (cell.end > lights[i].s0 && cell.start < lights[i].s1) :
                                      (cell.at >= lights[i].s0 && cell.at <= lights[i].s1))
                    return true;
            }
        return false;
    }

    size_t Bytes() const
    {
        size_t bytes = sizeof(*this) + cells.capacity() * sizeof(Cell) + domains.capacity() * sizeof(Domain) +
                       transfers.capacity() * sizeof(MathColour) + temporaryBytes;
        for (const Domain& domain : domains)
            bytes += sizeof(MediaVector::VectorType) + domain.medias.capacity() * sizeof(Media *) +
                     sizeof(LightSourceEntryVector::VectorType) + domain.lights.capacity() * sizeof(LightSourceEntry) +
                     sizeof(RayInteriorVector::VectorType) + domain.ray.GetInteriors().capacity() * sizeof(Interior *) +
                     domain.modifiers.capacity() * sizeof(MediaModifier);
        return bytes;
    }

    /// Where a lobe's centre falls along a cell's ray, and its squared clearance widened by the lobe's spread.
    bool Aim(const Cell& cell, const EmitterLobe& lobe, DBL& foot, DBL& clearance2) const
    {
        const Ray& ray = domains[cell.domain].ray;
        const DBL speed2 = ray.Direction.lengthSqr();
        const Vector3d toLobe = lobe.centre - ray.Origin;
        foot = dot(toLobe, ray.Direction) / speed2;
        clearance2 = (std::max(0.0, toLobe.lengthSqr() - foot * foot * speed2) + lobe.spread2) / speed2;
        return IsFiniteNumber(foot) && IsFiniteNumber(clearance2) && clearance2 > 0.0;
    }

    /// A light's inverse-square reach over a cell: integrated along a continuous cell, at a point cell's point.
    DBL CellReach(const Cell& cell, const vector<EmitterLobe>& lobes) const
    {
        const Ray& ray = domains[cell.domain].ray;
        DBL sum = 0.0, foot, clearance2;
        for (const EmitterLobe& lobe : lobes)
            if (!cell.continuous)
                sum += lobe.share / ((ray.Evaluate(cell.at) - lobe.centre).lengthSqr() + lobe.spread2);
            else if (Aim(cell, lobe, foot, clearance2))
                sum += lobe.share * PreparedEquiangularMass(cell.start, cell.end, foot, clearance2) / ray.Direction.lengthSqr();
        return sum;
    }

    /// The density along a continuous cell of drawing distance `at` in proportion to the light's reach.
    DBL AimPdf(const Cell& cell, const vector<EmitterLobe>& lobes, DBL reach, DBL at) const
    {
        if (!(at >= cell.start) || !(at <= cell.end))
            return 0.0;
        DBL sum = 0.0, foot, clearance2;
        for (const EmitterLobe& lobe : lobes)
            if (Aim(cell, lobe, foot, clearance2))
                sum += lobe.share / (Sqr(at - foot) + clearance2);
        return sum / (domains[cell.domain].ray.Direction.lengthSqr() * reach);
    }

    /// A distance along a continuous cell drawn in proportion to the light's reach: a lobe by its share, then its angle.
    DBL AimDraw(const Cell& cell, const vector<EmitterLobe>& lobes, DBL reach, DBL u) const
    {
        const DBL speed2 = domains[cell.domain].ray.Direction.lengthSqr();
        DBL choose = u * reach * speed2, foot = 0.0, clearance2 = 0.0, part = 0.0;
        for (const EmitterLobe& lobe : lobes)
            if (Aim(cell, lobe, foot, clearance2))
            {
                part = lobe.share * PreparedEquiangularMass(cell.start, cell.end, foot, clearance2);
                if (choose < part)
                    break;
                choose -= part;
            }
        const DBL within = part > 0.0 ? std::min(std::nextafter(1.0, 0.0), std::max(0.0, choose / part)) : 0.5;
        return SamplePreparedEquiangularDistance(cell.start, cell.end, within, foot, clearance2).distance;
    }

    void Finish(MathColour& colour) override
    {
        collecting = false;
        if (cells.empty())
            return;
        struct PhotonState final
        {
            bool& flag;
            bool saved;
            explicit PhotonState(bool& value) : flag(value), saved(value) {}
            ~PhotonState() { flag = saved; }
        } photonState(owner.threadData->litObjectIgnoresPhotons);
        owner.threadData->Stats()[Media_Scatter_Legs]++;
        owner.threadData->Stats()[Media_Scatter_Domains] += domains.size();
        owner.threadData->Stats()[Media_Scatter_Cells] += cells.size();
        auto& maximum = owner.threadData->Stats()[Media_Scatter_Max_Cells];
        maximum = std::max(static_cast<POV_ULONG>(maximum), static_cast<POV_ULONG>(cells.size()));
        size_t liveBytes = 0;
        for (FixedLightPath *path = this; path != nullptr; path = path->previous)
            liveBytes += path->Bytes();
        vector<MathColour>& suffix = storage.suffix;
        suffix.assign(transfers.size() + 1, MathColour(1.0));
        for (size_t i = transfers.size(); i-- > 0;)
            suffix[i] = transfers[i] * suffix[i + 1];
        vector<size_t>& order = storage.order;
        order.resize(cells.size());
        for (size_t i = 0; i < order.size(); i++)
            order[i] = i;
        std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
            return domains[cells[a].domain].offset + cells[a].at < domains[cells[b].domain].offset + cells[b].at;
        });
        vector<LightSource *>& sources = storage.sources;
        sources.clear();
        for (const Domain& domain : domains)
            for (const LightSourceEntry& entry : domain.lights)
                if ((entry.light->emitter || domain.allLights) && std::find(sources.begin(), sources.end(), entry.light) == sources.end())
                    sources.push_back(entry.light);
        vector<size_t>& selected = storage.selected;
        vector<DBL>& cdf = storage.cdf;
        vector<DBL>& mass = storage.mass;
        vector<DBL>& base = storage.base;
        vector<DBL>& lit = storage.lit;
        for (size_t source = 0; source < sources.size(); source++)
        {
            LightSource& light = *sources[source];
            selected.clear();
            cdf.clear();
            mass.clear();
            base.clear();
            lit.clear();
            vector<EmitterLobe>& lobes = storage.lobes;
            lobes.clear();
            if (light.emitter)
                lobes = light.emitter->Lobes();
            // A light that does not fade with distance spreads over all space: it has no reach to aim at.
            if (lobes.empty())
                lobes.push_back(EmitterLobe{light.Center, (light.emitter || light.Fade_Power > 0.0) ?
                    Sqr(max(light.lawRadius, light.emitter ? 0.0 : light.Fade_Distance)) : HUGE_VAL, 1.0});
            DBL total = 0.0, totalMass = 0.0, totalLit = 0.0;
            bool resolved = true, reached = true;
            for (size_t i : order)
                if (Affected(cells[i], &light))
                {
                    const Cell& cell = cells[i];
                    const Domain& domain = domains[cell.domain];
                    const DBL scale = std::max(1.0e-12, (light.Center - domain.ray.Origin).length());
                    const DBL support = cell.continuous && domain.infinite ? PreparedDistanceMass(cell.start, cell.end, scale) : cell.width;
                    total += support;
                    base.push_back(support);
                    selected.push_back(i);
                    DBL weight = 0.0;
                    for (int ch = 0; ch < MathColour::channels; ch++)
                    {
                        const DBL value = (cells[i].scattering[ch] == 0.0 || suffix[cells[i].mark][ch] == 0.0) ? 0.0 :
                            std::abs(DBL(cells[i].scattering[ch]) * cells[i].transfer[ch] * suffix[cells[i].mark][ch]);
                        resolved = resolved && IsFiniteNumber(value);
                        weight = std::max(weight, value);
                    }
                    mass.push_back(weight);
                    totalMass += weight;
                    const DBL geometric = CellReach(cell, lobes);
                    DBL reach = 0.0;
                    for (int ch = 0; ch < MathColour::channels; ch++)
                        if (cell.scattering[ch] != 0.0 && suffix[cell.mark][ch] != 0.0)
                            reach = std::max(reach, std::abs(DBL(cell.scattering[ch]) * suffix[cell.mark][ch]) * geometric *
                                (cell.continuous ? std::exp(-DBL(cell.extinction[ch]) * cell.start) : DBL(cell.transfer[ch])));
                    reached = reached && IsFiniteNumber(reach);
                    lit.push_back(reach);
                    totalLit += reach;
                }
            if (!IsFiniteNumber(total))
                throw POV_EXCEPTION_STRING("Prepared-media scattering path length is not finite.");
            if (!(total > 0.0))
                continue;
            resolved = resolved && (totalMass > 0.0) && IsFiniteNumber(totalMass);
            reached = reached && (totalLit > 0.0) && IsFiniteNumber(totalLit);
            // Half the cells' choice follows the light's inverse-square reach, the rest their length and scattering.
            const DBL litShare = reached ? 0.5 : 0.0, massShare = resolved ? 0.5 * (1.0 - litShare) : 0.0;
            DBL cumulative = 0.0;
            for (size_t i = 0; i < selected.size(); i++)
            {
                cumulative += (1.0 - litShare - massShare) * base[i] / total + (resolved ? massShare * mass[i] / totalMass : 0.0) +
                              (reached ? litShare * lit[i] / totalLit : 0.0);
                cdf.push_back(cumulative);
            }
            if (!(cumulative > 0.0) || !IsFiniteNumber(cumulative))
                throw POV_EXCEPTION_STRING("Prepared-media scattering proposal cannot be normalized.");
            for (DBL& value : cdf)
                value /= cumulative;
            cdf.back() = 1.0;
            int count = light.emitter ? std::max(1, light.Area_Size1) : 1;
            if (!light.emitter)
                for (size_t index : selected)
                    count = std::max(count, domains[cells[index].domain].ordinarySamples);
            if (!light.emitter && light.Area_Light)
                count *= kMediaAreaLightPoints;
            const std::uint64_t sourceKey = DeriveKey(key, kDrawAreaLight, std::uint64_t(light.index) | (light.lightGroupLight ? (std::uint64_t(1) << 63) : 0));
            const PhotonSobol sequence(sourceKey);
            const size_t scratch = suffix.capacity() * sizeof(MathColour) + sources.capacity() * sizeof(LightSource *) +
                (order.capacity() + selected.capacity()) * sizeof(size_t) + (cdf.capacity() + mass.capacity() + base.capacity()) * sizeof(DBL);
            auto& peak = owner.threadData->Stats()[Media_Scatter_Peak_Bytes];
            peak = std::max(static_cast<POV_ULONG>(peak), static_cast<POV_ULONG>(liveBytes + scratch));
            for (int j = 0; j < count; j++)
            {
                owner.threadData->Stats()[Media_Scatter_Pairs]++;
                const DBL distance = EmitterDraw::JitterSelection(sequence(j, 0), count, Draw(sourceKey, kDrawMediaSample, j));
                const size_t picked = std::min(cdf.size() - 1, size_t(std::upper_bound(cdf.begin(), cdf.end(), distance) - cdf.begin()));
                const DBL probability = cdf[picked] - (picked ? cdf[picked - 1] : 0.0);
                const Cell& cell = cells[selected[picked]];
                Domain& domain = domains[cell.domain];
                DBL at = cell.at;
                DBL distancePdf = 1.0;
                MathColour transfer = cell.transfer;
                if (cell.continuous)
                {
                    const DBL within = std::min(std::nextafter(1.0, 0.0), std::max(0.0,
                        (distance - (picked ? cdf[picked - 1] : 0.0)) / probability));
                    const DBL scale = std::max(1.0e-12, (light.Center - domain.ray.Origin).length());
                    DBL channelMass[MathColour::channels], channelTotal = 0.0;
                    bool importance = true;
                    for (int ch = 0; ch < MathColour::channels; ch++)
                    {
                        // An exponential draw needs a rate of zero or more, and above zero on an unbounded cell.
                        const bool drawable = cell.extinction[ch] > 0.0 || (cell.extinction[ch] == 0.0 && IsFiniteNumber(cell.end));
                        channelMass[ch] = !drawable || cell.scattering[ch] == 0.0 || suffix[cell.mark][ch] == 0.0 ? 0.0 :
                            std::abs(DBL(cell.scattering[ch]) * cell.transfer[ch] * suffix[cell.mark][ch]);
                        importance = importance && IsFiniteNumber(channelMass[ch]);
                        channelTotal += channelMass[ch];
                    }
                    importance = importance && IsFiniteNumber(channelTotal) && channelTotal > 0.0;
                    const DBL cellReach = CellReach(cell, lobes);
                    const bool aimed = cellReach > 0.0 && IsFiniteNumber(cellReach);
                    const DBL aimShare = aimed ? 0.5 : 0.0, expShare = importance ? 0.5 * (1.0 - aimShare) : 0.0;
                    const DBL uniformShare = 1.0 - aimShare - expShare;
                    PreparedDistanceSample position;
                    if (within < uniformShare)
                        position = SamplePreparedDistance(cell.start, cell.end, within / uniformShare, scale);
                    else if (within < uniformShare + expShare)
                    {
                        DBL choose = std::min(std::nextafter(channelTotal, 0.0), (within - uniformShare) / expShare * channelTotal);
                        int channel = 0;
                        while (channel + 1 < MathColour::channels && choose >= channelMass[channel])
                        {
                            choose -= channelMass[channel];
                            channel++;
                        }
                        while (channelMass[channel] == 0.0 && channel > 0)
                            channel--;
                        const DBL u = std::min(std::nextafter(1.0, 0.0), choose / channelMass[channel]);
                        position = SamplePreparedExponentialDistance(cell.start, cell.end, u, cell.extinction[channel]);
                    }
                    else
                        position.distance = AimDraw(cell, lobes, cellReach,
                            std::min(std::nextafter(1.0, 0.0), (within - uniformShare - expShare) / aimShare));
                    at = position.distance;
                    distancePdf = uniformShare * (IsFiniteNumber(cell.end) ? 1.0 / (cell.end - cell.start) :
                        (scale + cell.start) / (scale + at) / (scale + at));
                    if (importance)
                        for (int ch = 0; ch < MathColour::channels; ch++)
                            if (channelMass[ch] > 0.0)
                                distancePdf += expShare * channelMass[ch] / channelTotal *
                                    PreparedExponentialDistancePdf(cell.start, cell.end, at, cell.extinction[ch]);
                    if (aimed)
                        distancePdf += aimShare * AimPdf(cell, lobes, cellReach, at);
                    for (int ch = 0; ch < MathColour::channels; ch++)
                        transfer[ch] = std::exp(-DBL(cell.extinction[ch]) * at) / distancePdf;
                }
                struct TicketScope final
                {
                    TraceTicket& ticket;
                    TraceTicket saved;
                    TicketScope(TraceTicket& t, const TraceTicket& state) : ticket(t), saved(t) { ticket = state; }
                    ~TicketScope()
                    {
                        const unsigned int found = ticket.maxFoundTraceLevel;
                        ticket = saved;
                        ticket.maxFoundTraceLevel = std::max(ticket.maxFoundTraceLevel, found);
                    }
                } ticketScope(domain.ray.GetTicket(), domain.ticket);
                Ray shadow(domain.ray);
                shadow.hasDifferentials = false;
                shadow.SetKey(DeriveKey(sourceKey, kDrawShadow, j));
                owner.threadData->litObjectIgnoresPhotons = true;
                for (const Media *medium : domain.medias)
                    owner.threadData->litObjectIgnoresPhotons = owner.threadData->litObjectIgnoresPhotons && medium->ignore_photons;
                const EmitterDraw draw(EmitterDraw::JitterSelection(sequence(j, 1), count, Draw(sourceKey, kDrawEmitter, j)),
                                       sequence(j, 2), sequence(j, 3), sequence(j, 4),
                                       DeriveKey(DeriveKey(sourceKey, kDrawEmitter, j), kDrawEmitter, UINT64_C(0x706f736974696f6e)));
                MathColour incoming;
                DBL depth;
                const Vector3d point = shadow.Evaluate(at);
                MathColour scattering = cell.scattering;
                if (cell.dynamic)
                {
                    ModifierScope modifierScope(owner, domain.modifiers.empty() ? nullptr : &domain.modifiers);
                    MathColour emission, extinction;
                    owner.ComputeMediaCoefficients(domain.medias, domain.ray, point, false, emission, extinction, scattering);
                    Ray camera(domain.ray);
                    camera.SetFlags(Ray::OtherRay, true, false);
                    Intersection prefix;
                    prefix.Depth = at;
                    prefix.Object = nullptr;
                    MathColour transmitted(1.0);
                    ColourChannel unused = 0.0;
                    owner.ComputeMedia(domain.medias, domain.modifiers.empty() ? nullptr : &domain.modifiers,
                                       camera, prefix, transmitted, unused);
                    transfer = transmitted / distancePdf;
                }
                if (scattering.IsZero())
                    continue;
                if (light.emitter)
                {
                    if (!owner.trace->SampleEmitterConnection(light, point, shadow, depth, incoming, draw))
                        continue;
                }
                else
                {
                    const Vector2d area(sequence(j, 1), sequence(j, 2));
                    owner.threadData->Stats()[Media_Ordinary_Light_Draws]++;
                    if (owner.trace->TestShadow(light, depth, shadow, point, incoming, &area))
                        continue;
                }
                MathColour local;
                // Source weights already contain j/(r² p); compensate the complete independent cell proposal.
                ModifierScope modifierScope(owner, domain.modifiers.empty() ? nullptr : &domain.modifiers);
                if (light.emitter || domain.physicalOrdinary)
                    owner.ComputePhysicalMediaScattering(domain.medias, local,
                        incoming / ((light.emitter ? 4.0 * M_PI : 4.0) * count * probability), point, domain.ray, shadow);
                else
                    owner.ComputeMediaScatteringAttenuation(domain.medias, local, scattering,
                        incoming / (count * probability), domain.ray, shadow);
                colour += local * transfer * suffix[cell.mark];
            }
        }
    }
};

std::unique_ptr<Trace::MediaFunctor::Path> MediaFunction::BeginPath(const Ray& ray)
{
    // Numerical curved chords share the scope; a recursive surface continuation starts another one.
    if (ray.IsShadowTestRay() || ray.IsPhotonRay())
        return nullptr;
    return std::unique_ptr<Trace::MediaFunctor::Path>(new FixedLightPath(*this, ray));
}

void MediaFunction::ScalePathRadiance(const MathColour& factor)
{
    if (currentPath && currentPath->collecting)
        currentPath->Scale(factor);
}

void MediaFunction::SetPathOffset(DBL offset)
{
    if (currentPath && currentPath->collecting)
        currentPath->offset = offset;
}

MediaFunction::MediaFunction(TraceThreadData *td, Trace *t, PhotonGatherer *pg) :
    currentPath(nullptr),
    modifiers(nullptr),
    drawKey(0),
    threadData(td),
    trace(t),
    photonGatherer(pg),
    lightSampleIndex(0),
    lightSampleShift(-1.0, -1.0)
{
}

void MediaFunction::ComputeMedia(vector<Media>& mediasource, const Ray& ray, Intersection& isect, MathColour& colour, ColourChannel& transm)
{
    if(!mediasource.empty())
    {
        MediaVector medialist;

        for(vector<Media>::iterator im(mediasource.begin()); im != mediasource.end(); im++)
            medialist.push_back(&(*im));

        // Note: this version of ComputeMedia does not deposit photons. This is
        // intentional.  Even though we're processing a photon ray, we don't want
        // to deposit photons in the infinite atmosphere, only in contained
        // media, which is processed later (in ComputeLightedTexture).  [nk]
        if(!medialist.empty())
            ComputeMedia(medialist, ray, isect, colour, transm);
    }
}

void MediaFunction::ComputeMedia(const RayInteriorVector& mediasource, const Ray& ray, Intersection& isect, MathColour& colour, ColourChannel& transm)
{
    if(!mediasource.empty())
    {
        MediaVector medialist;
        MediaModifierVector mods;
        CollectInteriorMedia(mediasource, medialist, threadData->GetSceneData()->mediaBlendModes ? &mods : nullptr);

        // Note: this version of ComputeMedia does not deposit photons. This is
        // intentional.  Even though we're processing a photon ray, we don't want
        // to deposit photons in the infinite atmosphere, only in contained
        // media, which is processed later (in ComputeLightedTexture).  [nk]
        if(!medialist.empty())
            ComputeMedia(medialist, &mods, ray, isect, colour, transm);
    }
}

/*****************************************************************************
* INPUT
*   Ray       - Current ray, start point P0
*   Inter     - Current intersection, end point P1
*   Colour    - Color emitted at P1 towards P0
*   light_ray - true if we are looking at a light source ray
* OUTPUT
*   Colour    - Color arriving at the end point
******************************************************************************/

void MediaFunction::ComputeMedia(MediaVector& medias, const Ray& ray, Intersection& isect, MathColour& colour, ColourChannel& transm)
{
    ComputeMedia(medias, nullptr, ray, isect, colour, transm);
}

void MediaFunction::ComputeMedia(MediaVector& medias, const MediaModifierVector *mods, const Ray& ray, Intersection& isect, MathColour& colour, ColourChannel& transm)
{
    ModifierScope scope(*this, mods);
    Intersection endpoint(isect);
    if (endpoint.unbounded && (PreparedRequired(medias, ray) ||
        (ConstantMediaCoefficients(medias, ray) && !threadData->GetSceneData()->mediaLights.empty())))
        endpoint.Depth = std::numeric_limits<DBL>::infinity();
    ReceiverScope receiverScope(*this, endpoint.Object != nullptr ? endpoint.Object : pathReceiver);
    std::unique_ptr<Trace::MediaFunctor::Path> localPath;
    if (!currentPath && !ray.IsShadowTestRay() && !ray.IsPhotonRay())
        localPath = BeginPath(ray);
    FixedLightPath *path = (!ray.IsShadowTestRay() && !ray.IsPhotonRay() && currentPath && currentPath->collecting) ? currentPath : nullptr;
    const size_t firstPathCell = path ? path->cells.size() : 0;
    LightSourceEntryVector lights;
    LitIntervalVector litintervals;
    MediaIntervalVector mediaintervals;
    Media *IMedia;
    bool all_constant_and_light_ray = ray.IsShadowTestRay();  // is all the media constant?
    bool ignore_photons = true;
    bool use_extinction = false;
    bool use_scattering = false;
    bool use_emission = false;
    int minSamples;
    DBL aa_threshold = HUGE_VAL;

    // Find media with the largest number of intervals.
    IMedia = medias.front();

    for(MediaVector::iterator i(medias.begin()); i != medias.end(); i++)
    {
        // find media with the most intervals
        if((*i)->Intervals > IMedia->Intervals)
            IMedia = (*i);

        // find smallest AA_Threshold
        if((*i)->AA_Threshold < aa_threshold)
            aa_threshold = (*i)->AA_Threshold;

        // do not ignore photons if at least one media wants photons
        ignore_photons = ignore_photons && (*i)->ignore_photons;

        // use extinction if at leeast one media wants extinction
        use_extinction = use_extinction || (*i)->use_extinction;

        // use scattering if at leeast one media wants scattering
        use_scattering = use_scattering || (*i)->use_scattering;
        use_emission = use_emission || (*i)->use_emission;

        // NK fast light_ray media calculation for constant media
        for (vector<PIGMENT*>::iterator ii = (*i)->Density.begin(); ii != (*i)->Density.end(); ++ ii)
            all_constant_and_light_ray = all_constant_and_light_ray && ((*ii)->Type == PLAIN_PATTERN);
    }

    all_constant_and_light_ray = all_constant_and_light_ray && (modifiers == nullptr);
    if (!use_extinction && !use_scattering && !use_emission)
        return;

    if ((ray.IsRadiosityRay() || ray.IsPhotonRay()) && !use_extinction && !use_scattering)
        return;

    // If this is a light ray and no extinction is used we can return.
    if((ray.IsShadowTestRay()) && (!use_extinction))
        return;

    // Prepare the Monte Carlo integration along the ray from P0 to P1.
    if(!ray.IsShadowTestRay())
        ComputeMediaLightInterval(lights, litintervals, ray, endpoint);

    if(litintervals.empty())
        litintervals.push_back(LitInterval(false, 0.0, endpoint.Depth, 0, 0));

    const bool preparedPolicy = PreparedRequired(medias, ray);
    const bool constantCoefficients = ConstantMediaCoefficients(medias, ray);
    bool volumeLighting = false;
    for (const LightSourceEntry& entry : lights)
        volumeLighting = volumeLighting || (entry.light->emitter != nullptr);
    const bool analytic = constantCoefficients && (preparedPolicy || (path && volumeLighting));
    const DBL requestedResolution = PreparedResolution(medias, ray, endpoint.Depth);
    DBL preparedFrom = 0.0, preparedTo = endpoint.Depth;
    DBL preparedCoarsening = 1.0;
    if ((requestedResolution > 0.0) && !analytic)
    {
        PreparedRange(medias, ray, preparedFrom, preparedTo);
        CoarsenPreparedResolution(medias, ray, preparedFrom, preparedTo, requestedResolution, preparedCoarsening);
    }
    const DBL preparedResolution = requestedResolution * preparedCoarsening;
    const bool fieldsPrepared = PreparedRequired(medias, ray);
    if (fieldsPrepared)
        PrepareFields(medias, ray);
    if (fieldsPrepared && preparedTo <= preparedFrom)
        return;
    const bool fastCamera = fieldsPrepared && !ray.IsShadowTestRay();
    const bool fastShadow = fieldsPrepared && ray.IsShadowTestRay();
    if (!ray.IsShadowTestRay() && !ray.IsPhotonRay())
    {
        threadData->Stats()[fastCamera ? Media_Fixed_Calls : Media_Classic_Calls]++;
        bool varying = false;
        for (const Media *medium : medias)
            varying = varying || !medium->Density.empty();
        if (!fastCamera && !varying && modifiers != nullptr && !modifiers->empty())
            threadData->Stats()[Media_Modifier_Fallback_Calls]++;
    }

    // Set up sampling intervals (makes sure we will always have enough intervals)
    if (analytic)
    {
        for (const LitInterval& interval : litintervals)
            if (interval.s1 > interval.s0)
                mediaintervals.push_back(MediaInterval(interval.lit, 0, interval.s0, interval.s1,
                                                       interval.ds, interval.l0, interval.l1));
    }
    else
        ComputeMediaSampleInterval(litintervals, mediaintervals, IMedia, fastCamera || fastShadow);
    if (mediaintervals.empty())
        return;

    if(mediaintervals.front().s0 > 0.0)
        mediaintervals.insert(mediaintervals.begin(),
                              MediaInterval(false, 0,
                              0.0,
                              mediaintervals.front().s0,
                              mediaintervals.front().s0,
                              0, 0));
    if(mediaintervals.back().s1 < endpoint.Depth)
        mediaintervals.push_back(MediaInterval(false, 0,
                                 mediaintervals.back().s1,
                                 endpoint.Depth,
                                 endpoint.Depth - mediaintervals.back().s1,
                                 0, 0));

    if (fieldsPrepared)
    {
        for (MediaInterval& interval : mediaintervals)
        {
            interval.s0 = std::max(interval.s0, preparedFrom);
            interval.s1 = std::max(interval.s0, std::min(interval.s1, preparedTo));
            interval.ds = interval.s1 - interval.s0;
        }
        SplitPreparedIntervals(medias, ray, mediaintervals);
    }

    minSamples = IMedia->Min_Samples;

    const unsigned int savedIndex = lightSampleIndex;
    const Vector2d savedShift = lightSampleShift;
    const std::uint64_t savedKey = drawKey;
    drawKey = ray.NextChildKey(kDrawMedia);
    trace->MarkGrain();
    if(!ray.IsShadowTestRay())
    {
        lightSampleIndex = 0;
        lightSampleShift = Vector2d(-1.0, -1.0);
    }

    // Sample all intervals.
    if (analytic)
        ComputeMediaConstantSampling(medias, lights, mediaintervals, ray, use_scattering, fieldsPrepared);
    else if(ray.IsShadowTestRay() && !all_constant_and_light_ray)
    {
        bool fast = fieldsPrepared && (ray.GetMediaLight() != nullptr) && (modifiers == nullptr);
        if (fast)
        {
            const LightSource& light = *ray.GetMediaLight();
            fast = light.Parallel && dot(ray.Direction, -light.Direction) > 0.999999;
            for (MediaInterval& interval : mediaintervals)
            {
                interval.od.Clear();
                for (Media* medium : medias)
                {
                    if (!medium->use_extinction)
                        continue;
                    MathColour depth;
                    if (medium->Density.empty())
                        depth = medium->Extinction * interval.ds;
                    else
                    {
                        DBL from = interval.s0, to = interval.s1;
                        Media::FastCache *binding = medium->Sample_Method == 4 ? medium->SinglePreparedBinding() : nullptr;
                        if (!binding)
                        {
                            fast = false;
                            break;
                        }
                        if (!binding->Segment(ray, from, to))
                            continue;
                        if (!fast || !binding->OpticalDepth(*medium, light, ray.Evaluate(from),
                                                          ray.Evaluate(to), depth, threadData))
                        {
                            fast = false;
                            break;
                        }
                    }
                    interval.od += depth;
                }
                if (!fast)
                    break;
                interval.te.Clear();
                interval.samples = 1;
            }
        }
        if (!fast)
            ComputeMediaTransmittance(medias, mediaintervals, ray, IMedia);
    }
    else if(fastCamera)
        ComputeMediaFixedSampling(medias, lights, mediaintervals, ray, preparedResolution / 3.0,
                                  ignore_photons, use_scattering, preparedCoarsening);
    else if((IMedia->Sample_Method == 3) && !all_constant_and_light_ray) // explicitly selected legacy adaptive sampling
        ComputeMediaAdaptiveSampling(medias, lights, mediaintervals, ray, IMedia, aa_threshold, minSamples, ignore_photons, use_scattering);
    else
        ComputeMediaRegularSampling(medias, lights, mediaintervals, ray, IMedia, minSamples, ignore_photons, use_scattering, all_constant_and_light_ray);

    if (!fieldsPrepared && !analytic && path && use_scattering)
        CollectLegacyLightIntervals(medias, lights, mediaintervals, ray);
    ComputeMediaColour(mediaintervals, colour, transm);
    if (path)
    {
        MathColour depth;
        for (const MediaInterval& interval : mediaintervals)
            depth += interval.od * (1.0 / DBL(interval.samples));
        path->Transfer(depth, firstPathCell);
    }

    lightSampleIndex = savedIndex;
    lightSampleShift = savedShift;
    drawKey = savedKey;
    if (localPath)
        localPath->Finish(colour);
}

// Most density pigments, and media, a ray plans once; beyond them each batch of points classifies its own.
const size_t kMaxDensityPlans = 8;

// A shadow ray's media classified once, for extinction at points and its range along segments.
class ExtinctionPlan final
{
    public:
        bool Plan(MediaVector& medias, TraceThreadData *threadData);
        void Evaluate(const Vector3d *points, MathColour *extinction, size_t n, TraceThreadData *threadData) const;
        bool Range(const Vector3d& a, const Vector3d& b, MathColour& lo, MathColour& hi) const;
    private:
        DensityPigmentPlan plans[kMaxDensityPlans];
        size_t ends[kMaxDensityPlans];
        const MathColour *extinction[kMaxDensityPlans];
        size_t medias;
        size_t legacyThree = 0;
};

bool ExtinctionPlan::Plan(MediaVector& list, TraceThreadData *threadData)
{
    size_t n = 0;
    medias = 0;
    for(MediaVector::iterator i(list.begin()); i != list.end(); i++)
    {
        if (!(*i)->use_extinction)
            continue;
        if ((*i)->Sample_Method == 4)
        {
            threadData->Stats()[Media_Forbidden4_Procedural]++;
            throw POV_EXCEPTION_STRING("Prepared media cannot enter a legacy density plan.");
        }
        legacyThree += (*i)->Sample_Method == 3 ? 1 : 0;
        if((medias == kMaxDensityPlans) || (n + (*i)->Density.size() > kMaxDensityPlans))
            return false;
        for(vector<PIGMENT*>::iterator d = (*i)->Density.begin(); d != (*i)->Density.end(); ++d)
            Plan_Density_Pigment(plans[n++], *d, threadData);
        ends[medias] = n;
        extinction[medias++] = &(*i)->Extinction;
    }
    return true;
}

void ExtinctionPlan::Evaluate(const Vector3d *points, MathColour *result, size_t n, TraceThreadData *threadData) const
{
    threadData->Stats()[Media_Legacy3_Queries] += legacyThree * n;
    MathColour density[kDensityBatch];

    for(size_t j = 0; j < n; j++)
        result[j].Clear();
    for(size_t m = 0, first = 0; m < medias; first = ends[m++])
    {
        for(size_t j = 0; j < n; j++)
            density[j].Set(1.0);
        for(size_t k = ends[m]; k > first; k--)
            Apply_Density_Pigment(plans[k - 1], points, density, n, threadData);
        for(size_t j = 0; j < n; j++)
            result[j] += density[j] * (*extinction[m]);
    }
}

bool ExtinctionPlan::Range(const Vector3d& a, const Vector3d& b, MathColour& lo, MathColour& hi) const
{
    lo.Clear();
    hi.Clear();
    for(size_t m = 0, k = 0; m < medias; m++)
    {
        MathColour dlo(1.0), dhi(1.0), plo, phi;
        for(; k < ends[m]; k++)
        {
            if(!Density_Range(plans[k], a, b, plo, phi))
                return false;
            for(int ch = 0; ch < MathColour::channels; ch++)
            {
                const ColourChannel p[4] = { dlo[ch] * plo[ch], dlo[ch] * phi[ch], dhi[ch] * plo[ch], dhi[ch] * phi[ch] };
                dlo[ch] = min(min(p[0], p[1]), min(p[2], p[3]));
                dhi[ch] = max(max(p[0], p[1]), max(p[2], p[3]));
            }
        }
        for(int ch = 0; ch < MathColour::channels; ch++)
        {
            const ColourChannel e = (*extinction[m])[ch];
            lo[ch] += min(dlo[ch] * e, dhi[ch] * e);
            hi[ch] += max(dlo[ch] * e, dhi[ch] * e);
        }
    }
    return true;
}

void MediaFunction::ComputeMediaExtinction(MediaVector& medias, const ExtinctionPlan *plan, const Ray& ray, const DBL *depths,
                                           MathColour *extinction, size_t n)
{
    Vector3d points[kDensityBatch];
    MathColour density[kDensityBatch];

    threadData->Stats()[Media_Samples] += n;
    threadData->Stats()[Media_Shadow_Coefficients] += n;
    for(size_t j = 0; j < n; j++)
        points[j] = ray.Evaluate(depths[j]);
    if(plan != nullptr)
    {
        plan->Evaluate(points, extinction, n, threadData);
        return;
    }
    for(size_t j = 0; j < n; j++)
        extinction[j].Clear();
    if(modifiers != nullptr)
    {
        std::vector<MathColour> densities(medias.size() * n);
        for(size_t m = 0; m < medias.size(); m++)
        {
            if (!medias[m]->use_extinction)
                for (size_t j = 0; j < n; j++)
                    density[j].Clear();
            else
            {
                if (medias[m]->Sample_Method == 4)
                {
                    threadData->Stats()[Media_Forbidden4_Procedural]++;
                    throw POV_EXCEPTION_STRING("Prepared media cannot use procedural extinction batching.");
                }
                if (medias[m]->Sample_Method == 3)
                    threadData->Stats()[Media_Legacy3_Queries] += n;
                Evaluate_Density_Pigment(medias[m]->Density, points, density, n, threadData);
            }
            for(size_t j = 0; j < n; j++)
                densities[j * medias.size() + m] = density[j];
        }
        for(size_t j = 0; j < n; j++)
            AddModifiedCoefficients(medias, &densities[j * medias.size()], points[j], ModifierSource::kPattern, extinction[j], nullptr, nullptr);
        return;
    }
    for(MediaVector::iterator i(medias.begin()); i != medias.end(); i++)
    {
        if (!(*i)->use_extinction)
            continue;
        if ((*i)->Sample_Method == 4)
        {
            threadData->Stats()[Media_Forbidden4_Procedural]++;
            throw POV_EXCEPTION_STRING("Prepared media cannot use procedural extinction batching.");
        }
        if ((*i)->Sample_Method == 3)
            threadData->Stats()[Media_Legacy3_Queries] += n;
        Evaluate_Density_Pigment((*i)->Density, points, density, n, threadData);
        for(size_t j = 0; j < n; j++)
            extinction[j] += density[j] * (*i)->Extinction;
    }
}

static bool BelowMediaOpacity(const MediaIntervalVector& intervals, int points, const MathColour& hi, DBL budget, DBL& roundingError)
{
    const DBL rounding = (2.0 * points + intervals.size() + 64.0) * std::numeric_limits<ColourChannel>::epsilon();
    if(rounding >= 0.01)
        return false;
    for(int ch = 0; ch < MathColour::channels; ch++)
        if(!(hi[ch] >= 0.0) || !IsFiniteNumber(hi[ch]))
            return false;
    const DBL sumUpper = DBL(hi.Max()) * (2.0 * points) / (1.0 - rounding);
    if(sumUpper >= std::numeric_limits<ColourChannel>::max())
        return false;
    DBL length = 0.0;
    for(const auto& interval : intervals)
    {
        if(!(interval.ds >= 0.0) || !IsFiniteNumber(interval.ds))
            return false;
        length += interval.ds;
    }
    const DBL upper = (DBL(hi.Min()) + 1e-6 * DBL(hi.Max())) * length / (1.0 - rounding);
    roundingError = rounding * DBL(hi.Max()) * length / (1.0 - rounding);
    return IsFiniteNumber(upper) && (upper < log(1024.0)) && (roundingError < budget);
}

bool MediaFunction::ConstantMediaCoefficients(const MediaVector& medias, const Ray& ray) const
{
    for (const Media *medium : medias)
        if (MediaAffectsTransport(*medium, ray) && !ConstantDensity(*medium))
            return false;
    if (modifiers)
        for (const MediaModifier& modifier : *modifiers)
            if (ModifierAffectsTransport(*modifier.medium, ray) && !ConstantDensity(*modifier.medium))
                return false;
    return true;
}

bool MediaFunction::PreparedRequired(const MediaVector& medias, const Ray& ray) const
{
    for (const Media *medium : medias)
        if ((medium->Sample_Method == 4) && MediaAffectsTransport(*medium, ray))
            return true;
    if (modifiers)
        for (const MediaModifier& modifier : *modifiers)
            if ((modifier.medium->Sample_Method == 4) && ModifierAffectsTransport(*modifier.medium, ray))
                return true;
    return false;
}

DBL MediaFunction::PreparedResolution(MediaVector& medias, const Ray& ray, DBL end)
{
    if (!PreparedRequired(medias, ray))
        return 0.0;
    DBL resolution = HUGE_VAL;
    auto layout = [&](Media& medium) {
        if (medium.Sample_Method != 4)
            return;
        medium.RequirePreparedField();
        resolution = std::min(resolution, medium.PreparedStep(ray, 0.0, end));
    };
    for (Media *medium : medias)
        if (MediaAffectsTransport(*medium, ray))
            layout(*medium);
    if (modifiers)
        for (const MediaModifier& modifier : *modifiers)
            if (ModifierAffectsTransport(*modifier.medium, ray))
                layout(*modifier.medium);
    return resolution;
}

void MediaFunction::PreparedRange(MediaVector& medias, const Ray& ray, DBL& from, DBL& to)
{
    DBL first = to, last = from;
    for (Media* medium : medias)
    {
        if (!MediaAffectsTransport(*medium, ray))
            continue;
        if (medium->Density.empty() || (medium->Sample_Method != 4))
            return;
        DBL entry = from, exit = to;
        if (medium->PreparedRange(ray, entry, exit))
        {
            first = std::min(first, entry);
            last = std::max(last, exit);
        }
    }
    from = first;
    to = std::max(first, last);
}

DBL MediaFunction::PreparedSteps(MediaVector& medias, const Ray& ray, DBL from, DBL to, DBL resolution, DBL coarsening)
{
    const DBL divisor = ray.IsShadowTestRay() ? 1.0 : 3.0;
    DBL count = 2.0 * medias.size() + 1.0;
    for (Media* medium : medias)
    {
        if (!MediaAffectsTransport(*medium, ray))
            continue;
        if (medium->Density.empty())
        {
            if (!ray.IsShadowTestRay() && (medium->use_scattering || medium->use_emission))
                count += ceil((to - from) * divisor / resolution);
            continue;
        }
        if (medium->Sample_Method != 4)
        {
            count += ceil(std::max(1, medium->Min_Samples) / coarsening);
            continue;
        }
        DBL entry = from, exit = to;
        if (medium->PreparedRange(ray, entry, exit))
            count += ceil((exit - entry) * divisor / (medium->PreparedStep(ray, entry, exit) * coarsening));
    }
    if (modifiers)
        for (const MediaModifier& modifier : *modifiers)
            if ((modifier.medium->Sample_Method != 4) && !modifier.medium->Density.empty() &&
                ModifierAffectsTransport(*modifier.medium, ray))
                count += ceil(std::max(1, modifier.medium->Min_Samples) / coarsening);
    const DBL modifierResolution = ModifierResolution(ray, from, to);
    if (modifierResolution < HUGE_VAL)
        count += ceil((to - from) * divisor / (modifierResolution * coarsening));
    return count;
}

void MediaFunction::CoarsenPreparedResolution(MediaVector& medias, const Ray& ray, DBL from, DBL to,
                                              DBL resolution, DBL& coarsening)
{
    coarsening = 1.0;
    const DBL steps = PreparedSteps(medias, ray, from, to, resolution);
    if (steps <= 4096.0)
        return;
    // One uniform factor coarsens every medium's steps until the march fits the budget, so the prepared path runs.
    coarsening = ceil(steps / 4096.0);
    while (PreparedSteps(medias, ray, from, to, resolution * coarsening, coarsening) > 4096.0)
        coarsening *= 2.0;
    char detail[512];
    std::snprintf(detail, sizeof(detail),
        "The prepared march over a span of %.9g at resolution %.9g needs %.0f steps, above the budget of 4096, so it runs "
        "coarsened at resolution %.9g. Splitting long containers or a coarser resolution avoids this coarsening.",
        to - from, resolution, steps, resolution * coarsening);
    WarnFastMedia(threadData, *medias.front(), kFastCoarse, detail,
                  medias.front()->fastCache ? medias.front()->fastCache->container : nullptr);
}

void MediaFunction::SplitPreparedIntervals(MediaVector& medias, const Ray& ray, MediaIntervalVector& intervals)
{
    if (medias.size() == 1)
        return;
    MediaIntervalVector split;
    for (const MediaInterval& interval : intervals)
    {
        if (interval.ds <= 0.0)
            continue;
        vector<DBL> cuts{interval.s0, interval.s1};
        for (Media* medium : medias)
        {
            if (medium->Density.empty() || (medium->Sample_Method != 4) || !MediaAffectsTransport(*medium, ray))
                continue;
            medium->PreparedCuts(ray, interval.s0, interval.s1, cuts);
        }
        std::sort(cuts.begin(), cuts.end());
        for (size_t i = 1; i < cuts.size(); i++)
            if (cuts[i] > cuts[i - 1])
                split.push_back(MediaInterval(interval.lit, 0, cuts[i - 1], cuts[i], cuts[i] - cuts[i - 1], interval.l0, interval.l1));
    }
    intervals = split;
}

DBL MediaFunction::PreparedStep(MediaVector& medias, const Ray& ray, const MediaInterval& interval, DBL fallback, DBL coarsening)
{
    DBL step = interval.ds;
    for (Media* medium : medias)
    {
        if (!MediaAffectsTransport(*medium, ray))
            continue;
        if (medium->Density.empty())
        {
            if (!ray.IsShadowTestRay() && (medium->use_scattering || medium->use_emission))
                step = std::min(step, fallback);
            continue;
        }
        const DBL fieldStep = medium->Sample_Method == 4 ? medium->PreparedStep(ray, interval.s0, interval.s1) :
                             interval.ds / std::max(1, medium->Min_Samples);
        step = std::min(step, fieldStep * coarsening / (ray.IsShadowTestRay() ? 1.0 : 3.0));
    }
    if (modifiers)
        for (const MediaModifier& modifier : *modifiers)
            if ((modifier.medium->Sample_Method != 4) && !modifier.medium->Density.empty() &&
                ModifierAffectsTransport(*modifier.medium, ray))
                step = std::min(step, interval.ds * coarsening / std::max(1, modifier.medium->Min_Samples));
    const DBL modifierResolution = ModifierResolution(ray, interval.s0, interval.s1);
    if (modifierResolution < HUGE_VAL)
        step = std::min(step, modifierResolution * coarsening / (ray.IsShadowTestRay() ? 1.0 : 3.0));
    return step;
}

bool MediaFunction::PrepareFields(MediaVector& medias, const Ray& ray)
{
    bool ready = true;
    for (Media* medium : medias)
        if ((medium->Sample_Method == 4) && (!ray.IsShadowTestRay() || medium->use_extinction))
            medium->RequirePreparedField();
    if (modifiers)
        for (const MediaModifier& modifier : *modifiers)
            if ((modifier.medium->Sample_Method == 4) && ModifierAffectsTransport(*modifier.medium, ray))
                modifier.medium->RequirePreparedField();
    return ready;
}

void MediaFunction::ComputeMediaTransmittance(MediaVector& medias, MediaIntervalVector& mediaintervals, const Ray& ray, const Media *IMedia)
{
    const bool method3 = (IMedia->Sample_Method == 3 || IMedia->Sample_Method == 4);
    const int points = method3 ? 2 * max((IMedia->Min_Samples + 1) / 2, 1) + 1 : max(IMedia->Min_Samples, 1);
    const DBL requestedResolution = PreparedResolution(medias, ray, mediaintervals.back().s1);
    DBL coarsening = 1.0;
    if (requestedResolution > 0.0)
        CoarsenPreparedResolution(medias, ray, mediaintervals.front().s0, mediaintervals.back().s1,
                                  requestedResolution, coarsening);
    const DBL resolution = requestedResolution * coarsening;
    if (PreparedRequired(medias, ray))
    {
        PrepareFields(medias, ray);
        threadData->Stats()[Media_Intervals] += mediaintervals.size();
        ComputeMediaFieldTransmittance(medias, mediaintervals, ray, resolution, coarsening);
        return;
    }

    ExtinctionPlan plan;
    const bool planned = (modifiers == nullptr) && plan.Plan(medias, threadData);
    MathColour lo, hi;
    DBL roundingError;
    threadData->Stats()[Media_Intervals] += mediaintervals.size();
    if(planned && (ray.GetMediaErrorBudget() > 0.0) &&
       plan.Range(ray.Evaluate(mediaintervals.front().s0), ray.Evaluate(mediaintervals.back().s1), lo, hi) && (lo.Min() >= 0.0) &&
       BelowMediaOpacity(mediaintervals, points, hi, ray.GetMediaErrorBudget(), roundingError))
    {
        ray.SetMediaErrorBudget(ray.GetMediaErrorBudget() - roundingError);
        ComputeMediaBoundedTransmittance(medias, mediaintervals, ray, plan, method3, points, lo, hi);
    }
    else
        ComputeMediaPointTransmittance(medias, mediaintervals, ray, planned ? &plan : nullptr, method3, points);
}

void MediaFunction::ComputeMediaFieldTransmittance(MediaVector& medias, MediaIntervalVector& mediaintervals, const Ray& ray,
                                                  DBL resolution, DBL coarsening)
{
    const DBL opaque = log(1024.0);
    MathColour total;
    bool dark = false;
    std::vector<MathColour> densities;
    for (MediaInterval& interval : mediaintervals)
    {
        MathColour depth;
        if (!dark && (interval.ds > 0.0))
        {
            const DBL width = PreparedStep(medias, ray, interval, resolution, coarsening);
            const int count = std::max(1, int(ceil(interval.ds / width)));
            const DBL step = interval.ds / count;
            for (int j = 0; j < count; j++)
            {
                const Vector3d point = ray.Evaluate(interval.s0 + (j + 0.5) * step);
                MathColour extinction;
                densities.clear();
                for (Media* medium : medias)
                {
                    MathColour density;
                    if (!medium->use_extinction)
                        density.Clear();
                    else
                        DensityForDeclaredMode(*medium, point, density);
                    if (modifiers != nullptr)
                        densities.push_back(density);
                    else
                        extinction += density * medium->Extinction;
                }
                if (modifiers != nullptr)
                    AddModifiedCoefficients(medias, &densities[0], point, ModifierSource::kField, extinction, nullptr, nullptr);
                depth += extinction * step;
                threadData->Stats()[Media_Samples]++;
                threadData->Stats()[Media_Shadow_Coefficients]++;
                if ((total + depth).Min() > opaque)
                {
                    dark = true;
                    break;
                }
            }
        }
        interval.od = depth;
        interval.te.Clear();
        interval.samples = 1;
        total += depth;
    }
}

void MediaFunction::ComputeMediaPointTransmittance(MediaVector& medias, MediaIntervalVector& mediaintervals, const Ray& ray,
                                                   const ExtinctionPlan *plan, bool method3, int points)
{
    // The points the old sampling used, without per-sample lighting; stops once transmittance is below 1/1024.
    const DBL opaque = log(1024.0);
    MathColour total, carried;
    DBL carriedAt = -1.0;
    bool dark = false;
    DBL at[kDensityBatch];
    MathColour extinction[kDensityBatch];

    for(MediaIntervalVector::iterator i(mediaintervals.begin()); i != mediaintervals.end(); i++)
    {
        MathColour sum;
        DBL weights = 0.0;
        for(int j0 = 0; (j0 < points) && !dark; j0 += int(kDensityBatch))
        {
            const int n = min(points - j0, int(kDensityBatch));
            for(int k = 0; k < n; k++)
                at[k] = method3 ? i->s0 + i->ds * (j0 + k) / (points - 1) : i->s0 + i->ds * (j0 + k + 0.5) / points;
            const int reuse = (method3 && (j0 == 0) && (at[0] == carriedAt)) ? 1 : 0;
            ComputeMediaExtinction(medias, plan, ray, at + reuse, extinction + reuse, n - reuse);
            if(reuse)
                extinction[0] = carried;
            for(int k = 0; (k < n) && !dark; k++)
            {
                const int j = j0 + k;
                DBL weight = 1.0;
                if(method3)
                {
                    weight = ((j == 0) || (j == points - 1) || (j % 2)) ? 1.0 : 2.0;
                    if(j == points - 1)
                    {
                        carried = extinction[k];
                        carriedAt = at[k];
                    }
                }
                sum += extinction[k] * weight;
                weights += weight;
                dark = (total + sum * (i->ds / (method3 ? (points - 1) * 1.5 : points))).Min() > opaque;
            }
        }

        i->od = (weights > 0.0) ? sum * (i->ds / (method3 ? (points - 1) * 1.5 : points)) : MathColour();
        i->te.Clear();
        i->samples = 1;
        total += i->od;
    }
}

void MediaFunction::ComputeMediaBoundedTransmittance(MediaVector& medias, MediaIntervalVector& mediaintervals, const Ray& ray,
                                                     const ExtinctionPlan& plan, bool method3, int points,
                                                     const MathColour& rayLo, const MathColour& rayHi)
{
    // Today's points and weights, but a stretch of them whose extinction range fits its share of a 1/1024 budget
    // takes the middle of the range instead; see doc/PERF.md.
    const int minStretch = 4;
    const int maxPieces = 8;
    const int capacity = 64;
    DBL budget = ray.GetMediaErrorBudget();
    DBL remaining = 0.0;
    MathColour lo, hi;
    DBL at[kDensityBatch];
    MathColour extinction[kDensityBatch];
    int stack[capacity];

    for(MediaIntervalVector::iterator i(mediaintervals.begin()); i != mediaintervals.end(); i++)
        remaining += i->ds;

    for(MediaIntervalVector::iterator i(mediaintervals.begin()); i != mediaintervals.end(); i++)
    {
        const DBL scale = i->ds / (method3 ? (points - 1) * 1.5 : points);
        auto depth = [&](int j) { return method3 ? i->s0 + i->ds * j / (points - 1) : i->s0 + i->ds * (j + 0.5) / points; };
        auto weight = [&](int j) { return (!method3 || (j == 0) || (j == points - 1) || (j % 2)) ? 1.0 : 2.0; };
        bool whole = (mediaintervals.size() == 1);
        MathColour od;
        int top = 0;
        stack[top++] = 0;
        stack[top++] = points - 1;
        while(top > 0)
        {
            const int b = stack[--top];
            const int a = stack[--top];
            const int n = b - a + 1;
            const DBL weights = method3 ? n + (b / 2 + 1) - ((a + 1) / 2) - (a == 0) - (b == points - 1) : n;
            const DBL length = weights * scale;

            bool ranged = false;
            if(n >= minStretch)
            {
                if(whole)
                {
                    lo = rayLo;
                    hi = rayHi;
                    ranged = true;
                }
                else
                    ranged = plan.Range(ray.Evaluate(depth(a)), ray.Evaluate(depth(b)), lo, hi) && (lo.Min() >= 0.0);
            }
            whole = false;
            if(ranged)
            {
                DBL spread = 0.0;
                for(int ch = 0; ch < MathColour::channels; ch++)
                    spread = max(spread, DBL(hi[ch] - lo[ch]));
                const DBL error = (0.5 * spread + 1e-6 * hi.Max()) * length;
                const DBL allowed = (remaining > length) ? budget * length / remaining : budget;
                if(error <= allowed)
                {
                    od += (lo + hi) * (0.5 * length);
                    budget -= error;
                    remaining -= length;
                    continue;
                }
                // Where extinction is smooth the error shrinks with the square of a stretch's length, its share only linearly.
                const int pieces = int(min(DBL(min(maxPieces, n / minStretch)), ceil(2.0 * error / max(allowed, 1e-300))));
                if((pieces >= 2) && (top + 2 * pieces <= capacity))
                {
                    for(int k = pieces - 1; k >= 0; k--)
                    {
                        stack[top++] = a + (n * k) / pieces;
                        stack[top++] = a + (n * (k + 1)) / pieces - 1;
                    }
                    continue;
                }
            }

            MathColour sum;
            for(int j0 = a; j0 <= b; j0 += int(kDensityBatch))
            {
                const int m = min(b + 1 - j0, int(kDensityBatch));
                for(int k = 0; k < m; k++)
                    at[k] = depth(j0 + k);
                ComputeMediaExtinction(medias, &plan, ray, at, extinction, m);
                for(int k = 0; k < m; k++)
                {
                    sum += extinction[k] * weight(j0 + k);
                }
            }
            od += sum * scale;
            remaining -= length;
        }

        i->od = od;
        i->te.Clear();
        i->samples = 1;
    }
    ray.SetMediaErrorBudget(budget);
}

void MediaFunction::ComputeMediaRegularSampling(MediaVector& medias, LightSourceEntryVector& lights, MediaIntervalVector& mediaintervals,
                                                const Ray& ray, const Media *IMedia, int minsamples, bool ignore_photons, bool use_scattering, bool all_constant_and_light_ray)
{
    if (PreparedRequired(medias, ray))
    {
        threadData->Stats()[Media_Forbidden4_Integration]++;
        throw POV_EXCEPTION_STRING("Declared prepared media reached a legacy integration policy.");
    }

    int j;
    DBL n;
    MathColour Va;
    DBL d0;
    MathColour C0;
    MathColour od0;

    threadData->Stats()[Media_Intervals] += mediaintervals.size();
    for(MediaIntervalVector::iterator i(mediaintervals.begin()); i != mediaintervals.end(); i++)
    {
        const std::uint64_t intervalKey = DeriveKey(drawKey, kDrawMedia, i - mediaintervals.begin());

        // Sample current interval.

        for(j = 0; j < minsamples; j++)
        {
            if(IMedia->Sample_Method == 2)
            {
                d0 = (j + 0.5) / minsamples + (Draw(intervalKey, kDrawMediaSample, j) * IMedia->Jitter / minsamples);
                ComputeOneMediaSample(medias, lights, *i, ray, d0, C0, od0, 2, ignore_photons, use_scattering, false);
            }
            else
            {
                // we may get here with media method 3
                d0 = Draw(intervalKey, kDrawMediaSample, j);
                ComputeOneMediaSample(medias, lights, *i, ray, d0, C0, od0, 1, ignore_photons, use_scattering, false);
            }

            if(all_constant_and_light_ray)
                j = minsamples;
        }
    }

    // Cast additional samples if necessary.
    if((!ray.IsShadowTestRay()) && (IMedia->Max_Samples > minsamples))
    {
        for(MediaIntervalVector::iterator i(mediaintervals.begin()); i != mediaintervals.end(); i++)
        {
            if(i->samples < IMedia->Max_Samples)
            {
                const std::uint64_t intervalKey = DeriveKey(drawKey, kDrawMedia, i - mediaintervals.begin());

                // Get variance of samples.
                n = 1.0 / (DBL)i->samples;

                Va = ((i->te2 * n) - Sqr(i->te * n)) * n;

                // Take additional samples until variance is small enough.
                while(!Va.IsNearZero(IMedia->Sample_Threshold[i->samples - 1]))
                {
                    // Sample current interval again.
                    ComputeOneMediaSample(medias, lights, *i, ray, Draw(intervalKey, kDrawMediaSample, minsamples + i->samples), C0, od0, 1, ignore_photons, use_scattering, false);

                    // Have we reached maximum number of samples.
                    if(i->samples > IMedia->Max_Samples)
                        break;

                    // Get variance of samples.
                    n = 1.0 / (DBL)i->samples;

                    Va = ((i->te2 * n) - Sqr(i->te * n)) * n;
                }
            }
        }
    }
}

DBL MediaFunction::ScatteringStep(MediaVector& medias, const MediaInterval& interval, const Ray& ray) const
{
    DBL step = HUGE_VAL;
    for (const Media *medium : medias)
        if (medium->use_scattering)
            step = std::min(step, medium->Density.empty() || medium->Sample_Method != 4 ? interval.ds / std::max(1, medium->Min_Samples)
                                                          : medium->PreparedStep(ray, interval.s0, interval.s1) / 3.0);
    return step;
}

static std::vector<DBL> ContinuousLightCuts(const MediaInterval& interval, const LightSourceEntryVector& lights)
{
    std::vector<DBL> cuts{interval.s0, interval.s1};
    for (const LightSourceEntry& light : lights)
    {
        if (light.s0 > interval.s0 && light.s0 < interval.s1)
            cuts.push_back(light.s0);
        if (light.s1 > interval.s0 && light.s1 < interval.s1)
            cuts.push_back(light.s1);
    }
    std::sort(cuts.begin(), cuts.end());
    cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());
    return cuts;
}

void MediaFunction::ComputeMediaConstantSampling(MediaVector& medias, LightSourceEntryVector& lights,
                                                  MediaIntervalVector& intervals, const Ray& ray,
                                                  bool use_scattering, bool prepared)
{
    MathColour emission, extinction, scattering;
    ComputeMediaCoefficients(medias, ray, ray.Origin, prepared, emission, extinction, scattering);
    FixedLightPath *path = currentPath && currentPath->collecting && !ray.IsShadowTestRay() && !ray.IsPhotonRay() &&
                          use_scattering && !scattering.IsZero() ? currentPath : nullptr;
    const size_t domain = path ? path->domains.size() : 0;
    if (path)
    {
        path->domains.emplace_back(medias, lights, ray, path->offset);
        auto& description = path->domains.back();
        description.allLights = true;
        description.physicalOrdinary = prepared;
        description.infinite = !IsFiniteNumber(intervals.back().s1);
        if (modifiers)
            description.modifiers = *modifiers;
        for (const Media *medium : medias)
            description.ordinarySamples = std::max(description.ordinarySamples, medium->Min_Samples);
        description.ordinarySamples = std::max(description.ordinarySamples, kMediaAreaLightPoints);
    }
    for (MediaInterval& interval : intervals)
    {
        if (!(interval.ds > 0.0))
        {
            interval.samples = 1;
            continue;
        }
        if (!IsFiniteNumber(interval.ds))
            threadData->Stats()[Media_Analytic_Infinite_Intervals]++;
        const PreparedConstantTransfer transfer = EvaluatePreparedConstantTransfer(extinction, emission, interval.ds);
        interval.te = transfer.emitted;
        for (int ch = 0; ch < MathColour::channels; ch++)
            interval.od[ch] = extinction[ch] == 0.0 ? 0.0 : extinction[ch] * interval.ds;
        interval.samples = 1;
        threadData->Stats()[Media_Intervals]++;
        if (!ray.IsShadowTestRay() && !ray.IsPhotonRay() && use_scattering && !scattering.IsZero() && photonGatherer)
        {
            photonGatherer->map = const_cast<PhotonMap *>(&threadData->GetSceneData()->GetPreparedSet(ray.GetPreparedSetId()).mediaPhotonMap);
            if (photonGatherer->map->numPhotons > 0 || threadData->GetSceneData()->photonSettings.Progressive())
            {
                int count = 1;
                DBL rate = HUGE_VAL;
                for (const Media *medium : medias)
                    count = std::max(count, medium->Min_Samples);
                for (int ch = 0; ch < MathColour::channels; ch++)
                    if (scattering[ch] != 0.0)
                        rate = std::min(rate, DBL(extinction[ch]));
                const bool importance = IsFiniteNumber(rate) && rate > 0.0;
                const DBL scale = importance ? 1.0 / rate : 1.0;
                const std::uint64_t key = DeriveKey(drawKey, kDrawGather, &interval - &intervals[0]);
                for (int sample = 0; sample < count; sample++)
                {
                    const DBL u = (sample + Draw(key, kDrawMediaSample, sample)) / count;
                    PreparedDistanceSample position = importance && u >= 0.5 ?
                        SamplePreparedExponentialDistance(interval.s0, interval.s1, 2.0 * u - 1.0, rate) :
                        SamplePreparedDistance(interval.s0, interval.s1, importance ? 2.0 * u : u, scale);
                    const DBL at = position.distance;
                    DBL pdf = IsFiniteNumber(interval.s1) ? 1.0 / interval.ds :
                        (scale + interval.s0) / (scale + at) / (scale + at);
                    if (importance)
                        pdf = 0.5 * pdf + 0.5 * PreparedExponentialDistancePdf(interval.s0, interval.s1, at, rate);
                    MathColour indirect, attenuation;
                    ComputeMediaPhotons(medias, indirect, scattering, ray, ray.Evaluate(at), prepared);
                    for (int ch = 0; ch < MathColour::channels; ch++)
                        attenuation[ch] = std::exp(-DBL(extinction[ch]) * (at - interval.s0));
                    interval.te += indirect * attenuation / (count * pdf);
                }
            }
        }
        if (path && interval.lit)
        {
            const auto cuts = ContinuousLightCuts(interval, lights);
            for (size_t piece = 1; piece < cuts.size(); piece++)
            {
            const DBL width = cuts[piece] - cuts[piece - 1];
            if (!(width > 0.0))
                continue;
            FixedLightPath::Cell cell;
            cell.domain = domain;
            cell.firstLight = interval.l0;
            cell.lastLight = interval.l1;
            cell.start = cuts[piece - 1];
            cell.end = cuts[piece];
            cell.at = IsFiniteNumber(width) ? cell.start + width * 0.5 : std::nextafter(cell.start, cell.end);
            cell.width = IsFiniteNumber(width) ? width : 1.0;
            cell.scattering = scattering;
            cell.extinction = extinction;
            cell.continuous = true;
            for (int ch = 0; ch < MathColour::channels; ch++)
            {
                const DBL integral = extinction[ch] > 0.0 ?
                    -std::expm1(-DBL(extinction[ch]) * width) / extinction[ch] : width;
                cell.transfer[ch] = std::exp(-DBL(extinction[ch]) * cell.start) * integral;
            }
            path->cells.push_back(cell);
            }
        }
    }
}

void MediaFunction::CollectLegacyLightIntervals(MediaVector& medias, LightSourceEntryVector& lights,
                                                 const MediaIntervalVector& intervals, const Ray& ray)
{
    if (!currentPath || !currentPath->collecting || ray.IsShadowTestRay() || ray.IsPhotonRay())
        return;
    bool volume = false;
    for (const LightSourceEntry& entry : lights)
        volume = volume || bool(entry.light->emitter);
    if (!volume)
        return;
    const size_t domain = currentPath->domains.size();
    currentPath->domains.emplace_back(medias, lights, ray, currentPath->offset);
    auto& description = currentPath->domains.back();
    description.infinite = !IsFiniteNumber(intervals.back().s1);
    if (modifiers)
        description.modifiers = *modifiers;
    for (const MediaInterval& interval : intervals)
    {
        if (!(interval.ds > 0.0) || !interval.lit)
            continue;
        const auto cuts = ContinuousLightCuts(interval, lights);
        for (size_t piece = 1; piece < cuts.size(); piece++)
        {
        const DBL width = cuts[piece] - cuts[piece - 1];
        if (!(width > 0.0))
            continue;
        FixedLightPath::Cell cell;
        cell.domain = domain;
        cell.firstLight = interval.l0;
        cell.lastLight = interval.l1;
        cell.start = cuts[piece - 1];
        cell.end = cuts[piece];
        cell.at = IsFiniteNumber(width) ? cell.start + width * 0.5 : std::nextafter(cell.start, cell.end);
        cell.width = IsFiniteNumber(width) ? width : 1.0;
        cell.continuous = cell.dynamic = true;
        MathColour emitted;
        ComputeMediaCoefficients(medias, ray, ray.Evaluate(cell.at), false, emitted, cell.extinction, cell.scattering);
        for (int ch = 0; ch < MathColour::channels; ch++)
            cell.transfer[ch] = cell.width * std::exp(-DBL(cell.extinction[ch]) * cell.at);
        currentPath->cells.push_back(cell);
        }
    }
}

void MediaFunction::ComputeMediaFixedSampling(MediaVector& medias, LightSourceEntryVector& lights, MediaIntervalVector& mediaintervals,
                                              const Ray& ray, DBL resolution, bool ignore_photons, bool use_scattering, DBL coarsening)
{
    FixedLightPath *path = (currentPath && currentPath->collecting && use_scattering && !ray.IsPhotonRay()) ? currentPath : nullptr;
    bool volume = false;
    for (const LightSourceEntry& entry : lights)
        volume = volume || (entry.light->emitter != nullptr);
    if (!volume)
        path = nullptr;
    const size_t domain = path ? path->domains.size() : 0;
    if (path)
    {
        path->domains.emplace_back(medias, lights, ray, path->offset);
        if (modifiers)
            path->domains.back().modifiers = *modifiers;
    }
    MathColour previousDepth;
    // A stride's light is gathered once, unscattered; each step's own media scatter it.
    std::vector<MediaIncident> incident;
    std::vector<MathColour> stepDensities;
    for (MediaInterval& interval : mediaintervals)
    {
        if (interval.ds <= 0.0)
        {
            interval.samples = 1;
            continue;
        }
        const DBL width = PreparedStep(medias, ray, interval, resolution, coarsening);
        const int count = std::max(1, int(ceil(interval.ds / width)));
        const DBL step = interval.ds / count;
        if (step <= 0.0)
        {
            interval.samples = 1;
            continue;
        }
        MathColour accumulated, transmitted;
        // Scattering media coarser than the step take media lights' light once per stride of steps, at a step drawn within it.
        bool mediaLit = false;
        for (size_t l = interval.l0; interval.lit && (l <= interval.l1) && (l < lights.size()); l++)
            mediaLit = mediaLit || (lights[l].light->emitter != nullptr);
        int stride = 1;
        if (mediaLit && use_scattering && (modifiers == nullptr) && !ray.IsPhotonRay() &&
            ((photonGatherer == nullptr) ||
             (threadData->GetSceneData()->GetPreparedSet(ray.GetPreparedSetId()).mediaPhotonMap.numPhotons == 0)))
            stride = std::max(1, std::min(count, int(ScatteringStep(medias, interval, ray) / step)));
        struct Cell final
        {
            DBL at;
            MathColour emission, extinction, scattering;
        };
        vector<Cell> cells(count);
        if (path)
            path->temporaryBytes = std::max(path->temporaryBytes, cells.capacity() * sizeof(Cell));
        // A stride's steps scatter with the densities their coefficients read; a shadow ray reads only extinction.
        const size_t mediaCount = medias.size();
        const bool keepDensities = (stride > 1) && !ray.IsShadowTestRay();
        stepDensities.resize(keepDensities ? size_t(count) * mediaCount : 0);
        for (int j = 0; j < count; j++)
        {
            Cell& cell = cells[j];
            cell.at = interval.s0 + j * step + 0.5 * step;
            ComputeMediaCoefficients(medias, ray, ray.Evaluate(cell.at), true,
                                     cell.emission, cell.extinction, cell.scattering,
                                     keepDensities ? &stepDensities[size_t(j) * mediaCount] : nullptr);
        }
        int lit = -1;
        for (int j = 0; j < count; j++)
        {
            MediaInterval cell(interval.lit, 0, interval.s0 + j * step, interval.s0 + (j + 1) * step,
                               step, interval.l0, interval.l1);
            const Cell& coefficients = cells[j];
            MathColour emission = coefficients.emission;
            const MathColour depth = coefficients.extinction * step;
            const MathColour& scattering = coefficients.scattering;
            ComputeMediaSampleLighting(medias, lights, cell, ray, coefficients.at, ray.Evaluate(coefficients.at),
                                      scattering, emission, true, use_scattering && (stride == 1), path == nullptr);
            emission *= step;
            if (path && cell.lit && !scattering.IsZero())
            {
                FixedLightPath::Cell sample;
                sample.domain = domain;
                sample.firstLight = cell.l0;
                sample.lastLight = cell.l1;
                sample.at = coefficients.at;
                sample.width = step;
                sample.scattering = scattering;
                sample.transfer = step * Exp(-(transmitted + depth * 0.5)) * Exp(-previousDepth);
                path->cells.push_back(sample);
            }
            if (stride > 1)
            {
                const int group = j / stride, first = group * stride, size = std::min(stride, count - first);
                if (group != lit)
                {
                    const int at = first + std::min(size - 1, int(Draw(drawKey, kDrawMediaSample, kMediaLightDraw + group) * size));
                    const DBL d = interval.s0 + (at + 0.5) * step;
                    incident.clear();
                    threadData->Stats()[Media_Stride_Lights]++;
                    MathColour unused;
                    ComputeMediaLight(medias, lights, cell, ray, d, ray.Evaluate(d), MathColour(1.0), unused, path == nullptr, true,
                                      &incident);
                    lit = group;
                }
                if (!incident.empty() && !scattering.IsZero())
                {
                    MathColour scattered;
                    ScatterIncident(medias, scattered, incident.data(), incident.size(), ray.Evaluate(coefficients.at), ray,
                                    keepDensities ? &stepDensities[size_t(j) * mediaCount] : nullptr);
                    emission += scattered * step;
                }
                if (!ignore_photons && photonGatherer && !scattering.IsZero() &&
                    threadData->GetSceneData()->photonSettings.Progressive())
                {
                    photonGatherer->map = const_cast<PhotonMap *>(&threadData->GetSceneData()->GetPreparedSet(ray.GetPreparedSetId()).mediaPhotonMap);
                    MathColour indirect;
                    ComputeMediaPhotons(medias, indirect, scattering, ray, ray.Evaluate(cell.s0 + step * 0.5), true);
                    emission += indirect * step;
                }
            }
            accumulated += emission * Exp(-(transmitted + depth * 0.5));
            transmitted += depth;
        }
        interval.te = accumulated;
        interval.od = transmitted;
        previousDepth += transmitted;
        interval.samples = 1;
        threadData->Stats()[Media_Intervals]++;
    }
}

void MediaFunction::ComputeMediaAdaptiveSampling(MediaVector& medias, LightSourceEntryVector& lights, MediaIntervalVector& mediaintervals,
                                                 const Ray& ray, const Media *IMedia, DBL aa_threshold, int minsamples, bool ignore_photons, bool use_scattering)
{
    if (PreparedRequired(medias, ray))
    {
        threadData->Stats()[Media_Forbidden4_Integration]++;
        throw POV_EXCEPTION_STRING("Declared prepared media reached a legacy integration policy.");
    }

    // adaptive sampling
    int subIntervalCount;
    int j;
    DBL d0, d1, dd;
    MathColour C0, C1, Result;
    MathColour ODResult;
    MathColour od0, od1;

    for(MediaIntervalVector::iterator i(mediaintervals.begin()); i != mediaintervals.end(); i++)
    {
        // Sample current interval.

        threadData->Stats()[Media_Intervals]++;

        subIntervalCount = (minsamples + 1) / 2;

        // TODO - if minsamples is guaranteed to be >=1, the following is redundant:
        if(subIntervalCount < 1)
            subIntervalCount = 1;

        dd = 1.0 / (DBL)subIntervalCount;
        const std::uint64_t intervalKey = DeriveKey(drawKey, kDrawMedia, i - mediaintervals.begin());

        ComputeOneMediaSample(medias, lights, *i, ray, (IMedia->Jitter == 0.0) ? 0.0 : dd * IMedia->Jitter * (Draw(intervalKey, kDrawMediaSample, 0) - 0.5), C0, od0, 3, ignore_photons, use_scattering, false);

        // clear out od & te
        i->te.Clear();
        i->od.Clear();

        d0 = 0.0;
        for(j = 1; j <= subIntervalCount; j++)
        {
            d1 = d0 + dd;
            ComputeOneMediaSample(medias, lights, *i, ray, (IMedia->Jitter == 0.0) ? d1 : d1 + dd * IMedia->Jitter * (Draw(intervalKey, kDrawMediaSample, j) - 0.5), C1, od1, 3, ignore_photons, use_scattering, false);
            ComputeOneMediaSampleRecursive(medias, lights, *i, ray, d0, d1, Result, C0, C1, ODResult, od0, od1, IMedia->AA_Level - 1,
                                           IMedia->Jitter, aa_threshold, ignore_photons, use_scattering, false,
                                           (IMedia->Jitter == 0.0) ? 0 : DeriveKey(intervalKey, kDrawMedia, j));

            // keep a sum of the results
            // do some attenuation, too, since we are doing samples in order
            // TODO - we could do even better if we handled attenuation on a per-sample basis

            // Compute attenuation due to earlier sub-intervals.
            Result *= Exp(-(i->od) * dd);
            // Compute attenuation due to the sub-interval itself.
            // NB: This formula is mathematically precise under the presumption that the ratio of emission to absorbtion
            // remains constant throughout the entire sub-interval.
            for (int iChannel = 0; iChannel < Result.channels; ++iChannel)
            {
                if (ODResult[iChannel] != 0.0)
                    Result[iChannel] *= (1.0 - exp(-ODResult[iChannel] * dd)) / (ODResult[iChannel] * dd);
            }
            i->te += Result;

            // move c1 to c0 to go on to next sample/interval
            C0 = C1;

            // now do the same for optical depth
            i->od += ODResult;

            // move od1 to od0 to go on to the next sample/interval
            od0 = od1;

            d0 = d1;
        }

        i->samples = subIntervalCount;
    }
}

void MediaFunction::ComputeMediaColour(MediaIntervalVector& mediaintervals, MathColour& colour, ColourChannel& transm)
{
    MathColour Od, Te;
    DBL n;

    // Sum the influences of all intervals.
    for(MediaIntervalVector::iterator i(mediaintervals.begin()); i != mediaintervals.end(); i++)
    {
        n = 1.0 / (DBL)i->samples;

        // Add total emission.
        Te += i->te * n * Exp(-Od);

        // Add optical depth of ient interval.
        Od += i->od * n;
    }

    // Add contribution estimated for the participating media.
    Od = Exp(-Od);

    colour = colour * Od + Te;
    transm *= Od.Greyscale(); // TODO - in the long run, we should make transm a full-fledged RGB term
}

void MediaFunction::ComputeMediaSampleInterval(LitIntervalVector& litintervals, MediaIntervalVector& mediaintervals, const Media *media,
                                               bool fixed)
{
    size_t i, j, n, r, remaining, intervals;
    DBL delta, sum, weight;

    // Set up sampling intervals.
    //
    // NK samples - we will always have enough intervals
    // we always use the larger of the two numbers
    intervals = max(fixed ? size_t(1) : size_t(media->Intervals), litintervals.size());

    // Choose intervals.
    if(litintervals.size() == 1)
    {
        // Use one interval if no lit intervals and constant media.
        if((litintervals[0].lit == false) && (media->is_constant == true))
        {
            mediaintervals.push_back(MediaInterval(false, 0,
                                                   litintervals[0].s0,
                                                   litintervals[0].s1,
                                                   litintervals[0].ds,
                                                   0, 0));
        }
        else // Use uniform intervals.
        {
            delta = litintervals[0].ds / (DBL)intervals;

            for(i = 0; i < intervals; i++)
            {
                mediaintervals.push_back(MediaInterval(litintervals[0].lit, 0,
                                                       litintervals[0].s0 + delta * (DBL)i,
                                                       litintervals[0].s0 + delta * (DBL)(i + 1),
                                                       delta,
                                                       litintervals[0].l0, litintervals[0].l1));
            }
        }
    }
    else // Choose intervals according to the specified ratio.
    {
        sum = 0.0;

        for(i = 0; i < litintervals.size(); i++)
            sum += ((litintervals[i].lit) ? (media->Ratio) : (1.0 - media->Ratio));

        remaining = intervals;

        for(i = 0; i < litintervals.size(); i++)
        {
            weight = ((litintervals[i].lit) ? (media->Ratio) : (1.0 - media->Ratio));
            n = size_t(weight / sum * (DBL)intervals) + 1;
            r = remaining - litintervals.size() + i + 1;

            if(n > r)
                n = r;

            delta = litintervals[i].ds / (DBL)n;

            for (j = 0; j < n; j++)
            {
                mediaintervals.push_back(MediaInterval(litintervals[i].lit, 0,
                                                       litintervals[i].s0 + delta * (DBL)j,
                                                       litintervals[i].s0 + delta * (DBL)(j + 1),
                                                       delta,
                                                       litintervals[i].l0, litintervals[i].l1));
            }

            remaining -= n;
        }
    }
}

void MediaFunction::ComputeMediaLightInterval(LightSourceEntryVector& lights, LitIntervalVector& litintervals, const Ray& ray, const Intersection& isect)
{
    const PreparedSet& view = threadData->GetSceneData()->GetPreparedSet(ray.GetPreparedSetId());
    if ((isect.Object == nullptr) || ((isect.Object->Flags & NO_GLOBAL_LIGHTS_FLAG) != NO_GLOBAL_LIGHTS_FLAG))
        for (unsigned int i : view.globalLights)
            if (threadData->lightSources[i]->Media_Interaction)
                ComputeOneMediaLightInterval(threadData->lightSources[i], lights, ray, isect);
    if (isect.Object != nullptr)
        for (LightSource *light : isect.Object->LLights)
            if (light->Media_Interaction && view.UsesGroupLight(light->index))
                ComputeOneMediaLightInterval(light, lights, ray, isect);

    if(lights.empty() == false)
    {
#if 1
        // Using thread storage duration for the following temporary lists to avoid repeated
        // cycles of allocation, construction, upsizing and destruction. Of course we still
        // need to make sure we start with a clean slate each time around. We also set an
        // initial minimum capacity.
        thread_local POV_SIMPLE_VECTOR<DBL> s0;
        thread_local POV_SIMPLE_VECTOR<DBL> s1;
        s0.clear();
        s1.clear();
        s0.reserve(LIGHTSOURCE_VECTOR_SIZE);
        s1.reserve(LIGHTSOURCE_VECTOR_SIZE);

        for (LightSourceEntryVector::iterator i (lights.begin()); i != lights.end(); i++)
        {
            s0.push_back(i->s0);
            s1.push_back(i->s1);
        }
        std::sort(s0.begin(), s0.end());
        std::sort(s1.begin(), s1.end());

        if (s0[0] > 0.0)
            litintervals.push_back(LitInterval(false, 0.0, s0[0], 0, lights.size() - 1));
        litintervals.push_back(LitInterval(true, s0[0], s1[0], 0, lights.size() - 1));
        for (int i = 1; i < lights.size(); i++)
        {
            if (s0[i] > litintervals.back().s1)
            {
                litintervals.push_back(LitInterval(false, litintervals.back().s1, s0[i], 0, lights.size() - 1));
                litintervals.push_back(LitInterval(true, s0[i], s1[i], 0, lights.size() - 1));
            }
            else
            {
                if (s1[i] > litintervals.back().s1)
                    litintervals.back().s1 = s1[i];
            }
        }

        if (litintervals.back().s1 < isect.Depth)
            litintervals.push_back(LitInterval(false, litintervals.back().s1, isect.Depth, 0, lights.size() - 1));
        for (LitIntervalVector::iterator i(litintervals.begin()); i != litintervals.end(); i++)
            i->ds = i->s1 - i->s0;
#else
        // After sorting the following holds true for the whole array:
        // l[i].s <= l[i + 1].s
        // Where i is the index and s is the start of the interval
        // lit by the light source in the array l.
        sort(lights.begin(), lights.end());

        LightSourceIntersectionVector lsie;

        for(size_t i = 0; i < lights.size(); i++)
        {
            lsie.push_back(LightSourceIntersectionEntry(lights[i].s0, i, true));
            lsie.push_back(LightSourceIntersectionEntry(lights[i].s1, i, false));
        }

        sort(lsie.begin(), lsie.end());

        // TODO - Everything below this line can be merged such that no LitIntervals are needed
        // because ComputeMediaLightInterval just iterates over this LitIntervals with ++ and
        // thus we can generate them on the fly and do not need the temporary storage for all
        // the LitIntervals! [trf]

        // if there is at least one interval (two values in lsie)
        if(lsie.size() > 1)
        {
            size_t lits = 0;
            if(lsie[0].lit == true)
                lits++;
            for(size_t i = 1, maxl = 0, minl = lsie[0].l; i < lsie.size(); i++)
            {
                maxl = max(maxl, lsie[i].l);
                litintervals.push_back(LitInterval(lits > 0, lsie[i - 1].s, lsie[i].s, minl, maxl));
                if(lsie[i].lit == false)
                    lits--;
                else
                {
                    if(lits == 0)
                        minl = lsie[i].l;
                    lits++;
                }
            }
        }
#endif
    }
}

void MediaFunction::ComputeOneMediaLightInterval(LightSource *light, LightSourceEntryVector&lights, const Ray& ray, const Intersection& isect)
{
    LightSourceEntry lse;
    DBL t1 = 0.0, t2 = 0.0;
    bool insert = false;

    lse.light = light;

    // Init interval.
    lse.s0 = 0.0;
    lse.s1 = MAX_DISTANCE;

    switch(light->Light_Type)
    {
        case CYLINDER_SOURCE:
            if(ComputeCylinderLightInterval(ray, light, &t1, &t2))
                insert = ((t1 < isect.Depth) && (t2 > SMALL_TOLERANCE));
            break;
        case POINT_SOURCE:
            t1 = 0.0;
            t2 = isect.Depth;
            insert = true;
            break;
        case SPOT_SOURCE:
            if(ComputeSpotLightInterval(ray, light, &t1, &t2))
                insert = ((t1 < isect.Depth) && (t2 > SMALL_TOLERANCE));
            break;
    }

    if(insert == true)
    {
        lse.s0 = max(t1, 0.0);
        lse.s1 = min(t2, isect.Depth);

        lights.push_back(lse);
    }
}

bool MediaFunction::ComputeSpotLightInterval(const Ray &ray, const LightSource *Light, DBL *d1, DBL *d2)
{
    int viewpoint_is_in_cone;
    DBL a, b, c, d, m, l, l1, l2, t, t1, t2, k1, k2, k3, k4;
    Vector3d V1;

    // Get cone's slope. Note that cos(falloff) is stored in Falloff!
    m = 1 / (Light->Falloff * Light->Falloff);

    V1 = ray.Origin - Light->Center;
    k1 = dot(ray.Direction, Light->Direction);
    k2 = dot(V1, Light->Direction);
    l = V1.length();

    if(l > EPSILON)
        viewpoint_is_in_cone = (k2 / l >= Light->Falloff);
    else
        viewpoint_is_in_cone = false;

    if((k1 <= 0.0) && (k2 < 0.0))
        return false;

    k3 = dot(V1, ray.Direction);
    k4 = V1.lengthSqr();

    a = 1.0 - Sqr(k1) * m;
    b = k3 - k1 * k2 * m;
    c = k4 - Sqr(k2) * m;

    if(a != 0.0)
    {
        d = Sqr(b) - a * c;

        if(d > EPSILON)
        {
            d = sqrt(d);

            t1 = (-b + d) / a;
            t2 = (-b - d) / a;

            if(t1 > t2)
            {
                t = t1;
                t1 = t2;
                t2 = t;
            }

            l1 = k2 + t1 * k1;
            l2 = k2 + t2 * k1;

            if((l1 <= 0.0) && (l2 <= 0.0))
                return false;

            if((l1 <= 0.0) || (l2 <= 0.0))
            {
                if(l1 <= 0.0)
                {
                    if(viewpoint_is_in_cone)
                    {
                        t1 = 0.0;
                        t2 = (t2 > 0.0) ? (t2) : (MAX_DISTANCE);
                    }
                    else
                    {
                        t1 = t2;
                        t2 = MAX_DISTANCE;
                    }
                }
                else
                {
                    if(viewpoint_is_in_cone)
                    {
                        t2 = t1;
                        t1 = 0.0;
                    }
                    else
                        t2 = MAX_DISTANCE;
                }
            }

            *d1 = t1;
            *d2 = t2;

            return true;
        }
        else if(d > -EPSILON)
        {
            if(viewpoint_is_in_cone)
            {
                *d1 = 0.0;
                *d2 = -b / a;
            }
            else
            {
                *d1 = -b / a;
                *d2 = MAX_DISTANCE;
            }

            return true;
        }
    }
    else if(viewpoint_is_in_cone)
    {
        *d1 = 0.0;
        *d2 = -c/b;

        return true;
    }

    return false;
}

bool MediaFunction::ComputeCylinderLightInterval(const Ray &ray, const LightSource *Light, DBL *d1, DBL *d2)
{
    DBL a, b, c, d, l1, l2, t, t1, t2, k1, k2, k3, k4;
    Vector3d V1;

    V1 = ray.Origin - Light->Center;
    k1 = dot(ray.Direction, Light->Direction);
    k2 = dot(V1, Light->Direction);

    if((k1 <= 0.0) && (k2 < 0.0))
        return false;

    a = 1.0 - Sqr(k1);

    if(a != 0.0)
    {
        k3 = dot(V1, ray.Direction);
        k4 = V1.lengthSqr();

        b = k3 - k1 * k2;
        c = k4 - Sqr(k2) - Sqr(Light->Falloff);
        d = Sqr(b) - a * c;

        if(d > EPSILON)
        {
            d = sqrt(d);

            t1 = (-b + d) / a;
            t2 = (-b - d) / a;

            if(t1 > t2)
            {
                t = t1;
                t1 = t2;
                t2 = t;
            }

            l1 = k2 + t1 * k1;
            l2 = k2 + t2 * k1;

            if((l1 <= 0.0) && (l2 <= 0.0))
                return false;

            if((l1 <= 0.0) || (l2 <= 0.0))
            {
                if(l1 <= 0.0)
                    t1 = 0.0;
                else
                    t2 = (MAX_DISTANCE - k2) / k1;
            }

            *d1 = t1;
            *d2 = t2;

            return true;
        }
    }

    return false;
}

/*****************************************************************************
* INPUT
*   dist  - distance of current sample
*   Ray   - pointer to ray
*   IMedia - pointer to media to use
* OUTPUT
*   Col          - color of current sample
******************************************************************************/

void MediaFunction::ComputeMediaCoefficients(MediaVector& medias, const Ray& ray, const Vector3d& H, bool prepared,
                                              MathColour& Emission, MathColour& Extinction, MathColour& Scattering,
                                              MathColour *densities)
{
    const bool appearance = !ray.IsShadowTestRay() && !ray.IsRadiosityRay() && !ray.IsPhotonRay();
    MathColour C0;
    threadData->Stats()[Media_Samples]++;
    threadData->Stats()[ray.IsShadowTestRay() ? Media_Shadow_Coefficients : ray.IsPhotonRay() ? Media_Photon_Coefficients :
                        prepared ? Media_Fixed_Coefficients : Media_Classic_Coefficients]++;
    // Get coefficients in current sample location.
    if (modifiers != nullptr)
        densityScratch.clear();
    for(MediaVector::iterator i(medias.begin()); i != medias.end(); i++)
    {
        const Vector3d& P = H;
        if (!MediaAffectsTransport(**i, ray))
        {
            if (modifiers)
                densityScratch.push_back(MathColour());
            if (densities != nullptr)
                densities[i - medias.begin()] = MathColour();
            continue;
        }
        DensityForDeclaredMode(**i, P, C0);
        if (densities != nullptr)
            densities[i - medias.begin()] = C0;

        if (modifiers != nullptr)
        {
            densityScratch.push_back(C0);
            continue;
        }

        Extinction += C0 * (*i)->Extinction;

        if(!ray.IsShadowTestRay())
        {
            if (appearance)
                Emission += C0 * (*i)->Emission;
            Scattering += C0 * (*i)->Scattering;
        }
    }
    if (modifiers != nullptr)
        AddModifiedCoefficients(medias, &densityScratch[0], H, prepared ? ModifierSource::kCamera : ModifierSource::kPattern,
                                Extinction, appearance ? &Emission : nullptr,
                                ray.IsShadowTestRay() ? nullptr : &Scattering);
}

void MediaFunction::ComputeMediaSampleLighting(MediaVector& medias, LightSourceEntryVector& lights,
                                                const MediaInterval& mediainterval, const Ray& ray, DBL d1,
                                                const Vector3d& H, const MathColour& Scattering, MathColour& Emission,
                                                bool prepared, bool use_scattering, bool volumeLighting)
{
    const bool emptyScattering = prepared && Scattering.IsZero();
    if (emptyScattering && !ray.IsShadowTestRay() && use_scattering && !ray.IsPhotonRay() && mediainterval.lit)
        lightSampleIndex++;
    if(!ray.IsShadowTestRay() && use_scattering && !ray.IsPhotonRay() && !emptyScattering)
    {
        if(mediainterval.lit)
            ComputeMediaLight(medias, lights, mediainterval, ray, d1, H, Scattering, Emission, volumeLighting, prepared);

        // process media photons whether or not the interval is directly lit
        if (photonGatherer != nullptr)
            photonGatherer->map = const_cast<PhotonMap *>(&threadData->GetSceneData()->GetPreparedSet(ray.GetPreparedSetId()).mediaPhotonMap);
        if((photonGatherer != nullptr) && (photonGatherer->map->numPhotons > 0 ||
           threadData->GetSceneData()->photonSettings.Progressive()))
        {
            ComputeMediaPhotons(medias, Emission, Scattering, ray, H, prepared);
        }
    }
}

void MediaFunction::ComputeOneMediaSample(MediaVector& medias, LightSourceEntryVector& lights, MediaInterval& mediainterval, const Ray &ray, DBL d0, MathColour& SampCol,
                                          MathColour& SampOptDepth, int sample_method, bool ignore_photons, bool use_scattering, bool photonPass, bool prepared,
                                          MathColour *scattering)
{
    d0 *= mediainterval.ds;
    const DBL d1 = mediainterval.s0 + d0;
    const Vector3d H = ray.Evaluate(d1);
    MathColour Emission, Extinction, Scattering;
    ComputeMediaCoefficients(medias, ray, H, prepared, Emission, Extinction, Scattering);

    // Get estimate for the total optical depth of the current interval.
    SampOptDepth = Extinction * mediainterval.ds;
    if (scattering != nullptr)
        *scattering = Scattering;

    if(sample_method != 3)
        mediainterval.od += SampOptDepth;

    ComputeMediaSampleLighting(medias, lights, mediainterval, ray, d1, H, Scattering, Emission, prepared, use_scattering);

    if(sample_method == 3)
    {
        // We're doing the samples in order, so we can attenuate correctly
        // instead of assuming a constant absorption/extinction.
        // Therefore, we do the attenuation later (back up in Simulate_Media).
        Emission *=  mediainterval.ds;
    }
    else
    {
        // NOTE: this assumes constant absorption+extinction over the length of the interval
        Emission *=  mediainterval.ds * Exp(-Extinction * d0);
    }

    SampCol = Emission;

    if(sample_method != 3)
    {
        // Add emission.
        mediainterval.te  += Emission;
        mediainterval.te2 += Sqr(Emission);
    }

    mediainterval.samples++;
}

void MediaFunction::ComputeMediaLight(MediaVector& medias, LightSourceEntryVector& lights, const MediaInterval& mediainterval, const Ray& ray,
                                      DBL d1, const Vector3d& P, const MathColour& Scattering, MathColour& Emission, bool volumeLighting, bool preparedSampling,
                                      std::vector<MediaIncident>* incident)
{
    if (ray.IsShadowTestRay())
        threadData->Stats()[Media_Shadow_Lighting_Calls]++;
    DBL len;
    MathColour Light_Colour;
    Ray Light_Ray(ray);
    Light_Ray.hasDifferentials = false;

    // ComputeShadowColour reads whether every medium here ignores photons
    threadData->litObjectIgnoresPhotons = true;
    for(MediaVector::iterator i(medias.begin()); i != medias.end(); i++)
    {
        if(!(*i)->ignore_photons)
        {
            threadData->litObjectIgnoresPhotons = false;
            break;
        }
    }

    // Draws are scattered together, so the media's densities at P are evaluated once, in the same order of sums.
    const size_t base = pointIncident.size();
    std::vector<MediaIncident>& gathered = incident ? *incident : pointIncident;
    const auto scatter = [&]() {
        if (pointIncident.size() > base)
            ScatterIncident(medias, Emission, pointIncident.data() + base, pointIncident.size() - base, P, ray);
        pointIncident.resize(base);
    };

    // Area lights: a few points per sample, spread over the light along the ray (an R2 sequence).
    const double k = lightSampleIndex;
    Light_Ray.SetKey(DeriveKey(drawKey, kDrawMediaSample, lightSampleIndex++));

    // Process all light sources.
    for(size_t i = mediainterval.l0; i <= mediainterval.l1; i++)
    {
        // Use light only if active and within it's boundaries.
        if((d1 >= lights[i].s0) && (d1 <= lights[i].s1))
        {
            if(lights[i].light->Area_Light && (lightSampleShift[U] < 0.0))
                lightSampleShift = Vector2d(Draw(drawKey, kDrawMediaAreaShift, 0), Draw(drawKey, kDrawMediaAreaShift, 1));
            if (lights[i].light->emitter)
            {
                if (!volumeLighting || (currentPath && currentPath->collecting) || Light_Below_Cutoff(lights[i].light, P))
                    continue;
                const int count = max(1, lights[i].light->Area_Size1);
                threadData->Stats()[preparedSampling ? Emitter_Media_Fixed_Draws : Emitter_Media_Classic_Draws] += count;
                if (Scattering.IsZero())
                    threadData->Stats()[Media_Zero_Scatter_Emitter_Draws] += count;
                const std::uint64_t key = DeriveKey(DeriveKey(drawKey, kDrawMediaSample, lightSampleIndex - 1), kDrawAreaLight, i);
                const PhotonSobol sequence(key);
                for (int j = 0; j < count; j++)
                {
                    const EmitterDraw draw(EmitterDraw::JitterSelection(sequence(j, 0), count, Draw(key, kDrawEmitter, j)),
                                           sequence(j, 1), sequence(j, 2), sequence(j, 3),
                                           DeriveKey(DeriveKey(key, kDrawEmitter, j), kDrawEmitter, UINT64_C(0x706f736974696f6e)));
                    Light_Ray.SetKey(DeriveKey(key, kDrawShadow, j));
                    if (!trace->SampleEmitterConnection(*lights[i].light, P, Light_Ray, len, Light_Colour, draw))
                        continue;
                    gathered.push_back({Light_Colour / (4.0 * M_PI * count), Light_Ray.Direction});
                }
                continue;
            }
            const int points = lights[i].light->Area_Light ? kMediaAreaLightPoints : 1;
            threadData->Stats()[Media_Ordinary_Light_Draws] += points;
            MathColour Lit_Colour;
            for(int j = 0; j < points; j++)
            {
                const double kj = k * points + j;
                const double su = lightSampleShift[U] + kj * 0.7548776662466927 + i * 0.6180339887498949;
                const double sv = lightSampleShift[V] + kj * 0.5698402909980532 + i * 0.4142135623730950;
                const Vector2d areaSample(su - floor(su), sv - floor(sv));
                Light_Ray.SetKey(DeriveKey(DeriveKey(drawKey, kDrawMediaSample, lightSampleIndex - 1), kDrawEmitter, i * points + j));
                if (!(trace->TestShadow(*lights[i].light, len, Light_Ray, P, Light_Colour, &areaSample)))
                {
                    if (preparedSampling)
                        gathered.push_back({Light_Colour / (4.0 * points), Light_Ray.Direction});
                    else
                        Lit_Colour += Light_Colour;
                }
            }
            if(!Lit_Colour.IsZero())
            {
                scatter();
                ComputeMediaScatteringAttenuation(medias, Emission, Scattering, Lit_Colour / points, ray, Light_Ray);
            }
        }
    }
    scatter();
}

void MediaFunction::ComputeOneMediaSampleRecursive(MediaVector& medias, LightSourceEntryVector& lights, MediaInterval& mediainterval, const Ray& ray,
                                                   DBL d1, DBL d3, MathColour& Result, const MathColour& C1, const MathColour& C3, MathColour& ODResult, const MathColour& od1, const MathColour& od3,
                                                   int depth, DBL Jitter, DBL aa_threshold, bool ignore_photons, bool use_scattering, bool photonPass, std::uint64_t key)
{
    MathColour C2, Result2;
    MathColour od2, ODResult2;
    DBL d2, jdist;

    // d2 is between d1 and d3 (all in range of 0..1
    d2 = 0.5 * (d1 + d3);
    jdist = (Jitter == 0.0) ? d2 : d2 + Jitter * (d3 - d1) * (Draw(key, kDrawMediaSample, 0) - 0.5);

    ComputeOneMediaSample(medias, lights, mediainterval, ray, jdist, C2, od2, 3, ignore_photons, use_scattering, photonPass);

    // TODO FIXME - this gives C1, C2 and C3 a weigt of 1:1:1,
    // which is no good as C1 and C3 are on the border of the interval, and may influence the neighboring interval as well.
    // (see individual comments for how to fix this.)

    // if we're at max depth, then let's just use this last sample and average it with the two end points
    if(depth <= 0)
    {
        // average colors & optical depth
        Result   = (C1  + C2  + C3)  / 3.0;
        ODResult = (od1 + od2 + od3) / 3.0;
        // TODO FIXME - this should be
        // Result   = (C1  + 2*C2 + C3)  / 4.0;
        // ODResult = (od1 + 2*C2 + od3) / 4.0;
        // (because C1 and C3 also affect adjacent intervals, while C2 only affects this one)

        // bail out - we're done now
        return;
    }

    // check if we should sample between points 1 and 2
    if(ColourDistance(C1, C2) > aa_threshold)
    {
        // recurse again
        ComputeOneMediaSampleRecursive(medias, lights, mediainterval, ray, d1, d2, Result2, C1, C2, ODResult2, od1, od2,
                                       depth - 1, Jitter, aa_threshold, ignore_photons, use_scattering, photonPass, (Jitter == 0.0) ? 0 : DeriveKey(key, kDrawMedia, 0));

        // average colors & optical depth (well, actually do half of the averaging; we'll ad another "half a color" later)
        Result   = Result2   / 2.0;
        ODResult = ODResult2 / 2.0;
        // TODO FIXME - this is actually ok, no fixing required
    }
    else
    {
        // no new points needed - just average what we've got.
        // (we're giving c1 and c2 a relative weight of 2:1, as c2 - the middle point - will make another appearance later)

        // average colors & optical depth (well, actually do half of the averaging; we'll ad another "half a color" later)
        Result   = C1  / 3.0 + C2  / 6.0;
        ODResult = od1 / 3.0 + od2 / 6.0;
        // TODO FIXME - this should be
        // Result   = (C1  + C2)  / 4.0;
        // ODResult = (od1 + od2) / 4.0;
        // (because C1 also affects the adjacent interval)
    }

    // check if we should sample between points 2 and 3
    if(ColourDistance(C2, C3) > aa_threshold)
    {
        // recurse again
        ComputeOneMediaSampleRecursive(medias, lights, mediainterval, ray,  d2, d3, Result2, C2, C3, ODResult2, od2, od3,
                                       depth - 1, Jitter, aa_threshold, ignore_photons, use_scattering, photonPass, (Jitter == 0.0) ? 0 : DeriveKey(key, kDrawMedia, 1));

        // average colors & optical depth (well, actually do half of the averaging; we already did "half a color" earlier)
        Result   += Result2   / 2.0;
        ODResult += ODResult2 / 2.0;
        // TODO FIXME - this is actually ok, no fixing required
    }
    else
    {
        // no new points needed - just average what we've got.
        // (we're giving c2 and c3 a relative weight of 1:2, as c2 - the middle point - already made an appearance earlier)

        // average colors & optical depth (well, actually do half of the averaging; we already did "half a color" earlier)
        Result   += C2  / 6.0 + C3  / 3.0;
        ODResult += od2 / 6.0 + od3 / 3.0;
        // TODO FIXME - this should be
        // Result   = (C2  + C3)  / 4.0;
        // ODResult = (od2 + od3) / 4.0;
        // (because C3 also affects the adjacent interval)
    }
}


std::shared_ptr<const ProgressiveVolumeDomain> MediaFunction::VolumeDomain(const MediaVector& medias)
{
    bool match = volumeDomain && volumeDomain->media.size() == medias.size() &&
                 volumeDomain->modifiers.size() == (modifiers ? modifiers->size() : 0);
    for (size_t i = 0; match && i < medias.size(); ++i)
        match = volumeDomain->media[i] == medias[i];
    for (size_t i = 0; match && modifiers && i < modifiers->size(); ++i)
        match = volumeDomain->modifiers[i] == (*modifiers)[i].interior && volumeDomain->below[i] == (*modifiers)[i].below;
    if (!match)
    {
        auto domain = std::make_shared<ProgressiveVolumeDomain>();
        domain->media.assign(medias.begin(), medias.end());
        if (modifiers)
            for (const auto& modifier : *modifiers)
            {
                domain->modifiers.push_back(modifier.interior);
                domain->below.push_back(modifier.below);
            }
        volumeDomain = domain;
    }
    return volumeDomain;
}

void MediaFunction::ComputeMediaPhotons(MediaVector& medias, MathColour& Te, const MathColour& Sc, const BasicRay& ray, const Vector3d& H,
                                        bool prepared)
{
    if (threadData->GetSceneData()->photonSettings.Progressive() && photonGatherer)
    {
        if (!threadData->GetSceneData()->photonSettings.photonsEnabled)
            return;
        for (const auto& set : threadData->GetSceneData()->preparedSets)
        {
            if (&set->mediaPhotonMap != photonGatherer->map)
                continue;
            const auto& map = set->progressiveMedia;
            if (map.photons.empty())
                return;
            const auto domain = VolumeDomain(medias);
            const ConstObjectPtr receiver = photonReceiver;
            const bool global = receiver == nullptr || !Test_Flag(receiver, NO_GLOBAL_LIGHTS_FLAG);
            if (!global && !map.grouped)
                return;
            const auto gather = [&](const auto& accept) {
                MathColour sum;
                const double radius = map.RadiusAt(H, *domain, accept);
                map.Visit(H, radius, [&](const ProgressiveVolumePhoton& photon) {
                    if (!(*photon.domain == *domain) || !accept(photon))
                        return;
                    BasicRay light;
                    light.Direction = photon.direction;
                    light.Origin = photon.point - photon.direction;
                    double middle = photon.depth;
                    MathColour integral = photon.Integral(H, radius, &middle);
                    if (photon.light != nullptr && photon.depth >= 0.0)
                        integral *= Light_Photon_Path_Weight(photon.light, middle);
                    // the same scattering law as the direct light on this medium
                    if (photon.light && photon.light->emitter)
                        ComputePhysicalMediaScattering(medias, sum, integral / (4.0 * M_PI), H, ray, light);
                    else if (prepared)
                        ComputePhysicalMediaScattering(medias, sum, integral / 4.0, H, ray, light);
                    else
                        ComputeMediaScatteringAttenuation(medias, sum, Sc, integral, ray, light);
                    threadData->progressiveContributions++;
                });
                Te += sum / ProgressivePhotonBudget::KernelVolume(radius);
            };
            if (map.grouped)
                gather([receiver, global](const ProgressiveVolumePhoton& photon) {
                    return photon.grouped ? receiver != nullptr && PhotonLightAffectsObject(photon.light, receiver) : global;
                });
            else
                gather([](const ProgressiveVolumePhoton&) { return true; });
            return;
        }
        return;
    }
    BasicRay Light_Ray;
    DBL r;
    int j;
    MathColour Light_Colour;
    MathColour Colour2;

    if((photonGatherer != nullptr) && (photonGatherer->map->numPhotons > 0))
    {
        //PhotonGatherer gatherer2(photonGatherer->map,photonGatherer->photonSettings);
        photonGatherer->gathered = false;
        // statistics
        threadData->Stats()[Gather_Performed_Count]++;

        if(photonGatherer->gathered)
            r = photonGatherer->alreadyGatheredRadius;
        else
            r = photonGatherer->gatherPhotonsAdaptive(&H, nullptr, false);

        Colour2.Clear();

        // now go through these photons and add up their contribution
        for(j = 0; j < photonGatherer->gatheredPhotons.numFound; j++)
        {
            // DBL theta,phi;
            int theta,phi;

            // convert small color to normal color
            Light_Colour = ToMathColour(RGBColour(photonGatherer->gatheredPhotons.photonGatherList[j]->colour));

            // convert theta/phi to vector direction
            // Use a pre-computed array of sin/cos to avoid many calls to the
            // sin() and cos() functions.  These arrays were initialized in
            // InitBacktraceEverything.
            theta = photonGatherer->gatheredPhotons.photonGatherList[j]->theta + 127;
            phi = photonGatherer->gatheredPhotons.photonGatherList[j]->phi + 127;

            Light_Ray.Direction[Y] = sinCosData.sinTheta[theta];
            Light_Ray.Direction[X] = sinCosData.cosTheta[theta];

            Light_Ray.Direction[Z] = Light_Ray.Direction[X]*sinCosData.sinTheta[phi];
            Light_Ray.Direction[X] = Light_Ray.Direction[X]*sinCosData.cosTheta[phi];

            Light_Ray.Origin = Vector3d(photonGatherer->gatheredPhotons.photonGatherList[j]->Loc) - Light_Ray.Direction;

            // method 1 shoots only ordinary lights, whose physical scattering takes a quarter, as their direct light does
            if (prepared)
                photonIncidents.push_back({Light_Colour / 4.0, Light_Ray.Direction});
            else
                ComputeMediaScatteringAttenuation(medias, Colour2, Sc, Light_Colour, ray, Light_Ray);
        }
        if (!photonIncidents.empty())
            ScatterIncident(medias, Colour2, photonIncidents.data(), photonIncidents.size(), H, ray);
        photonIncidents.clear();

        // finish the photons equation
        Colour2 *= ( 3.0 / (M_PI * r*r*r * 4.0) );

        Te += Colour2;
    }
}

void MediaFunction::ComputePhysicalMediaScattering(MediaVector& medias, MathColour& output, const MathColour& incoming,
                                                   const Vector3d& point, const BasicRay& ray, const BasicRay& lightRay)
{
    const MediaIncident sample{incoming, lightRay.Direction};
    ScatterIncident(medias, output, &sample, 1, point, ray);
}

void MediaFunction::ScatterIncident(MediaVector& medias, MathColour& output, const MediaIncident* incident, size_t count,
                                    const Vector3d& point, const BasicRay& ray, const MathColour *densities)
{
    scatterDensity.assign(medias.size(), MathColour());
    for (size_t i = 0; i < medias.size(); ++i)
        if (medias[i]->use_scattering)
        {
            if (densities != nullptr)
                scatterDensity[i] = densities[i];
            else
                DensityForDeclaredMode(*medias[i], point, scatterDensity[i]);
        }
    for (size_t s = 0; s < count; ++s)
    {
        const DBL cosine = dot(ray.Direction, incident[s].direction);
        MathColour kernel;
        if (modifiers != nullptr)
        {
            MathColour unused;
            AddModifiedCoefficients(medias, scatterDensity.data(), point, ModifierSource::kPattern, unused, nullptr, &kernel, &cosine);
        }
        else
            for (size_t i = 0; i < medias.size(); ++i)
                if (medias[i]->use_scattering)
                    kernel += scatterDensity[i] * medias[i]->Scattering *
                              PhysicalPhaseFactor(medias[i]->Type, medias[i]->Eccentricity, cosine);
        output += kernel * incident[s].incoming;
    }
}

void MediaFunction::ComputeMediaScatteringAttenuation(MediaVector& medias, MathColour& OutputColor, const MathColour& Sc, const MathColour& Light_Colour, const BasicRay& ray, const BasicRay& Light_Ray)
{
    DBL k = 0.0, g = 0.0, g2 = 0.0, alpha = 0.0;

    for(MediaVector::iterator i(medias.begin()); i != medias.end(); i++)
    {
        switch((*i)->Type)
        {
            case RAYLEIGH_SCATTERING:
                alpha = dot(Light_Ray.Direction, ray.Direction);
                k += 0.799372013 * (1.0 + Sqr(alpha));
                break;
            case MIE_HAZY_SCATTERING:
                alpha = dot(Light_Ray.Direction, ray.Direction);
                k += 0.576655375 * (1.0 + 9.0 * pow(0.5 * (1.0 + alpha), 8.0));
                break;
            case MIE_MURKY_SCATTERING:
                alpha = dot(Light_Ray.Direction, ray.Direction);
                k += 0.495714547 * (1.0 + 50.0 * pow(0.5 * (1.0 + alpha), 32.0));
                break;
            case HENYEY_GREENSTEIN_SCATTERING:
                alpha = dot(Light_Ray.Direction, ray.Direction);
                g = (*i)->Eccentricity;
                g2 = Sqr(g);
                k += (1.0 - g2) / pow(1.0 + g2 - 2.0 * g * alpha, 1.5);
                break;
            case ISOTROPIC_SCATTERING:
            default:
                k += 1.0;
                break;
        }
    }

    k /= (DBL)(medias.size());

    OutputColor += k * Sc * Light_Colour;
}

}
// end of namespace pov
