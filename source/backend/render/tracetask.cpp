//******************************************************************************
///
/// @file backend/render/tracetask.cpp
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

// Unit header file must be the first file included within POV-Ray *.cpp files (pulls in config)
#include "backend/render/tracetask.h"

// C++ variants of C standard header files
//  (none at the moment)

// C++ standard header files
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <cstdlib>
#include <ctime>
#include <time.h>
#include <limits>
#include <new>
#include <string>

// POV-Ray header files (base module)
#include "base/image/colourspace.h"
#ifdef PROFILE_INTERSECTIONS
#include "base/image/image_fwd.h"
#endif

// POV-Ray header files (core module)
#include "core/material/normal.h"
#include "core/math/chi2.h"
#include "core/math/jitter.h"
#include "core/math/matrix.h"
#include "core/render/trace.h"
#include "core/support/statistics.h"

// POV-Ray header files (POVMS module)
//  (none at the moment)

// POV-Ray header files (backend module)
#include "backend/scene/backendscenedata.h"
#include "backend/scene/view.h"
#include "backend/scene/viewthreaddata.h"

// this must be the last file included
#include "base/povdebug.h"

namespace pov
{

using std::min;
using std::max;
using std::vector;

class BlockTimer final
{
    public:
        BlockTimer() : started(Last()) { }
        POV_LONG Elapsed() const
        {
            const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
            const POV_LONG elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - started).count();
            Last() = now;
            return std::max<POV_LONG>(1, elapsed);
        }
    private:
        static std::chrono::steady_clock::time_point& Last()
        {
            static thread_local std::chrono::steady_clock::time_point last = std::chrono::steady_clock::now();
            return last;
        }
        std::chrono::steady_clock::time_point started;
};

#ifdef PROFILE_INTERSECTIONS
    bool gDoneBSP;
    bool gDoneBVH;
    POV_ULONG gMinVal = std::numeric_limits<POV_ULONG>::max();
    POV_ULONG gMaxVal = 0;
    POV_ULONG gIntersectionTime;
    vector<vector<POV_ULONG>> gBSPIntersectionTimes;
    vector<vector<POV_ULONG>> gBVHIntersectionTimes;
    vector<vector<POV_ULONG>> *gIntersectionTimes;
#endif

class SmartBlock final
{
    public:
        SmartBlock(int ox, int oy, int bw, int bh);

        bool GetFlag(int x, int y) const;
        void SetFlag(int x, int y, bool f);

        RGBTColour& operator()(int x, int y);
        const RGBTColour& operator()(int x, int y) const;

        vector<RGBTColour>& GetPixels();
    private:
        vector<RGBTColour> framepixels;
        vector<RGBTColour> pixels;
        vector<bool> frameflags;
        vector<bool> flags;
        int offsetx;
        int offsety;
        int blockwidth;
        int blockheight;

        int GetOffset(int x, int y) const;
};

SmartBlock::SmartBlock(int ox, int oy, int bw, int bh) :
    offsetx(ox),
    offsety(oy),
    blockwidth(bw),
    blockheight(bh)
{
    framepixels.resize((blockwidth * 2) + (blockheight * 2) + 4);
    pixels.resize(blockwidth * blockheight);
    frameflags.resize((blockwidth * 2) + (blockheight * 2) + 4);
    flags.resize(blockwidth * blockheight);
}

bool SmartBlock::GetFlag(int x, int y) const
{
    int offset = GetOffset(x, y);

    if(offset < 0)
        return frameflags[-1 - offset];
    else
        return flags[offset];
}

void SmartBlock::SetFlag(int x, int y, bool f)
{
    int offset = GetOffset(x, y);

    if(offset < 0)
        frameflags[-1 - offset] = f;
    else
        flags[offset] = f;
}

RGBTColour& SmartBlock::operator()(int x, int y)
{
    int offset = GetOffset(x, y);

    if(offset < 0)
        return framepixels[-1 - offset];
    else
        return pixels[offset];
}

const RGBTColour& SmartBlock::operator()(int x, int y) const
{
    int offset = GetOffset(x, y);

    if(offset < 0)
        return framepixels[-1 - offset];
    else
        return pixels[offset];
}

vector<RGBTColour>& SmartBlock::GetPixels()
{
    return pixels;
}

int SmartBlock::GetOffset(int x, int y) const
{
    x -= offsetx;
    y -= offsety;

    if(x < 0)
        x = -1;
    else if(x >= blockwidth)
        x = blockwidth;

    if(y < 0)
        y = -1;
    else if(y >= blockheight)
        y = blockheight;

    if((x < 0) && (y < 0))
        return -1;
    else if((x >= blockwidth) && (y < 0))
        return -2;
    else if((x < 0) && (y >= blockheight))
        return -3;
    else if((x >= blockwidth) && (y >= blockheight))
        return -4;
    else if(x < 0)
        return -(4 + y);
    else if(y < 0)
        return -(4 + x + blockheight);
    else if(x >= blockwidth)
        return -(4 + y + blockheight + blockwidth);
    else if(y >= blockheight)
        return -(4 + x + blockheight + blockwidth + blockheight);
    else
        return (x + (y * blockwidth));
}

TraceTask::SubdivisionBuffer::SubdivisionBuffer(size_t s) :
    colors(s * s),
    sampled(s * s),
    size(s)
{
    Clear();
}

void TraceTask::SubdivisionBuffer::SetSample(size_t x, size_t y, const RGBTColour& col)
{
    colors[x + (y * size)] = col;
    sampled[x + (y * size)] = true;
}

bool TraceTask::SubdivisionBuffer::Sampled(size_t x, size_t y)
{
    return sampled[x + (y * size)];
}

RGBTColour& TraceTask::SubdivisionBuffer::operator()(size_t x, size_t y)
{
    return  colors[x + (y * size)];
}

void TraceTask::SubdivisionBuffer::Clear()
{
    sampled.assign(sampled.size(), false);
}

void TraceTask::SubdivisionBuffer::SaveEdge(size_t pos, bool column, Edge& edge) const
{
    edge.colors.resize(size);
    edge.sampled.resize(size);
    for(size_t i = 0; i < size; i++)
    {
        size_t index = column ? (pos + (i * size)) : (i + (pos * size));
        edge.colors[i] = colors[index];
        edge.sampled[i] = sampled[index];
    }
}

void TraceTask::SubdivisionBuffer::LoadEdge(size_t pos, bool column, const Edge& edge)
{
    for(size_t i = 0; i < size; i++)
    {
        if(edge.sampled[i])
        {
            size_t index = column ? (pos + (i * size)) : (i + (pos * size));
            colors[index] = edge.colors[i];
            sampled[index] = true;
        }
    }
}

TraceTask::TraceTask(ViewData *vd, unsigned int tm, DBL js,
                     DBL aat, DBL aac, unsigned int aad, pov_base::GammaCurvePtr& aag,
                     unsigned int ps, bool psc, bool contributesToImage, bool hr, size_t seed,
                     int level, unsigned int ls, bool lf, DBL aab, int aap, int aar) :
    RenderTask(vd, seed, "Trace"),
    trace(vd->GetSceneData(), &vd->GetCamera(), GetViewDataPtr(), vd->GetSceneData()->parsedMaxTraceLevel, vd->GetSceneData()->parsedAdcBailout,
          vd->GetQualityFeatureFlags(), cooperate, media, radiosity),
    cooperate(*this),
    tracingMethod(tm),
    jitterScale(js),
    aaThreshold(aat),
    aaConfidence(aac),
    aaDepth(aad),
    aaBudget(aab),
    aaGamma(aag),
    previewSize(ps),
    previewSkipCorner(psc),
    passContributesToImage(contributesToImage),
    passCompletesImage((ps == 0) || ((ps == 1) && contributesToImage)),
    highReproducibility(hr),
    progressLevel(level),
    latticeStep(ls),
    latticeFirst(lf),
    aaPass(aap),
    aaRound(aar),
    media(GetViewDataPtr(), &trace, &photonGatherer),
    radiosity(vd->GetSceneData(), GetViewDataPtr(),
              vd->GetSceneData()->radiositySettings, vd->GetRadiosityCaches(), cooperate, true, vd->GetCamera().Location),
    photonGatherer(&vd->GetSceneData()->GetPreparedSet(0).mediaPhotonMap, vd->GetSceneData()->photonSettings)
{
#ifdef PROFILE_INTERSECTIONS
    Rectangle ra = vd->GetRenderArea();
    if (vd->GetSceneData()->boundingMethod == 2)
    {
        gBSPIntersectionTimes.clear();
        gBSPIntersectionTimes.resize(ra.bottom + 1);
        for (int i = 0; i < ra.bottom + 1; i++)
            gBSPIntersectionTimes[i].resize(ra.right + 1);
        gIntersectionTimes = &gBSPIntersectionTimes;
        gDoneBSP = true;
    }
    else
    {
        gBVHIntersectionTimes.clear();
        gBVHIntersectionTimes.resize(ra.bottom + 1);
        for (int i = 0; i < ra.bottom + 1; i++)
            gBVHIntersectionTimes[i].resize(ra.right + 1);
        gIntersectionTimes = &gBVHIntersectionTimes;
        gDoneBVH = true;
    }
#endif
    // TODO: this could be initialised someplace more suitable
    GetViewDataPtr()->qualityFlags = vd->GetQualityFeatureFlags();
    trace.SetTextureFilterScale(vd->textureFilterScale);
    trace.SetTextureFilterTaps(vd->textureFilterTaps);
    trace.SetTextureFilterKinds(vd->textureFilterPigments, vd->textureFilterNormals);
    trace.recordShadows = (tracingMethod == 4);
}

TraceTask::~TraceTask()
{
}

void TraceTask::Run()
{
#ifdef RTR_HACK
    bool forever = GetViewData()->GetRealTimeRaytracing();
    do
    {
#endif
        if(progressLevel >= 0)
        {
            if(latticeStep > 0)
                ProgressiveLevel();
            else if(tracingMethod == 4)
                ProgressiveM4();
            else if(tracingMethod == 1)
                ProgressiveRefineM1();
            else if(tracingMethod == 2)
                ProgressiveRefineM2();
        }
        else switch(tracingMethod)
        {
            case 0:
                if(previewSize > 0)
                    SimpleSamplingM0P();
                else
                    SimpleSamplingM0();
                break;
            case 1:
                NonAdaptiveSupersamplingM1();
                break;
            case 2:
                AdaptiveSupersamplingM2();
                break;
            case 3:
                StochasticSupersamplingM3();
                break;
        }

#ifdef RTR_HACK
        if(forever)
        {
            const Camera *camera = GetViewData()->GetRTRData()->CompletedFrame();
            Cooperate();
            if (camera != nullptr)
                trace.SetupCamera(*camera);
        }
    } while(forever);
#endif

    trace.FlushSubsurfaceCounts();
    GetViewData()->SetHighestTraceLevel(trace.GetHighestTraceLevel());
}

void TraceTask::Stopped()
{
    // nothing to do for now [trf]
}

void TraceTask::Finish()
{
    GetViewDataPtr()->timeType = TraceThreadData::kRenderTime;
    GetViewDataPtr()->realTime = ConsumedRealTime();
    GetViewDataPtr()->cpuTime = ConsumedCPUTime();

#ifdef PROFILE_INTERSECTIONS
    if (gDoneBSP && gDoneBVH)
    {
        int width = gBSPIntersectionTimes[0].size();
        int height = gBSPIntersectionTimes.size();
        if (width == gBVHIntersectionTimes[0].size() && height == gBVHIntersectionTimes.size())
        {
            SNGL scale = 1.0 / (gMaxVal - gMinVal);
            ImageWriteOptions opts;
            opts.bitsPerChannel = 16;
            Image *img = Image::Create(width, height, ImageDataType::Gray_Int16, false);
            for (int y = 0 ; y < height ; y++)
                for (int x = 0 ; x < width ; x++)
                    img->SetGrayValue(x, y, (gBSPIntersectionTimes[y][x] - gMinVal) * scale);
            OStream *imagefile(NewOStream("bspprofile.png", 0, false));
            Image::Write(Image::PNG, imagefile, img, opts);
            delete imagefile;
            delete img;

            img = Image::Create(width, height, ImageDataType::Gray_Int16, false);
            imagefile = NewOStream("bvhprofile.png", 0, false);
            for (int y = 0 ; y < height ; y++)
                for (int x = 0 ; x < width ; x++)
                    img->SetGrayValue(x, y, (gBVHIntersectionTimes[y][x] - gMinVal) * scale);
            Image::Write(Image::PNG, imagefile, img, opts);
            delete imagefile;
            delete img;

            img = Image::Create(width, height, ImageDataType::Gray_Int16, false);
            imagefile = NewOStream("summedprofile.png", 0, false);
            for (int y = 0 ; y < height ; y++)
                for (int x = 0 ; x < width ; x++)
                    img->SetGrayValue(x, y, 0.5f + ((((gBSPIntersectionTimes[y][x] - gMinVal) - (gBVHIntersectionTimes[y][x] - gMinVal)) * scale) / 2));
            Image::Write(Image::PNG, imagefile, img, opts);
            delete imagefile;
            delete img;

            img = Image::Create(width, height, ImageDataType::RGBFT_Float, false);
            imagefile = NewOStream("rgbprofile.png", 0, false);
            for (int y = 0 ; y < height ; y++)
            {
                for (int x = 0 ; x < width ; x++)
                {
                    RGBTColour col;
                    float bspval = (gBSPIntersectionTimes[y][x] - gMinVal) * scale ;
                    float bvhval = (gBVHIntersectionTimes[y][x] - gMinVal) * scale ;
                    float diff = bspval - bvhval ;
                    if (diff > 0.0)
                        col.blue() += diff ;
                    else
                        col.red() -= diff ;
                    img->SetRGBTValue(x, y, col);
                }
            }
            Image::Write(Image::PNG, imagefile, img, opts);
            delete imagefile;
            delete img;
        }
        gDoneBSP = gDoneBVH = false;
        gMinVal = std::numeric_limits<POV_ULONG>::max();
        gMaxVal = 0;
    }
#endif
}

void TraceTask::SimpleSamplingM0()
{
    POVRect rect;
    vector<RGBTColour> pixels;
    unsigned int serial;

    while(GetViewData()->GetNextRectangle(rect, serial) == true)
    {
        BlockTimer blockTimer;
        radiosity.BeforeTile(highReproducibility? serial : 0, RadiosityFunction::FINAL_TRACE);

        pixels.clear();
        pixels.reserve(rect.GetArea());

        for(DBL y = DBL(rect.top); y <= DBL(rect.bottom); y++)
        {
            for(DBL x = DBL(rect.left); x <= DBL(rect.right); x++)
            {
#ifdef PROFILE_INTERSECTIONS
                POV_LONG it = std::numeric_limits<POV_ULONG>::max();
                for (int i = 0 ; i < 3 ; i++)
                {
                    TransColour c;
                    gIntersectionTime = 0;
                    trace(x+0.5, y+0.5, GetViewData()->GetWidth(), GetViewData()->GetHeight(), c);
                    if (gIntersectionTime < it)
                        it = gIntersectionTime;
                }
                (*gIntersectionTimes)[(int) y] [(int) x] = it;
                if (it < gMinVal)
                    gMinVal = it;
                if (it > gMaxVal)
                    gMaxVal = it;
#endif
                RGBTColour col;

                trace(x+0.5, y+0.5, GetViewData()->GetWidth(), GetViewData()->GetHeight(), col);
                GetViewDataPtr()->Stats()[Number_Of_Pixels]++;

                pixels.push_back(col);

                Cooperate();
            }
        }

        radiosity.AfterTile();

        GetViewDataPtr()->AfterTile();
        GetViewData()->CompletedRectangle(rect, serial, pixels, 1, passContributesToImage, passCompletesImage,
                                          1.0f, nullptr, -1, blockTimer.Elapsed());

        Cooperate();
    }
}

void TraceTask::SimpleSamplingM0P()
{
    DBL stepsize(previewSize);
    POVRect rect;
    vector<Vector2d> pixelpositions;
    vector<RGBTColour> pixelcolors;
    unsigned int serial;

    while(GetViewData()->GetNextRectangle(rect, serial) == true)
    {
        BlockTimer blockTimer;
        radiosity.BeforeTile(highReproducibility? serial : 0, RadiosityFunction::FINAL_TRACE);

        unsigned int px = (rect.GetWidth() + previewSize - 1) / previewSize;
        unsigned int py = (rect.GetHeight() + previewSize - 1) / previewSize;

        pixelpositions.clear();
        pixelpositions.reserve(px * py);
        pixelcolors.clear();
        pixelcolors.reserve(px * py);

        for(DBL y = DBL(rect.top); y <= DBL(rect.bottom); y += stepsize)
        {
            for(DBL x = DBL(rect.left); x <= DBL(rect.right); x += stepsize)
            {
                if((previewSkipCorner == true) && (fmod(x, stepsize * 2.0) < EPSILON) && (fmod(y, stepsize * 2.0) < EPSILON))
                    continue;

#ifdef PROFILE_INTERSECTIONS
                POV_LONG it = std::numeric_limits<POV_ULONG>::max();
                for (int i = 0 ; i < 3 ; i++)
                {
                    TransColour c;
                    gIntersectionTime = 0;
                    trace(x+0.5, y+0.5, GetViewData()->GetWidth(), GetViewData()->GetHeight(), c);
                    if (gIntersectionTime < it)
                        it = gIntersectionTime;
                }
                (*gIntersectionTimes)[(int) y] [(int) x] = it;
                if (it < gMinVal)
                    gMinVal = it;
                if (it > gMaxVal)
                    gMaxVal = it;
#endif
                RGBTColour col;

                trace(x+0.5, y+0.5, GetViewData()->GetWidth(), GetViewData()->GetHeight(), col);
                GetViewDataPtr()->Stats()[Number_Of_Pixels]++;

                pixelpositions.push_back(Vector2d(x, y));
                pixelcolors.push_back(col);

                Cooperate();
            }
        }

        radiosity.AfterTile();

        GetViewDataPtr()->AfterTile();
        if(pixelpositions.size() > 0)
            GetViewData()->CompletedRectangle(rect, serial, pixelpositions, pixelcolors, previewSize, passContributesToImage,
                                              passCompletesImage, 1.0f, nullptr, -1, blockTimer.Elapsed());

        Cooperate();
    }
}

void TraceTask::NonAdaptiveSupersamplingM1()
{
    POVRect rect;
    unsigned int serial;

    jitterScale = jitterScale / DBL(aaDepth);

    while(GetViewData()->GetNextRectangle(rect, serial) == true)
    {
        BlockTimer blockTimer;
        radiosity.BeforeTile(highReproducibility? serial : 0, RadiosityFunction::FINAL_TRACE);

        SmartBlock pixels(rect.left, rect.top, rect.GetWidth(), rect.GetHeight());

        // sample line above current block
        for(int x = rect.left; x <= rect.right; x++)
        {
            trace(x+0.5, rect.top-0.5, GetViewData()->GetWidth(), GetViewData()->GetHeight(), pixels(x, rect.top - 1));
            GetViewDataPtr()->Stats()[Number_Of_Pixels]++;

            // Cannot supersample this pixel, so just claim it was already supersampled! [trf]
            // [CJC] see comment for leftmost pixels below; similar situation applies here
            pixels.SetFlag(x, rect.top - 1, true);

            Cooperate();
        }

        for(int y = rect.top; y <= rect.bottom; y++)
        {
            trace(rect.left-0.5, y+0.5, GetViewData()->GetWidth(), GetViewData()->GetHeight(), pixels(rect.left - 1, y)); // sample pixel left of current line in block
            GetViewDataPtr()->Stats()[Number_Of_Pixels]++;

            // Cannot supersample this pixel, so just claim it was already supersampled! [trf]

            // [CJC] NB this could in some circumstances cause an artifact at a block boundary,
            // if the leftmost pixel on this blockline ends up not being supersampled because the
            // difference between it and the rightmost pixel on the same line in the last block
            // was insufficient to trigger the supersample right now, *BUT* if the rightmost pixel
            // *had* been supersampled, AND the difference was then enough to trigger the call
            // to supersample the current pixel, AND when the block on the left was/is rendered,
            // the abovementioned rightmost pixel *does* get supersampled due to the logic applied
            // when the code rendered *that* block ... [a long set of preconditions but possible].

            // there's no easy solution to this because if we *do* supersample right now, the
            // reverse situation could apply if the rightmost pixel in the last block ends up
            // not being supersampled ...
            pixels.SetFlag(rect.left - 1, y, true);

            Cooperate();

            for(int x = rect.left; x <= rect.right; x++)
            {
                // trace current pixel
                trace(x+0.5, y+0.5, GetViewData()->GetWidth(), GetViewData()->GetHeight(), pixels(x, y));
                GetViewDataPtr()->Stats()[Number_Of_Pixels]++;

                Cooperate();

                bool sampleleft = (pixels.GetFlag(x - 1, y) == false);
                bool sampletop = (pixels.GetFlag(x, y - 1) == false);
                bool samplecurrent = true;

                // perform antialiasing
                NonAdaptiveSupersamplingForOnePixel(DBL(x), DBL(y), pixels(x - 1, y), pixels(x, y - 1), pixels(x, y), sampleleft, sampletop, samplecurrent);

                // if these pixels have been supersampled, set their supersampling flag
                if(sampleleft == true)
                    pixels.SetFlag(x - 1, y, true);
                if(sampletop == true)
                    pixels.SetFlag(x, y - 1, true);
                if(samplecurrent == true)
                    pixels.SetFlag(x, y, true);
            }
        }

        radiosity.AfterTile();

        GetViewDataPtr()->AfterTile();
        GetViewData()->CompletedRectangle(rect, serial, pixels.GetPixels(), 1, passContributesToImage, passCompletesImage,
                                          1.0f, nullptr, -1, blockTimer.Elapsed());

        Cooperate();
    }
}

void TraceTask::AdaptiveSupersamplingM2()
{
    POVRect rect;
    unsigned int serial;
    size_t subsize = (size_t(1) << aaDepth);
    SubdivisionBuffer buffer(subsize + 1);

    jitterScale = jitterScale / DBL((1 << aaDepth) + 1);

    while(GetViewData()->GetNextRectangle(rect, serial) == true)
    {
        BlockTimer blockTimer;
        radiosity.BeforeTile(highReproducibility? serial : 0, RadiosityFunction::FINAL_TRACE);

        SmartBlock pixels(rect.left, rect.top, rect.GetWidth(), rect.GetHeight());

        for(int y = rect.top; y <= rect.bottom + 1; y++)
        {
            for(int x = rect.left; x <= rect.right + 1; x++)
            {
                // trace upper-left corners of all pixels
                trace(x, y, GetViewData()->GetWidth(), GetViewData()->GetHeight(), pixels(x, y));
                GetViewDataPtr()->Stats()[Number_Of_Pixels]++;

                Cooperate();
            }
        }

        // note that the bottom and/or right corner are the
        // upper-left corner of the bottom and/or right pixels
        for(int y = rect.top; y <= rect.bottom; y++)
        {
            for(int x = rect.left; x <= rect.right; x++)
            {
                buffer.Clear();

                buffer.SetSample(0, 0, pixels(x, y));
                buffer.SetSample(0, subsize, pixels(x, y + 1));
                buffer.SetSample(subsize, 0, pixels(x + 1, y));
                buffer.SetSample(subsize, subsize, pixels(x + 1, y + 1));

                SubdivideOnePixel(DBL(x), DBL(y), 0.5, 0, 0, subsize, buffer, pixels(x, y), aaDepth - 1);

                Cooperate();
            }
        }

        radiosity.AfterTile();

        GetViewDataPtr()->AfterTile();
        GetViewData()->CompletedRectangle(rect, serial, pixels.GetPixels(), 1, passContributesToImage, passCompletesImage,
                                          1.0f, nullptr, -1, blockTimer.Elapsed());

        Cooperate();
    }
}

void TraceTask::StochasticSupersamplingM3()
{
    POVRect rect;
    vector<RGBTColour> pixels;
    vector<PreciseRGBTColour> pixelsSum;
    vector<PreciseRGBTColour> pixelsSumSqr;
    vector<unsigned int> pixelsSamples;
    unsigned int serial;
    bool sampleMore;

    // Create list of thresholds for confidence test.
    vector<double> confidenceFactor;
    unsigned int minSamples = 1; // TODO currently hard-coded
    unsigned int maxSamples = max(minSamples, 1u << (aaDepth*2));

    confidenceFactor.reserve(maxSamples*5);
    double threshold  = aaThreshold;
    double confidence = aaConfidence;
    if(maxSamples > 1)
    {
        for(int n = 1; n <= maxSamples*5; n++)
            confidenceFactor.push_back(ndtri((1+confidence)/2) / sqrt((double)n));
    }
    else
        confidenceFactor.push_back(0.0);

    while(GetViewData()->GetNextRectangle(rect, serial) == true)
    {
        BlockTimer blockTimer;
        radiosity.BeforeTile(highReproducibility? serial : 0, RadiosityFunction::FINAL_TRACE);

        pixels.clear();
        pixelsSum.clear();
        pixelsSumSqr.clear();
        pixelsSamples.clear();
        pixels.reserve(rect.GetArea());
        pixelsSum.reserve(rect.GetArea());
        pixelsSumSqr.reserve(rect.GetArea());
        pixelsSamples.reserve(rect.GetArea());

        do
        {
            sampleMore = false;
            unsigned int index = 0;
            for(unsigned int y = rect.top; y <= rect.bottom; y++)
            {
                for(unsigned int x = rect.left; x <= rect.right; x++)
                {
                    PreciseRGBTColour neighborSum;
                    PreciseRGBTColour neighborSumSqr;
                    unsigned int neighborSamples(0);
                    unsigned int samples(0);
                    unsigned int index2;

                    if (index < pixelsSamples.size())
                    {
                        samples          = pixelsSamples [index];
                        neighborSum      = pixelsSum     [index];
                        neighborSumSqr   = pixelsSumSqr  [index];
                        neighborSamples  = pixelsSamples [index];
                    }

                    // TODO - we should obtain information about the neighboring render blocks as well
                    index2 = index - 1;
                    if (x > rect.left)
                    {
                        neighborSum     += pixelsSum     [index2];
                        neighborSumSqr  += pixelsSumSqr  [index2];
                        neighborSamples += pixelsSamples [index2];
                    }
                    index2 = index - rect.GetWidth();
                    if (y > rect.top)
                    {
                        neighborSum     += pixelsSum     [index2];
                        neighborSumSqr  += pixelsSumSqr  [index2];
                        neighborSamples += pixelsSamples [index2];
                    }
                    index2 = index + 1;
                    if ((x < rect.right) && (index2 < pixelsSamples.size()))
                    {
                        neighborSum     += pixelsSum     [index2];
                        neighborSumSqr  += pixelsSumSqr  [index2];
                        neighborSamples += pixelsSamples [index2];
                    }
                    index2 = index + rect.GetWidth();
                    if ((y < rect.bottom) && (index2 < pixelsSamples.size()))
                    {
                        neighborSum     += pixelsSum     [index2];
                        neighborSumSqr  += pixelsSumSqr  [index2];
                        neighborSamples += pixelsSamples [index2];
                    }

                    while(true)
                    {
                        if (samples >= minSamples)
                        {
                            if (samples >= maxSamples)
                                break;

                            PreciseRGBTColour variance = (neighborSumSqr - Sqr(neighborSum)/neighborSamples) / (neighborSamples-1);
                            double cf = confidenceFactor[neighborSamples-1];
                            PreciseRGBTColour sqrtvar = Sqrt(variance);
                            PreciseRGBTColour confidenceDelta = sqrtvar * cf;
                            if (confidenceDelta.red() +
                                confidenceDelta.green() +
                                confidenceDelta.blue() +
                                confidenceDelta.transm() <= threshold)
                                break;
                        }

                        RGBTColour colTemp;
                        PreciseRGBTColour col, colSqr;

                        const std::uint64_t pixelKey = DeriveKey(GetViewDataPtr()->stochasticRandomSeedBase, kDrawAntialias, (std::uint64_t(y) << 32) + x);
                        Vector2d jitter(Draw(pixelKey, kDrawAntialias, 2 * samples) - 0.5, Draw(pixelKey, kDrawAntialias, 2 * samples + 1) - 0.5);
                        trace(x+0.5 + jitter.x(), y+0.5 + jitter.y(), GetViewData()->GetWidth(), GetViewData()->GetHeight(), colTemp);

                        col = PreciseRGBTColour(GammaCurve::Encode(aaGamma, colTemp));
                        colSqr = Sqr(col);

                        if (index >= pixelsSamples.size())
                        {
                            GetViewDataPtr()->Stats()[Number_Of_Pixels]++;

                            pixels.push_back(colTemp);
                            pixelsSum.push_back(col);
                            pixelsSumSqr.push_back(colSqr);
                            pixelsSamples.push_back(1);
                        }
                        else
                        {
                            pixels [index] += colTemp;
                            pixelsSum [index] += col;
                            pixelsSumSqr [index] += colSqr;
                            pixelsSamples [index] ++;
                        }

                        neighborSum     += col;
                        neighborSumSqr  += colSqr;
                        neighborSamples ++;

                        samples         ++;

                        // Whenever one or more pixels are re-sampled, neighborhood variance constraints may require us to also re-sample others.
                        sampleMore = true;

                        Cooperate();

                        if (samples >= minSamples) // TODO
                            break;
                    }

                    index ++;
                }
            }
        }
        while (sampleMore);

        // So far we've just accumulated the samples for any pixel;
        // now compute the actual average.
        unsigned int index = 0;
        for(unsigned int y = rect.top; y <= rect.bottom; y++)
        {
            for(unsigned int x = rect.left; x <= rect.right; x++)
            {
                pixels [index] /= pixelsSamples[index];
                index ++;
            }
        }

        radiosity.AfterTile();

        GetViewDataPtr()->AfterTile();
        GetViewData()->CompletedRectangle(rect, serial, pixels, 1, passContributesToImage, passCompletesImage,
                                          1.0f, nullptr, -1, blockTimer.Elapsed());

        Cooperate();
    }
}

void TraceTask::M4Record(std::uint32_t& id, float& w, std::uint32_t& light)
{
    ViewData* vd = GetViewData();
    id = vd->AaObjectId(trace.primaryObject);
    // one over the distance, so a scaled scene keeps every ratio of it
    w = ((trace.primaryDepth > 0.0) && (trace.primaryDepth < HUGE_VAL)) ? float(1.0 / trace.primaryDepth) : 0.0f;
    // the set of (light, first blocker) pairs, hashed in a fixed order; 0 for an empty set
    std::vector<std::pair<int, std::uint32_t>> set;
    set.reserve(trace.primaryShadows.size());
    for(const auto& s : trace.primaryShadows)
        set.emplace_back(s.first, vd->AaObjectId(s.second));
    std::sort(set.begin(), set.end());
    set.erase(std::unique(set.begin(), set.end()), set.end());
    std::uint64_t h = 0;
    for(const auto& s : set)
    {
        h ^= (std::uint64_t(std::uint32_t(s.first)) << 32 | std::uint64_t(s.second)) + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
        h *= 0xBF58476D1CE4E5B9ull;
        h ^= h >> 31;
    }
    light = set.empty() ? 0u : (std::uint32_t(h ^ (h >> 32)) | 1u);
}

void TraceTask::ProgressiveLevel()
{
    const POVRect& area = GetViewData()->GetRenderArea();
    const bool corners = (tracingMethod == 2);
    const bool keep = GetViewData()->KeepsLatticeSamples();
    POVRect rect;
    vector<Vector2d> positions;
    vector<RGBTColour> colors;
    std::vector<POVMSInt> map;
    unsigned int serial;

    while(GetViewData()->GetNextRectangle(rect, serial) == true)
    {
        BlockTimer blockTimer;
        radiosity.BeforeTile(highReproducibility? serial : 0, RadiosityFunction::FINAL_TRACE);

        positions.clear();
        colors.clear();
        map.clear();

        // the last blocks of the area also own the pixel corners on its far edges
        unsigned int right = rect.right + ((corners && (rect.right == area.right)) ? 1 : 0);
        unsigned int bottom = rect.bottom + ((corners && (rect.bottom == area.bottom)) ? 1 : 0);
        unsigned int left = ((rect.left + latticeStep - 1) / latticeStep) * latticeStep;
        unsigned int top = ((rect.top + latticeStep - 1) / latticeStep) * latticeStep;

        for(unsigned int y = top; y <= bottom; y += latticeStep)
        {
            for(unsigned int x = left; x <= right; x += latticeStep)
            {
                if(!latticeFirst && (x % (2 * latticeStep) == 0) && (y % (2 * latticeStep) == 0))
                    continue;

                RGBTColour col;

                if(corners)
                    trace(DBL(x), DBL(y), GetViewData()->GetWidth(), GetViewData()->GetHeight(), col);
                else
                    trace(DBL(x)+0.5, DBL(y)+0.5, GetViewData()->GetWidth(), GetViewData()->GetHeight(), col);
                GetViewDataPtr()->Stats()[Number_Of_Pixels]++;

                if(keep)
                {
                    GetViewData()->LatticeSample(x, y) = col;
                    if(tracingMethod == 4)
                    {
                        std::uint32_t id, light;
                        float w;
                        M4Record(id, w, light);
                        GetViewData()->AaId(x, y) = id;
                        GetViewData()->AaW(x, y) = w;
                        GetViewData()->AaLight(x, y) = light;
                        std::uint32_t bits;
                        std::memcpy(&bits, &w, sizeof(bits));
                        map.push_back(POVMSInt(id));
                        map.push_back(POVMSInt(bits));
                        map.push_back(POVMSInt(light));
                    }
                }
                positions.push_back(Vector2d(x, y));
                colors.push_back(col);

                Cooperate();
            }
        }

        radiosity.AfterTile();

        GetViewDataPtr()->AfterTile();
        if(positions.empty())
            GetViewData()->CompletedRectangle(rect, serial, 0.0f);
        else
            GetViewData()->CompletedRectangle(rect, serial, positions, colors, latticeStep, true, true,
                                              float(positions.size()) / float(rect.GetArea()), nullptr, progressLevel,
                                              blockTimer.Elapsed(), (keep && (tracingMethod == 4)) ? &map : nullptr);

        Cooperate();
    }
}

bool TraceTask::DiffersFromSample(const RGBTColour& gcCur, unsigned int x, unsigned int y)
{
    return (ColourDistanceRGBT(GammaCurve::Encode(aaGamma, GetViewData()->LatticeSample(x, y)), gcCur) >= aaThreshold);
}

void TraceTask::ProgressiveRefineM1()
{
    const POVRect& area = GetViewData()->GetRenderArea();
    POVRect rect;
    vector<RGBTColour> pixels;
    unsigned int serial;

    jitterScale = jitterScale / DBL(aaDepth);

    while(GetViewData()->GetNextRectangle(rect, serial) == true)
    {
        BlockTimer blockTimer;
        radiosity.BeforeTile(highReproducibility? serial : 0, RadiosityFunction::FINAL_TRACE);

        pixels.clear();
        pixels.reserve(rect.GetArea());

        for(unsigned int y = rect.top; y <= rect.bottom; y++)
        {
            for(unsigned int x = rect.left; x <= rect.right; x++)
            {
                RGBTColour col = GetViewData()->LatticeSample(x, y);
                RGBTColour gcCur = GammaCurve::Encode(aaGamma, col);

                // every neighbour is a real centre sample, including those in other blocks
                if(((x > area.left)   && DiffersFromSample(gcCur, x - 1, y)) ||
                   ((x < area.right)  && DiffersFromSample(gcCur, x + 1, y)) ||
                   ((y > area.top)    && DiffersFromSample(gcCur, x, y - 1)) ||
                   ((y < area.bottom) && DiffersFromSample(gcCur, x, y + 1)))
                    SupersampleOnePixel(DBL(x), DBL(y), col);

                pixels.push_back(col);

                Cooperate();
            }
        }

        radiosity.AfterTile();

        GetViewDataPtr()->AfterTile();
        GetViewData()->CompletedRectangle(rect, serial, pixels, 1, true, true, 1.0f, nullptr, progressLevel,
                                          blockTimer.Elapsed());

        Cooperate();
    }
}

void TraceTask::ProgressiveRefineM2()
{
    POVRect rect;
    vector<RGBTColour> pixels;
    unsigned int serial;
    size_t subsize = (size_t(1) << aaDepth);
    SubdivisionBuffer buffer(subsize + 1);
    SubdivisionBuffer::Edge leftEdge;
    vector<SubdivisionBuffer::Edge> topEdges;

    jitterScale = jitterScale / DBL((1 << aaDepth) + 1);

    while(GetViewData()->GetNextRectangle(rect, serial) == true)
    {
        BlockTimer blockTimer;
        radiosity.BeforeTile(highReproducibility? serial : 0, RadiosityFunction::FINAL_TRACE);

        pixels.clear();
        pixels.reserve(rect.GetArea());
        topEdges.resize(rect.GetWidth());

        for(unsigned int y = rect.top; y <= rect.bottom; y++)
        {
            for(unsigned int x = rect.left; x <= rect.right; x++)
            {
                RGBTColour col;

                // samples on an edge shared with the pixel to the left or above were traced for that pixel
                buffer.Clear();
                if(x > rect.left)
                    buffer.LoadEdge(0, true, leftEdge);
                if(y > rect.top)
                    buffer.LoadEdge(0, false, topEdges[x - rect.left]);

                buffer.SetSample(0, 0, GetViewData()->LatticeSample(x, y));
                buffer.SetSample(0, subsize, GetViewData()->LatticeSample(x, y + 1));
                buffer.SetSample(subsize, 0, GetViewData()->LatticeSample(x + 1, y));
                buffer.SetSample(subsize, subsize, GetViewData()->LatticeSample(x + 1, y + 1));

                SubdivideOnePixel(DBL(x), DBL(y), 0.5, 0, 0, subsize, buffer, col, aaDepth - 1);

                buffer.SaveEdge(subsize, true, leftEdge);
                buffer.SaveEdge(subsize, false, topEdges[x - rect.left]);
                pixels.push_back(col);

                Cooperate();
            }
        }

        radiosity.AfterTile();

        GetViewDataPtr()->AfterTile();
        GetViewData()->CompletedRectangle(rect, serial, pixels, 1, true, true, 1.0f, nullptr, progressLevel,
                                          blockTimer.Elapsed());

        Cooperate();
    }
}

TraceTask::OkLab TraceTask::OkLabOf(ViewData* vd, const RGBTColour& col)
{
    const RGBColour c = GammaCurve::Decode(vd->GetSceneData()->workingGamma, col.rgb());
    const float l = std::cbrt(0.4122214708f * c.red() + 0.5363325363f * c.green() + 0.0514459929f * c.blue());
    const float m = std::cbrt(0.2119034982f * c.red() + 0.6806995451f * c.green() + 0.1073969566f * c.blue());
    const float s = std::cbrt(0.0883024619f * c.red() + 0.2817188376f * c.green() + 0.6299787005f * c.blue());
    return OkLab { 0.2104542553f * l + 0.7936177850f * m - 0.0040720468f * s,
                   1.9779984951f * l - 2.4285922050f * m + 0.4505937099f * s,
                   0.0259040371f * l + 0.7827717662f * m - 0.8086757660f * s,
                   col.transm() };
}

TraceTask::OkLab TraceTask::ToOkLab(const RGBTColour& col)
{
    return OkLabOf(GetViewData(), col);
}

void TraceTask::TraceSample(DBL x, DBL y, RGBTColour& col, DBL footprint, std::uint32_t* id, float* w, std::uint32_t* light)
{
    trace.footprintFraction = footprint;
    // the random draws are the pixel centre's, so extra samples average grain no more at an edge than elsewhere
    trace(x, y, GetViewData()->GetWidth(), GetViewData()->GetHeight(), col, Vector2d(std::floor(x) + 0.5, std::floor(y) + 0.5));
    trace.footprintFraction = 1.0;
    if(id)
        M4Record(*id, *w, *light);
    GetViewDataPtr()->Stats()[Number_Of_Samples]++;
    Cooperate();
}

namespace
{
/// Probes stop on a segment whose line can move by less than this, in pixels.
constexpr float kM4Gap = 0.001f;
/// The fewest contour crossings a straight or curved run may be fitted to.
constexpr int kM4RunMin = 4;
/// A parabola must beat a line by this margin per pixel of its sag to be preferred.
constexpr float kM4Bend = 2.0f;
/// Edge map: a change of 1/distance is a break when it is this many times what the surface's own curvature explains.
constexpr double kM4Break = 4.0;
/// Edge map: changes of 1/distance below this fraction of it are rounding.
constexpr double kM4Floor = 1.0 / 65536.0;

inline float Dist2(const float* a, const float* b)
{
    const float d0 = a[0] - b[0], d1 = a[1] - b[1], d2 = a[2] - b[2], d3 = a[3] - b[3];
    return d0 * d0 + d1 * d1 + d2 * d2 + d3 * d3;
}

/// Priorities within about a tenth of an octave share a bucket, and buckets are what the planner ties on.
int M4Bucket(float priority)
{
    return std::min(255, std::max(0, int(10.0f * (std::log2(std::max(priority, 1.0e-6f)) + 14.0f))));
}

struct M4Poly final { float x[8], y[8]; int n; };

/// The part of a convex polygon on one side of the line n.q = s.
M4Poly M4Clip(const M4Poly& p, float nx, float ny, float s, bool beyond)
{
    M4Poly o;
    o.n = 0;
    const float sign = beyond ? 1.0f : -1.0f;
    for(int i = 0; i < p.n; i++)
    {
        const int j = (i + 1) % p.n;
        const float di = sign * (nx * p.x[i] + ny * p.y[i] - s), dj = sign * (nx * p.x[j] + ny * p.y[j] - s);
        if(di >= 0.0f)
        {
            o.x[o.n] = p.x[i];
            o.y[o.n] = p.y[i];
            o.n++;
        }
        if((di >= 0.0f) != (dj >= 0.0f))
        {
            const float t = di / (di - dj);
            o.x[o.n] = p.x[i] + t * (p.x[j] - p.x[i]);
            o.y[o.n] = p.y[i] + t * (p.y[j] - p.y[i]);
            o.n++;
        }
    }
    return o;
}

float M4PolyArea(const M4Poly& p)
{
    float a = 0.0f;
    for(int i = 0; i < p.n; i++)
    {
        const int j = (i + 1) % p.n;
        a += p.x[i] * p.y[j] - p.x[j] * p.y[i];
    }
    return std::fabs(a) * 0.5f;
}

/// Fraction of the unit pixel lying beyond the line n.q = s, with the pixel centre at the origin.
float M4Coverage(float nx, float ny, float s)
{
    float a = std::fabs(nx), b = std::fabs(ny);
    if(a < b)
        std::swap(a, b);
    const float ext = 0.5f * (a + b);
    if(s <= -ext)
        return 1.0f;
    if(s >= ext)
        return 0.0f;
    float cdf;
    const float m = 0.5f * (a - b);
    if(b < 1.0e-6f)
        cdf = 0.5f + s / a;
    else if(s < -m)
        cdf = (s + ext) * (s + ext) / (2.0f * a * b);
    else if(s > m)
        cdf = 1.0f - (ext - s) * (ext - s) / (2.0f * a * b);
    else
        cdf = 0.5f + s / a;
    return 1.0f - std::min(1.0f, std::max(0.0f, cdf));
}

/// A colour held per channel within the range of the pixel's own sample and the region colours it was built from.
RGBTColour M4Bounded(const RGBTColour& c, const RGBTColour& own, const RGBTColour* sums, const double* weights, int n = 4)
{
    RGBTColour lo = own, hi = own;
    for(int i = 0; i < n; i++)
    {
        if(weights[i] <= 0.0)
            continue;
        const RGBTColour r = sums[i] * (1.0 / weights[i]);
        lo = RGBTColour(std::min(lo.red(), r.red()), std::min(lo.green(), r.green()), std::min(lo.blue(), r.blue()), std::min(lo.transm(), r.transm()));
        hi = RGBTColour(std::max(hi.red(), r.red()), std::max(hi.green(), r.green()), std::max(hi.blue(), r.blue()), std::max(hi.transm(), r.transm()));
    }
    return RGBTColour(std::min(hi.red(), std::max(lo.red(), c.red())), std::min(hi.green(), std::max(lo.green(), c.green())),
                      std::min(hi.blue(), std::max(lo.blue(), c.blue())), std::min(hi.transm(), std::max(lo.transm(), c.transm())));
}

/// Where a contour crosses a link between two neighbouring samples, and the pixels of those samples, the one left of travel first.
struct M4Bracket final { float cx, cy; int lx, ly, rx, ry; };

/// The line with the widest gap between the samples that must lie beyond it (left of the chain) and short of it (right).
/// Returns that gap, negative when no line separates them; n.q = s is the line, with n pointing at the left samples.
float M4BestLine(const std::vector<M4Bracket>& br, int i0, int i1, float& nx, float& ny, float& s)
{
    float tx = br[i1].cx - br[i0].cx, ty = br[i1].cy - br[i0].cy;
    float len = std::sqrt(tx * tx + ty * ty);
    if(len < 1.0e-3f)
    {
        tx = 1.0f;
        ty = 0.0f;
        len = 1.0f;
    }
    const float theta0 = std::atan2(-tx / len, ty / len);

    auto gapAt = [&](float th, float& lo, float& hi)
    {
        const float c = std::cos(th), sn = std::sin(th);
        float minL = std::numeric_limits<float>::max(), maxR = -minL;
        for(int i = i0; i <= i1; i++)
        {
            const M4Bracket& b = br[i];
            minL = std::min(minL, c * (float(b.lx) + 0.5f) + sn * (float(b.ly) + 0.5f));
            maxR = std::max(maxR, c * (float(b.rx) + 0.5f) + sn * (float(b.ry) + 0.5f));
        }
        lo = maxR;
        hi = minL;
        return minL - maxR;
    };

    float best = -std::numeric_limits<float>::max(), bestTh = theta0, bestLo = 0.0f, bestHi = 0.0f, lo, hi;
    const float step0 = 3.14159265f / 24.0f;
    for(int k = -8; k <= 8; k++)
    {
        const float th = theta0 + float(k) * step0;
        const float g = gapAt(th, lo, hi);
        if(g > best)
        {
            best = g; bestTh = th; bestLo = lo; bestHi = hi;
        }
    }
    float step = step0;
    for(int r = 0; r < 6; r++)
    {
        step *= 0.5f;
        for(int d = -1; d <= 1; d += 2)
        {
            const float th = bestTh + float(d) * step;
            const float g = gapAt(th, lo, hi);
            if(g > best)
            {
                best = g; bestTh = th; bestLo = lo; bestHi = hi;
            }
        }
    }
    nx = std::cos(bestTh);
    ny = std::sin(bestTh);
    s = 0.5f * (bestLo + bestHi);
    return best;
}


/// Every line v = a + b u that keeps the colour A points below it and the colour B points above is possible; take the
/// middle of those and work out how far either way the line could lie at three places along it. False when none is.
bool M4EstimateQ(AaSeg& s, float bend);

bool M4Estimate(AaSeg& s)
{
    if(s.quad)
        return M4EstimateQ(s, kM4Bend);
    auto alo = [&](float b)
    {
        float m = -std::numeric_limits<float>::max();
        for(const AaSeg::Pt& p : s.hullA)
            m = std::max(m, p.v - b * p.u);
        return m;
    };
    auto ahi = [&](float b)
    {
        float m = std::numeric_limits<float>::max();
        for(const AaSeg::Pt& p : s.hullB)
            m = std::min(m, p.v - b * p.u);
        return m;
    };
    auto gap = [&](float b) { return ahi(b) - alo(b); };

    const float bmax = 0.6f;
    float l = -bmax, r = bmax;
    for(int i = 0; i < 48; i++)
    {
        const float m1 = l + (r - l) / 3.0f, m2 = r - (r - l) / 3.0f;
        if(gap(m1) < gap(m2))
            l = m1;
        else
            r = m2;
    }
    const float bs = 0.5f * (l + r);
    if(gap(bs) < 0.0f)
    {
        s.b = bs;
        s.a = 0.5f * (alo(bs) + ahi(bs));
        s.w[0] = s.w[1] = s.w[2] = 0.0f;
        return false;
    }

    // the slopes that leave room form an interval around the widest one
    float b1 = -bmax, b2 = bmax;
    if(gap(b1) < 0.0f)
    {
        float lo = b1, hi = bs;
        for(int i = 0; i < 40; i++)
        {
            const float mid = 0.5f * (lo + hi);
            if(gap(mid) >= 0.0f) hi = mid; else lo = mid;
        }
        b1 = hi;
    }
    if(gap(b2) < 0.0f)
    {
        float lo = bs, hi = b2;
        for(int i = 0; i < 40; i++)
        {
            const float mid = 0.5f * (lo + hi);
            if(gap(mid) >= 0.0f) lo = mid; else hi = mid;
        }
        b2 = lo;
    }
    s.b = 0.5f * (b1 + b2);
    s.a = 0.5f * (alo(s.b) + ahi(s.b));

    const float us[3] = { s.uLo, 0.5f * (s.uLo + s.uHi), s.uHi };
    for(int k = 0; k < 3; k++)
    {
        const float u = us[k];
        float lo = b1, hi = b2;
        for(int i = 0; i < 48; i++)
        {
            const float m1 = lo + (hi - lo) / 3.0f, m2 = hi - (hi - lo) / 3.0f;
            if(ahi(m1) + m1 * u < ahi(m2) + m2 * u) lo = m1; else hi = m2;
        }
        const float top = ahi(0.5f * (lo + hi)) + 0.5f * (lo + hi) * u;
        lo = b1;
        hi = b2;
        for(int i = 0; i < 48; i++)
        {
            const float m1 = lo + (hi - lo) / 3.0f, m2 = hi - (hi - lo) / 3.0f;
            if(alo(m1) + m1 * u > alo(m2) + m2 * u) lo = m1; else hi = m2;
        }
        const float bottom = alo(0.5f * (lo + hi)) + 0.5f * (lo + hi) * u;
        s.w[k] = std::max(0.0f, top - bottom);
    }
    return true;
}

/// The offsets a that let v = a + b u + c u^2 pass above every colour A point and below every colour B one: [lo, hi].
inline void M4Room(const AaSeg& s, float b, float c, float& lo, float& hi)
{
    lo = -std::numeric_limits<float>::max();
    hi = std::numeric_limits<float>::max();
    for(const AaSeg::Pt& p : s.hullA)
        lo = std::max(lo, p.v - (b + c * p.u) * p.u);
    for(const AaSeg::Pt& p : s.hullB)
        hi = std::min(hi, p.v - (b + c * p.u) * p.u);
}

/// Golden-section maximum of a concave function on [lo, hi].
template<typename F> float M4GoldenMax(F f, float lo, float hi, int iterations, float& at)
{
    const float g = 0.618034f;
    float x1 = hi - g * (hi - lo), x2 = lo + g * (hi - lo), f1 = f(x1), f2 = f(x2);
    for(int i = 0; i < iterations; i++)
    {
        if(f1 < f2) { lo = x1; x1 = x2; f1 = f2; x2 = lo + g * (hi - lo); f2 = f(x2); }
        else        { hi = x2; x2 = x1; f2 = f1; x1 = hi - g * (hi - lo); f1 = f(x1); }
    }
    at = 0.5f * (lo + hi);
    return f(at);
}

/// Maximum over slope b and curvature c of a function concave in both, by nested golden sections.
template<typename F> float M4Max2(F f, float bmax, float cmax, float& bb, float& cc)
{
    auto overB = [&](float c) { float b; return M4GoldenMax([&](float x) { return f(x, c); }, -bmax, bmax, 26, b); };
    const float best = M4GoldenMax(overB, -cmax, cmax, 26, cc);
    M4GoldenMax([&](float x) { return f(x, cc); }, -bmax, bmax, 26, bb);
    return best;
}

/// The parabola fit: the most room between the samples, with curvature only where it buys more room than it bends.
bool M4EstimateQ(AaSeg& s, float bend)
{
    const float len = std::max(1.0f, s.uHi - s.uLo), bmax = 0.6f, cmax = std::min(0.35f, 1.2f / len);
    const float sag = 0.25f * len * len;
    auto gap = [&](float b, float c) { float lo, hi; M4Room(s, b, c, lo, hi); return hi - lo; };
    float b, c, lo, hi;
    if(M4Max2(gap, bmax, cmax, b, c) < 0.0f)
    {
        M4Room(s, b, c, lo, hi);
        s.b = b; s.c = c; s.a = 0.5f * (lo + hi);
        s.w[0] = s.w[1] = s.w[2] = 0.0f;
        return false;
    }
    float pb, pc;
    M4Max2([&](float x, float y) { return gap(x, y) - bend * std::fabs(y) * sag; }, bmax, cmax, pb, pc);
    if(gap(pb, pc) >= 0.0f)
    {
        b = pb;
        c = pc;
    }
    M4Room(s, b, c, lo, hi);
    s.b = b; s.c = c; s.a = 0.5f * (lo + hi);

    const float big = 1000.0f;
    const float us[3] = { s.uLo, 0.5f * (s.uLo + s.uHi), s.uHi };
    for(int k = 0; k < 3; k++)
    {
        const float u = us[k];
        float tb, tc;
        const float top = M4Max2([&](float x, float y) { float l, h; M4Room(s, x, y, l, h); return h + (x + y * u) * u + big * std::min(0.0f, h - l); }, bmax, cmax, tb, tc);
        const float bottom = -M4Max2([&](float x, float y) { float l, h; M4Room(s, x, y, l, h); return -(l + (x + y * u) * u) + big * std::min(0.0f, h - l); }, bmax, cmax, tb, tc);
        s.w[k] = std::max(0.0f, top - bottom);
    }
    return true;
}

/// Whether one parabola, in the frame of the chord from bracket i0 to i1, can keep every left sample above it and every right one below.
bool M4FeasibleQ(const std::vector<M4Bracket>& br, int i0, int i1, float& nx, float& ny, float& s)
{
    float dx = br[i1].cx - br[i0].cx, dy = br[i1].cy - br[i0].cy;
    const float dl = std::sqrt(dx * dx + dy * dy);
    if(dl < 1.0e-3f) { dx = 1.0f; dy = 0.0f; } else { dx /= dl; dy /= dl; }
    nx = -dy;
    ny = dx;
    float side = 0.0f;
    for(int i = i0; i <= i1; i++)
        side += nx * float(br[i].lx - br[i].rx) + ny * float(br[i].ly - br[i].ry);
    if(side < 0.0f) { nx = -nx; ny = -ny; }
    const float tx = -ny, ty = nx, mx = 0.5f * (br[i0].cx + br[i1].cx), my = 0.5f * (br[i0].cy + br[i1].cy);
    s = nx * mx + ny * my;
    const float uc = tx * mx + ty * my;
    AaSeg q;
    float uLo = std::numeric_limits<float>::max(), uHi = -uLo;
    for(int i = i0; i <= i1; i++)
    {
        const M4Bracket& b = br[i];
        const float ax = float(b.rx) + 0.5f, ay = float(b.ry) + 0.5f, bx = float(b.lx) + 0.5f, by = float(b.ly) + 0.5f;
        q.hullA.push_back(AaSeg::Pt { tx * ax + ty * ay - uc, nx * ax + ny * ay - s, 0 });
        q.hullB.push_back(AaSeg::Pt { tx * bx + ty * by - uc, nx * bx + ny * by - s, 1 });
        uLo = std::min(uLo, tx * b.cx + ty * b.cy - uc);
        uHi = std::max(uHi, tx * b.cx + ty * b.cy - uc);
    }
    const float len = std::max(1.0f, uHi - uLo + 1.0f);
    float b, c;
    return M4Max2([&](float x, float y) { float lo, hi; M4Room(q, x, y, lo, hi); return hi - lo; }, 0.6f, std::min(0.35f, 1.2f / len), b, c) >= 0.0f;
}

/// The tangent of a segment's edge at the point along it nearest (px, py), as the line n.q = s in image coordinates.
inline void M4LineAt(const AaSeg& sg, float px, float py, float& nx, float& ny, float& s)
{
    const float u = (px - sg.cx) * sg.tx + (py - sg.cy) * sg.ty;
    const float a = sg.a - sg.c * u * u, b = sg.b + 2.0f * sg.c * u;
    const float inv = 1.0f / std::sqrt(1.0f + b * b);
    nx = (sg.nx - b * sg.tx) * inv;
    ny = (sg.ny - b * sg.ty) * inv;
    s = a * inv + nx * sg.cx + ny * sg.cy;
}

/// Fold the last round's probe results into each segment's bounds and re-estimate its line.
void M4FoldSegments(ViewData* vd)
{
    for(AaSeg& s : vd->aaSegs)
    {
        if(s.fresh.empty())
            continue;
        const std::vector<AaSeg::Pt> keepA = s.hullA, keepB = s.hullB;
        bool third = false;
        for(const AaSeg::Pt& p : s.fresh)
        {
            if(p.b == 2)
                third = true;
            else
                (p.b ? s.hullB : s.hullA).push_back(p);
        }
        if(third)
        {
            // a primitive neither side shows: bisection takes its pixels
            s.fresh.clear();
            s.state = 3;
            continue;
        }
        s.fresh.clear();
        bool ok = M4Estimate(s);
        if(!ok && !s.quad)
        {
            // probes have shown the edge bends: let it
            s.quad = true;
            ok = M4Estimate(s);
            s.quad = ok;
        }
        if(!ok)
        {
            // a probe that contradicts the rest: keep the line as it was, probe this segment no more, and let bisection have it
            s.hullA = keepA;
            s.hullB = keepB;
            M4Estimate(s);
            s.state = 3;
        }
    }
}

/// The colours either side of a segment's line, near a position along it.
void M4LocalColours(const AaSeg& s, float u, RGBTColour& a, RGBTColour& b)
{
    const size_t n = s.bu.size();
    const size_t k = size_t(std::lower_bound(s.bu.begin(), s.bu.end(), u) - s.bu.begin());
    const size_t lo = (k > 1) ? (k - 1) : 0, hi = std::min(n, k + 2);
    a.Clear();
    b.Clear();
    for(size_t i = lo; i < hi; i++)
    {
        a += s.bA[i];
        b += s.bB[i];
    }
    const double inv = 1.0 / double(std::max<size_t>(1, hi - lo));
    a = a * inv;
    b = b * inv;
}

/// Choose the segments to probe this round, best first, before any ray is traced for them: a probe at the end where
/// the line is least certain is worth the contrast, times how uncertain it is, times how many pixels the line crosses.
void M4PlanSegments(ViewData* vd, bool enabledIn)
{
    const POVRect& area = vd->GetRenderArea();
    // what the edge probes may still spend, once the bisection tier's share is set aside
    const std::int64_t avail = vd->aaBudgetLeft - vd->aaReserve;
    const bool enabled = enabledIn && (avail > 0);
    std::vector<int> bucket(vd->aaSegs.size(), -1);
    std::int64_t hist[256] = {}, total = 0;
    if(enabled)
    {
        for(size_t i = 0; i < vd->aaSegs.size(); i++)
        {
            const AaSeg& s = vd->aaSegs[i];
            const float wavg = 0.25f * (s.w[0] + 2.0f * s.w[1] + s.w[2]);
            if(s.state || (wavg <= kM4Gap))
                continue;
            bucket[i] = M4Bucket(s.contrast * wavg * std::max(1.0f, s.length));
            hist[bucket[i]]++;
            total++;
        }
    }

    int threshold = 0;
    std::int64_t admitted = total;
    bool cut = false;
    if(enabled)
    {
        std::int64_t cum = 0;
        for(int b = 255; b >= 0; b--)
        {
            cum += hist[b];
            if(cum >= avail)
            {
                threshold = b;
                admitted = cum;
                cut = true;
                break;
            }
        }
    }

    vd->aaProbeList.clear();
    vd->aaProbeNext.store(0);
    for(size_t i = 0; i < vd->aaSegs.size(); i++)
    {
        if((bucket[i] < 0) || (bucket[i] < threshold))
            continue;
        AaSeg& s = vd->aaSegs[i];
        const float len = s.uHi - s.uLo;
        // near whichever end is less certain
        const float inset = std::min(2.0f, 0.25f * len);
        const float u = (len < 4.0f) ? 0.5f * (s.uLo + s.uHi) : ((s.w[0] >= s.w[2]) ? (s.uLo + inset) : (s.uHi - inset));
        const float v = s.a + (s.b + s.c * u) * u;
        const float px = s.cx + u * s.tx + v * s.nx, py = s.cy + u * s.ty + v * s.ny;
        if((px < float(area.left)) || (px >= float(area.right) + 1.0f) || (py < float(area.top)) || (py >= float(area.bottom) + 1.0f))
        {
            s.state = 1;
            continue;
        }
        s.probeU = u;
        s.probeV = v;
        vd->aaProbeList.push_back(std::uint32_t(i));
    }
    vd->aaBudgetLeft -= admitted;
    if(cut && (vd->aaReserve <= 0))
        vd->aaExhausted = true;
}

void M4SortBrackets(AaSeg& s)
{
    std::vector<size_t> idx(s.bu.size());
    for(size_t i = 0; i < idx.size(); i++)
        idx[i] = i;
    std::sort(idx.begin(), idx.end(), [&](size_t p, size_t q) { return s.bu[p] < s.bu[q]; });
    std::vector<float> bu(idx.size());
    std::vector<RGBTColour> bA(idx.size()), bB(idx.size());
    std::vector<AaSeg::Side> bg(idx.size());
    for(size_t i = 0; i < idx.size(); i++)
    {
        bu[i] = s.bu[idx[i]];
        bA[i] = s.bA[idx[i]];
        bB[i] = s.bB[idx[i]];
        bg[i] = s.bg[idx[i]];
    }
    s.bu.swap(bu);
    s.bA.swap(bA);
    s.bB.swap(bB);
    s.bg.swap(bg);
}

/// Which side of a segment a sample of primitive id and 1/distance w at (px, py) lies on: 0 for A, 1 for B, 2 for a
/// primitive that neither side shows. Primitives decide where they differ; on one primitive, the nearer side's depth line.
std::uint8_t M4SideOf(const AaSeg& s, float u, float px, float py, std::uint32_t id, float w, std::uint32_t light)
{
    const size_t n = s.bu.size();
    size_t k = std::min(n - 1, size_t(std::lower_bound(s.bu.begin(), s.bu.end(), u) - s.bu.begin()));
    if((k > 0) && (std::fabs(s.bu[k - 1] - u) < std::fabs(s.bu[k] - u)))
        k--;
    const AaSeg::Side& g = s.bg[k];
    if(g.idA != g.idB)
        return (id == g.idA) ? 0 : ((id == g.idB) ? 1 : 2);
    if(id != g.idA)
        return 2;
    if(g.lightA != g.lightB)
        return (light == g.lightA) ? 0 : ((light == g.lightB) ? 1 : 2);
    const float along = (px - g.ax) * g.ex + (py - g.ay) * g.ey;
    const float pa = g.wA + g.gA * along, pb = g.wB + g.gB * (along - 1.0f);
    return (std::fabs(w - pa) <= std::fabs(w - pb)) ? 0 : 1;
}

/// Method 4's edge map, from each centre sample's primitive, 1/distance w and shadow signature. Per pixel, bits 0 and 1
/// mark the east and south links as boundaries; bits 2 and 3 as depth breaks, 4 and 5 as changes of shadow, on one primitive.
/// For a plane seen in perspective w is linear across the image, so its differences are compared only with each other.
void M4EdgeMap(ViewData* vd)
{
    const POVRect& area = vd->GetRenderArea();
    const int x0 = int(area.left), y0 = int(area.top), w = int(area.GetWidth()), h = int(area.GetHeight());
    vd->aaLink.assign(size_t(w) * size_t(h), 0);
    std::vector<std::uint32_t> id, light;
    std::vector<double> iw, d, bend;
    std::vector<std::uint8_t> same, step;
    // one row or column of n samples from (sx, sy) in steps of (dx, dy); a link's bits go to the pixel before it
    auto line = [&](int sx, int sy, int dx, int dy, int n, std::uint8_t idBit, std::uint8_t depthBit, std::uint8_t lightBit)
    {
        id.resize(size_t(n));
        light.resize(size_t(n));
        iw.resize(size_t(n));
        d.assign(size_t(n), 0.0);
        bend.assign(size_t(n), 0.0);
        same.assign(size_t(n), 0);
        step.assign(size_t(n), 0);
        for(int k = 0; k < n; k++)
        {
            id[size_t(k)] = vd->AaId(unsigned(sx + k * dx), unsigned(sy + k * dy));
            light[size_t(k)] = vd->AaLight(unsigned(sx + k * dx), unsigned(sy + k * dy));
            iw[size_t(k)] = double(vd->AaW(unsigned(sx + k * dx), unsigned(sy + k * dy)));
        }
        for(int k = 0; k + 1 < n; k++)
        {
            same[size_t(k)] = (id[size_t(k)] == id[size_t(k + 1)]) && (id[size_t(k)] != 0);
            d[size_t(k)] = same[size_t(k)] ? (iw[size_t(k + 1)] - iw[size_t(k)]) : 0.0;
        }
        auto has = [&](int k) { return (k >= 0) && (k + 1 < n) && same[size_t(k)]; };
        auto floorAt = [&](int k) { return kM4Floor * std::max(iw[size_t(k)], iw[size_t(k + 1)]); };
        // a step: the link's change against the slope beside it, beyond what the change of that slope explains
        for(int k = 0; k + 1 < n; k++)
        {
            if(!has(k) || (!has(k - 1) && !has(k + 1)))
                continue;
            double slope, curve = 0.0;
            if(has(k - 1) && has(k + 1))
            {
                slope = 0.5 * (d[size_t(k - 1)] + d[size_t(k + 1)]);
                curve = std::fabs(d[size_t(k + 1)] - d[size_t(k - 1)]);
            }
            else if(has(k - 1))
            {
                slope = d[size_t(k - 1)];
                curve = has(k - 2) ? std::fabs(d[size_t(k - 1)] - d[size_t(k - 2)]) : 0.0;
            }
            else
            {
                slope = d[size_t(k + 1)];
                curve = has(k + 2) ? std::fabs(d[size_t(k + 2)] - d[size_t(k + 1)]) : 0.0;
            }
            step[size_t(k)] = (std::fabs(d[size_t(k)] - slope) > kM4Break * std::max(curve, floorAt(k))) ? 1 : 0;
        }
        // a crease: the change of slope across the link, against the change one pixel further out on either side
        auto smooth = [&](int k) { return has(k) && !step[size_t(k)]; };
        for(int k = 0; k + 1 < n; k++)
            if(smooth(k - 1) && smooth(k) && smooth(k + 1))
                bend[size_t(k)] = std::fabs(d[size_t(k + 1)] - d[size_t(k - 1)]);
        for(int k = 0; k + 1 < n; k++)
        {
            std::uint8_t& bits = vd->aaLink[size_t(sx + k * dx - x0) + size_t(sy + k * dy - y0) * size_t(w)];
            if(id[size_t(k)] != id[size_t(k + 1)])
            {
                bits |= idBit;
                continue;
            }
            bool crease = false;
            if(bend[size_t(k)] > 0.0)
            {
                double out = -1.0;
                if(smooth(k - 2))
                    out = std::fabs(d[size_t(k - 1)] - d[size_t(k - 2)]);
                if(smooth(k + 2))
                    out = std::max(out, std::fabs(d[size_t(k + 2)] - d[size_t(k + 1)]));
                // the largest bend of its neighbourhood, the first of equals, so a crease between two pixels marks one link
                crease = (out >= 0.0) && (bend[size_t(k)] > kM4Break * std::max(out, floorAt(k))) &&
                         ((k == 0) || (bend[size_t(k)] >= bend[size_t(k - 1)])) && ((k + 2 >= n) || (bend[size_t(k)] > bend[size_t(k + 1)]));
            }
            if(step[size_t(k)] || crease)
                bits |= idBit | depthBit;
            else if(light[size_t(k)] != light[size_t(k + 1)])
                bits |= idBit | lightBit;
        }
    };
    for(int y = y0; y < y0 + h; y++)
        line(x0, y, 1, 0, w, 1, 4, 16);
    for(int x = x0; x < x0 + w; x++)
        line(x, y0, 0, 1, h, 2, 8, 32);
}

/// Method 4's fit: follow every contour of the edge map, cut it into the longest straight or curved runs the map
/// allows, and give each pixel the line of the run it lies most inside of. Map pixels no run covers are left to bisection.
void M4ChainFit(ViewData* vd)
{
    vd->aaSegs.clear();
    const POVRect& area = vd->GetRenderArea();
    const int x0 = int(area.left), y0 = int(area.top), w = int(area.GetWidth()), h = int(area.GetHeight());
    auto at = [&](int x, int y) { return size_t(x - x0) + size_t(y - y0) * size_t(w); };
    auto lab = [&](int x, int y) { return vd->AaLabAt(unsigned(x), unsigned(y)); };
    M4EdgeMap(vd);
    const std::vector<std::uint8_t>& link = vd->aaLink;
    // how far apart two samples' geometry is: 2 for different primitives, else their relative difference of 1/distance
    auto apart = [&](int ax, int ay, int bx, int by)
    {
        const std::uint32_t ia = vd->AaId(unsigned(ax), unsigned(ay)), ib = vd->AaId(unsigned(bx), unsigned(by));
        if(ia != ib)
            return 2.0;
        if(vd->AaLight(unsigned(ax), unsigned(ay)) != vd->AaLight(unsigned(bx), unsigned(by)))
            return 1.5;
        const double wa = vd->AaW(unsigned(ax), unsigned(ay)), wb = vd->AaW(unsigned(bx), unsigned(by));
        return (std::max(wa, wb) > 0.0) ? std::fabs(wa - wb) / std::max(wa, wb) : 0.0;
    };

    // join the crossings inside each 2x2 cell of samples; a node is a link, id 2 * pixel + (south ? 1 : 0)
    const size_t nodes = 2 * size_t(w) * size_t(h);
    std::vector<std::int32_t> nb(2 * nodes, -1);
    auto connect = [&](size_t a, size_t b)
    {
        for(int k = 0; k < 2; k++)
            if(nb[2 * a + k] < 0) { nb[2 * a + k] = std::int32_t(b); break; }
        for(int k = 0; k < 2; k++)
            if(nb[2 * b + k] < 0) { nb[2 * b + k] = std::int32_t(a); break; }
    };
    for(int y = y0; y + 1 < y0 + h; y++)
    {
        for(int x = x0; x + 1 < x0 + w; x++)
        {
            const size_t T = 2 * at(x, y), B = 2 * at(x, y + 1), L = 2 * at(x, y) + 1, R = 2 * at(x + 1, y) + 1;
            const bool fT = link[at(x, y)] & 1, fB = link[at(x, y + 1)] & 1, fL = link[at(x, y)] & 2, fR = link[at(x + 1, y)] & 2;
            const int k = int(fT) + int(fB) + int(fL) + int(fR);
            if(k == 2)
            {
                size_t ids[2];
                int n = 0;
                if(fT) ids[n++] = T;
                if(fR) ids[n++] = R;
                if(fB) ids[n++] = B;
                if(fL) ids[n++] = L;
                connect(ids[0], ids[1]);
            }
            else if(k == 4)
            {
                // the diagonal whose geometry matches stays joined through the middle; the other pair's corners are cut off
                if(apart(x, y, x + 1, y + 1) <= apart(x + 1, y, x, y + 1))
                {
                    connect(T, R);
                    connect(L, B);
                }
                else
                {
                    connect(T, L);
                    connect(R, B);
                }
            }
        }
    }

    std::vector<std::uint8_t> visited(nodes, 0), covered(nodes, 0);
    std::vector<std::pair<size_t, int>> overshoot;
    std::vector<size_t> chain;
    std::vector<M4Bracket> br;
    auto degree = [&](size_t id) { return int(nb[2 * id] >= 0) + int(nb[2 * id + 1] >= 0); };
    auto walk = [&](size_t start)
    {
        chain.clear();
        std::int64_t prev = -1;
        size_t cur = start;
        for(;;)
        {
            chain.push_back(cur);
            visited[cur] = 1;
            const std::int32_t a = nb[2 * cur], b = nb[2 * cur + 1];
            std::int64_t next = -1;
            if((a >= 0) && (a != prev) && !visited[size_t(a)])
                next = a;
            else if((b >= 0) && (b != prev) && !visited[size_t(b)])
                next = b;
            if(next < 0)
                break;
            prev = std::int64_t(cur);
            cur = size_t(next);
        }
    };
    // a side's 1/distance as a line along the link's axis (ex, ey), from the pixel beyond it where that is on the same smooth surface
    auto slopeBeyond = [&](int px, int py, int ex, int ey)
    {
        const int qx = px + ex, qy = py + ey;
        if((qx < x0) || (qx >= x0 + w) || (qy < y0) || (qy >= y0 + h))
            return 0.0f;
        const int lx = std::min(px, qx), ly = std::min(py, qy);
        if((link[at(lx, ly)] & ((ex != 0) ? 1 : 2)) || (vd->AaId(unsigned(qx), unsigned(qy)) != vd->AaId(unsigned(px), unsigned(py))))
            return 0.0f;
        return (vd->AaW(unsigned(qx), unsigned(qy)) - vd->AaW(unsigned(px), unsigned(py))) * float(ex + ey);
    };

    auto process = [&]()
    {
        const int m = int(chain.size());
        br.resize(size_t(m));
        for(int i = 0; i < m; i++)
        {
            const size_t id = chain[size_t(i)], pix = id >> 1;
            const int px = x0 + int(pix % size_t(w)), py = y0 + int(pix / size_t(w));
            const bool south = (id & 1) != 0;
            br[size_t(i)].cx = south ? float(px) + 0.5f : float(px) + 1.0f;
            br[size_t(i)].cy = south ? float(py) + 1.0f : float(py) + 0.5f;
            br[size_t(i)].lx = px;
            br[size_t(i)].ly = py;
            br[size_t(i)].rx = south ? px : px + 1;
            br[size_t(i)].ry = south ? py + 1 : py;
        }
        if(m < kM4RunMin)
            return;
        // the left sample is the one on the left of the direction of travel
        for(int i = 0; i < m; i++)
        {
            const int ia = std::max(0, i - 1), ib = std::min(m - 1, i + 1);
            const float tx = br[size_t(ib)].cx - br[size_t(ia)].cx, ty = br[size_t(ib)].cy - br[size_t(ia)].cy;
            M4Bracket& b = br[size_t(i)];
            const float side = (float(b.lx) + 0.5f - b.cx) * ty - (float(b.ly) + 0.5f - b.cy) * tx;
            if(side < 0.0f)
            {
                std::swap(b.lx, b.rx);
                std::swap(b.ly, b.ry);
            }
        }
        float nx, ny, s;
        auto feasible = [&](int a, int b) { return M4FeasibleQ(br, a, b, nx, ny, s); };

        int i0 = 0;
        while(i0 < m - 1)
        {
            // the longest run from i0 that one line still separates: double, then bisect
            int good = i0 + 1;
            if(feasible(i0, good))
            {
                int step = 2, bad = -1;
                for(;;)
                {
                    const int t = std::min(m - 1, good + step);
                    if(t == good)
                        break;
                    if(feasible(i0, t))
                    {
                        good = t;
                        step *= 2;
                    }
                    else
                    {
                        bad = t;
                        break;
                    }
                }
                if(bad >= 0)
                {
                    int lo = good, hi = bad;
                    while(hi - lo > 1)
                    {
                        const int mid = (lo + hi) / 2;
                        if(feasible(i0, mid))
                            lo = mid;
                        else
                            hi = mid;
                    }
                    good = lo;
                }
            }

            // the brackets at a corner belong to the line the contour turns onto; leave them out of this run, since a
            // few of them can pin its end up to a pixel from where the line really is
            const bool atEnd = (good >= m - 1);
            if(good - i0 + 1 > kM4RunMin)
            {
                const int qa = i0 + (good - i0) / 4, qb = good - (good - i0) / 4;
                float rx = br[size_t(qb)].cx - br[size_t(qa)].cx, ry = br[size_t(qb)].cy - br[size_t(qa)].cy;
                const float rl = std::sqrt(rx * rx + ry * ry);
                if(rl > 1.0e-3f)
                {
                    rx /= rl;
                    ry /= rl;
                    auto turned = [&](int i)
                    {
                        const int ia = std::max(0, i - 1), ib = std::min(m - 1, i + 1);
                        const float dx = br[size_t(ib)].cx - br[size_t(ia)].cx, dy = br[size_t(ib)].cy - br[size_t(ia)].cy;
                        const float dl = std::sqrt(dx * dx + dy * dy);
                        return (dl > 1.0e-3f) && (std::fabs(dx * ry - dy * rx) / dl > 0.77f);
                    };
                    for(int k = 0; (k < 3) && (good - i0 + 1 > kM4RunMin) && turned(good); k++)
                        good--;
                    for(int k = 0; (k < 3) && (good - i0 + 1 > kM4RunMin) && turned(i0); k++)
                        i0++;
                }
            }

            if(good - i0 + 1 >= kM4RunMin)
            {
                // a line wherever one fits the run; a parabola only for runs no line can
                const bool curved = (M4BestLine(br, i0, good, nx, ny, s) < 0.0f);
                if(curved)
                    M4FeasibleQ(br, i0, good, nx, ny, s);
                const float tx = -ny, ty = nx;
                float uA = tx * br[size_t(i0)].cx + ty * br[size_t(i0)].cy, uB = tx * br[size_t(good)].cx + ty * br[size_t(good)].cy;
                if(uA > uB)
                    std::swap(uA, uB);
                const float uLo = uA - 0.5f, uHi = uB + 0.5f;

                // the run as a segment: the samples either side of it, in a frame with its line as the u axis
                const float uc = 0.5f * (uA + uB);
                vd->aaSegs.emplace_back();
                {
                    AaSeg& sg = vd->aaSegs.back();
                    sg.cx = nx * s + tx * uc;
                    sg.cy = ny * s + ty * uc;
                    sg.tx = tx;
                    sg.ty = ty;
                    sg.nx = nx;
                    sg.ny = ny;
                    sg.uLo = uLo - uc;
                    sg.uHi = uHi - uc;
                    sg.length = uHi - uLo;
                    for(int i = i0; i <= good; i++)
                    {
                        const M4Bracket& bk = br[size_t(i)];
                        const float ax = float(bk.rx) + 0.5f, ay = float(bk.ry) + 0.5f, bx = float(bk.lx) + 0.5f, by = float(bk.ly) + 0.5f;
                        sg.hullA.push_back(AaSeg::Pt { tx * ax + ty * ay - uc, nx * ax + ny * ay - s, 0 });
                        sg.hullB.push_back(AaSeg::Pt { tx * bx + ty * by - uc, nx * bx + ny * by - s, 1 });
                        covered[chain[size_t(i)]] = 1;
                    }
                    sg.quad = curved;
                    const M4Bracket& bm = br[size_t((i0 + good) / 2)];
                    sg.contrast = std::sqrt(Dist2(lab(bm.lx, bm.ly), lab(bm.rx, bm.ry)));
                    for(int i = i0; i <= good; i++)
                    {
                        const M4Bracket& bk = br[size_t(i)];
                        sg.bu.push_back(tx * bk.cx + ty * bk.cy - uc);
                        sg.bA.push_back(vd->LatticeSample(unsigned(bk.rx), unsigned(bk.ry)));
                        sg.bB.push_back(vd->LatticeSample(unsigned(bk.lx), unsigned(bk.ly)));
                        const int ex = bk.lx - bk.rx, ey = bk.ly - bk.ry;
                        sg.bg.push_back(AaSeg::Side { vd->AaId(unsigned(bk.rx), unsigned(bk.ry)), vd->AaId(unsigned(bk.lx), unsigned(bk.ly)),
                                                      vd->AaLight(unsigned(bk.rx), unsigned(bk.ry)), vd->AaLight(unsigned(bk.lx), unsigned(bk.ly)),
                                                      float(bk.rx) + 0.5f, float(bk.ry) + 0.5f, float(ex), float(ey),
                                                      vd->AaW(unsigned(bk.rx), unsigned(bk.ry)), -slopeBeyond(bk.rx, bk.ry, -ex, -ey),
                                                      vd->AaW(unsigned(bk.lx), unsigned(bk.ly)), slopeBeyond(bk.lx, bk.ly, ex, ey) });
                    }
                    M4SortBrackets(sg);
                    if(!M4Estimate(sg))
                        sg.state = 1;
                }
            }

            if(atEnd || (good >= m - 1))
                break;
            // the next run starts inside this one, so every pixel has a run it is well inside of
            i0 = std::max(i0 + 1, good - std::max(1, (good - i0) / 4));
        }
    };

    // open contours from their ends first, then the closed ones
    for(size_t id = 0; id < nodes; id++)
    {
        if(!visited[id] && (degree(id) == 1))
        {
            walk(id);
            process();
        }
    }
    for(size_t id = 0; id < nodes; id++)
    {
        if(!visited[id] && (degree(id) == 2))
        {
            walk(id);
            process();
        }
    }

    std::vector<std::int32_t>().swap(nb);
    std::vector<std::uint8_t>().swap(visited);
    std::vector<float> weight(size_t(w) * size_t(h), 0.0f), weight2(size_t(w) * size_t(h), 0.0f);

    // each pixel takes the line of the segment it lies most inside of
    for(size_t id = 0; id < vd->aaSegs.size(); id++)
    {
        const AaSeg& sg = vd->aaSegs[id];
        float nx, ny, s;
        M4LineAt(sg, sg.cx, sg.cy, nx, ny, s);
        // probes can move the line anywhere the bounds allow, so own every pixel it might come to cross
        const float slack = 0.5f * std::max(sg.w[0], std::max(sg.w[1], sg.w[2])) + 1.0e-3f;
        const float uMid = 0.5f * (sg.uLo + sg.uHi), half = 0.5f * (sg.uHi - sg.uLo);

        auto visit = [&](int px, int py)
        {
            if((px < x0) || (px >= x0 + w) || (py < y0) || (py >= y0 + h))
                return;
            const float pcx = float(px) + 0.5f, pcy = float(py) + 0.5f;
            float lx, ly, ls;
            M4LineAt(sg, pcx, pcy, lx, ly, ls);
            if(std::fabs(ls - (lx * pcx + ly * pcy)) > 0.5f * (std::fabs(lx) + std::fabs(ly)) + slack + 1.0e-3f)
                return;
            const float upos = (pcx - sg.cx) * sg.tx + (pcy - sg.cy) * sg.ty;
            // past the end of the run its line is only a guess, good enough to be a second line at a junction
            if(std::fabs(upos - uMid) > half + 0.5f)
            {
                overshoot.push_back(std::make_pair(at(px, py), int(id)));
                return;
            }
            const float wt = std::max(0.001f, 1.0f - std::fabs(upos - uMid) / half);
            AaFit& f = vd->Fit(unsigned(px), unsigned(py));
            if(f.seg == int(id))
                return;
            if(wt <= weight[at(px, py)])
            {
                if(wt > weight2[at(px, py)])
                {
                    vd->GeomFor(f).seg2 = int(id);
                    weight2[at(px, py)] = wt;
                }
                return;
            }
            AaGeom& geom = vd->GeomFor(f);
            if(f.seg >= 0)
            {
                geom.seg2 = f.seg;
                weight2[at(px, py)] = weight[at(px, py)];
            }
            weight[at(px, py)] = wt;
            f.kind = 1;
            f.seg = int(id);
            geom.contrast = sg.contrast;
            f.probes = 0;
            M4LocalColours(sg, upos, geom.a, geom.b);
        };

        // every pixel the curve passes near, stepping along it
        if(sg.quad)
        {
            const int reach = int(std::ceil(slack + 0.75f));
            for(float u = sg.uLo - 3.5f; u <= sg.uHi + 3.5f; u += 0.25f)
            {
                const float v = sg.a + (sg.b + sg.c * u) * u;
                const int cx = int(std::floor(sg.cx + u * sg.tx + v * sg.nx)), cy = int(std::floor(sg.cy + u * sg.ty + v * sg.ny));
                for(int j = cy - reach; j <= cy + reach; j++)
                    for(int i = cx - reach; i <= cx + reach; i++)
                        visit(i, j);
            }
            continue;
        }
        // every pixel the line passes through, column by column for a flat line, else row by row
        const float ax = sg.cx + (sg.uLo - 3.5f) * sg.tx + (sg.a + sg.b * (sg.uLo - 3.5f)) * sg.nx;
        const float ay = sg.cy + (sg.uLo - 3.5f) * sg.ty + (sg.a + sg.b * (sg.uLo - 3.5f)) * sg.ny;
        const float bx = sg.cx + (sg.uHi + 3.5f) * sg.tx + (sg.a + sg.b * (sg.uHi + 3.5f)) * sg.nx;
        const float by = sg.cy + (sg.uHi + 3.5f) * sg.ty + (sg.a + sg.b * (sg.uHi + 3.5f)) * sg.ny;
        if(std::fabs(ny) >= std::fabs(nx))
        {
            const float xmin = std::min(ax, bx), xmax = std::max(ax, bx);
            for(int i = int(std::floor(xmin)); i <= int(std::floor(xmax)); i++)
            {
                const float xa = std::max(xmin, float(i)), xb = std::min(xmax, float(i + 1));
                if(xb < xa)
                    continue;
                const float ya = (s - nx * xa) / ny, yb = (s - nx * xb) / ny;
                const float reachY = slack / std::fabs(ny);
                for(int j = int(std::floor(std::min(ya, yb) - reachY)); j <= int(std::floor(std::max(ya, yb) + reachY)); j++)
                    visit(i, j);
            }
        }
        else
        {
            const float ymin = std::min(ay, by), ymax = std::max(ay, by);
            for(int j = int(std::floor(ymin)); j <= int(std::floor(ymax)); j++)
            {
                const float ya = std::max(ymin, float(j)), yb = std::min(ymax, float(j + 1));
                if(yb < ya)
                    continue;
                const float xa = (s - ny * ya) / nx, xb = (s - ny * yb) / nx;
                const float reachX = slack / std::fabs(nx);
                for(int i = int(std::floor(std::min(xa, xb) - reachX)); i <= int(std::floor(std::max(xa, xb) + reachX)); i++)
                    visit(i, j);
            }
        }
    }

    for(const auto& ov : overshoot)
    {
        AaFit& f = vd->Fit(unsigned(x0 + int(ov.first % size_t(w))), unsigned(y0 + int(ov.first / size_t(w))));
        if((f.seg >= 0) && (f.seg != ov.second) && (vd->Geom(f).seg2 < 0))
            vd->GeomFor(f).seg2 = ov.second;
    }

    // both pixels of every map link no fitted run covers, and no line holds, are bisected
    for(int y = y0; y < y0 + h; y++)
    {
        for(int x = x0; x < x0 + w; x++)
        {
            for(int k = 0; k < 2; k++)
            {
                if(!(link[at(x, y)] & (1 << k)) || covered[2 * at(x, y) + size_t(k)])
                    continue;
                AaFit& f = vd->Fit(unsigned(x), unsigned(y));
                AaFit& g = vd->Fit(unsigned(x + 1 - k), unsigned(y + k));
                if(f.kind == 0)
                    f.kind = 2;
                if(g.kind == 0)
                    g.kind = 2;
            }
        }
    }
}

/// How steeply 1/distance runs across a pixel's smooth surroundings: the largest change to a 4-neighbour on its
/// primitive that the map does not mark as a boundary.
double M4Slope(ViewData* vd, int x, int y)
{
    const POVRect& area = vd->GetRenderArea();
    const int x0 = int(area.left), y0 = int(area.top), w = int(area.GetWidth()), h = int(area.GetHeight());
    const std::uint32_t id = vd->AaId(unsigned(x), unsigned(y));
    const double wc = vd->AaW(unsigned(x), unsigned(y));
    double g = 0.0;
    const int dx[4] = { 1, -1, 0, 0 }, dy[4] = { 0, 0, 1, -1 };
    for(int k = 0; k < 4; k++)
    {
        const int qx = x + dx[k], qy = y + dy[k];
        if((qx < x0) || (qx >= x0 + w) || (qy < y0) || (qy >= y0 + h) || (vd->AaId(unsigned(qx), unsigned(qy)) != id))
            continue;
        const int lx = std::min(x, qx), ly = std::min(y, qy);
        if(vd->aaLink[size_t(lx - x0) + size_t(ly - y0) * size_t(w)] & ((dx[k] != 0) ? 1 : 2))
            continue;
        g = std::max(g, std::fabs(double(vd->AaW(unsigned(qx), unsigned(qy))) - wc));
    }
    return g;
}

/// Whether two samples a distance apart, in pixels, of a map pixel whose smooth slope of 1/distance is given, lie on
/// different surfaces: different primitives or shadows, or 1/distance changing more than that slope explains.
bool M4Apart(std::uint32_t ia, std::uint32_t la, float wa, std::uint32_t ib, std::uint32_t lb, float wb, double distance, double slope)
{
    if((ia != ib) || (la != lb))
        return true;
    return (ia != 0) && (std::fabs(double(wa) - double(wb)) > kM4Break * (slope * distance + kM4Floor * std::max(double(wa), double(wb))));
}
}

void TraceTask::PlanM4(ViewData* vd, int pass, int round)
{
    const POVRect& area = vd->GetRenderArea();
    if(pass == 5)
    {
        vd->EndAaPlan();
        return;
    }
    if(pass == 0)
    {
        vd->StartAaPlan();
        for(unsigned int y = area.top; y <= area.bottom; y++)
        {
            for(unsigned int x = area.left; x <= area.right; x++)
            {
                const OkLab l = OkLabOf(vd, vd->LatticeSample(x, y));
                float* dst = vd->AaLabAt(x, y);
                dst[0] = l.l; dst[1] = l.a; dst[2] = l.b; dst[3] = l.t;
            }
        }
        M4ChainFit(vd);
        const double pixels = double(area.GetWidth()) * double(area.GetHeight());
        vd->aaBudgetLeft = std::max<std::int64_t>(1, std::int64_t(std::ceil(pixels * vd->aaFraction)));
        vd->aaExhausted = false;
        // the map's pixels no line holds take the share of the budget they are of all the map's pixels
        std::int64_t nFit = 0, nOpen = 0;
        for(unsigned int y = area.top; y <= area.bottom; y++)
        {
            for(unsigned int x = area.left; x <= area.right; x++)
            {
                const AaFit& f = vd->Fit(x, y);
                nFit += (f.kind == 1) ? 1 : 0;
                nOpen += (f.kind == 2) ? 1 : 0;
            }
        }
        vd->aaReserve = std::int64_t(double(nOpen) / double(std::max<std::int64_t>(1, nFit + nOpen)) * double(vd->aaBudgetLeft));
        return;
    }

    const bool probe = (pass == 2);
    const bool enabled = !vd->aaExhausted && (!probe || (round < kM4Rounds));
    M4FoldSegments(vd);
    if(probe)
    {
        M4PlanSegments(vd, enabled);
        return;
    }

    // bisection: every map pixel no line holds starts with one leaf, its centre sample, which is split first; after that,
    // each round splits its largest leaf that touches a cell on another surface, by size times their difference in colour
    const unsigned int w = area.GetWidth();
    std::vector<float> score(size_t(w) * area.GetHeight(), 0.0f);
    const float thr2 = float(vd->aaThr * vd->aaThr);
    const float minSize = 1.0f / float(1 << std::max(1, std::min(8, vd->aaDepth)));
    for(unsigned int y = area.top; y <= area.bottom; y++)
    {
        for(unsigned int x = area.left; x <= area.right; x++)
        {
            AaFit& f = vd->Fit(x, y);
            f.leaf = -1;
            const bool broken = (f.kind == 1) && (f.seg >= 0) && (vd->aaSegs[size_t(f.seg)].state == 3);
            if((f.kind != 2) && !broken)
                continue;
            const AaCell centre = vd->CentreLeaf(x, y);
            const AaCell* cells = (f.cells >= 0) ? vd->Leaves(f) : &centre;
            const size_t nCells = (f.cells >= 0) ? f.nCells : 1;
            const double slope = M4Slope(vd, int(x), int(y));
            const std::uint8_t here = vd->aaLink[(x - area.left) + (y - area.top) * size_t(w)];
            float best = 0.0f;
            // a leaf index must fit the planner's 16 bits
            for(size_t i = 0; (i < nCells) && (nCells <= 32764); i++)
            {
                const AaCell& c = cells[i];
                const float cx = c.X(), cy = c.Y(), cs = c.Size();
                if(cs <= minSize * 1.01f)
                    continue;
                float far2 = 0.0f;
                auto touch = [&](float ox, float oy, float os, const float* lab, std::uint32_t id, std::uint32_t lo, float wo, bool across)
                {
                    if((std::fabs(ox - cx) > 0.5f * (os + cs) + 1.0e-4f) || (std::fabs(oy - cy) > 0.5f * (os + cs) + 1.0e-4f))
                        return;
                    const double dist = std::sqrt(double(ox - cx) * double(ox - cx) + double(oy - cy) * double(oy - cy));
                    if(across || M4Apart(c.id, c.light, c.w, id, lo, wo, dist, slope))
                        far2 = std::max(far2, Dist2(lab, c.lab));
                };
                for(size_t j = 0; j < nCells; j++)
                    if(j != i)
                        touch(cells[j].X(), cells[j].Y(), cells[j].Size(), cells[j].lab, cells[j].id, cells[j].light, cells[j].w, false);
                for(int dy = -1; dy <= 1; dy++)
                {
                    for(int dx = -1; dx <= 1; dx++)
                    {
                        const int qx = int(x) + dx, qy = int(y) + dy;
                        if(((dx == 0) && (dy == 0)) || (qx < int(area.left)) || (qx > int(area.right)) || (qy < int(area.top)) || (qy > int(area.bottom)))
                            continue;
                        // the centre sample's own map links count as boundaries, whatever the leaves later show
                        bool across = false;
                        if((nCells == 1) && ((dx == 0) != (dy == 0)))
                        {
                            const int lx = std::min(int(x), qx), ly = std::min(int(y), qy);
                            const std::uint8_t bits = (lx == int(x)) && (ly == int(y)) ? here : vd->aaLink[size_t(lx) - area.left + (size_t(ly) - area.top) * w];
                            across = (bits & ((dx != 0) ? 1 : 2)) != 0;
                        }
                        touch(float(dx), float(dy), 1.0f, vd->AaLabAt(unsigned(qx), unsigned(qy)), vd->AaId(unsigned(qx), unsigned(qy)),
                              vd->AaLight(unsigned(qx), unsigned(qy)), vd->AaW(unsigned(qx), unsigned(qy)), across);
                    }
                }
                if(far2 < thr2)
                    continue;
                const float sc = cs * std::sqrt(far2);
                if(sc > best)
                {
                    best = sc;
                    f.leaf = std::int16_t(i);
                }
            }
            score[(x - area.left) + (y - area.top) * w] = best;
        }
    }
    auto bucketOf = [&](const AaFit& f, unsigned int x, unsigned int y) -> int
    {
        return (f.leaf >= 0) ? M4Bucket(score[(x - area.left) + (y - area.top) * w]) : -1;
    };

    std::int64_t hist[256] = {};
    std::int64_t total = 0;
    if(enabled)
    {
        for(unsigned int y = area.top; y <= area.bottom; y++)
        {
            for(unsigned int x = area.left; x <= area.right; x++)
            {
                const int b = bucketOf(vd->Fit(x, y), x, y);
                if(b >= 0)
                {
                    hist[b]++;
                    total++;
                }
            }
        }
    }

    // take whole buckets, best first, until the budget is reached; the bucket that crosses it is taken whole
    // and the pass is the last, so the result never depends on which of equal candidates came first
    const int factor = 4;
    int threshold = 0;
    std::int64_t admitted = total;
    bool cut = false;
    std::vector<std::uint8_t> drawn;
    std::vector<size_t> split;
    if(enabled)
    {
        std::int64_t cum = 0;
        for(int b = 255; b >= 0; b--)
        {
            cum += hist[b];
            if(cum * factor >= vd->aaBudgetLeft)
            {
                threshold = b;
                admitted = cum;
                cut = true;
                break;
            }
        }
        // unless that bucket alone would overrun by more than a quarter of the whole budget: then its candidates
        // are drawn in an order fixed by their coordinates until the budget is met
        const double whole = std::ceil(double(area.GetWidth()) * double(area.GetHeight()) * vd->aaFraction);
        if(cut && (double(admitted * factor - vd->aaBudgetLeft) > 0.25 * whole))
        {
            const std::int64_t before = admitted - hist[threshold];
            const std::int64_t take = std::max<std::int64_t>(0, (vd->aaBudgetLeft - before * factor + factor - 1) / factor);
            std::vector<std::pair<std::uint32_t, std::uint32_t>> order;
            for(unsigned int y = area.top; y <= area.bottom; y++)
                for(unsigned int x = area.left; x <= area.right; x++)
                    if(bucketOf(vd->Fit(x, y), x, y) == threshold)
                    {
                        std::uint32_t hh = (x * 73856093u) ^ (y * 19349663u);
                        hh ^= hh >> 16; hh *= 0x7feb352du; hh ^= hh >> 15; hh *= 0x846ca68bu; hh ^= hh >> 16;
                        order.emplace_back(hh, (x - area.left) + (y - area.top) * area.GetWidth());
                    }
            std::sort(order.begin(), order.end());
            drawn.assign(size_t(area.GetWidth()) * area.GetHeight(), 0);
            for(size_t i = 0; i < size_t(take); i++)
                drawn[order[i].second] = 1;
            admitted = before + take;
        }
    }

    for(unsigned int y = area.top; y <= area.bottom; y++)
    {
        for(unsigned int x = area.left; x <= area.right; x++)
        {
            AaFit& f = vd->Fit(x, y);
            const int b = enabled ? bucketOf(f, x, y) : -1;
            const size_t at = (x - area.left) + (y - area.top) * area.GetWidth();
            const bool taken = (b > threshold) || ((b == threshold) && (drawn.empty() || drawn[at]));
            f.planned = ((b >= 0) && taken) ? 1 : 0;
            if(f.planned)
                split.push_back(at);
        }
    }
    if(!split.empty())
        vd->ReserveAaSplits(split);
    vd->aaBudgetLeft -= factor * admitted;
    if(cut)
        vd->aaExhausted = true;
}

void TraceTask::ProbeSegmentsM4()
{
    ViewData* vd = GetViewData();
    radiosity.BeforeTile(0, RadiosityFunction::FINAL_TRACE);
    for(;;)
    {
        const std::uint32_t k = vd->aaProbeNext.fetch_add(1);
        if(k >= vd->aaProbeList.size())
            break;
        AaSeg& s = vd->aaSegs[vd->aaProbeList[k]];
        const float px = s.cx + s.probeU * s.tx + s.probeV * s.nx, py = s.cy + s.probeU * s.ty + s.probeV * s.ny;
        RGBTColour col;
        std::uint32_t id, light;
        float w;
        TraceSample(px, py, col, 0.25, &id, &w, &light);
        s.fresh.push_back(AaSeg::Pt { s.probeU, s.probeV, M4SideOf(s, s.probeU, px, py, id, w, light) });
        s.probes++;
        Cooperate();
    }
    radiosity.AfterTile();
    GetViewDataPtr()->AfterTile();
}

void TraceTask::ProgressiveM4()
{
    if(aaPass == 2)
    {
        ProbeSegmentsM4();
        return;
    }
    const bool traces = (aaPass == 3);
    POVRect rect;
    vector<RGBTColour> pixels;
    unsigned int serial;

    while(GetViewData()->GetNextRectangle(rect, serial) == true)
    {
        BlockTimer blockTimer;
        if(traces)
            radiosity.BeforeTile(highReproducibility? serial : 0, RadiosityFunction::FINAL_TRACE);
        pixels.clear();
        if(aaPass == 4)
            pixels.reserve(rect.GetArea());

        for(unsigned int y = rect.top; y <= rect.bottom; y++)
        {
            for(unsigned int x = rect.left; x <= rect.right; x++)
            {
                AaFit& fit = GetViewData()->Fit(x, y);
                if((aaPass == 3) && fit.planned)
                {
                    AaCell* cells = GetViewData()->Leaves(fit);
                    const AaCell parent = cells[size_t(fit.leaf)];
                    const int q = 256 >> parent.depth;
                    for(int k = 0; k < 4; k++)
                    {
                        AaCell c;
                        c.x = std::int16_t(parent.x + ((k & 1) ? q : -q));
                        c.y = std::int16_t(parent.y + ((k & 2) ? q : -q));
                        c.depth = std::uint8_t(parent.depth + 1);
                        TraceSample(DBL(x) + 0.5 + c.X(), DBL(y) + 0.5 + c.Y(), c.col, double(c.Size()), &c.id, &c.w, &c.light);
                        const OkLab l = ToOkLab(c.col);
                        c.lab[0] = l.l; c.lab[1] = l.a; c.lab[2] = l.b; c.lab[3] = l.t;
                        cells[(k == 0) ? size_t(fit.leaf) : size_t(fit.nCells++)] = c;
                    }
                    fit.probes = std::uint8_t(std::min(250, int(fit.probes) + 4));
                }
                else if(aaPass == 4)
                {
                    const AaGeom& geom = GetViewData()->Geom(fit);
                    RGBTColour col = GetViewData()->LatticeSample(x, y);
                    const AaCell* bisected = GetViewData()->Leaves(fit);
                    if(bisected && (fit.nCells > 1))
                    {
                        col.Clear();
                        for(size_t i = 0; i < fit.nCells; i++)
                            col += bisected[i].col * double(bisected[i].Size() * bisected[i].Size());
                        GetViewDataPtr()->Stats()[Number_Of_Pixels_Supersampled]++;
                    }
                    else if(fit.kind == 1)
                    {
                        float nx = geom.nx, ny = geom.ny, s;
                        int probes = fit.probes;
                        if(fit.seg >= 0)
                        {
                            // the segment's line now, after any probes, seen from this pixel's centre
                            const AaSeg& sg = GetViewData()->aaSegs[size_t(fit.seg)];
                            M4LineAt(sg, float(x) + 0.5f, float(y) + 0.5f, nx, ny, s);
                            s -= nx * (float(x) + 0.5f) + ny * (float(y) + 0.5f);
                            probes = sg.probes;
                        }
                        else
                            s = 0.5f * (geom.lo + geom.hi);

                        // a line must put this pixel's own 3x3 samples on the sides their colours say; where the
                        // run's line has turned away from the curve, try the second line, else keep the sample
                        bool trusted = true, secondTaken = false;
                        if(fit.seg >= 0)
                        {
                            const POVRect& area = GetViewData()->GetRenderArea();
                            const OkLab pa = ToOkLab(geom.a), pb = ToOkLab(geom.b);
                            const float v[4] = { pb.l - pa.l, pb.a - pa.a, pb.b - pa.b, pb.t - pa.t };
                            const float vv = v[0] * v[0] + v[1] * v[1] + v[2] * v[2] + v[3] * v[3];
                            auto fits = [&](float lx, float ly, float ls)
                            {
                                for(int dy = -1; dy <= 1; dy++)
                                {
                                    for(int dx = -1; dx <= 1; dx++)
                                    {
                                        const int qx = int(x) + dx, qy = int(y) + dy;
                                        if((qx < int(area.left)) || (qx > int(area.right)) || (qy < int(area.top)) || (qy > int(area.bottom)) || (vv <= 0.0f))
                                            continue;
                                        const float* l = GetViewData()->AaLabAt(unsigned(qx), unsigned(qy));
                                        const float t = ((l[0] - pa.l) * v[0] + (l[1] - pa.a) * v[1] + (l[2] - pa.b) * v[2] + (l[3] - pa.t) * v[3]) / vv;
                                        if(std::fabs(t - 0.5f) < 0.25f)
                                            continue;
                                        const float side = lx * float(dx) + ly * float(dy) - ls;
                                        if((std::fabs(side) > 0.3f) && ((side > 0.0f) != (t > 0.5f)))
                                            return false;
                                    }
                                }
                                return true;
                            };
                            if(!fits(nx, ny, s))
                            {
                                trusted = false;
                                if(geom.seg2 >= 0)
                                {
                                    float n2x, n2y, s2;
                                    M4LineAt(GetViewData()->aaSegs[size_t(geom.seg2)], float(x) + 0.5f, float(y) + 0.5f, n2x, n2y, s2);
                                    s2 -= n2x * (float(x) + 0.5f) + n2y * (float(y) + 0.5f);
                                    if(fits(n2x, n2y, s2))
                                    {
                                        nx = n2x; ny = n2y; s = s2;
                                        trusted = secondTaken = true;
                                    }
                                }
                            }
                        }
                        const double f = M4Coverage(nx, ny, s);

                        // two lines of different directions both crossing the pixel: a junction, up to four regions
                        bool twoLines = false;
                        float n2x = 0.0f, n2y = 0.0f, s2 = 0.0f;
                        if(trusted && !secondTaken && (fit.seg >= 0) && (geom.seg2 >= 0))
                        {
                            const AaSeg& g2 = GetViewData()->aaSegs[size_t(geom.seg2)];
                            M4LineAt(g2, float(x) + 0.5f, float(y) + 0.5f, n2x, n2y, s2);
                            s2 -= n2x * (float(x) + 0.5f) + n2y * (float(y) + 0.5f);
                            const float c2 = M4Coverage(n2x, n2y, s2);
                            twoLines = (std::fabs(nx * n2x + ny * n2y) < 0.95f) && (f > 0.0) && (f < 1.0) && (c2 > 0.0f) && (c2 < 1.0f);
                        }

                        if(twoLines)
                        {
                            const POVRect& area = GetViewData()->GetRenderArea();
                            RGBTColour qc[4];
                            double qw[4] = { 0.0, 0.0, 0.0, 0.0 };
                            for(int oy = -2; oy <= 2; oy++)
                            {
                                for(int ox = -2; ox <= 2; ox++)
                                {
                                    const int sx = int(x) + ox, sy = int(y) + oy;
                                    if((sx < int(area.left)) || (sx > int(area.right)) || (sy < int(area.top)) || (sy > int(area.bottom)))
                                        continue;
                                    const int q = int(nx * float(ox) + ny * float(oy) > s) + 2 * int(n2x * float(ox) + n2y * float(oy) > s2);
                                    const double wgt = 1.0 / double((1 + ox * ox + oy * oy) * (1 + ox * ox + oy * oy));
                                    qc[q] += GetViewData()->LatticeSample(unsigned(sx), unsigned(sy)) * wgt;
                                    qw[q] += wgt;
                                }
                            }
                            M4Poly sq;
                            sq.n = 4;
                            const float cxs[4] = { -0.5f, 0.5f, 0.5f, -0.5f }, cys[4] = { -0.5f, -0.5f, 0.5f, 0.5f };
                            for(int k = 0; k < 4; k++)
                            {
                                sq.x[k] = cxs[k];
                                sq.y[k] = cys[k];
                            }
                            RGBTColour own = col, centreModel = own;
                            col.Clear();
                            const int q0 = int(0.0f > s) + 2 * int(0.0f > s2);
                            for(int q = 0; q < 4; q++)
                            {
                                const double ar = double(M4PolyArea(M4Clip(M4Clip(sq, nx, ny, s, (q & 1) != 0), n2x, n2y, s2, (q & 2) != 0)));
                                int src = q;
                                if(qw[src] <= 0.0)
                                    src = (qw[q ^ 1] > 0.0) ? (q ^ 1) : ((qw[q ^ 2] > 0.0) ? (q ^ 2) : -1);
                                const RGBTColour qcol = (src >= 0) ? (qc[src] * (1.0 / qw[src])) : own;
                                if(q == q0)
                                    centreModel = qcol;
                                if(ar >= 1.0e-4)
                                    col += qcol * ar;
                            }
                            col = M4Bounded(own + col - centreModel, own, qc, qw);
                        }
                        // a pixel the final line does not cross stays as it was sampled
                        else if(trusted && ((fit.seg < 0) || ((f > 0.0) && (f < 1.0))))
                        {
                            // the pixel keeps its own sample; the fit adds what the rest of its area, beyond the centre's side, contributes
                            const OkLab pa = ToOkLab(geom.a), pb = ToOkLab(geom.b), po = ToOkLab(col);
                            const float v[4] = { pb.l - pa.l, pb.a - pa.a, pb.b - pa.b, pb.t - pa.t };
                            const float vv = v[0] * v[0] + v[1] * v[1] + v[2] * v[2] + v[3] * v[3];
                            const bool centreB = (vv > 0.0f) && (((po.l - pa.l) * v[0] + (po.a - pa.a) * v[1] + (po.b - pa.b) * v[2] + (po.t - pa.t) * v[3]) >= 0.5f * vv);
                            const double fb = centreB ? std::max(f, 0.5) : std::min(f, 0.5);
                            const RGBTColour own = col;
                            col += (geom.b - geom.a) * (fb - (centreB ? 1.0 : 0.0));
                            const RGBTColour ends[2] = { geom.a, geom.b };
                            const double ew[2] = { 1.0, 1.0 };
                            col = M4Bounded(col, own, ends, ew, 2);
                        }
                        if(probes > 0)
                            GetViewDataPtr()->Stats()[Number_Of_Pixels_Supersampled]++;
                    }
                    pixels.push_back(col);
                }
                if(traces)
                    Cooperate();
            }
            Cooperate();
        }

        if(traces)
            radiosity.AfterTile();

        GetViewDataPtr()->AfterTile();
        if(aaPass == 4)
            GetViewData()->CompletedRectangle(rect, serial, pixels, 1, true, true, 1.0f, nullptr, progressLevel,
                                              blockTimer.Elapsed());
        else
            GetViewData()->CompletedRectangle(rect, serial, 0.0f);

        Cooperate();
    }
}


void TraceTask::NonAdaptiveSupersamplingForOnePixel(DBL x, DBL y, RGBTColour& leftcol, RGBTColour& topcol, RGBTColour& curcol, bool& sampleleft, bool& sampletop, bool& samplecurrent)
{
    RGBTColour gcLeft = GammaCurve::Encode(aaGamma, leftcol);
    RGBTColour gcTop  = GammaCurve::Encode(aaGamma, topcol);
    RGBTColour gcCur  = GammaCurve::Encode(aaGamma, curcol);

    bool leftdiff = (ColourDistanceRGBT(gcLeft, gcCur) >= aaThreshold);
    bool topdiff  = (ColourDistanceRGBT(gcTop,  gcCur) >= aaThreshold);

    sampleleft = sampleleft && leftdiff;
    sampletop = sampletop && topdiff;
    samplecurrent = ((leftdiff == true) || (topdiff == true));

    if(sampleleft == true)
        SupersampleOnePixel(x - 1.0, y, leftcol);

    if(sampletop == true)
        SupersampleOnePixel(x, y - 1.0, topcol);

    if(samplecurrent == true)
        SupersampleOnePixel(x, y, curcol);
}

void TraceTask::SupersampleOnePixel(DBL x, DBL y, RGBTColour& col)
{
    DBL step(1.0 / DBL(aaDepth));
    DBL range(0.5 - (step * 0.5));
    DBL rx, ry;
    RGBTColour tempcol;

    GetViewDataPtr()->Stats()[Number_Of_Pixels_Supersampled]++;

    for(DBL yy = -range; yy <= (range + EPSILON); yy += step)
    {
        for(DBL xx = -range; xx <= (range + EPSILON); xx += step)
        {
            if (jitterScale > 0.0)
            {
                Jitter2d(x + xx, y + yy, rx, ry);
                trace(x+0.5 + xx + (rx * jitterScale), y+0.5 + yy + (ry * jitterScale), GetViewData()->GetWidth(), GetViewData()->GetHeight(), tempcol);
            }
            else
                trace(x+0.5 + xx, y+0.5 + yy, GetViewData()->GetWidth(), GetViewData()->GetHeight(), tempcol);

            col += tempcol;
            GetViewDataPtr()->Stats()[Number_Of_Samples]++;

            Cooperate();
        }
    }

    col /= (aaDepth * aaDepth + 1);
}

void TraceTask::SubdivideOnePixel(DBL x, DBL y, DBL d, size_t bx, size_t by, size_t bstep, SubdivisionBuffer& buffer, RGBTColour& result, int level)
{
    RGBTColour& cx0y0 = buffer(bx, by);
    RGBTColour& cx0y2 = buffer(bx, by + bstep);
    RGBTColour& cx2y0 = buffer(bx + bstep, by);
    RGBTColour& cx2y2 = buffer(bx + bstep, by + bstep);
    size_t bstephalf = bstep / 2;

    // o = no operation, + = input, * = output

    // Input:
    // +o+
    // ooo
    // +o+

    RGBTColour cx0y0g = GammaCurve::Encode(aaGamma, cx0y0);
    RGBTColour cx0y2g = GammaCurve::Encode(aaGamma, cx0y2);
    RGBTColour cx2y0g = GammaCurve::Encode(aaGamma, cx2y0);
    RGBTColour cx2y2g = GammaCurve::Encode(aaGamma, cx2y2);

    if((level > 0) &&
       ((ColourDistanceRGBT(cx0y0g, cx0y2g) >= aaThreshold) ||
        (ColourDistanceRGBT(cx0y0g, cx2y0g) >= aaThreshold) ||
        (ColourDistanceRGBT(cx0y0g, cx2y2g) >= aaThreshold) ||
        (ColourDistanceRGBT(cx0y2g, cx2y0g) >= aaThreshold) ||
        (ColourDistanceRGBT(cx0y2g, cx2y2g) >= aaThreshold) ||
        (ColourDistanceRGBT(cx2y0g, cx2y2g) >= aaThreshold)))
    {
        RGBTColour rcx0y0;
        RGBTColour rcx0y1;
        RGBTColour rcx1y0;
        RGBTColour rcx1y1;
        RGBTColour col;
        DBL rxcx0y1, rycx0y1;
        DBL rxcx1y0, rycx1y0;
        DBL rxcx2y1, rycx2y1;
        DBL rxcx1y2, rycx1y2;
        DBL rxcx1y1, rycx1y1;
        DBL d2 = d * 0.5;

        // Trace:
        // ooo
        // *oo
        // ooo
        if(buffer.Sampled(bx, by + bstephalf) == false)
        {
            if (jitterScale > 0.0)
            {
                Jitter2d(x - d, y, rxcx0y1, rycx0y1);
                trace(x+0.5 - d + (rxcx0y1 * jitterScale), y+0.5 + (rycx0y1 * jitterScale), GetViewData()->GetWidth(), GetViewData()->GetHeight(), col);
            }
            else
                trace(x+0.5 - d, y+0.5, GetViewData()->GetWidth(), GetViewData()->GetHeight(), col);

            buffer.SetSample(bx, by + bstephalf, col);

            GetViewDataPtr()->Stats()[Number_Of_Samples]++;
            Cooperate();
        }

        // Trace:
        // o*o
        // ooo
        // ooo
        if(buffer.Sampled(bx + bstephalf, by) == false)
        {
            if (jitterScale > 0.0)
            {
                Jitter2d(x, y - d, rxcx1y0, rycx1y0);
                trace(x+0.5 + (rxcx1y0 * jitterScale), y+0.5 - d + (rycx1y0 * jitterScale), GetViewData()->GetWidth(), GetViewData()->GetHeight(), col);
            }
            else
                trace(x+0.5, y+0.5 - d, GetViewData()->GetWidth(), GetViewData()->GetHeight(), col);

            buffer.SetSample(bx + bstephalf, by, col);

            GetViewDataPtr()->Stats()[Number_Of_Samples]++;
            Cooperate();
        }

        // Trace:
        // ooo
        // oo*
        // ooo
        if(buffer.Sampled(bx + bstep, by + bstephalf) == false)
        {
            if (jitterScale > 0.0)
            {
                Jitter2d(x + d, y, rxcx2y1, rycx2y1);
                trace(x+0.5 + d + (rxcx2y1 * jitterScale), y+0.5 + (rycx2y1 * jitterScale), GetViewData()->GetWidth(), GetViewData()->GetHeight(), col);
            }
            else
                trace(x+0.5 + d, y+0.5, GetViewData()->GetWidth(), GetViewData()->GetHeight(), col);

            buffer.SetSample(bx + bstep, by + bstephalf, col);

            GetViewDataPtr()->Stats()[Number_Of_Samples]++;
            Cooperate();
        }

        // Trace:
        // ooo
        // ooo
        // o*o
        if(buffer.Sampled(bx + bstephalf, by + bstep) == false)
        {
            if (jitterScale > 0.0)
            {
                Jitter2d(x, y + d, rxcx1y2, rycx1y2);
                trace(x+0.5 + (rxcx1y2 * jitterScale), y+0.5 + d + (rycx1y2 * jitterScale), GetViewData()->GetWidth(), GetViewData()->GetHeight(), col);
            }
            else
                trace(x+0.5, y+0.5 + d, GetViewData()->GetWidth(), GetViewData()->GetHeight(), col);

            buffer.SetSample(bx + bstephalf, by + bstep, col);

            GetViewDataPtr()->Stats()[Number_Of_Samples]++;
            Cooperate();
        }

        // Trace:
        // ooo
        // o*o
        // ooo
        if(buffer.Sampled(bx + bstephalf, by + bstephalf) == false)
        {
            if (jitterScale > 0.0)
            {
                Jitter2d(x, y, rxcx1y1, rycx1y1);
                trace(x+0.5 + (rxcx1y1 * jitterScale), y+0.5 + (rycx1y1 * jitterScale), GetViewData()->GetWidth(), GetViewData()->GetHeight(), col);
            }
            else
                trace(x+0.5, y+0.5, GetViewData()->GetWidth(), GetViewData()->GetHeight(), col);

            buffer.SetSample(bx + bstephalf, by + bstephalf, col);

            GetViewDataPtr()->Stats()[Number_Of_Samples]++;
            Cooperate();
        }

        // Subdivide Input:
        // ++o
        // ++o
        // ooo
        // Subdivide Output:
        // *o
        // oo
        SubdivideOnePixel(x - d2, y - d2, d2, bx, by, bstephalf, buffer, rcx0y0, level - 1);

        // Subdivide Input:
        // ooo
        // ++o
        // ++o
        // Subdivide Output:
        // oo
        // *o
        SubdivideOnePixel(x - d2, y + d2, d2, bx, by + bstephalf, bstephalf, buffer, rcx0y1, level - 1);

        // Subdivide Input:
        // o++
        // o++
        // ooo
        // Subdivide Output:
        // o*
        // oo
        SubdivideOnePixel(x + d2, y - d2, d2, bx + bstephalf, by, bstephalf, buffer, rcx1y0, level - 1);

        // Subdivide Input:
        // ooo
        // o++
        // o++
        // Subdivide Output:
        // oo
        // o*
        SubdivideOnePixel(x + d2, y + d2, d2, bx + bstephalf, by + bstephalf, bstephalf, buffer, rcx1y1, level - 1);

        result = (rcx0y0 + rcx0y1 + rcx1y0 + rcx1y1) / 4.0;
    }
    else
    {
        result = (cx0y0 + cx0y2 + cx2y0 + cx2y2) / 4.0;
    }
}

}
// end of namespace pov
