//******************************************************************************
///
/// @file backend/scene/view.h
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

#ifndef POVRAY_BACKEND_VIEW_H
#define POVRAY_BACKEND_VIEW_H

// Module config header file must be the first file included within POV-Ray unit header files
#include "backend/configbackend.h"
#include "backend/scene/view_fwd.h"

// C++ variants of C standard header files
//  (none at the moment)

// C++ standard header files
#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <cstdint>
#include <deque>
#include <functional>
#include <unordered_map>
#include <atomic>
#include <chrono>
#include <vector>

// POV-Ray header files (base module)
#include "base/path.h"
#include "base/types.h" // TODO - only appears to be pulled in for POVRect - can we avoid this?
#include "base/image/colourspace_fwd.h"

// POV-Ray header files (core module)
#include "core/core_fwd.h"
#include "core/bounding/bsptree.h"
#include "core/lighting/radiosity.h"
#include "core/lighting/progressivephoton.h"
#include "core/scene/camera.h"

// POV-Ray header files (backend module)
#include "backend/control/scene_fwd.h"
#include "backend/scene/viewthreaddata_fwd.h"
#include "backend/support/taskqueue.h"

namespace pov
{

using namespace pov_base;

/// Method 4: one leaf of a pixel's bisection, a square of side size around (x, y) from the pixel centre, and its sample.
struct AaCell final
{
    std::int16_t x = 0, y = 0;      ///< Offset from the pixel centre in 1/1024 pixel; leaves are never smaller than 1/256.
    std::uint8_t depth = 0;         ///< The leaf's side is 2^-depth.
    std::uint32_t id = 0;           ///< The primitives its ray hit first and last, as one number; 0 for none.
    std::uint32_t light = 0;        ///< Its lights' shadow signature.
    float w = 0;                    ///< One over the distance to that hit, 0 for none.
    RGBTColour col;
    float lab[4] = { 0, 0, 0, 0 };
    float X() const { return float(x) * (1.0f / 1024.0f); }
    float Y() const { return float(y) * (1.0f / 1024.0f); }
    float Size() const { return 1.0f / float(1u << depth); }
};

/// Method 4: what is known about every pixel; the geometry of the few that hold a line is kept apart, in an AaGeom.
struct AaFit final
{
    std::int32_t seg = -1;          ///< Chain mode: the segment whose line this pixel takes, or -1.
    std::int32_t geo = -1;          ///< The pixel's AaGeom, or -1 for none (all zero).
    std::int32_t cells = -1;        ///< Bisection: where its leaves start in the leaf arena, or -1 while it has only its centre sample.
    std::int16_t leaf = -1;         ///< Bisection: the leaf the planner chose to split.
    std::uint16_t nCells = 0;       ///< Bisection: how many leaves it has there.
    std::uint8_t kind = 0;          ///< 0 off the edge map, 1 holds a fitted line, 2 on the map with no line: bisected.
    std::uint8_t planned = 0;       ///< Chosen by the planner for the next pass: 1 by the edge map, 2 by colour variance.
    std::uint8_t probes = 0;        ///< Probes traced so far.
};

/// Method 4: the line a pixel holds.
struct AaGeom final
{
    float nx = 0, ny = 0;           ///< Unit normal, pointing at colour B.
    float lo = 0, hi = 0;           ///< The edge lies between these offsets along the normal from the pixel centre.
    float contrast = 0;             ///< OkLab distance between the two colours.
    std::int32_t seg2 = -1;         ///< Chain mode: a second segment whose line may also cross it.
    RGBTColour a, b;                ///< Colours on the two sides.
};

/// Method 4, chain mode: a straight run of a contour, held as what is known about where its line can lie.
/// The frame has its origin on the line, u along it and v towards colour B; the line is v = a + b u.
struct AaSeg final
{
    struct Pt final { float u, v; std::uint8_t b; };    ///< A sample or probe in the frame; b is 1 when it lies on side B.
    /// The geometry either side of one link the run crosses: each side's primitive, and its 1/distance as a line along the link.
    struct Side final { std::uint32_t idA, idB, lightA, lightB; float ax, ay, ex, ey, wA, gA, wB, gB; };
    float cx = 0, cy = 0, tx = 1, ty = 0, nx = 0, ny = 1;
    float a = 0, b = 0, c = 0;      ///< The edge v = a + b u + c u^2 in the frame; c is 0 unless fitted as a curve.
    bool quad = false;              ///< Fitted as a curve: its bounds keep every point, since only a line's lie on hulls.
    float uLo = 0, uHi = 0;         ///< Extent along the line.
    float contrast = 0, length = 0;
    std::vector<Pt> hullA, hullB;   ///< Upper hull of the colour A points, lower hull of the colour B ones: all that bound the line.
    std::vector<Pt> fresh;          ///< Probe results not yet folded into the hulls.
    std::vector<float> bu;          ///< The samples either side of the line, by position along it: u, colour A, colour B.
    std::vector<RGBTColour> bA, bB;
    std::vector<Side> bg;
    float w[3] = { 0, 0, 0 };       ///< How far the line could lie from its estimate at the low end, middle and high end.
    float probeU = 0, probeV = 0;   ///< Where the planner wants the next probe.
    std::uint8_t probes = 0;
    std::uint8_t state = 0;         ///< 0 open, 1 settled or contradicted.
};

class RTRData final
{
    public:
        RTRData(ViewData& v, int mrt);
        ~RTRData();

        /// wait for other threads to complete the current frame. returns the new camera if it's to change.
        const Camera *CompletedFrame();
        /// number of frames rendered in real-time raytracing mode
        unsigned int numRTRframes;
        /// this holds the pixels rendered in real-time-raytracing mode
        std::vector<POVMSFloat> rtrPixels;
        /// the number of render threads to wait for
        int numRenderThreads;
        /// the number of render threads that have completed the current RTR frame
        volatile int numRenderThreadsCompleted;

    private:
        ViewData& viewData;
        std::mutex counterMutex;
        std::mutex eventMutex;
        std::condition_variable event;
        int width;
        int height;
        unsigned int numPixelsCompleted;
};

/**
 *  ViewData class representing holding view specific data.
 *  For private use by View and Renderer classes only!
 *  Unlike scene data, dependencies on direct access to
 *  view specific data members has been removed, and as
 *  such there are no public members but only accessor
 *  methods. Please do not add public data members!!!
 */
class ViewData final
{
        // View needs access to the private view data constructor as well
        // as some private data in order to initialise it properly!
        friend class View;
    public:

        typedef std::set<unsigned int> BlockIdSet;

        /**
         *  Container for information about a rectangle to be retained between passes.
         *  To be subclasses by trace tasks.
         */
        class BlockInfo
        {
            public:
                virtual ~BlockInfo() {}
        };

        /**
         *  Get the next sub-rectangle of the view to render (if any).
         *  This method is called by the render threads when they have
         *  completed rendering one block and are ready to start rendering
         *  the next block.
         *  @param  rect            Rectangle to render.
         *  @param  serial          Rectangle serial number.
         *  @return                 True if there is another rectangle to be dispatched, false otherwise.
         */
        bool GetNextRectangle(POVRect& rect, unsigned int& serial);

        /**
         *  Get the next sub-rectangle of the view to render (if any).
         *  This method is called by the render threads when they have
         *  completed rendering one block and are ready to start rendering
         *  the next block.
         *  Avoids rectangles with certain offsets from busy rectangles.
         *  @param  rect            Rectangle to render.
         *  @param  serial          Rectangle serial number.
         *  @param  blockInfo       Additional information about the rectangle.
         *                          `nullptr` if the block is being dispatched for the first time.
         *  @param  stride          Avoid-Busy stride. If this value is non-zero, any blocks following a busy block
         *                          with an offset of a multiple of this value will not be dispatched until the busy block
         *                          has been completed.
         *  @return                 True if there is another rectangle ready to be dispatched, false otherwise.
         */
        bool GetNextRectangle(POVRect& rect, unsigned int& serial, BlockInfo*& blockInfo, unsigned int stride);

        /**
         *  Called to (fully or partially) complete rendering of a specific sub-rectangle of the view.
         *  The pixel data is sent to the frontend and pixel progress information
         *  is updated and sent to the frontend.
         *  @param  rect            Rectangle just completed.
         *  @param  serial          Serial number of rectangle just completed.
         *  @param  pixels          Pixels of completed rectangle.
         *  @param  size            Size of each pixel (width and height).
         *  @param  relevant        Mark the block as relevant for the final image for continue-trace.
         *  @param  complete        Mark the block as completely rendered for continue-trace.
         *  @param  completion      Approximate contribution of current pass to completion of this rectangle.
         *  @param  blockInfo       Pointer to additional information about the rectangle. If this value is non-`nullptr`,
         *                          the rectangle will be scheduled to be re-dispatched for another pass, and the
         *                          data passed to whichever rendering thread the rectangle will be re-dispatched to.
         *                          If this value is `nullptr`, the rectangle will not be re-dispatched.
         */
        void CompletedRectangle(const POVRect& rect, unsigned int serial, const std::vector<RGBTColour>& pixels,
                                unsigned int size, bool relevant, bool complete, float completion = 1.0,
                                BlockInfo* blockInfo = nullptr, int progressLevel = -1, POV_LONG blockTime = 0);

        /**
         *  Called to (fully or partially) complete rendering of a specific sub-rectangle of the view.
         *  The pixel data is sent to the frontend and pixel progress information
         *  is updated and sent to the frontend.
         *  @param  rect            Rectangle just completed.
         *  @param  serial          Serial number of rectangle just completed.
         *  @param  positions       Pixel positions within rectangle.
         *  @param  colors          Pixel colors for each pixel position.
         *  @param  size            Size of each pixel (width and height).
         *  @param  relevant        Mark the block as relevant for the final image for continue-trace.
         *  @param  complete        Mark the block as completely rendered for continue-trace.
         *  @param  completion      Approximate contribution of current pass to completion of this rectangle.
         *  @param  blockInfo       Pointer to additional information about the rectangle. If this value is non-`nullptr`,
         *                          the rectangle will be scheduled to be re-dispatched for another pass, and the
         *                          data passed to whichever rendering thread the rectangle will be re-dispatched to.
         *                          If this value is `nullptr`, the rectangle will not be re-dispatched.
         */
        void CompletedRectangle(const POVRect& rect, unsigned int serial, const std::vector<Vector2d>& positions,
                                const std::vector<RGBTColour>& colors, unsigned int size, bool relevant, bool complete,
                                float completion = 1.0, BlockInfo* blockInfo = nullptr, int progressLevel = -1, POV_LONG blockTime = 0,
                                const std::vector<POVMSInt>* map = nullptr);

        /**
         *  Called to (fully or partially) complete rendering of a specific sub-rectangle of the view without updating pixel data.
         *  Pixel progress information is updated and sent to the frontend.
         *  @param  rect            Rectangle just completed.
         *  @param  serial          Serial number of rectangle just completed.
         *  @param  completion      Approximate contribution of current pass to completion of this rectangle.
         *  @param  blockInfo       Pointer to additional information about the rectangle. If this value is non-`nullptr`,
         *                          the rectangle will be scheduled to be re-dispatched for another pass, and the
         *                          data passed to whichever rendering thread the rectangle will be re-dispatched to.
         *                          If this value is `nullptr`, the rectangle will not be re-dispatched.
         */
        void CompletedRectangle(const POVRect& rect, unsigned int serial, float completion = 1.0, BlockInfo* blockInfo = nullptr);

        /**
         *  Set the blocks not to generate with GetNextRectangle because they have
         *  already been rendered.
         *  @param  bsl             Block serial numbers to skip.
         *  @param  fs              First block to start with checking with serial number.
         */
        void SetNextRectangle(const BlockIdSet& bsl, unsigned int fs, bool keepProgress = false);

        /**
         *  Get width of view in pixels.
         *  @return                 Width in pixels.
         */
        inline unsigned int GetWidth() const { return width; }

        /**
         *  Get height of view in pixels.
         *  @return                 Height in pixels.
         */
        inline unsigned int GetHeight() const { return height; }

        /**
         *  Get area of view to be rendered in pixels.
         *  @return                 Area rectangle in pixels.
         */
        inline const POVRect& GetRenderArea() const { return renderArea; }

        /**
         *  Get the camera for this view.
         *  @return                 Current camera.
         */
        inline const Camera& GetCamera() const { return camera; }

        /**
         *  Get the scene data for this view.
         *  @return                 Scene data.
         */
        inline std::shared_ptr<BackendSceneData>& GetSceneData() { return sceneData; }

        /**
         *  Get the subsurface cell cache shared by this view's render threads.
         *  @return                 Subsurface cache.
         */
        std::shared_ptr<SubsurfaceCache> GetSubsurfaceCache() const { return subsurfaceCache; }
        /// When photon shooting began, for its `Preparation: photons` lines.
        std::chrono::steady_clock::time_point photonPreparationStart;
        /// Steady-clock milliseconds of the last `Preparation: photons` line, shared by the shooting threads.
        std::atomic<long long> photonPreparationLine{0};

        /**
         *  Get the view id for this view.
         *  @return                 View id.
         */
        inline RenderBackend::ViewId GetViewId() { return viewId; } // TODO FIXME - more like a hack, need a better way to do this

        /**
         *  Get the highest trace level found when last rendering this view.
         *  @return                 Highest trace level found so far.
         */
        unsigned int GetHighestTraceLevel();

        /**
         *  Set the highest trace level found while rendering this view.
         *  @param  htl             Highest trace level found so far.
         */
        void SetHighestTraceLevel(unsigned int htl);

        /**
         *  Get the render quality features to use when rendering this view.
         *  @return                 Quality feature flags.
         */
        const QualityFlags& GetQualityFeatureFlags() const;

        /**
         *  Get the radiosity cache.
         *  @return                 Radiosity cache.
         */
        RadiosityCache& GetRadiosityCache();
        RadiosityCache& GetRadiosityCache(const Camera& camera);
        const RadiosityCaches& GetRadiosityCaches() const { return radiosityCaches; }

        /// Sample of a progressive render's lattice (pixel centres, or pixel corners for method 2), kept for anti-aliasing.
        void ActivateLatticeSamples() { latticeSamplesActive = true; }
        void EnsureLatticeSamples(unsigned int width, unsigned int height)
        {
            if ((width == 0) || (height == 0))
                return;
            latticeSamplesActive = true;
            if (latticeSamples.empty() || (latticeWidth != width))
            {
                latticeWidth = width;
                latticeSamples.assign(size_t(latticeWidth) * height, RGBTColour());
            }
        }
        RGBTColour& LatticeSample(unsigned int x, unsigned int y) { return latticeSamples[x + y * latticeWidth]; }
        POV_LONG photonPrepareCPU = 0;
        POV_LONG photonMapCPU = 0;
        size_t photonPeakBytes = 0;
        unsigned int photonThreads = 0;
        /// The sets this view shoots photons for, in the order a render of its camera alone registers them, which keys their draws.
        std::vector<PreparedSetId> photonSets;
        bool KeepsLatticeSamples() const { return latticeSamplesActive; }

        /// Method 4: a pixel's index in its per-pixel state, which covers the render area only.
        size_t AaIndex(unsigned int x, unsigned int y) const { return size_t(x - aaLeft) + size_t(y - aaTop) * aaWidth; }
        /// Method 4: OkLab (l, a, b, transmittance) of a lattice sample, for the edge fits.
        float* AaLabAt(unsigned int x, unsigned int y) { return &aaLab[4 * AaIndex(x, y)]; }
        /// Method 4: the edge fitted around a pixel.
        AaFit& Fit(unsigned int x, unsigned int y) { return aaFit[AaIndex(x, y)]; }
        /// Method 4: a pixel's geometry, all zero when it has none.
        const AaGeom& Geom(const AaFit& f) const { static const AaGeom none; return (f.geo >= 0) ? aaGeom[size_t(f.geo)] : none; }
        /// Method 4: a pixel's geometry to write, made if it has none; only the single-threaded planner makes any, unless every pixel has one.
        AaGeom& GeomFor(AaFit& f)
        {
            if(f.geo < 0)
            {
                f.geo = std::int32_t(aaGeom.size());
                aaGeom.emplace_back();
            }
            return aaGeom[size_t(f.geo)];
        }
        /// Method 4: the primitives a pixel's centre ray hit first and at the end of its eye path, as one number; 0 for none.
        std::uint32_t& AaId(unsigned int x, unsigned int y) { return aaId[AaIndex(x, y)]; }
        /// Method 4: a hash of which lights were blocked at that hit, and by what; 0 when none were.
        std::uint32_t& AaLight(unsigned int x, unsigned int y) { return aaLight[AaIndex(x, y)]; }
        /// Method 4: a primitive's number in the scene's object tree, the same in a continued render; an error for one outside it.
        std::uint32_t AaObjectId(const void* object) const;
        /// Method 4: one over the distance along the centre ray to that hit, 0 for none.
        float& AaW(unsigned int x, unsigned int y) { return aaW[AaIndex(x, y)]; }
        /// Method 4: what is recorded as the centre samples are traced, over the render area.
        void StartAaState();
        /// Method 4: the planner's per-pixel state, made when the centre samples are all in.
        void StartAaPlan();
        /// Method 4: drop the planner's state once the image is resolved.
        void EndAaPlan();
        /// Method 4: samples the planner may still commit; a pass that would go past it is the last.
        std::int64_t aaBudgetLeft = 0;
        std::int64_t aaReserve = 0;     ///< Budget the edge probes leave for bisecting the map's pixels no line holds.
        /// Method 4: bisection leaves, each bisected pixel's in one run within a chunk; runs are made and moved by the planner only.
        static constexpr unsigned int kAaChunkBits = 16;
        std::vector<std::unique_ptr<AaCell[]>> aaChunks;
        size_t aaLeafEnd = 0, aaLeafLive = 0, aaLeafDead = 0;
        /// Method 4: a bisected pixel's leaves, or null while it has none.
        AaCell* Leaves(const AaFit& f)
        {
            return (f.cells >= 0) ? &aaChunks[size_t(f.cells) >> kAaChunkBits][size_t(f.cells) & ((size_t(1) << kAaChunkBits) - 1)] : nullptr;
        }
        /// Method 4: a pixel's centre sample as its one leaf.
        AaCell CentreLeaf(unsigned int x, unsigned int y)
        {
            AaCell c;
            c.col = LatticeSample(x, y);
            c.id = AaId(x, y);
            c.light = AaLight(x, y);
            c.w = AaW(x, y);
            for(int k = 0; k < 4; k++)
                c.lab[k] = AaLabAt(x, y)[k];
            return c;
        }
        /// Method 4: room for three more leaves for each pixel (by AaIndex, ascending) about to be split, moving its run if need be.
        void ReserveAaSplits(const std::vector<size_t>& split);
        bool aaExhausted = false;
        DBL aaFraction = 0.0;
        DBL aaThr = 0.0;
        int aaDepth = 3;                ///< Method 4: +R, the deepest a bisection may split.
        /// Pixel footprint scale camera rays filter textures over; zero when pigment and normal filtering are both off.
        DBL textureFilterScale = 0.0;
        /// Whether hits with a footprint filter their pigments and their normals: both with +TF unless opted out.
        bool textureFilterPigments = false, textureFilterNormals = false;
        /// Taps a texture filter starts with: 8, or 3 (the centre and two corners); any value up to 3 means 3, any other 8.
        int textureFilterTaps = 8;
        /// Method 4: the fitted segments, and the ones the planner chose to probe this round.
        std::vector<AaSeg> aaSegs;
        /// Method 4: the edge map, per pixel; bits 0 and 1 mark the east and south links as boundaries, 2 and 3 those that are
        /// depth breaks, 4 and 5 those that are changes of shadow on one surface.
        std::vector<std::uint8_t> aaLink;
        std::vector<std::uint32_t> aaProbeList;
        std::atomic<std::uint32_t> aaProbeNext {0};

        /**
         *  Get the value of the real-time raytracing option
         *  @return                 true if RTR was requested in render options
         */
        bool GetRealTimeRaytracing() { return realTimeRaytracing; }

        /**
         *  Return a pointer to the real-time raytracing data
         *  @return                 pointer to instance of class RTRData, or `nullptr` if RTR is not enabled
         */
        RTRData *GetRTRData() { return rtrData; }

    private:

        struct BlockPostponedEntry final
        {
            unsigned int blockId;
            unsigned int pass;
            BlockPostponedEntry(unsigned int id, unsigned int p) : blockId(id), pass(p) {}
        };

        /// pixels pending
        volatile unsigned int pixelsPending;
        /// pixels completed
        volatile unsigned int pixelsCompleted;
        /// Next block counter for algorithm to distribute parts of the scene to render threads.
        /// @note   Blocks with higher serial numbers may be dispatched out-of-order for certain reasons;
        ///         in that case, the dispatched block must be entered into @ref blockSkipList instead of
        ///         advancing this variable.
        /// @note   When advancing this variable, the new value should be checked against @ref blockSkipList;
        ///         if the value is in the list, it should be removed, and this variable advanced again,
        ///         repeating the process until a value is reached that is not found in @ref blockSkipList.
        volatile unsigned int nextBlock;
        /// next block counter mutex
        std::mutex nextBlockMutex;
        /// set data mutex
        std::mutex setDataMutex;
        /// Whether all blocks have been dispatched at least once.
        bool completedFirstPass;
        /// highest reached trace level
        unsigned int highestTraceLevel;
        /// width of view
        unsigned int width;
        /// height of view
        unsigned int height;
        /// width of view in blocks
        unsigned int blockWidth;
        /// height of view in blocks
        unsigned int blockHeight;
        /// width and height of a block
        unsigned int blockSize;
        /// List of blocks already rendered out-of-order.
        /// This list holds the serial numbers of all blocks ahead of nextBlock
        /// that have already been rendered in a previous aborted render now being continued.
        BlockIdSet blockSkipList;
        /// list of blocks currently rendering
        BlockIdSet blockBusyList;
        /// list of blocks postponed for some reason
        BlockIdSet blockPostponedList;
        /// list of additional block information
        std::vector<BlockInfo*> blockInfoList;
        /// area of view to be rendered
        POVRect renderArea;
        /// camera of this view
        Camera camera;
        std::shared_ptr<SubsurfaceCache> subsurfaceCache;
        RadiosityCaches radiosityCaches;
        /// scene data
        std::shared_ptr<BackendSceneData> sceneData;
        /// view id
        RenderBackend::ViewId viewId;

        /// true if real-time raytracing is requested (experimental feature)
        bool realTimeRaytracing;
        /// data specifically associated with the RTR feature
        RTRData *rtrData;

        std::vector<RGBTColour> latticeSamples;
        unsigned int latticeWidth;
        bool latticeSamplesActive;
        std::vector<float> aaLab;
        std::vector<AaFit> aaFit;
        std::deque<AaGeom> aaGeom;     ///< Grown in blocks, so it never holds twice what it needs, and what it holds never moves.
        std::vector<std::uint32_t> aaId;    ///< Method 4: the edge map's first and last primitive per pixel.
        std::vector<float> aaW;             ///< Method 4: the edge map's 1/distance per pixel.
        std::vector<std::uint32_t> aaLight; ///< Method 4: the edge map's shadow signature per pixel.
        std::unordered_map<const void*, std::uint32_t> aaObjectIds; ///< Method 4: every object of the scene tree, numbered from 1.
        unsigned int aaLeft = 0, aaTop = 0, aaWidth = 0;    ///< Method 4: the render area the per-pixel state covers.

        /// functions to compute the X & Y block
        void getBlockXY(const unsigned int nb, unsigned int &x, unsigned int &y);

        /// pattern number to use for rendering
        unsigned int renderPattern;

        /// adjusted step size for renderering (using clock arithmetic)
        unsigned int renderBlockStep;

        QualityFlags qualityFlags; // TODO FIXME - put somewhere else or split up

        /**
         *  Create view data.
         *  @param  sd              Scene data associated with the view data.
         */
        ViewData(std::shared_ptr<BackendSceneData> sd);

        /**
         *  Destructor.
         */
        ~ViewData();
};

/**
 *  View class representing an view with a specific camera
 *  being rendered.
 */
class View final
{
        // Scene needs access to the private view constructor!
        friend class Scene;
    public:
        typedef std::function<bool(ConstObjectPtr)> ObjectMatch;

        /**
         *  Destructor. Rendering will be stopped as necessary.
         */
        ~View();

        /**
         *  Render the view with the specified options. Be
         *  aware that this method is asynchronous! Threads
         *  will be started to perform the parsing and this
         *  method will return. The frontend is notified by
         *  messages of the state of rendering and all warnings
         *  and errors found.
         *  Options shall be in a kPOVObjectClass_RenderOptions
         *  POVMS obect, which is created when parsing the INI
         *  file or command line in the frontend.
         *  @param  renderOptions   Render options to use.
         */
        void StartRender(POVMS_Object& renderOptions);

        /**
         *  Stop rendering. Rendering may take a few seconds to
         *  stop. Internally stopping is performed by throwing
         *  an exception at well-defined points.
         *  If rendering is not in progress, no action is taken.
         */
        void StopRender();

        /**
         *  Pause rendering. Rendering may take a few seconds to
         *  pause. Internally pausing is performed by checking
         *  flag at well-defined points, and if it is true, a
         *  loop will repeatedly set the render threads to sleep
         *  for a few milliseconds until the pause flag is
         *  cleared again or rendering is stopped.
         *  If rendering is not in progress, no action is taken.
         */
        void PauseRender();

        /**
         *  Resume rendering that has previously been stopped.
         *  If rendering is not paussed, no action is taken.
         */
        void ResumeRender();

        /**
         *  Determine if any render thread is currently running.
         *  @return                 True if any is running, false otherwise.
         */
        bool IsRendering();

        /**
         *  Determine if rendering is paused. The rendering is considered
         *  paused if at least one render thread is paused.
         *  @return                 True if paused, false otherwise.
         */
        bool IsPaused();

        /**
         *  Determine if a previously run render thread failed.
         *  @return                 True if failed, false otherwise.
         */
        bool Failed();

        /**
         *  Get the current render statistics for the view.
         *  Note that this will query each thread, compute the total
         *  and return it.
         *  @param[out] renderStats On return, the current statistics.
         */
        void GetStatistics(POVMS_Object& renderStats);
    private:
        /// running and pending render tasks for this view
        TaskQueue renderTasks;
        /// view thread data (i.e. statistics)
        std::vector<ViewThreadData *> viewThreadData;
        /// view data
        ViewData viewData;
        /// stop request flag
        bool stopRequsted;
        /// render control thread
        std::thread *renderControlThread;
        /// BSP tree mailbox
        BSPTree::Mailbox mailbox;

        View() = delete;
        View(const View&) = delete;

        /**
         *  Create an view and associate a scene's data with it.
         *  @param  sd              Scene data to be associated with the view.
         *  @param  width           Width of view in pixels.
         *  @param  height          Height of view in pixels.
         *  @param  vid             Id of this view to include with
         *                          POVMS messages sent to the frontend.
         */
        explicit View(std::shared_ptr<BackendSceneData> sd, unsigned int width, unsigned int height, RenderBackend::ViewId vid);

        View& operator=(const View&) = delete;

        /**
         *  Dispatch any shutdown messages appropriate at the end of rendering a view (e.g. max_gradient).
         *  @param  taskq           The task queue that executed this method.
         */
        void DispatchShutdownMessages(TaskQueue&);

        /**
         *  Send the render statistics upon completion of a render.
         *  @param  taskq           The task queue that executed this method.
         */
        void SendStatistics(TaskQueue& taskq);

        /**
         *  Set the blocks not to generate with GetNextRectangle because they have
         *  already been rendered.
         *  @param  taskq           The task queue that executed this method.
         *  @param  bsl             Block serial numbers to skip.
         *  @param  fs              First block to start with checking with serial number.
         */
        void SetNextRectangle(TaskQueue& taskq, std::shared_ptr<ViewData::BlockIdSet> bsl, unsigned int fs);

        void QueueProgressiveRender(POVMS_Object& renderOptions, unsigned int tracingMethod, DBL jitterScale, DBL aaThreshold,
                                    DBL aaConfidence, unsigned int aaDepth, GammaCurvePtr& aaGamma, bool highReproducibility,
                                    size_t seed, int maxRenderThreads, int resumeLevel, std::shared_ptr<ViewData::BlockIdSet> resumeSkip,
                                    DBL aaBudget = 0.0);

        void StartLevel(TaskQueue& taskq, std::shared_ptr<ViewData::BlockIdSet> bsl, bool keepProgress);
        void QueueProgressivePhotons(size_t seed, int threads);
        void PreparePhotonBatch(TaskQueue& taskq, std::shared_ptr<std::vector<ViewThreadData*>> shooters, unsigned int pass,
                                      unsigned int batches, unsigned int neighbours);
        void ReleaseProgressiveData(TaskQueue& taskq, std::shared_ptr<std::vector<ViewThreadData*>> workers, ViewThreadData* total);
        /// Indexes the merged or loaded photon maps, counting bytes already held; false when out of memory, the queue then failing.
        bool BuildProgressivePhotonMaps(TaskQueue& taskq, unsigned int neighbours, size_t bytes);
        size_t progressivePhotonSeed = 0;
        double progressivePhotonMemory = 0.0;   ///< the photon memory budget, read once at the first batch
        ProgressivePhotonCeiling progressivePhotonCeiling;   ///< where the budget comes from, resolved once per render

        /// Method 4's planning step between two passes.
        void PlanAntialias(TaskQueue& taskq, int pass, int round);

        void EndRadiosityStateFile(TaskQueue& taskq, Path file);

        /**
         *  Thread controlling the render task queue.
         */
        void RenderControlThread();

        /// An object containing the point that the match accepts, or nullptr.
        ConstObjectPtr FindCameraObject(const Vector3d& point, const ObjectMatch& match);
        ConstObjectPtr FindCameraObject(const Vector3d& point, const BBOX_TREE *node, const ObjectMatch& match);
};

}
// end of namespace pov

#endif // POVRAY_BACKEND_VIEW_H
