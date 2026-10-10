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
#include <cmath>
#include <cstdio>
#include <limits>
#include <map>
#include <mutex>

// POV-Ray header files (base module)
//  (none at the moment)

// POV-Ray header files (core module)
#include "core/lighting/emitter.h"
#include "core/lighting/lightsource.h"
#include "core/lighting/photons.h"
#include "core/material/pattern.h"
#include "core/material/pigment.h"
#include "core/math/chi2.h"
#include "core/render/ray.h"
#include "core/scene/object.h"
#include "core/scene/scenedata.h"
#include "core/scene/tracethreaddata.h"
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
    kFastMixed = 8, kFastLength = 16, kFastPhoton = 32
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
    td->mediaMessages->Warning(kWarningGeneral,
        "Media method 4 is using classic sampling: %s Resolution %.6g; container bounds <%.6g,%.6g,%.6g> to <%.6g,%.6g,%.6g>%s. "
        "This can be much slower. %s",
        detail, media.FastResolution, low[X], low[Y], low[Z], high[X], high[Y], high[Z],
        container ? "" : " (unavailable)",
        fieldFailure ? "This warning is reported once for this prepared field." : "Further warnings for this reason are suppressed.");
}

class Media::FastCache final
{
    public:
        FastCache(ObjectPtr object, DBL resolution) : container(object), step(resolution) {}
        ObjectPtr container;
        DBL step;
        std::once_flag fieldOnce;
        bool fieldReady = false;
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

        std::mutex lightMutex;
        std::map<const LightSource*, std::shared_ptr<const Optical>> lights;

        bool EnsureField(Media& media, TraceThreadData *ttd);
        bool Segment(const Ray& ray, DBL& from, DBL& to) const;
        bool DensityAt(const Vector3d& point, MathColour& result) const;
        bool Contains(const Vector3d& point) const;
        bool CameraDensityAt(const Vector3d& point, MathColour& result) const;
        bool OpticalDepth(Media& media, const LightSource& light, const Vector3d& a, const Vector3d& b,
                          MathColour& result, TraceThreadData *ttd);
    private:
        bool BuildField(Media& media, TraceThreadData *ttd);
        std::shared_ptr<const Optical> BuildOptical(const Media& media, const LightSource& light, TraceThreadData *ttd) const;
};

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

bool Media::FastCache::EnsureField(Media& media, TraceThreadData *ttd)
{
    std::call_once(fieldOnce, [&] {
        fieldReady = BuildField(media, ttd);
        if (!fieldReady)
            WarnFastMedia(ttd, media, failureReason, failure.c_str(), container);
    });
    return fieldReady;
}

bool Media::FastCache::BuildField(Media& media, TraceThreadData *ttd)
{
    for (int ch = 0; ch < MathColour::channels; ch++)
        if (!std::isfinite(media.Extinction[ch]) || (media.Extinction[ch] < 0.0))
            return false;
    for (const PIGMENT *pigment : media.Density)
        if ((pigment->Type != PLAIN_PATTERN) &&
            ((pigment->Type <= LAST_SPECIAL_PATTERN) ||
             (dynamic_cast<const ContinuousPattern*>(pigment->pattern.get()) == nullptr)))
            return false;
    Make_min_max_from_BBox(low, size, container->BBox);
    size -= low;
    if (!std::isfinite(size[X]) || !std::isfinite(size[Y]) || !std::isfinite(size[Z]) ||
        (size[X] <= 0.0) || (size[Y] <= 0.0) || (size[Z] <= 0.0))
        return false;
    const DBL sx = ceil(size[X] / step), sy = ceil(size[Y] / step), sz = ceil(size[Z] / step);
    if ((sx < 1) || (sy < 1) || (sz < 1) || (sx * sy * sz > 1000000.0))
    {
        failureReason = kFastGrid;
        DBL lower = step, upper = std::max(size[X], std::max(size[Y], size[Z]));
        for (int i = 0; i < 48; i++)
        {
            const DBL candidate = (lower + upper) * 0.5;
            if (ceil(size[X] / candidate) * ceil(size[Y] / candidate) * ceil(size[Z] / candidate) > 1000000.0)
                lower = candidate;
            else
                upper = candidate;
        }
        char detail[256];
        std::snprintf(detail, sizeof(detail),
            "The %.0f x %.0f x %.0f density grid exceeds 1000000 cells. Try resolution %.6g or coarser, automatic resolution, or split the container.",
            sx, sy, sz, upper * 1.00001);
        failure = detail;
        return false;
    }
    nx = int(sx); ny = int(sy); nz = int(sz);
    const Vector3d cell(size[X] / nx, size[Y] / ny, size[Z] / nz);
    density.resize(size_t(nx) * ny * nz);
    rough.resize(density.size());
    DBL effect = 0.0;
    for (int ch = 0; ch < MathColour::channels; ch++)
        effect = std::max(effect, DBL(std::max(fabs(media.Extinction[ch]),
                          std::max(fabs(media.Scattering[ch]), fabs(media.Emission[ch])))));
    if (media.mix == kMediaBlendMultiply)
        effect = std::max(effect, 1.0);
    const DBL cellWidth = std::max(cell[X], std::max(cell[Y], cell[Z]));
    for (int z = 0; z < nz; z++)
        for (int y = 0; y < ny; y++)
            for (int x = 0; x < nx; x++)
            {
                MathColour& value = density[(size_t(z) * ny + y) * nx + x];
                MathColour lowValue, highValue;
                bool first = true;
                for (int dz = 0; dz < 2; dz++)
                    for (int dy = 0; dy < 2; dy++)
                        for (int dx = 0; dx < 2; dx++)
                        {
                            const Vector3d point = low + Vector3d((x + 0.25 + 0.5 * dx) * cell[X],
                                                                   (y + 0.25 + 0.5 * dy) * cell[Y],
                                                                   (z + 0.25 + 0.5 * dz) * cell[Z]);
                            MathColour sample;
                            Evaluate_Density_Pigment(media.Density, point, sample, ttd);
                            for (int ch = 0; ch < MathColour::channels; ch++)
                                if (!std::isfinite(sample[ch]) || (sample[ch] < 0.0))
                                {
                                    density.clear();
                                    return false;
                                }
                            value += sample;
                            if (first)
                            {
                                lowValue = highValue = sample;
                                first = false;
                            }
                            else
                                for (int ch = 0; ch < MathColour::channels; ch++)
                                {
                                    lowValue[ch] = std::min(lowValue[ch], sample[ch]);
                                    highValue[ch] = std::max(highValue[ch], sample[ch]);
                                }
                        }
                value *= 0.125;
                rough[(size_t(z) * ny + y) * nx + x] =
                    effect * cellWidth * (highValue - lowValue).Max() > 1.0 / 1024.0;
            }
    return true;
}

bool Media::FastCache::DensityAt(const Vector3d& point, MathColour& result) const
{
    if ((point[X] < low[X]) || (point[Y] < low[Y]) || (point[Z] < low[Z]) ||
        (point[X] > low[X] + size[X]) || (point[Y] > low[Y] + size[Y]) || (point[Z] > low[Z] + size[Z]))
        return false;
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
    const int n[3] = { nx, ny, nz };
    int base[3];
    for (int axis = 0; axis < 3; axis++)
    {
        if ((point[axis] < low[axis]) || (point[axis] > low[axis] + size[axis]))
        {
            result.Clear();
            return true;
        }
        base[axis] = std::max(0, std::min(n[axis] - 1,
                    int(floor((point[axis] - low[axis]) * n[axis] / size[axis] - 0.5))));
    }
    for (int z = 0; z < 2; z++)
        for (int y = 0; y < 2; y++)
            for (int x = 0; x < 2; x++)
                if (rough[(size_t(std::min(base[2] + z, nz - 1)) * ny + std::min(base[1] + y, ny - 1)) * nx +
                           std::min(base[0] + x, nx - 1)])
                    return false;
    return DensityAt(point, result);
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
                                                     const LightSource& light, TraceThreadData *ttd) const
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
        const DBL count = ceil((hi - grid->lo[axis]) / step);
        if (!std::isfinite(count) || (count < 1.0) || (count > 1000000.0))
            return nullptr;
        grid->n[axis] = int(count);
        grid->ds[axis] = (hi - grid->lo[axis]) / grid->n[axis];
    }
    const double cells = double(grid->n[0]) * grid->n[1] * grid->n[2];
    if (cells > 1000000.0)
        return nullptr;
    grid->prefix.resize(size_t(grid->n[0]) * grid->n[1] * (grid->n[2] + 1));
    for (int y = 0; y < grid->n[1]; y++)
        for (int x = 0; x < grid->n[0]; x++)
            for (int z = 0; z < grid->n[2]; z++)
            {
                const Vector3d point = grid->u * (grid->lo[0] + (x + 0.5) * grid->ds[0]) +
                                       grid->v * (grid->lo[1] + (y + 0.5) * grid->ds[1]) +
                                       grid->w * (grid->lo[2] + (z + 0.5) * grid->ds[2]);
                MathColour value;
                if (!Inside_Object(point, container, ttd) || !DensityAt(point, value))
                    value.Clear();
                grid->prefix[grid->Index(x, y, z + 1)] = grid->prefix[grid->Index(x, y, z)] +
                                                          value * media.Extinction * grid->ds[2];
            }
    return grid;
}

bool Media::FastCache::OpticalDepth(Media& media, const LightSource& light, const Vector3d& a,
                                    const Vector3d& b, MathColour& result, TraceThreadData *ttd)
{
    if (!light.Parallel || light.Area_Light || (light.Light_Type == CYLINDER_SOURCE) ||
        !light.Media_Attenuation || !light.Media_Interaction || (light.portal != nullptr) ||
        !ttd->GetSceneData()->portalMouths.empty())
        return false;
    if (!EnsureField(media, ttd))
        return false;
    std::shared_ptr<const Optical> grid;
    {
        std::lock_guard<std::mutex> guard(lightMutex);
        auto found = lights.find(&light);
        if (found == lights.end())
        {
            if (lights.size() >= 4)
                return false;
            found = lights.emplace(&light, BuildOptical(media, light, ttd)).first;
        }
        grid = found->second;
    }
    MathColour first, last;
    if (!grid || !grid->Depth(a, first) || !grid->Depth(b, last))
        return false;
    result = last - first;
    for (int ch = 0; ch < MathColour::channels; ch++)
        if (result[ch] < 0.0)
            result[ch] = 0.0;
    return true;
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
    Refraction = 0.0;

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
        Refraction = source.Refraction;
        Ratio = source.Ratio;
        Confidence = source.Confidence;
        Variance = source.Variance;
        AA_Threshold = source.AA_Threshold;
        AA_Level = source.AA_Level;
        FastResolution = source.FastResolution;
        mix = source.mix;
        priority = source.priority;
        light = source.light;
        fastCache.reset();

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
}

// The prepared grid's width for a container when none is given: 0 for an unbounded one.
static DBL AutomaticResolution(ObjectPtr object)
{
    Vector3d low, high;
    Make_min_max_from_BBox(low, high, object->BBox);
    const Vector3d size = high - low;
    const DBL volume = size[X] * size[Y] * size[Z];
    if (!std::isfinite(volume) || (size[X] <= 0.0) || (size[Y] <= 0.0) || (size[Z] <= 0.0))
        return 0.0;
    return std::max(std::max(size[X], std::max(size[Y], size[Z])) / 128.0, std::cbrt(volume / 500000.0));
}

void Media::SetFastContainer(ObjectPtr object)
{
    if (Sample_Method != 4)
        return;
    if (FastResolution == -1.0)
        FastResolution = AutomaticResolution(object);
    if ((FastResolution <= 0.0) || Density.empty())
        return;
    if (fastCache && (fastCache->container != object))
    {
        fastCache.reset();
        FastResolution = 0.0;
    }
    else if (!fastCache)
        fastCache.reset(new FastCache(object, FastResolution));
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

namespace
{

/// An emitting medium as a light: a table of the power of each cell of its prepared grid inside the container.
class VolumeEmitter final : public Emitter
{
    public:
        Vector3d low, cell;
        int nx = 1, ny = 1;
        vector<std::uint32_t> cells;
        vector<unsigned char> inside;
        vector<double> cdf;
        vector<MathColour> weights;
        MathColour intensity;
        Vector3d centre;
        double nearDistance = 0.0, radius = 0.0;

        virtual const MathColour& Intensity() const override { return intensity; }
        virtual const Vector3d& Centre() const override { return centre; }
        virtual double NearDistance() const override { return nearDistance; }
        virtual double Radius() const override { return radius; }
        virtual EmitterSample Sample(double s, std::uint64_t key) const override
        {
            const size_t i = std::min(cells.size() - 1, size_t(std::upper_bound(cdf.begin(), cdf.end(), s) - cdf.begin()));
            const std::uint32_t index = cells[i];
            const std::uint32_t x = index % nx, y = (index / nx) % ny, z = index / (std::uint32_t(nx) * ny);
            // a point in one of the cell's octants inside the container, each equally likely
            const unsigned mask = inside[i];
            int count = 0;
            for (int k = 0; k < 8; k++)
                count += (mask >> k) & 1;
            int pick = std::min(count - 1, int(Draw(key, kDrawEmitter, 3) * count)), octant = 0;
            for (; (pick > 0) || !((mask >> octant) & 1); octant++)
                if ((mask >> octant) & 1)
                    pick--;
            EmitterSample sample;
            sample.position = low + Vector3d((x + 0.5 * ((octant & 1) + Draw(key, kDrawEmitter, 0))) * cell[X],
                                             (y + 0.5 * (((octant >> 1) & 1) + Draw(key, kDrawEmitter, 1))) * cell[Y],
                                             (z + 0.5 * ((octant >> 2) + Draw(key, kDrawEmitter, 2))) * cell[Z]);
            sample.weight = weights[i];
            sample.pdf = (cdf[i] - (i > 0 ? cdf[i - 1] : 0.0)) / (cell[X] * cell[Y] * cell[Z] * count / 8.0);
            return sample;
        }
};

}

std::shared_ptr<const Emitter> MakeVolumeEmitter(Media& medium, ObjectPtr container, TraceThreadData *td, std::string& failure)
{
    for (int ch = 0; ch < MathColour::channels; ch++)
        if (!std::isfinite(medium.Emission[ch]) || (medium.Emission[ch] < 0.0))
        {
            failure = "its emission is negative or not finite";
            return nullptr;
        }
    if (medium.Emission.IsZero())
    {
        failure = "it has no emission";
        return nullptr;
    }
    if (Test_Flag(container, INFINITE_FLAG))
    {
        failure = "its container is infinite";
        return nullptr;
    }
    Vector3d low, size;
    Make_min_max_from_BBox(low, size, container->BBox);
    size -= low;
    DBL step = (medium.FastResolution > 0.0) ? medium.FastResolution : AutomaticResolution(container);
    if (!(step > 0.0))
    {
        failure = "its container is unbounded";
        return nullptr;
    }
    while (ceil(size[X] / step) * ceil(size[Y] / step) * ceil(size[Z] / step) > 1000000.0)
        step *= 1.05;
    std::shared_ptr<VolumeEmitter> emitter(new VolumeEmitter());
    const int n[3] = { int(ceil(size[X] / step)), int(ceil(size[Y] / step)), int(ceil(size[Z] / step)) };
    emitter->low = low;
    emitter->nx = n[X];
    emitter->ny = n[Y];
    emitter->cell = Vector3d(size[X] / n[X], size[Y] / n[Y], size[Z] / n[Z]);
    const Vector3d& cell = emitter->cell;
    const DBL share = cell[X] * cell[Y] * cell[Z] / 8.0;
    vector<double> power;
    double total = 0.0;
    Vector3d points[8];
    MathColour density[8];
    for (int z = 0; z < n[Z]; z++)
        for (int y = 0; y < n[Y]; y++)
            for (int x = 0; x < n[X]; x++)
            {
                size_t inside = 0;
                unsigned mask = 0;
                for (int k = 0; k < 8; k++)
                {
                    const Vector3d point = low + Vector3d((x + 0.25 + 0.5 * (k & 1)) * cell[X], (y + 0.25 + 0.5 * ((k >> 1) & 1)) * cell[Y],
                                                          (z + 0.25 + 0.5 * (k >> 2)) * cell[Z]);
                    if (Inside_Object(point, container, td))
                    {
                        points[inside++] = point;
                        mask |= 1u << k;
                    }
                }
                if (inside == 0)
                    continue;
                Evaluate_Density_Pigment(medium.Density, points, density, inside, td);
                MathColour sum;
                for (size_t k = 0; k < inside; k++)
                    sum += density[k];
                const MathColour cellPower = medium.Emission * sum * share;
                const double weight = cellPower.Weight();
                if (!std::isfinite(weight) || (cellPower.Min() < 0.0))
                {
                    failure = "its density is negative or not finite";
                    return nullptr;
                }
                if (weight <= 0.0)
                    continue;
                emitter->cells.push_back(std::uint32_t((size_t(z) * n[Y] + y) * n[X] + x));
                emitter->inside.push_back((unsigned char)mask);
                emitter->weights.push_back(cellPower);
                power.push_back(weight);
                emitter->intensity += cellPower;
                emitter->centre += (low + Vector3d((x + 0.5) * cell[X], (y + 0.5) * cell[Y], (z + 0.5) * cell[Z])) * weight;
                total += weight;
            }
    if (!(total > 0.0) || !std::isfinite(total))
    {
        failure = "its density is zero throughout its container";
        return nullptr;
    }
    emitter->centre /= total;
    emitter->nearDistance = 0.5 * cell.length();
    for (std::uint32_t index : emitter->cells)
    {
        const Vector3d middle = low + Vector3d((index % n[X] + 0.5) * cell[X], ((index / n[X]) % n[Y] + 0.5) * cell[Y],
                                               (index / (std::uint32_t(n[X]) * n[Y]) + 0.5) * cell[Z]);
        emitter->radius = std::max(emitter->radius, (middle - emitter->centre).length() + emitter->nearDistance);
    }
    // Z-order keeps an equal-power stratum of the table compact in space, so stratified draws spread in 3D.
    vector<std::pair<std::uint64_t, size_t>> order(power.size());
    for (size_t i = 0; i < order.size(); i++)
    {
        const std::uint32_t index = emitter->cells[i];
        const std::uint64_t at[3] = { index % std::uint32_t(n[X]), (index / n[X]) % std::uint32_t(n[Y]), index / (std::uint32_t(n[X]) * n[Y]) };
        std::uint64_t code = 0;
        for (int bit = 0; bit < 21; bit++)
            for (int axis = 0; axis < 3; axis++)
                code |= ((at[axis] >> bit) & 1) << (3 * bit + axis);
        order[i] = std::make_pair(code, i);
    }
    std::sort(order.begin(), order.end());
    vector<std::uint32_t> cells(order.size());
    vector<unsigned char> inside(order.size());
    vector<MathColour> weights(order.size());
    vector<double> sorted(order.size());
    for (size_t i = 0; i < order.size(); i++)
    {
        cells[i] = emitter->cells[order[i].second];
        inside[i] = emitter->inside[order[i].second];
        weights[i] = emitter->weights[order[i].second];
        sorted[i] = power[order[i].second];
    }
    emitter->cells.swap(cells);
    emitter->inside.swap(inside);
    emitter->weights.swap(weights);
    power.swap(sorted);
    emitter->cdf.resize(power.size());
    double sum = 0.0;
    for (size_t i = 0; i < power.size(); i++)
    {
        sum += power[i];
        emitter->cdf[i] = sum / total;
        for (int ch = 0; ch < MathColour::channels; ch++)
            emitter->weights[i][ch] = (emitter->intensity[ch] > 0.0) ? emitter->weights[i][ch] * (total / power[i]) / emitter->intensity[ch] : 0.0;
    }
    emitter->cdf.back() = 1.0;
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

}

static bool PlaysRole(const Media& medium, MediaRole role)
{
    return (role == MediaRole::kRefraction) ? (medium.Refraction != 0.0) : !medium.OnlyRefracts();
}

void CollectInteriorMedia(const RayInteriorVector& interiors, MediaVector& medias, MediaModifierVector *modifiers, MediaRole role)
{
    if (modifiers == nullptr)
    {
        for (size_t i = 0; i < interiors.size(); i++)
            for (Media& medium : interiors[i]->media)
                if (PlaysRole(medium, role))
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
        else if ((role == MediaRole::kRefraction) ? (medium->Refraction != 0.0)
                                                  : (medium->use_absorption || medium->use_emission || medium->use_scattering))
            medias.push_back(medium);
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

RefractionField::RefractionField(const RayInteriorVector& interiors, TraceThreadData *td) :
    threadData(td), low(HUGE_VAL), high(-HUGE_VAL), epsilon(HUGE_VAL), maxStep(HUGE_VAL), varies(false), bounded(true)
{
    CollectInteriorMedia(interiors, medias, td->GetSceneData()->mediaBlendModes ? &modifiers : nullptr, MediaRole::kRefraction);
    if (medias.empty())
        return;
    for (Interior *interior : interiors)
    {
        for (Media& medium : interior->media)
        {
            const bool modifier = std::any_of(modifiers.begin(), modifiers.end(),
                                              [&medium](const MediaModifier& m) { return m.medium == &medium; });
            if (ConstantDensity(medium) ||
                (!modifier && (std::find(medias.begin(), medias.end(), &medium) == medias.end())))
                continue;
            varies = true;
            const Vector3d size = interior->boundsHigh - interior->boundsLow;
            const DBL extent = std::max(size[X], std::max(size[Y], size[Z]));
            const bool finite = std::isfinite(extent) && (extent > 0.0) && (extent < BOUND_HUGE * 0.5);
            if (finite)
            {
                low = min(low, interior->boundsLow);
                high = max(high, interior->boundsHigh);
            }
            else
                bounded = false;
            if (medium.fastCache && (medium.FastResolution > 0.0))
            {
                epsilon = std::min(epsilon, medium.FastResolution * 0.5);
                maxStep = std::min(maxStep, medium.FastResolution * 2.0);
            }
            else if (finite)
            {
                epsilon = std::min(epsilon, extent * 1.0e-4);
                maxStep = std::min(maxStep, extent / 64.0);
            }
            else
            {
                epsilon = std::min(epsilon, 1.0e-4);
                maxStep = std::min(maxStep, 1.0);
            }
        }
    }
}

DBL RefractionField::MeanDensity(Media& medium, const Vector3d& point)
{
    if (medium.Density.empty())
        return 1.0;
    MathColour density;
    if (!medium.fastCache || !medium.fastCache->EnsureField(medium, threadData) || !medium.fastCache->DensityAt(point, density))
        Evaluate_Density_Pigment(medium.Density, point, density, threadData);
    return ChannelMean(density);
}

DBL RefractionField::Offset(const Vector3d& point)
{
    const size_t n = medias.size();
    terms.resize(n);
    for (size_t i = 0; i < n; i++)
        terms[i] = medias[i]->Refraction * MeanDensity(*medias[i], point);
    for (const MediaModifier& modifier : modifiers)
    {
        const bool multiply = (modifier.medium->mix == kMediaBlendMultiply);
        const DBL density = MeanDensity(*modifier.medium, point);
        const DBL amount = multiply ? density : fabs(modifier.medium->Refraction) * density;
        DBL scale = amount;
        if (!multiply)
        {
            DBL sum = 0.0;
            for (size_t t = 0; t < modifier.below; t++)
                sum += terms[t];
            const DBL reduced = (sum > 0.0) ? std::max(0.0, sum - amount) : std::min(0.0, sum + amount);
            scale = (sum != 0.0) ? reduced / sum : 0.0;
        }
        for (size_t t = 0; t < modifier.below; t++)
            terms[t] *= scale;
    }
    DBL offset = 0.0;
    for (size_t i = 0; i < n; i++)
        offset += terms[i];
    return offset;
}

DBL RefractionField::Offset(const Vector3d& point, Vector3d& gradient)
{
    for (int axis = X; axis <= Z; axis++)
    {
        Vector3d delta(0.0);
        delta[axis] = epsilon;
        gradient[axis] = (Offset(point + delta) - Offset(point - delta)) / (2.0 * epsilon);
    }
    return Offset(point);
}

bool RefractionField::Ahead(const Vector3d& origin, const Vector3d& direction, DBL& entry) const
{
    entry = 0.0;
    if (!bounded)
        return true;
    DBL from = 0.0, to = HUGE_VAL;
    for (int axis = X; axis <= Z; axis++)
    {
        const DBL a = low[axis] - maxStep, b = high[axis] + maxStep;
        if (fabs(direction[axis]) < 1.0e-12)
        {
            if ((origin[axis] < a) || (origin[axis] > b))
                return false;
            continue;
        }
        DBL first = (a - origin[axis]) / direction[axis], last = (b - origin[axis]) / direction[axis];
        if (first > last)
            std::swap(first, last);
        from = std::max(from, first);
        to = std::min(to, last);
        if (to < from)
            return false;
    }
    entry = from;
    return true;
}

DBL MediaFunction::ModifierResolution() const
{
    DBL resolution = HUGE_VAL;
    if (modifiers == nullptr)
        return resolution;
    for (const MediaModifier& modifier : *modifiers)
    {
        const Media& medium = *modifier.medium;
        if (medium.Density.empty())
            continue;
        if (medium.FastResolution > 0.0)
        {
            resolution = std::min(resolution, medium.FastResolution);
            continue;
        }
        const Vector3d size = modifier.interior->boundsHigh - modifier.interior->boundsLow;
        const DBL volume = size[X] * size[Y] * size[Z];
        if (std::isfinite(volume) && (volume > 0.0))
            resolution = std::min(resolution, std::max(std::max(size[X], std::max(size[Y], size[Z])) / 128.0,
                                                       std::cbrt(volume / 500000.0)));
    }
    return resolution;
}

void MediaFunction::ModifierDensity(Media& medium, const Vector3d& point, ModifierSource source, MathColour& local)
{
    if ((source != ModifierSource::kPattern) && medium.fastCache && !medium.Density.empty() &&
        medium.fastCache->EnsureField(medium, threadData) &&
        ((source == ModifierSource::kCamera) ? (medium.fastCache->Contains(point) && medium.fastCache->CameraDensityAt(point, local))
                                             : medium.fastCache->DensityAt(point, local)))
        return;
    Evaluate_Density_Pigment(medium.Density, point, local, threadData);
}

void MediaFunction::AddModifiedCoefficients(MediaVector& medias, const MathColour *density, const Vector3d& point, ModifierSource source,
                                            MathColour& extinction, MathColour *emission, MathColour *scattering, bool lightEmission)
{
    const size_t n = medias.size();
    modifierScratch.resize(3 * n);
    MathColour *absorbing = &modifierScratch[0], *scattered = absorbing + n, *emitted = scattered + n;
    for (size_t i = 0; i < n; i++)
    {
        absorbing[i] = density[i] * medias[i]->Absorption;
        scattered[i] = density[i] * medias[i]->Scattering;
        emitted[i] = (lightEmission || !medias[i]->light) ? density[i] * medias[i]->Emission : MathColour();
    }
    for (const MediaModifier& modifier : *modifiers)
    {
        Media& medium = *modifier.medium;
        MathColour local;
        ModifierDensity(medium, point, source, local);
        if (medium.mix == kMediaBlendMultiply)
            for (size_t t = 0; t < modifier.below; t++)
            {
                absorbing[t] *= local;
                scattered[t] *= local;
                emitted[t] *= local;
            }
        else
        {
            SubtractFromSum(absorbing, modifier.below, local * medium.Absorption);
            SubtractFromSum(scattered, modifier.below, local * medium.Scattering);
            SubtractFromSum(emitted, modifier.below, local * medium.Emission);
        }
    }
    for (size_t i = 0; i < n; i++)
    {
        extinction += absorbing[i] + scattered[i] * medias[i]->sc_ext;
        if (emission != nullptr)
            *emission += emitted[i];
        if (scattering != nullptr)
            *scattering += scattered[i];
    }
}

// Points of each area light tested per media sample; see doc/PERF.md.
static const int kMediaAreaLightPoints = 4;

// Where the draws picking the step a stride of prepared steps takes its light at start.
static const std::uint64_t kMediaLightDraw = std::uint64_t(1) << 40;

MediaFunction::MediaFunction(TraceThreadData *td, Trace *t, PhotonGatherer *pg) :
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
        // a media light's shadow rays skip media that only glow, so uniform media around them stay closed-form
        const LightSource *light = ray.GetMediaLight();
        if (ray.IsShadowTestRay() && (light != nullptr) && (light->emitter != nullptr) && mods.empty())
        {
            size_t kept = 0;
            for (size_t k = 0; k < medialist.size(); k++)
                if (medialist[k]->use_extinction)
                    medialist[kept++] = medialist[k];
            while (medialist.size() > kept)
                medialist.pop_back();
        }

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
    LightSourceEntryVector lights;
    LitIntervalVector litintervals;
    MediaIntervalVector mediaintervals;
    Media *IMedia;
    bool all_constant_and_light_ray = ray.IsShadowTestRay();  // is all the media constant?
    bool ignore_photons = true;
    bool use_extinction = false;
    bool use_scattering = false;
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

        // NK fast light_ray media calculation for constant media
        for (vector<PIGMENT*>::iterator ii = (*i)->Density.begin(); ii != (*i)->Density.end(); ++ ii)
            all_constant_and_light_ray = all_constant_and_light_ray && ((*ii)->Type == PLAIN_PATTERN);
    }

    all_constant_and_light_ray = all_constant_and_light_ray && (modifiers == nullptr);

    // If this is a light ray and no extinction is used we can return.
    if((ray.IsShadowTestRay()) && (!use_extinction))
        return;

    // Prepare the Monte Carlo integration along the ray from P0 to P1.
    if(!ray.IsShadowTestRay())
        ComputeMediaLightInterval(lights, litintervals, ray, isect);

    if(litintervals.empty())
        litintervals.push_back(LitInterval(false, 0.0, isect.Depth, 0, 0));

    const DBL preparedResolution = PreparedResolution(medias, ray);
    DBL preparedFrom = 0.0, preparedTo = isect.Depth;
    if (preparedResolution > 0.0)
        PreparedRange(medias, ray, preparedFrom, preparedTo);
    const DBL fastStep = ray.IsShadowTestRay() ? preparedResolution : preparedResolution / 3.0;
    const bool withinStepLimit = fastStep > 0.0 &&
        PreparedSteps(medias, ray, preparedFrom, preparedTo, preparedResolution) <= 4096.0;
    const bool fieldsPrepared = preparedResolution > 0.0 && (withinStepLimit || ray.IsShadowTestRay()) && PrepareFields(medias, ray);
    const bool fastPrepared = withinStepLimit && fieldsPrepared;
    if (fastPrepared && preparedTo <= preparedFrom)
        return;
    if (fastStep > 0.0 && !withinStepLimit && !ray.IsShadowTestRay() &&
        !(threadData->GetSceneData()->mediaWarningFlags.load(std::memory_order_relaxed) & kFastLength))
    {
        char detail[256];
        std::snprintf(detail, sizeof(detail),
            "Ray span %.9g with finest step %.9g exceeds 4096 prepared steps (%zu active media). Increase resolution or split long containers.",
            preparedTo - preparedFrom, fastStep, medias.size());
        WarnFastMedia(threadData, *medias.front(), kFastLength, detail,
                      medias.front()->fastCache ? medias.front()->fastCache->container : nullptr);
    }
    const bool fastCamera = fastPrepared && !ray.IsShadowTestRay() && !ray.IsPhotonRay();
    const bool fastShadow = fastPrepared && ray.IsShadowTestRay();

    // Set up sampling intervals (makes sure we will always have enough intervals)
    ComputeMediaSampleInterval(litintervals, mediaintervals, IMedia, fastCamera || fastShadow);

    if(mediaintervals.front().s0 > 0.0)
        mediaintervals.insert(mediaintervals.begin(),
                              MediaInterval(false, 0,
                              0.0,
                              mediaintervals.front().s0,
                              mediaintervals.front().s0,
                              0, 0));
    if(mediaintervals.back().s1 < isect.Depth)
        mediaintervals.push_back(MediaInterval(false, 0,
                                 mediaintervals.back().s1,
                                 isect.Depth,
                                 isect.Depth - mediaintervals.back().s1,
                                 0, 0));

    if (fastPrepared)
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
    if(ray.IsShadowTestRay() && !all_constant_and_light_ray)
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
                        if (!medium->fastCache->Segment(ray, from, to))
                            continue;
                        if (!fast || !medium->fastCache->OpticalDepth(*medium, light, ray.Evaluate(from),
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
                                  ignore_photons, use_scattering);
    else if((IMedia->Sample_Method == 3 || IMedia->Sample_Method == 4) && !all_constant_and_light_ray) // adaptive sampling and method 4 fallback
        ComputeMediaAdaptiveSampling(medias, lights, mediaintervals, ray, IMedia, aa_threshold, minSamples, ignore_photons, use_scattering);
    else
        ComputeMediaRegularSampling(medias, lights, mediaintervals, ray, IMedia, minSamples, ignore_photons, use_scattering, all_constant_and_light_ray);

    ComputeMediaColour(mediaintervals, colour, transm);

    lightSampleIndex = savedIndex;
    lightSampleShift = savedShift;
    drawKey = savedKey;
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
};

bool ExtinctionPlan::Plan(MediaVector& list, TraceThreadData *threadData)
{
    size_t n = 0;
    medias = 0;
    for(MediaVector::iterator i(list.begin()); i != list.end(); i++)
    {
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
            Evaluate_Density_Pigment(medias[m]->Density, points, density, n, threadData);
            for(size_t j = 0; j < n; j++)
                densities[j * medias.size() + m] = density[j];
        }
        for(size_t j = 0; j < n; j++)
            AddModifiedCoefficients(medias, &densities[j * medias.size()], points[j], ModifierSource::kPattern, extinction[j], nullptr, nullptr);
        return;
    }
    for(MediaVector::iterator i(medias.begin()); i != medias.end(); i++)
    {
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
        if(!(hi[ch] >= 0.0) || !std::isfinite(hi[ch]))
            return false;
    const DBL sumUpper = DBL(hi.Max()) * (2.0 * points) / (1.0 - rounding);
    if(sumUpper >= std::numeric_limits<ColourChannel>::max())
        return false;
    DBL length = 0.0;
    for(const auto& interval : intervals)
    {
        if(!(interval.ds >= 0.0) || !std::isfinite(interval.ds))
            return false;
        length += interval.ds;
    }
    const DBL upper = (DBL(hi.Min()) + 1e-6 * DBL(hi.Max())) * length / (1.0 - rounding);
    roundingError = rounding * DBL(hi.Max()) * length / (1.0 - rounding);
    return std::isfinite(upper) && (upper < log(1024.0)) && (roundingError < budget);
}

DBL MediaFunction::PreparedResolution(MediaVector& medias, const Ray& ray)
{
    DBL resolution = HUGE_VAL;
    Media* requested = nullptr;
    bool mixed = false;
    for (Media* medium : medias)
        if (!medium->Density.empty() && (!ray.IsShadowTestRay() || medium->use_extinction))
        {
            if (medium->Sample_Method == 4)
                requested = medium;
            else
                mixed = true;
        }
    if (!requested)
        return 0.0;
    if (ray.IsPhotonRay())
    {
        WarnFastMedia(threadData, *requested, kFastPhoton, "Photon propagation uses classic media sampling.");
        return 0.0;
    }
    if (mixed)
    {
        WarnFastMedia(threadData, *requested, kFastMixed,
                      "A varying classic medium overlaps method 4. Select method 4 on all overlapping varying media.",
                      requested->fastCache ? requested->fastCache->container : nullptr);
        return 0.0;
    }
    bool ready = true;
    for (Media* medium : medias)
    {
        if (medium->Density.empty() || (ray.IsShadowTestRay() && !medium->use_extinction))
            continue;
        if (!medium->fastCache)
        {
            WarnFastMedia(threadData, *medium, kFastContainer,
                          "No unique finite container is available for this medium. Use a bounded container with its own interior.");
            ready = false;
        }
        else
            resolution = std::min(resolution, medium->FastResolution);
    }
    return ready ? resolution : 0.0;
}

void MediaFunction::PreparedRange(MediaVector& medias, const Ray& ray, DBL& from, DBL& to)
{
    DBL first = to, last = from;
    for (Media* medium : medias)
    {
        if (ray.IsShadowTestRay() && !medium->use_extinction)
            continue;
        if (medium->Density.empty())
            return;
        DBL entry = from, exit = to;
        if (medium->fastCache->Segment(ray, entry, exit))
        {
            first = std::min(first, entry);
            last = std::max(last, exit);
        }
    }
    from = first;
    to = std::max(first, last);
}

DBL MediaFunction::PreparedSteps(MediaVector& medias, const Ray& ray, DBL from, DBL to, DBL resolution)
{
    const DBL divisor = ray.IsShadowTestRay() ? 1.0 : 3.0;
    DBL count = 2.0 * medias.size() + 1.0;
    for (Media* medium : medias)
    {
        if (ray.IsShadowTestRay() && !medium->use_extinction)
            continue;
        if (medium->Density.empty())
        {
            if (!ray.IsShadowTestRay() && (medium->use_scattering || medium->use_emission))
                return ceil((to - from) * divisor / resolution);
            continue;
        }
        DBL entry = from, exit = to;
        if (medium->fastCache->Segment(ray, entry, exit))
            count += ceil((exit - entry) * divisor / medium->FastResolution);
    }
    const DBL modifierResolution = ModifierResolution();
    if (modifierResolution < HUGE_VAL)
        count += ceil((to - from) * divisor / modifierResolution);
    return count;
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
            if (medium->Density.empty() || (ray.IsShadowTestRay() && !medium->use_extinction))
                continue;
            DBL from = interval.s0, to = interval.s1;
            if (medium->fastCache->Segment(ray, from, to))
            {
                cuts.push_back(from);
                cuts.push_back(to);
            }
        }
        std::sort(cuts.begin(), cuts.end());
        for (size_t i = 1; i < cuts.size(); i++)
            if (cuts[i] > cuts[i - 1])
                split.push_back(MediaInterval(interval.lit, 0, cuts[i - 1], cuts[i], cuts[i] - cuts[i - 1], interval.l0, interval.l1));
    }
    intervals = split;
}

DBL MediaFunction::PreparedStep(MediaVector& medias, const Ray& ray, const MediaInterval& interval, DBL fallback)
{
    DBL step = interval.ds;
    for (Media* medium : medias)
    {
        if (ray.IsShadowTestRay() && !medium->use_extinction)
            continue;
        if (medium->Density.empty())
        {
            if (!ray.IsShadowTestRay() && (medium->use_scattering || medium->use_emission))
                step = std::min(step, fallback);
            continue;
        }
        DBL from = interval.s0, to = interval.s1;
        if (medium->fastCache->Segment(ray, from, to))
            step = std::min(step, medium->FastResolution / (ray.IsShadowTestRay() ? 1.0 : 3.0));
    }
    const DBL modifierResolution = ModifierResolution();
    if (modifierResolution < HUGE_VAL)
        step = std::min(step, modifierResolution / (ray.IsShadowTestRay() ? 1.0 : 3.0));
    return step;
}

bool MediaFunction::PrepareFields(MediaVector& medias, const Ray& ray)
{
    bool ready = true;
    for (Media* medium : medias)
        if (!medium->Density.empty() && (!ray.IsShadowTestRay() || medium->use_extinction))
            ready = medium->fastCache->EnsureField(*medium, threadData) && ready;
    return ready;
}

void MediaFunction::ComputeMediaTransmittance(MediaVector& medias, MediaIntervalVector& mediaintervals, const Ray& ray, const Media *IMedia)
{
    const bool method3 = (IMedia->Sample_Method == 3 || IMedia->Sample_Method == 4);
    const int points = method3 ? 2 * max((IMedia->Min_Samples + 1) / 2, 1) + 1 : max(IMedia->Min_Samples, 1);
    const DBL resolution = PreparedResolution(medias, ray);
    const bool withinStepLimit = resolution > 0.0 &&
        PreparedSteps(medias, ray, mediaintervals.front().s0, mediaintervals.back().s1, resolution) <= 4096.0;
    if (withinStepLimit && PrepareFields(medias, ray))
    {
        threadData->Stats()[Media_Intervals] += mediaintervals.size();
        ComputeMediaFieldTransmittance(medias, mediaintervals, ray, resolution);
        return;
    }
    if (resolution > 0.0 && !withinStepLimit)
        WarnFastMedia(threadData, *medias.front(), kFastLength,
                      "This shadow segment exceeds 4096 prepared steps and has no usable optical-depth cache. Increase resolution or split long containers.",
                      medias.front()->fastCache ? medias.front()->fastCache->container : nullptr);

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

void MediaFunction::ComputeMediaFieldTransmittance(MediaVector& medias, MediaIntervalVector& mediaintervals, const Ray& ray, DBL resolution)
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
            const DBL width = PreparedStep(medias, ray, interval, resolution);
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
                    else if (medium->Density.empty())
                        density = MathColour(1.0);
                    else if (!medium->fastCache->DensityAt(point, density))
                        density.Clear();
                    if (modifiers != nullptr)
                        densities.push_back(density);
                    else
                        extinction += density * medium->Extinction;
                }
                if (modifiers != nullptr)
                    AddModifiedCoefficients(medias, &densities[0], point, ModifierSource::kField, extinction, nullptr, nullptr);
                depth += extinction * step;
                threadData->Stats()[Media_Samples]++;
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

DBL MediaFunction::ScatteringStep(MediaVector& medias, const MediaInterval& interval) const
{
    DBL step = HUGE_VAL;
    for (const Media *medium : medias)
        if (medium->use_scattering)
            step = std::min(step, medium->Density.empty() ? interval.ds / std::max(1, medium->Min_Samples)
                                                          : (medium->fastCache ? medium->FastResolution / 3.0 : 0.0));
    return step;
}

void MediaFunction::ComputeMediaFixedSampling(MediaVector& medias, LightSourceEntryVector& lights, MediaIntervalVector& mediaintervals,
                                              const Ray& ray, DBL resolution, bool ignore_photons, bool use_scattering)
{
    for (MediaInterval& interval : mediaintervals)
    {
        if (interval.ds <= 0.0)
        {
            interval.samples = 1;
            continue;
        }
        const DBL width = PreparedStep(medias, ray, interval, resolution);
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
            stride = std::max(1, std::min(count, int(ScatteringStep(medias, interval) / step)));
        MathColour light;
        int lit = -1;
        for (int j = 0; j < count; j++)
        {
            MediaInterval cell(interval.lit, 0, interval.s0 + j * step, interval.s0 + (j + 1) * step,
                               step, interval.l0, interval.l1);
            MathColour emission, depth, scattering;
            ComputeOneMediaSample(medias, lights, cell, ray, 0.5, emission, depth, 3,
                                  ignore_photons, use_scattering && (stride == 1), false, true, &scattering);
            if (stride > 1)
            {
                const int group = j / stride, first = group * stride, size = std::min(stride, count - first);
                if (group != lit)
                {
                    const int at = first + std::min(size - 1, int(Draw(drawKey, kDrawMediaSample, kMediaLightDraw + group) * size));
                    const DBL d = interval.s0 + (at + 0.5) * step;
                    light.Clear();
                    ComputeMediaLight(medias, lights, cell, ray, d, ray.Evaluate(d), MathColour(1.0), light);
                    lit = group;
                }
                emission += scattering * light * step;
            }
            accumulated += emission * Exp(-(transmitted + depth * 0.5));
            transmitted += depth;
        }
        interval.te = accumulated;
        interval.od = transmitted;
        interval.samples = 1;
        threadData->Stats()[Media_Intervals]++;
    }
}

void MediaFunction::ComputeMediaAdaptiveSampling(MediaVector& medias, LightSourceEntryVector& lights, MediaIntervalVector& mediaintervals,
                                                 const Ray& ray, const Media *IMedia, DBL aa_threshold, int minsamples, bool ignore_photons, bool use_scattering)
{
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

void MediaFunction::ComputeOneMediaSample(MediaVector& medias, LightSourceEntryVector& lights, MediaInterval& mediainterval, const Ray &ray, DBL d0, MathColour& SampCol,
                                          MathColour& SampOptDepth, int sample_method, bool ignore_photons, bool use_scattering, bool photonPass, bool prepared,
                                          MathColour *scattering)
{
    // NK samples - moved d0 to parameter list
    DBL d1;
    Vector3d P, H;
    MathColour C0;
    MathColour Emission, Extinction, Scattering;

    threadData->Stats()[Media_Samples]++;

    // Set up sampling location.
    d0 *= mediainterval.ds;
    d1 = mediainterval.s0 + d0;
    H = ray.Evaluate(d1);

    // Get coefficients in current sample location.
    if (modifiers != nullptr)
        densityScratch.clear();
    for(MediaVector::iterator i(medias.begin()); i != medias.end(); i++)
    {
        P = H;

        if (!prepared || ray.IsPhotonRay() || !(*i)->fastCache || !(*i)->fastCache->CameraDensityAt(P, C0))
            Evaluate_Density_Pigment((*i)->Density, P, C0, threadData);

        if (modifiers != nullptr)
        {
            densityScratch.push_back(C0);
            continue;
        }

        Extinction += C0 * (*i)->Extinction;

        if(!ray.IsShadowTestRay())
        {
            // radiosity gathers leave out the glow of media that light the scene directly
            if (!ray.IsRadiosityRay() || !(*i)->light)
                Emission += C0 * (*i)->Emission;
            Scattering += C0 * (*i)->Scattering;
        }
    }
    if (modifiers != nullptr)
        AddModifiedCoefficients(medias, &densityScratch[0], H, (prepared && !ray.IsPhotonRay()) ? ModifierSource::kCamera : ModifierSource::kPattern,
                                Extinction, ray.IsShadowTestRay() ? nullptr : &Emission,
                                ray.IsShadowTestRay() ? nullptr : &Scattering, !ray.IsRadiosityRay());

    // Get estimate for the total optical depth of the current interval.
    SampOptDepth = Extinction * mediainterval.ds;
    if (scattering != nullptr)
        *scattering = Scattering;

    if(sample_method != 3)
        mediainterval.od += SampOptDepth;

    const bool emptyScattering = prepared && Scattering.IsZero();
    if (emptyScattering && !ray.IsShadowTestRay() && use_scattering && !ray.IsPhotonRay() && mediainterval.lit)
        lightSampleIndex++;
    if(!ray.IsShadowTestRay() && use_scattering && !ray.IsPhotonRay() && !emptyScattering)
    {
        if(mediainterval.lit)
            ComputeMediaLight(medias, lights, mediainterval, ray, d1, P, Scattering, Emission);

        // process media photons whether or not the interval is directly lit
        if (photonGatherer != nullptr)
            photonGatherer->map = const_cast<PhotonMap *>(&threadData->GetSceneData()->GetPreparedSet(ray.GetPreparedSetId()).mediaPhotonMap);
        if((photonGatherer != nullptr) && (photonGatherer->map->numPhotons > 0 ||
           threadData->GetSceneData()->photonSettings.method == 2))
        {
            ComputeMediaPhotons(medias, Emission, Scattering, ray, H);
        }
    }

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
                                      DBL d1, const Vector3d& P, const MathColour& Scattering, MathColour& Emission)
{
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
            const int points = (lights[i].light->Area_Light && (lights[i].light->emitter == nullptr)) ? kMediaAreaLightPoints : 1;
            MathColour Lit_Colour;
            for(int j = 0; j < points; j++)
            {
                const double kj = k * points + j;
                const double su = lightSampleShift[U] + kj * 0.7548776662466927 + i * 0.6180339887498949;
                const double sv = lightSampleShift[V] + kj * 0.5698402909980532 + i * 0.4142135623730950;
                const Vector2d areaSample(su - floor(su), sv - floor(sv));
                if(!(trace->TestShadow(*lights[i].light, len, Light_Ray, P, Light_Colour, &areaSample)))
                    Lit_Colour += Light_Colour;
            }
            if(!Lit_Colour.IsZero())
                ComputeMediaScatteringAttenuation(medias, Emission, Scattering, Lit_Colour / points, ray, Light_Ray);
        }
    }
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


void MediaFunction::ComputeMediaPhotons(MediaVector& medias, MathColour& Te, const MathColour& Sc, const BasicRay& ray, const Vector3d& H)
{
    if (threadData->GetSceneData()->photonSettings.method == 2 && photonGatherer)
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
            MathColour sum;
            const double radius = map.RadiusAt(H);
            map.Visit(H, radius, [&](const ProgressivePhoton& photon) {
                BasicRay light;
                light.Direction = photon.direction;
                light.Origin = photon.point - photon.direction;
                ComputeMediaScatteringAttenuation(medias, sum, Sc, photon.flux, ray, light);
                threadData->progressiveContributions++;
            });
            Te += sum / ProgressivePhotonBudget::KernelVolume(radius, 3);
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

            ComputeMediaScatteringAttenuation(medias, Colour2, Sc, Light_Colour, ray, Light_Ray);
        }

        // finish the photons equation
        Colour2 *= ( 3.0 / (M_PI * r*r*r * 4.0) );

        Te += Colour2;
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
