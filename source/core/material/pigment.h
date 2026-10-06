//******************************************************************************
///
/// @file core/material/pigment.h
///
/// Declarations related to pigments.
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

#ifndef POVRAY_CORE_PIGMENT_H
#define POVRAY_CORE_PIGMENT_H

// Module config header file must be the first file included within POV-Ray unit header files
#include "core/configcore.h"

// C++ variants of C standard header files
//  (none at the moment)

// C++ standard header files
#include <memory>
#include <vector>

// POV-Ray header files (base module)
#include "base/image/colourspace_fwd.h"

// POV-Ray header files (core module)
#include "core/coretypes.h"
#include "core/material/blendmap.h"
#include "core/render/ray_fwd.h"

namespace pov
{

//##############################################################################
///
/// @defgroup PovCoreMaterialPigment Pigments
/// @ingroup PovCore
///
/// @{

/// Common interface for pigment-like blend maps.
///
/// This class provides the common interface for both pigment and colour blend maps.
///
class GenericPigmentBlendMap
{
    public:

        int                     blendMode;
        pov_base::GammaCurvePtr blendGamma;

        GenericPigmentBlendMap() : blendMode(0), blendGamma() {}
        virtual ~GenericPigmentBlendMap() {}

        virtual bool Compute(TransColour& colour, DBL value, const Vector3d& IPoint, const Intersection *Intersect, const Ray *ray, TraceThreadData *Thread) = 0;
        virtual void ComputeAverage(TransColour& colour, const Vector3d& EPoint, const Intersection *Intersect, const Ray *ray, TraceThreadData *Thread) = 0;
        virtual bool ComputeUVMapped(TransColour& colour, const Intersection *Intersect, const Ray *ray, TraceThreadData *Thread) = 0;
        virtual void Post(bool& rHasFilter) = 0;

        void Blend(TransColour& result, const TransColour& colour1, DBL weight1, const TransColour& colour2, DBL weight2, TraceThreadData *thread);
};

/// Colour blend map.
class ColourBlendMap final : public BlendMap<TransColour>, public GenericPigmentBlendMap
{
    public:

        ColourBlendMap();
        ColourBlendMap(int n, const Entry aEntries[]);

        virtual bool Compute(TransColour& colour, DBL value, const Vector3d& IPoint, const Intersection *Intersect, const Ray *ray, TraceThreadData *Thread) override;
        virtual void ComputeAverage(TransColour& colour, const Vector3d& EPoint, const Intersection *Intersect, const Ray *ray, TraceThreadData *Thread) override;
        virtual bool ComputeUVMapped(TransColour& colour, const Intersection *Intersect, const Ray *ray, TraceThreadData *Thread) override;
        virtual void Post(bool& rHasFilter) override;
};

/// Pigment blend map.
class PigmentBlendMap final : public BlendMap<PIGMENT*>, public GenericPigmentBlendMap
{
    public:

        PigmentBlendMap(BlendMapTypeId type);
        virtual ~PigmentBlendMap() override;

        virtual bool Compute(TransColour& colour, DBL value, const Vector3d& IPoint, const Intersection *Intersect, const Ray *ray, TraceThreadData *Thread) override;
        virtual void ComputeAverage(TransColour& colour, const Vector3d& EPoint, const Intersection *Intersect, const Ray *ray, TraceThreadData *Thread) override;
        virtual bool ComputeUVMapped(TransColour& colour, const Intersection *Intersect, const Ray *ray, TraceThreadData *Thread) override;
        virtual void Post(bool& rHasFilter) override;
};

typedef std::shared_ptr<GenericPigmentBlendMap>         GenericPigmentBlendMapPtr;
typedef std::shared_ptr<const GenericPigmentBlendMap>   GenericPigmentBlendMapConstPtr;

typedef PIGMENT*                                        PigmentBlendMapData;
typedef BlendMapEntry<PigmentBlendMapData>              PigmentBlendMapEntry;
typedef std::shared_ptr<PigmentBlendMap>                PigmentBlendMapPtr;
typedef std::shared_ptr<const PigmentBlendMap>          PigmentBlendMapConstPtr;

typedef TransColour                                     ColourBlendMapData;
typedef BlendMapEntry<ColourBlendMapData>               ColourBlendMapEntry;
typedef std::shared_ptr<ColourBlendMap>                 ColourBlendMapPtr;
typedef std::shared_ptr<const ColourBlendMap>           ColourBlendMapConstPtr;

struct Pigment_Struct final : public Pattern_Struct
{
    std::shared_ptr<GenericPigmentBlendMap> Blend_Map;
    TransColour colour;       // may have a filter/transmit component
    TransColour Quick_Colour; // may have a filter/transmit component    // TODO - can't we decide between regular colour and quick_colour at parse time already?
};


PIGMENT *Create_Pigment();
PIGMENT *Copy_Pigment(PIGMENT *Old);
void Copy_Pigments (std::vector<PIGMENT*>& New, const std::vector<PIGMENT*>& Old);
void Destroy_Pigment(PIGMENT *Pigment);
void Post_Pigment(PIGMENT *Pigment, bool* pHasFilter = nullptr);
bool Compute_Pigment(TransColour& colour, const PIGMENT *Pigment, const Vector3d& IPoint, const Intersection *Intersect, const Ray *ray, TraceThreadData *Thread);
bool PigmentRequiresRayContext(const PIGMENT *pigment);
void Evaluate_Density_Pigment(std::vector<PIGMENT*>& Density, const Vector3d& p, MathColour& c, TraceThreadData *ttd);

/// Most points the batched Evaluate_Density_Pigment takes at once.
const size_t kDensityBatch = 16;

/// Evaluate_Density_Pigment at each of n <= kDensityBatch points, bit for bit.
void Evaluate_Density_Pigment(std::vector<PIGMENT*>& Density, const Vector3d *p, MathColour *c, size_t n, TraceThreadData *ttd);

struct ContinuousPattern;

/// A density pigment classified once: a constant colour, a transformed continuous pattern over a linear colour map, or
/// anything else.
struct DensityPigmentPlan final
{
    const PIGMENT *pigment;
    const ContinuousPattern *pattern; ///< set for a transformed continuous pattern over a linear colour map
    const ColourBlendMap *map;
    bool constant;
    bool sorted;                      ///< the map's entries are in order, so ranges along segments hold
    MathColour colour;                ///< the colour where it is constant
};

void Plan_Density_Pigment(DensityPigmentPlan& plan, const PIGMENT *pigment, TraceThreadData *ttd);

/// Multiplies each of n <= kDensityBatch colours by the planned pigment's colour at its point, bit for bit as
/// Evaluate_Density_Pigment does.
void Apply_Density_Pigment(const DensityPigmentPlan& plan, const Vector3d *p, MathColour *c, size_t n, TraceThreadData *ttd);

/// Per-channel range of the planned pigment's colour along the segment from a to b; false where there is none.
bool Density_Range(const DensityPigmentPlan& plan, const Vector3d& a, const Vector3d& b, MathColour& lo, MathColour& hi);

/// @}
///
//##############################################################################

}
// end of namespace pov

#endif // POVRAY_CORE_PIGMENT_H
