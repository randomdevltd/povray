//******************************************************************************
///
/// @file backend/render/tracetask.h
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

#ifndef POVRAY_BACKEND_TRACETASK_H
#define POVRAY_BACKEND_TRACETASK_H

// Module config header file must be the first file included within POV-Ray unit header files
#include "backend/configbackend.h"

// C++ variants of C standard header files
//  (none at the moment)

// C++ standard header files
#include <cstdint>
#include <unordered_map>
#include <vector>

// POV-Ray header files (base module)
#include "base/types.h"
#include "base/image/colourspace_fwd.h"

// POV-Ray header files (core module)
#include "core/lighting/radiosity.h"
#include "core/material/media.h"
#include "core/render/tracepixel.h"

// POV-Ray header files (backend module)
#include "backend/render/rendertask.h"

namespace pov
{

#ifdef PROFILE_INTERSECTIONS
    // NB not thread-safe (and not intended to be)
    extern POV_ULONG gIntersectionTime;
    extern std::vector<std::vector<POV_ULONG>> gBSPIntersectionTimes;
    extern std::vector<std::vector<POV_ULONG>> gBVHIntersectionTimes;
    extern std::vector<std::vector<POV_ULONG>> *gIntersectionTimes;
#endif

class TraceTask final : public RenderTask
{
    public:
        TraceTask(ViewData *vd, unsigned int tm, DBL js,
                  DBL aat, DBL aac, unsigned int aad, pov_base::GammaCurvePtr& aag,
                  unsigned int ps, bool psc, bool contributesToImage, bool hr, size_t seed,
                  int level = -1, unsigned int ls = 0, bool lf = false, bool pairs = false,
                  DBL aab = 0.0);
        virtual ~TraceTask() override;

        virtual void Run() override;
        virtual void Stopped() override;
        virtual void Finish() override;
    private:
        class CooperateFunction final : public Trace::CooperateFunctor
        {
            public:
                CooperateFunction(Task& t) : task(t) { }
                virtual void operator()() override { task.Cooperate(); }
            private:
                Task& task;
        };

        class SubdivisionBuffer final
        {
            public:
                SubdivisionBuffer(size_t s);

                void SetSample(size_t x, size_t y, const RGBTColour& col);
                bool Sampled(size_t x, size_t y);

                RGBTColour& operator()(size_t x, size_t y);

                void Clear();

                struct Edge final
                {
                    std::vector<RGBTColour> colors;
                    std::vector<bool> sampled;
                };
                /// Copy one side of the buffer (column `pos`, or row `pos`) to or from a neighbouring pixel's buffer.
                void SaveEdge(size_t pos, bool column, Edge& edge) const;
                void LoadEdge(size_t pos, bool column, const Edge& edge);
            private:
                std::vector<RGBTColour> colors;
                std::vector<bool> sampled;
                size_t size;
        };

        unsigned int tracingMethod;
        DBL jitterScale;
        DBL aaThreshold;
        DBL aaConfidence;
        unsigned int aaDepth;
        DBL aaBudget;
        unsigned int previewSize;
        bool previewSkipCorner;
        bool passContributesToImage;    ///< Pass computes pixels for the final image.
        bool passCompletesImage;        ///< Pass is the last one computing pixels for the final image.
        bool highReproducibility;
        pov_base::GammaCurvePtr aaGamma;
        int progressLevel;              ///< Pass of a progressive render, or -1 for a render in block order.
        unsigned int latticeStep;       ///< Sample spacing of a progressive level; 0 for its anti-aliasing pass.
        bool latticeFirst;              ///< The first level, which also traces the points of the coarser lattice.
        bool aaPairs;                   ///< Method 4's first anti-aliasing pass, which tests pairs of neighbours.

        /// A colour in OKLab, with its transmittance, for method 4's comparisons.
        struct OkLab final { float l, a, b, t; };
        /// Method 4's cells of the pixel it refines: a quadtree keyed by level and position, each cell naming its sample.
        struct Cell final { int sample; bool split; bool traced; };
        std::unordered_map<std::uint64_t, Cell> cells;
        std::vector<RGBTColour> cellColours;
        std::vector<OkLab> cellLabs;
        std::vector<std::uint64_t> cellQueue;
        /// OKLab of the current block's centre samples, with a one-pixel border.
        std::vector<OkLab> blockLabs;
        int blockLabLeft, blockLabTop, blockLabWidth;
        unsigned int refineX, refineY, refineDirections;

        /// tracing core
        TracePixel trace;

        CooperateFunction cooperate;
        MediaFunction media;
        RadiosityFunction radiosity;
        PhotonGatherer photonGatherer;

        void SimpleSamplingM0();
        void SimpleSamplingM0P();
        void NonAdaptiveSupersamplingM1();
        void AdaptiveSupersamplingM2();
        void StochasticSupersamplingM3();

        void ProgressiveLevel();
        void ProgressiveRefineM1();
        void ProgressiveRefineM2();
        bool DiffersFromSample(const RGBTColour& gcCur, unsigned int x, unsigned int y);

        void ProgressivePairsM4();
        void ProgressiveRefineM4();
        OkLab ToOkLab(const RGBTColour& col);
        bool Contended(const OkLab& a, const OkLab& b) const;
        void LoadBlockLabs(const pov_base::POVRect& rect);
        const OkLab& BlockLab(int x, int y) const { return blockLabs[(x - blockLabLeft) + (y - blockLabTop) * blockLabWidth]; }
        void TraceSample(DBL x, DBL y, unsigned int px, unsigned int py, RGBTColour& col);
        void RefinePixelM4(unsigned int x, unsigned int y, unsigned int directions, RGBTColour& col);
        void SplitCell(unsigned int level, unsigned int cx, unsigned int cy, int direction);
        void CompareCell(std::uint64_t key);

        void NonAdaptiveSupersamplingForOnePixel(DBL x, DBL y, RGBTColour& leftcol, RGBTColour& topcol, RGBTColour& curcol, bool& sampleleft, bool& sampletop, bool& samplecurrent);
        void SupersampleOnePixel(DBL x, DBL y, RGBTColour& col);
        void SubdivideOnePixel(DBL x, DBL y, DBL d, size_t bx, size_t by, size_t bstep, SubdivisionBuffer& buffer, RGBTColour& result, int level);
};

}
// end of namespace pov

#endif // POVRAY_BACKEND_TRACETASK_H
