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
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
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
                     int level, unsigned int ls, bool lf, bool pairs, DBL aab, int aap, int aar) :
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
    aaPairs(pairs),
    aaPass(aap),
    aaRound(aar),
    media(GetViewDataPtr(), &trace, &photonGatherer),
    radiosity(vd->GetSceneData(), GetViewDataPtr(),
              vd->GetSceneData()->radiositySettings, vd->GetRadiosityCache(), cooperate, true, vd->GetCamera().Location),
    photonGatherer(&vd->GetSceneData()->mediaPhotonMap, vd->GetSceneData()->photonSettings)
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
            else if(tracingMethod == 5)
                ProgressiveM5();
            else if(tracingMethod == 1)
                ProgressiveRefineM1();
            else if(tracingMethod == 2)
                ProgressiveRefineM2();
            else if(aaPairs)
                ProgressivePairsM4();
            else
                ProgressiveRefineM4();
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
        GetViewData()->CompletedRectangle(rect, serial, pixels, 1, passContributesToImage, passCompletesImage);

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
            GetViewData()->CompletedRectangle(rect, serial, pixelpositions, pixelcolors, previewSize, passContributesToImage, passCompletesImage);

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
        GetViewData()->CompletedRectangle(rect, serial, pixels.GetPixels(), 1, passContributesToImage, passCompletesImage);

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
        GetViewData()->CompletedRectangle(rect, serial, pixels.GetPixels(), 1, passContributesToImage, passCompletesImage);

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
        GetViewData()->CompletedRectangle(rect, serial, pixels, 1, passContributesToImage, passCompletesImage);

        Cooperate();
    }
}

void TraceTask::ProgressiveLevel()
{
    const POVRect& area = GetViewData()->GetRenderArea();
    const bool corners = (tracingMethod == 2);
    const bool keep = GetViewData()->KeepsLatticeSamples();
    POVRect rect;
    vector<Vector2d> positions;
    vector<RGBTColour> colors;
    unsigned int serial;

    while(GetViewData()->GetNextRectangle(rect, serial) == true)
    {
        radiosity.BeforeTile(highReproducibility? serial : 0, RadiosityFunction::FINAL_TRACE);

        positions.clear();
        colors.clear();

        if (keep)
            GetViewData()->EnsureLatticeSamples(area.right + (corners ? 1 : 0) + 1, area.bottom + (corners ? 1 : 0) + 1);

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
                    GetViewData()->LatticeSample(x, y) = col;
                if(GetViewData()->KeepsGrain())
                    GetViewData()->LatticeGrain(x, y) = trace.Grainy();
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
                                              float(positions.size()) / float(rect.GetArea()), nullptr, progressLevel);

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
        GetViewData()->CompletedRectangle(rect, serial, pixels, 1, true, true, 1.0f, nullptr, progressLevel);

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
        GetViewData()->CompletedRectangle(rect, serial, pixels, 1, true, true, 1.0f, nullptr, progressLevel);

        Cooperate();
    }
}

namespace
{
// Directions to the eight neighbours; a pixel tests its pairs towards the first four, and d + 4 is opposite d.
const int kDirX[8] = { 1, 1, 0, -1, -1, -1,  0,  1 };
const int kDirY[8] = { 0, 1, 1,  1,  0, -1, -1, -1 };

std::uint64_t CellKey(unsigned int level, unsigned int cx, unsigned int cy)
{
    return (std::uint64_t(level) << 40) | (std::uint64_t(cy) << 20) | cx;
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

bool TraceTask::Contended(const OkLab& a, const OkLab& b) const
{
    const float dl = a.l - b.l, da = a.a - b.a, db = a.b - b.b;
    return ((dl * dl + da * da + db * db) >= aaThreshold * aaThreshold) || (std::fabs(a.t - b.t) >= aaThreshold);
}

void TraceTask::LoadBlockLabs(const POVRect& rect)
{
    const POVRect& area = GetViewData()->GetRenderArea();
    blockLabLeft = int(rect.left) - 1;
    blockLabTop = int(rect.top) - 1;
    blockLabWidth = int(rect.GetWidth()) + 2;
    blockLabs.assign(size_t(blockLabWidth) * (rect.GetHeight() + 2), OkLab());
    for (int y = max(blockLabTop, int(area.top)); y <= min(int(rect.bottom) + 1, int(area.bottom)); y++)
        for (int x = max(blockLabLeft, int(area.left)); x <= min(int(rect.right) + 1, int(area.right)); x++)
            blockLabs[(x - blockLabLeft) + (y - blockLabTop) * blockLabWidth] = ToOkLab(GetViewData()->LatticeSample(x, y));
}

void TraceTask::TraceSample(DBL x, DBL y, unsigned int px, unsigned int py, RGBTColour& col)
{
    // a sample keeps its pixel's random draws, so anti-aliasing keeps intended grain
    if (GetViewData()->KeepsGrain())
        trace(x, y, GetViewData()->GetWidth(), GetViewData()->GetHeight(), col, Vector2d(px + 0.5, py + 0.5));
    else
        trace(x, y, GetViewData()->GetWidth(), GetViewData()->GetHeight(), col);
    GetViewDataPtr()->Stats()[Number_Of_Samples]++;
    Cooperate();
}

void TraceTask::ProgressivePairsM4()
{
    const POVRect& area = GetViewData()->GetRenderArea();
    const bool keepGrain = GetViewData()->KeepsGrain();
    POVRect rect;
    unsigned int serial;

    while(GetViewData()->GetNextRectangle(rect, serial) == true)
    {
        radiosity.BeforeTile(highReproducibility? serial : 0, RadiosityFunction::FINAL_TRACE);
        LoadBlockLabs(rect);

        for(unsigned int y = rect.top; y <= rect.bottom; y++)
        {
            for(unsigned int x = rect.left; x <= rect.right; x++)
            {
                const OkLab& lp = BlockLab(x, y);
                const Vector2d centre(x + 0.5, y + 0.5);
                std::uint16_t bits = 0;

                for(int d = 0; d < 8; d++)
                {
                    const int qx = int(x) + kDirX[d], qy = int(y) + kDirY[d];
                    const bool outX = (qx < int(area.left)) || (qx > int(area.right));
                    const bool outY = (qy < int(area.top)) || (qy > int(area.bottom));
                    const Vector2d at(centre[X] + 0.5 * kDirX[d], centre[Y] + 0.5 * kDirY[d]);
                    RGBTColour probe, twin;

                    if(outX || outY)
                    {
                        // beyond the frame: one probe per side neighbour and per frame corner, as a diagonal along a side shares its edge
                        if((kDirX[d] != 0) && (kDirY[d] != 0) && !(outX && outY))
                            continue;
                        TraceSample(at[X], at[Y], x, y, probe);
                        if(Contended(ToOkLab(probe), lp))
                            bits |= (1 << d);
                        continue;
                    }
                    if((d >= 4) || !Contended(lp, BlockLab(qx, qy)))
                        continue;

                    const OkLab& lq = BlockLab(qx, qy);
                    if(keepGrain && (GetViewData()->LatticeGrain(x, y) || GetViewData()->LatticeGrain(qx, qy)))
                    {
                        // noise test: each pixel's own draws at the shared point; a probe matching its own centre clears it
                        trace.TracePair(at[X], at[Y], GetViewData()->GetWidth(), GetViewData()->GetHeight(), centre, Vector2d(qx + 0.5, qy + 0.5), probe, twin);
                        GetViewDataPtr()->Stats()[Number_Of_Samples] += 2;
                        Cooperate();
                        if(Contended(ToOkLab(probe), lp))
                            bits |= (1 << d);
                        if(Contended(ToOkLab(twin), lq))
                            bits |= (1 << (8 + d));
                        continue;
                    }

                    // a probe matching one side puts the edge in the other pixel; a third colour refines both, and one matching both neither
                    TraceSample(at[X], at[Y], x, y, probe);
                    const OkLab lb = ToOkLab(probe);
                    if(Contended(lb, lp))
                        bits |= (1 << d);
                    if(Contended(lb, lq))
                        bits |= (1 << (8 + d));
                }

                GetViewData()->Contention(x, y) = bits;
            }
        }

        radiosity.AfterTile();

        GetViewDataPtr()->AfterTile();
        GetViewData()->CompletedRectangle(rect, serial, 0.0f);

        Cooperate();
    }
}

void TraceTask::ProgressiveRefineM4()
{
    const POVRect& area = GetViewData()->GetRenderArea();
    POVRect rect;
    vector<RGBTColour> pixels;
    unsigned int serial;

    while(GetViewData()->GetNextRectangle(rect, serial) == true)
    {
        radiosity.BeforeTile(highReproducibility? serial : 0, RadiosityFunction::FINAL_TRACE);
        LoadBlockLabs(rect);

        pixels.clear();
        pixels.reserve(rect.GetArea());

        for(unsigned int y = rect.top; y <= rect.bottom; y++)
        {
            for(unsigned int x = rect.left; x <= rect.right; x++)
            {
                RGBTColour col = GetViewData()->LatticeSample(x, y);
                unsigned int directions = GetViewData()->Contention(x, y) & 0xff;

                // the neighbours to the west, north-west, north and north-east tested the pair and may ask this pixel to refine
                for(int d = 0; d < 4; d++)
                {
                    const int nx = int(x) - kDirX[d], ny = int(y) - kDirY[d];
                    if((nx >= int(area.left)) && (nx <= int(area.right)) && (ny >= int(area.top)) &&
                       (GetViewData()->Contention(nx, ny) & (1 << (8 + d))))
                        directions |= (1 << (d + 4));
                }

                if(directions != 0)
                    RefinePixelM4(x, y, directions, col);

                pixels.push_back(col);
                Cooperate();
            }
        }

        radiosity.AfterTile();

        GetViewDataPtr()->AfterTile();
        GetViewData()->CompletedRectangle(rect, serial, pixels, 1, true, true, 1.0f, nullptr, progressLevel);

        Cooperate();
    }
}

void TraceTask::RefinePixelM4(unsigned int x, unsigned int y, unsigned int directions, RGBTColour& col)
{
    GetViewDataPtr()->Stats()[Number_Of_Pixels_Supersampled]++;

    refineX = x;
    refineY = y;
    refineDirections = directions;
    cells.clear();
    cellColours.assign(1, col);
    cellLabs.assign(1, BlockLab(x, y));
    cellQueue.clear();
    cells[CellKey(0, 0, 0)] = Cell { 0, false, true };

    for(int d = 0; d < 8; d++)
        if(directions & (1 << d))
            SplitCell(0, 0, 0, d);
    for(size_t i = 0; i < cellQueue.size(); i++)
        CompareCell(cellQueue[i]);

    // the pixel is the area-weighted mean of its leaf cells
    col.Clear();
    for(const auto& cell : cells)
        if(!cell.second.split)
            col += cellColours[cell.second.sample] * (1.0 / DBL(std::uint64_t(1) << (2 * (cell.first >> 40))));
}

void TraceTask::SplitCell(unsigned int level, unsigned int cx, unsigned int cy, int d)
{
    if(level >= aaDepth)
        return;

    Cell& cell = cells[CellKey(level, cx, cy)];
    if(!cell.split)
    {
        cell.split = true;
        const Cell inherited { cell.sample, false, false };
        for(unsigned int j = 0; j < 4; j++)
            cells[CellKey(level + 1, 2 * cx + (j & 1), 2 * cy + (j >> 1))] = inherited;
    }

    // trace the children that face the direction: two for a side, one for a diagonal
    const DBL size = 1.0 / DBL(1u << (level + 1));
    for(unsigned int j = 0; j < 4; j++)
    {
        const int ix = int(j & 1), iy = int(j >> 1);
        if(((kDirX[d] != 0) && (ix != (kDirX[d] > 0 ? 1 : 0))) || ((kDirY[d] != 0) && (iy != (kDirY[d] > 0 ? 1 : 0))))
            continue;
        const unsigned int ccx = 2 * cx + ix, ccy = 2 * cy + iy;
        const std::uint64_t key = CellKey(level + 1, ccx, ccy);
        Cell& child = cells[key];
        if(child.split || child.traced)
            continue;
        RGBTColour sample;
        TraceSample(refineX + (ccx + 0.5) * size, refineY + (ccy + 0.5) * size, refineX, refineY, sample);
        child = Cell { int(cellColours.size()), false, true };
        cellColours.push_back(sample);
        cellLabs.push_back(ToOkLab(sample));
        cellQueue.push_back(key);
    }
}

void TraceTask::CompareCell(std::uint64_t key)
{
    const POVRect& area = GetViewData()->GetRenderArea();
    const unsigned int level = (unsigned int)(key >> 40);
    const unsigned int cx = (unsigned int)(key & 0xfffff), cy = (unsigned int)((key >> 20) & 0xfffff);
    const int n = int(1u << level);

    for(int d = 0; d < 8; d++)
    {
        const OkLab lc = cellLabs[cells[key].sample];
        const int nx = int(cx) + kDirX[d], ny = int(cy) + kDirY[d];

        if((nx < 0) || (ny < 0) || (nx >= n) || (ny >= n))
        {
            // across the pixel boundary, the neighbouring pixel's centre, towards neighbours whose pair put the edge here
            const int sx = (nx < 0 ? -1 : (nx >= n ? 1 : 0)), sy = (ny < 0 ? -1 : (ny >= n ? 1 : 0));
            const int px = int(refineX) + sx, py = int(refineY) + sy;
            int side = 0;
            while((kDirX[side] != sx) || (kDirY[side] != sy))
                side++;
            if((refineDirections & (1u << side)) && (px >= int(area.left)) && (px <= int(area.right)) &&
               (py >= int(area.top)) && (py <= int(area.bottom)) && Contended(lc, BlockLab(px, py)))
                SplitCell(level, cx, cy, d);
            continue;
        }

        // the leaf holding the neighbouring position, split too if it has a sample of its own; finer neighbours compare themselves
        for(unsigned int l = 0; l <= level; l++)
        {
            const unsigned int lx = unsigned(nx) >> (level - l), ly = unsigned(ny) >> (level - l);
            const Cell& other = cells[CellKey(l, lx, ly)];
            if(other.split)
                continue;
            if(Contended(lc, cellLabs[other.sample]))
            {
                SplitCell(level, cx, cy, d);
                if(other.traced)
                    SplitCell(l, lx, ly, (d + 4) % 8);
            }
            break;
        }
    }
}

namespace
{
/// Method 5's experiment switches, read once from POV_AA5, e.g. `W=7,FREE=0,NOISE=1,EMAX=2,GAP=0.0625,ROUNDS=6`.
struct M5Options final
{
    int window = 5;
    bool freeOnly = false;
    bool noise = true;
    float emax = 2.0f;
    float gapMin = 0.001f;
    int rounds = TraceTask::kM5Rounds;
    bool chain = false;             ///< Fit whole contours instead of a window round each pixel.
    int lmin = 4;                   ///< Shortest run of crossings a chain segment may be fitted to.
    float rho = 0.35f;              ///< Share of contended links in a 5x5 window above which it is noise.
    float ceps = 0.002f;            ///< Chain mode: differences below this are rounding, whatever their neighbours.
    float kappa = 2.0f;             ///< Chain mode: a link is a contour crossing when it jumps this many times its neighbours' ramp.
    bool map = false;               ///< Write where the samples went instead of the image.
    bool join = false;              ///< Chain mode: join collinear segments through junctions (worse than not, so far).
    float reach = 4.0f;             ///< Join: how far apart two segment ends may be, in pixels.
    float turn = 0.94f;             ///< Join: how nearly opposite their outward directions must be (cosine).
    float lateral = 1.0f;           ///< Join: how far one end may sit off the other's line, in pixels.
};

const M5Options& M5Opts()
{
    static const M5Options options = []
    {
        M5Options o;
        if(const char* env = std::getenv("POV_AA5"))
        {
            const std::string s(env);
            size_t pos = 0;
            while(pos < s.size())
            {
                size_t end = s.find(',', pos);
                if(end == std::string::npos)
                    end = s.size();
                const std::string item = s.substr(pos, end - pos);
                const size_t eq = item.find('=');
                if(eq != std::string::npos)
                {
                    const std::string key = item.substr(0, eq);
                    const double v = std::atof(item.c_str() + eq + 1);
                    if(key == "W")           o.window = int(v);
                    else if(key == "FREE")   o.freeOnly = (v != 0.0);
                    else if(key == "NOISE")  o.noise = (v != 0.0);
                    else if(key == "EMAX")   o.emax = float(v);
                    else if(key == "GAP")    o.gapMin = float(v);
                    else if(key == "ROUNDS") o.rounds = int(v);
                    else if(key == "CHAIN")  o.chain = (v != 0.0);
                    else if(key == "LMIN")   o.lmin = int(v);
                    else if(key == "RHO")    o.rho = float(v);
                    else if(key == "CEPS")   o.ceps = float(v);
                    else if(key == "KAPPA")  o.kappa = float(v);
                    else if(key == "MAP")    o.map = (v != 0.0);
                    else if(key == "JOIN")   o.join = (v != 0.0);
                    else if(key == "REACH")  o.reach = float(v);
                    else if(key == "TURN")   o.turn = float(v);
                    else if(key == "LATERAL") o.lateral = float(v);
                }
                pos = end + 1;
            }
        }
        o.window = std::min(7, std::max(3, o.window)) | 1;
        return o;
    }();
    return options;
}

inline float Dist2(const float* a, const float* b)
{
    const float d0 = a[0] - b[0], d1 = a[1] - b[1], d2 = a[2] - b[2], d3 = a[3] - b[3];
    return d0 * d0 + d1 * d1 + d2 * d2 + d3 * d3;
}

/// Priorities within about a tenth of an octave share a bucket, and buckets are what the planner ties on.
int M5Bucket(float priority)
{
    return std::min(255, std::max(0, int(10.0f * (std::log2(std::max(priority, 1.0e-6f)) + 14.0f))));
}

/// Fraction of the unit pixel lying beyond the line n.q = s, with the pixel centre at the origin.
float M5Coverage(float nx, float ny, float s)
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
}

namespace
{
/// Where a contour crosses a link between two neighbouring samples, and the pixels of those samples, the one left of travel first.
struct M5Bracket final { float cx, cy; int lx, ly, rx, ry; };

/// The line with the widest gap between the samples that must lie beyond it (left of the chain) and short of it (right).
/// Returns that gap, negative when no line separates them; n.q = s is the line, with n pointing at the left samples.
float M5BestLine(const std::vector<M5Bracket>& br, int i0, int i1, float& nx, float& ny, float& s)
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
            const M5Bracket& b = br[i];
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

/// Keep only the points that can bound a line: the upper hull when the points must lie below it, else the lower.
void M5Hull(std::vector<AaSeg::Pt>& p, bool upper)
{
    std::sort(p.begin(), p.end(), [](const AaSeg::Pt& a, const AaSeg::Pt& b) { return (a.u < b.u) || ((a.u == b.u) && (a.v < b.v)); });
    std::vector<AaSeg::Pt> h;
    for(const AaSeg::Pt& q : p)
    {
        while(h.size() >= 2)
        {
            const AaSeg::Pt& a = h[h.size() - 2];
            const AaSeg::Pt& b = h.back();
            const float cross = (b.u - a.u) * (q.v - a.v) - (b.v - a.v) * (q.u - a.u);
            if(upper ? (cross >= 0.0f) : (cross <= 0.0f))
                h.pop_back();
            else
                break;
        }
        h.push_back(q);
    }
    p.swap(h);
}

/// Every line v = a + b u that keeps the colour A points below it and the colour B points above is possible; take the
/// middle of those and work out how far either way the line could lie at three places along it. False when none is.
bool M5Estimate(AaSeg& s)
{
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

/// Fold the last round's probe results into each segment's bounds and re-estimate its line.
void M5FoldSegments(ViewData* vd)
{
    for(AaSeg& s : vd->aaSegs)
    {
        if(s.fresh.empty())
            continue;
        const std::vector<AaSeg::Pt> keepA = s.hullA, keepB = s.hullB;
        for(const AaSeg::Pt& p : s.fresh)
            (p.b ? s.hullB : s.hullA).push_back(p);
        s.fresh.clear();
        M5Hull(s.hullA, true);
        M5Hull(s.hullB, false);
        if(!M5Estimate(s))
        {
            // a probe that contradicts the rest: keep the line as it was, and probe this segment no more
            s.hullA = keepA;
            s.hullB = keepB;
            M5Estimate(s);
            s.state = 1;
        }
    }
}

bool M5StationOk(const AaSeg& s, float u);

/// Choose the segments to probe this round, best first, before any ray is traced for them: a probe at the end where
/// the line is least certain is worth the contrast, times how uncertain it is, times how many pixels the line crosses.
void M5PlanSegments(ViewData* vd, const M5Options& o, bool enabled)
{
    const POVRect& area = vd->GetRenderArea();
    std::vector<int> bucket(vd->aaSegs.size(), -1);
    std::int64_t hist[256] = {}, total = 0;
    if(enabled)
    {
        for(size_t i = 0; i < vd->aaSegs.size(); i++)
        {
            const AaSeg& s = vd->aaSegs[i];
            const float wavg = 0.25f * (s.w[0] + 2.0f * s.w[1] + s.w[2]);
            if(s.state || (wavg <= o.gapMin))
                continue;
            bucket[i] = M5Bucket(s.contrast * wavg * std::max(1.0f, s.length));
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
            if(cum >= vd->aaBudgetLeft)
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
        float u = 0.5f * (s.uLo + s.uHi);
        bool found = (len < 4.0f);
        if(len >= 4.0f)
        {
            // at whichever end is less certain, else the other, as near the end as the colours there stay steady
            const float inset = std::min(2.0f, 0.25f * len);
            for(int e = 0; (e < 2) && !found; e++)
            {
                const bool lowEnd = (e == 0) == (s.w[0] >= s.w[2]);
                for(int step = 0; (step < 6) && !found; step++)
                {
                    const float off = inset + 2.0f * float(step);
                    if(off > 0.4f * len)
                        break;
                    u = lowEnd ? (s.uLo + off) : (s.uHi - off);
                    found = M5StationOk(s, u);
                }
            }
        }
        if(!found)
            continue;
        const float v = s.a + s.b * u;
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
    if(cut)
        vd->aaExhausted = true;
}

/// The colours either side of a segment's line, near a position along it.
void M5LocalColours(const AaSeg& s, float u, RGBTColour& a, RGBTColour& b)
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

/// Whether the colours either side of a segment stay the same for a few samples each way of u, so that a probe there
/// can be told apart by them; through a checkerboard's corners they swap, and no probe should land near one.
bool M5StationOk(const AaSeg& s, float u)
{
    const size_t n = s.bu.size();
    if(n == 0)
        return false;
    const size_t k = std::min(n - 1, size_t(std::lower_bound(s.bu.begin(), s.bu.end(), u) - s.bu.begin()));
    const size_t lo = (k > 4) ? (k - 4) : 0, hi = std::min(n, k + 5);
    auto diff = [](const RGBTColour& p, const RGBTColour& q) { return std::fabs(p.red() - q.red()) + std::fabs(p.green() - q.green()) + std::fabs(p.blue() - q.blue()); };
    const float span = diff(s.bA[k], s.bB[k]);
    for(size_t i = lo; i < hi; i++)
        if((diff(s.bA[i], s.bA[k]) > 0.3f * span) || (diff(s.bB[i], s.bB[k]) > 0.3f * span))
            return false;
    return true;
}

void M5SortBrackets(AaSeg& s)
{
    std::vector<size_t> idx(s.bu.size());
    for(size_t i = 0; i < idx.size(); i++)
        idx[i] = i;
    std::sort(idx.begin(), idx.end(), [&](size_t p, size_t q) { return s.bu[p] < s.bu[q]; });
    std::vector<float> bu(idx.size());
    std::vector<RGBTColour> bA(idx.size()), bB(idx.size());
    for(size_t i = 0; i < idx.size(); i++)
    {
        bu[i] = s.bu[idx[i]];
        bA[i] = s.bA[idx[i]];
        bB[i] = s.bB[idx[i]];
    }
    s.bu.swap(bu);
    s.bA.swap(bA);
    s.bB.swap(bB);
}

/// Where a segment's line ends, and which way it runs on out of that end.
void M5SegEnd(const AaSeg& s, int end, float& px, float& py, float& dx, float& dy)
{
    const float u = end ? s.uHi : s.uLo;
    const float v = s.a + s.b * u;
    px = s.cx + u * s.tx + v * s.nx;
    py = s.cy + u * s.ty + v * s.ny;
    dx = s.tx + s.b * s.nx;
    dy = s.ty + s.b * s.ny;
    const float l = std::sqrt(dx * dx + dy * dy);
    dx /= l;
    dy /= l;
    if(!end)
    {
        dx = -dx;
        dy = -dy;
    }
}

/// Join b onto a when one line can still keep both their colour A samples on one side and their colour B samples on
/// the other; the colours themselves may swap across the join, as they do through a checkerboard's corners.
bool M5TryJoin(const AaSeg& a, const AaSeg& b, AaSeg& out)
{
    out = a;
    const bool flip = (a.nx * b.nx + a.ny * b.ny) < 0.0f;
    auto toA = [&](float u, float v, float& ua, float& va)
    {
        const float px = b.cx + u * b.tx + v * b.nx - a.cx, py = b.cy + u * b.ty + v * b.ny - a.cy;
        ua = px * a.tx + py * a.ty;
        va = px * a.nx + py * a.ny;
    };
    for(int side = 0; side < 2; side++)
    {
        const std::vector<AaSeg::Pt>& src = side ? b.hullB : b.hullA;
        const bool toB = (side == 1) != flip;
        for(const AaSeg::Pt& p : src)
        {
            float u, v;
            toA(p.u, p.v, u, v);
            (toB ? out.hullB : out.hullA).push_back(AaSeg::Pt { u, v, std::uint8_t(toB ? 1 : 0) });
        }
    }
    M5Hull(out.hullA, true);
    M5Hull(out.hullB, false);

    float u1, v1, u2, v2;
    toA(b.uLo, b.a + b.b * b.uLo, u1, v1);
    toA(b.uHi, b.a + b.b * b.uHi, u2, v2);
    out.uLo = std::min(out.uLo, std::min(u1, u2));
    out.uHi = std::max(out.uHi, std::max(u1, u2));
    for(size_t k = 0; k < b.bu.size(); k++)
    {
        float u, v;
        toA(b.bu[k], 0.0f, u, v);
        out.bu.push_back(u);
        out.bA.push_back(flip ? b.bB[k] : b.bA[k]);
        out.bB.push_back(flip ? b.bA[k] : b.bB[k]);
    }
    M5SortBrackets(out);

    const float la = a.length, lb = b.length;
    out.contrast = (a.contrast * la + b.contrast * lb) / std::max(1.0e-3f, la + lb);
    out.length = out.uHi - out.uLo;
    out.probes = std::uint8_t(std::min(255, int(a.probes) + int(b.probes)));
    return M5Estimate(out);
}

/// Lines run on through a junction: where two segments end close together heading the same way, and one line
/// fits the samples of both, they are one segment, so the whole edge is pinned down by every sample along it.
void M5JoinSegments(ViewData* vd, float reach, float turn, float lateral)
{
    std::vector<AaSeg>& segs = vd->aaSegs;
    const float cell = 4.0f;
    auto key = [](int gx, int gy) { return (std::uint64_t(std::uint32_t(gx)) << 32) | std::uint64_t(std::uint32_t(gy)); };
    for(int pass = 0; pass < 4; pass++)
    {
        std::unordered_map<std::uint64_t, std::vector<std::pair<int, int>>> grid;
        for(size_t i = 0; i < segs.size(); i++)
        {
            if(segs[i].state == 2)
                continue;
            for(int e = 0; e < 2; e++)
            {
                float px, py, dx, dy;
                M5SegEnd(segs[i], e, px, py, dx, dy);
                grid[key(int(std::floor(px / cell)), int(std::floor(py / cell)))].push_back(std::make_pair(int(i), e));
            }
        }
        std::vector<std::uint8_t> touched(segs.size(), 0);
        bool changed = false;
        for(size_t i = 0; i < segs.size(); i++)
        {
            for(int e1 = 0; (e1 < 2) && !touched[i] && (segs[i].state != 2); e1++)
            {
                float px, py, dx, dy;
                M5SegEnd(segs[i], e1, px, py, dx, dy);
                const float inv = 1.0f / std::sqrt(1.0f + segs[i].b * segs[i].b);
                const float nlx = (segs[i].nx - segs[i].b * segs[i].tx) * inv, nly = (segs[i].ny - segs[i].b * segs[i].ty) * inv;
                int bestJ = -1;
                float bestD = reach * reach;
                for(int gx = -1; gx <= 1; gx++)
                {
                    for(int gy = -1; gy <= 1; gy++)
                    {
                        auto it = grid.find(key(int(std::floor(px / cell)) + gx, int(std::floor(py / cell)) + gy));
                        if(it == grid.end())
                            continue;
                        for(const auto& cand : it->second)
                        {
                            const int j = cand.first;
                            if((j == int(i)) || (segs[size_t(j)].state == 2) || touched[size_t(j)])
                                continue;
                            float qx, qy, ex, ey;
                            M5SegEnd(segs[size_t(j)], cand.second, qx, qy, ex, ey);
                            const float d2 = (qx - px) * (qx - px) + (qy - py) * (qy - py);
                            if((d2 > bestD) || (dx * ex + dy * ey > -turn) || (std::fabs((qx - px) * nlx + (qy - py) * nly) > lateral))
                                continue;
                            bestD = d2;
                            bestJ = j;
                        }
                    }
                }
                if(bestJ < 0)
                    continue;
                AaSeg joined;
                if(M5TryJoin(segs[i], segs[size_t(bestJ)], joined))
                {
                    segs[i] = std::move(joined);
                    AaSeg& dead = segs[size_t(bestJ)];
                    dead.state = 2;
                    dead.hullA.clear(); dead.hullB.clear(); dead.bu.clear(); dead.bA.clear(); dead.bB.clear();
                    touched[i] = touched[size_t(bestJ)] = 1;
                    changed = true;
                }
            }
        }
        if(!changed)
            break;
    }
}

/// Method 5 without windows: find every contour in the centre samples, cut it into the longest straight runs the
/// samples allow, and give each pixel the line of the run it lies most inside of.
void M5ChainFit(ViewData* vd, const M5Options& o)
{
    vd->aaSegs.clear();
    const POVRect& area = vd->GetRenderArea();
    const int x0 = int(area.left), y0 = int(area.top), w = int(area.GetWidth()), h = int(area.GetHeight());
    const float eps2 = o.ceps * o.ceps, k2 = o.kappa * o.kappa;
    auto at = [&](int x, int y) { return size_t(x - x0) + size_t(y - y0) * size_t(w); };
    auto lab = [&](int x, int y) { return vd->AaLabAt(unsigned(x), unsigned(y)); };

    // squared distance to the east and south neighbours
    std::vector<float> dE(size_t(w) * size_t(h), 0.0f), dS(size_t(w) * size_t(h), 0.0f);
    for(int y = y0; y < y0 + h; y++)
    {
        for(int x = x0; x < x0 + w; x++)
        {
            if(x + 1 < x0 + w)
                dE[at(x, y)] = Dist2(lab(x, y), lab(x + 1, y));
            if(y + 1 < y0 + h)
                dS[at(x, y)] = Dist2(lab(x, y), lab(x, y + 1));
        }
    }

    // bit 0: the link to the east neighbour is a contour crossing, bit 1: the one to the south; a link is one when it
    // jumps well beyond the ramp on either side of it, so a faint line counts and a smooth gradient does not
    std::vector<std::uint8_t> link(size_t(w) * size_t(h), 0);
    for(int y = y0; y < y0 + h; y++)
    {
        for(int x = x0; x < x0 + w; x++)
        {
            std::uint8_t bits = 0;
            const float e = dE[at(x, y)];
            if(e >= eps2)
            {
                const float ramp = std::max((x > x0) ? dE[at(x - 1, y)] : 0.0f, (x + 1 < x0 + w) ? dE[at(x + 1, y)] : 0.0f);
                if(e >= k2 * ramp)
                    bits |= 1;
            }
            const float s = dS[at(x, y)];
            if(s >= eps2)
            {
                const float ramp = std::max((y > y0) ? dS[at(x, y - 1)] : 0.0f, (y + 1 < y0 + h) ? dS[at(x, y + 1)] : 0.0f);
                if(s >= k2 * ramp)
                    bits |= 2;
            }
            link[at(x, y)] = bits;
        }
    }

    // noise: a 5x5 window where too many links differ has no contour worth following
    std::vector<std::int32_t> sat(size_t(w + 1) * size_t(h + 1), 0);
    for(int y = 0; y < h; y++)
    {
        for(int x = 0; x < w; x++)
        {
            const std::uint8_t b = link[size_t(x) + size_t(y) * size_t(w)];
            sat[size_t(x + 1) + size_t(y + 1) * size_t(w + 1)] = int((b & 1) + ((b >> 1) & 1)) +
                sat[size_t(x) + size_t(y + 1) * size_t(w + 1)] + sat[size_t(x + 1) + size_t(y) * size_t(w + 1)] - sat[size_t(x) + size_t(y) * size_t(w + 1)];
        }
    }
    std::vector<std::uint8_t> noisy(size_t(w) * size_t(h), 0);
    for(int y = 0; y < h; y++)
    {
        for(int x = 0; x < w; x++)
        {
            const int xa = std::max(0, x - 2), xb = std::min(w, x + 3), ya = std::max(0, y - 2), yb = std::min(h, y + 3);
            const int sum = sat[size_t(xb) + size_t(yb) * size_t(w + 1)] - sat[size_t(xa) + size_t(yb) * size_t(w + 1)] -
                            sat[size_t(xb) + size_t(ya) * size_t(w + 1)] + sat[size_t(xa) + size_t(ya) * size_t(w + 1)];
            if(float(sum) > o.rho * 2.0f * float((xb - xa) * (yb - ya)))
            {
                noisy[size_t(x) + size_t(y) * size_t(w)] = 1;
                float far2 = 0.0f;
                const int dx[4] = { 1, -1, 0, 0 }, dy[4] = { 0, 0, 1, -1 };
                for(int k = 0; k < 4; k++)
                {
                    const int nx = x + x0 + dx[k], ny = y + y0 + dy[k];
                    if((nx >= x0) && (nx < x0 + w) && (ny >= y0) && (ny < y0 + h))
                        far2 = std::max(far2, Dist2(lab(x + x0, y + y0), lab(nx, ny)));
                }
                AaFit& f = vd->Fit(unsigned(x + x0), unsigned(y + y0));
                f.kind = 2;
                f.contrast = std::sqrt(far2);
            }
        }
    }
    for(int y = y0; y < y0 + h; y++)
    {
        for(int x = x0; x < x0 + w; x++)
        {
            std::uint8_t& bits = link[at(x, y)];
            if((bits & 1) && noisy[at(x, y)] && noisy[at(x + 1, y)])
                bits &= ~1;
            if((bits & 2) && noisy[at(x, y)] && noisy[at(x, y + 1)])
                bits &= ~2;
        }
    }

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
                // the diagonal that matches stays joined through the middle; the other pair's corners are cut off
                if(Dist2(lab(x, y), lab(x + 1, y + 1)) <= Dist2(lab(x + 1, y), lab(x, y + 1)))
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

    std::vector<std::uint8_t> visited(nodes, 0);
    std::vector<float> weight(size_t(w) * size_t(h), 0.0f);
    std::vector<size_t> chain;
    std::vector<M5Bracket> br;
    std::vector<std::uint8_t> weak(size_t(w) * size_t(h), 0);
    auto markWeak = [&](int a, int b)
    {
        for(int i = a; i <= b; i++)
        {
            weak[at(br[size_t(i)].lx, br[size_t(i)].ly)] = 1;
            weak[at(br[size_t(i)].rx, br[size_t(i)].ry)] = 1;
        }
    };
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
        if(m < o.lmin)
        {
            markWeak(0, m - 1);
            return;
        }
        // the left sample is the one on the left of the direction of travel
        for(int i = 0; i < m; i++)
        {
            const int ia = std::max(0, i - 1), ib = std::min(m - 1, i + 1);
            const float tx = br[size_t(ib)].cx - br[size_t(ia)].cx, ty = br[size_t(ib)].cy - br[size_t(ia)].cy;
            M5Bracket& b = br[size_t(i)];
            const float side = (float(b.lx) + 0.5f - b.cx) * ty - (float(b.ly) + 0.5f - b.cy) * tx;
            if(side < 0.0f)
            {
                std::swap(b.lx, b.rx);
                std::swap(b.ly, b.ry);
            }
        }
        float nx, ny, s;
        auto feasible = [&](int a, int b) { return M5BestLine(br, a, b, nx, ny, s) >= 0.0f; };

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
            if(good - i0 + 1 > o.lmin)
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
                    for(int k = 0; (k < 3) && (good - i0 + 1 > o.lmin) && turned(good); k++)
                        good--;
                    for(int k = 0; (k < 3) && (good - i0 + 1 > o.lmin) && turned(i0); k++)
                        i0++;
                }
            }

            if(good - i0 + 1 >= o.lmin)
            {
                M5BestLine(br, i0, good, nx, ny, s);
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
                        const M5Bracket& bk = br[size_t(i)];
                        const float ax = float(bk.rx) + 0.5f, ay = float(bk.ry) + 0.5f, bx = float(bk.lx) + 0.5f, by = float(bk.ly) + 0.5f;
                        sg.hullA.push_back(AaSeg::Pt { tx * ax + ty * ay - uc, nx * ax + ny * ay - s, 0 });
                        sg.hullB.push_back(AaSeg::Pt { tx * bx + ty * by - uc, nx * bx + ny * by - s, 1 });
                    }
                    M5Hull(sg.hullA, true);
                    M5Hull(sg.hullB, false);
                    const M5Bracket& bm = br[size_t((i0 + good) / 2)];
                    sg.contrast = std::sqrt(Dist2(lab(bm.lx, bm.ly), lab(bm.rx, bm.ry)));
                    for(int i = i0; i <= good; i++)
                    {
                        const M5Bracket& bk = br[size_t(i)];
                        sg.bu.push_back(tx * bk.cx + ty * bk.cy - uc);
                        sg.bA.push_back(vd->LatticeSample(unsigned(bk.rx), unsigned(bk.ry)));
                        sg.bB.push_back(vd->LatticeSample(unsigned(bk.lx), unsigned(bk.ly)));
                    }
                    M5SortBrackets(sg);
                    if(!M5Estimate(sg))
                        sg.state = 1;
                }
            }
            else
                markWeak(i0, good);

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

    // contours too short or too ragged to fit, with no good run through them, are noise
    if(o.join)
        M5JoinSegments(vd, o.reach, o.turn, o.lateral);

    // each pixel takes the line of the segment it lies most inside of, after any joining
    for(size_t id = 0; id < vd->aaSegs.size(); id++)
    {
        const AaSeg& sg = vd->aaSegs[id];
        if(sg.state == 2)
            continue;
        const float inv = 1.0f / std::sqrt(1.0f + sg.b * sg.b);
        const float nx = (sg.nx - sg.b * sg.tx) * inv, ny = (sg.ny - sg.b * sg.ty) * inv;
        const float s = sg.a * inv + nx * sg.cx + ny * sg.cy;
        const float ext = 0.5f * (std::fabs(nx) + std::fabs(ny));
        // probes can move the line anywhere the bounds allow, so own every pixel it might come to cross
        const float slack = 0.5f * std::max(sg.w[0], std::max(sg.w[1], sg.w[2])) + 1.0e-3f;
        const float uMid = 0.5f * (sg.uLo + sg.uHi), half = 0.5f * (sg.uHi - sg.uLo);

        auto visit = [&](int px, int py)
        {
            if((px < x0) || (px >= x0 + w) || (py < y0) || (py >= y0 + h) || noisy[at(px, py)])
                return;
            const float pcx = float(px) + 0.5f, pcy = float(py) + 0.5f;
            if(std::fabs(s - (nx * pcx + ny * pcy)) > ext + slack + 1.0e-3f)
                return;
            const float upos = (pcx - sg.cx) * sg.tx + (pcy - sg.cy) * sg.ty;
            const float wt = std::max(0.001f, 1.0f - std::fabs(upos - uMid) / half);
            if(wt <= weight[at(px, py)])
                return;
            weight[at(px, py)] = wt;
            AaFit& f = vd->Fit(unsigned(px), unsigned(py));
            f.kind = 1;
            f.seg = int(id);
            f.contrast = sg.contrast;
            f.probes = 0;
            M5LocalColours(sg, upos, f.a, f.b);
        };

        // every pixel the line passes through, column by column for a flat line, else row by row
        const float ax = sg.cx + (sg.uLo - 0.5f) * sg.tx + (sg.a + sg.b * (sg.uLo - 0.5f)) * sg.nx;
        const float ay = sg.cy + (sg.uLo - 0.5f) * sg.ty + (sg.a + sg.b * (sg.uLo - 0.5f)) * sg.ny;
        const float bx = sg.cx + (sg.uHi + 0.5f) * sg.tx + (sg.a + sg.b * (sg.uHi + 0.5f)) * sg.nx;
        const float by = sg.cy + (sg.uHi + 0.5f) * sg.ty + (sg.a + sg.b * (sg.uHi + 0.5f)) * sg.ny;
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

    // contours too short or too ragged to fit, with no good run through them, are noise
    for(int y = y0; y < y0 + h; y++)
    {
        for(int x = x0; x < x0 + w; x++)
        {
            AaFit& f = vd->Fit(unsigned(x), unsigned(y));
            if(weak[at(x, y)] && (f.kind == 0))
            {
                float far2 = std::max(dE[at(x, y)], dS[at(x, y)]);
                if(x > x0) far2 = std::max(far2, dE[at(x - 1, y)]);
                if(y > y0) far2 = std::max(far2, dS[at(x, y - 1)]);
                f.kind = 2;
                f.contrast = std::sqrt(far2);
            }
        }
    }
}
}

void TraceTask::PlanM5(ViewData* vd, int pass, int round)
{
    const M5Options& o = M5Opts();
    const POVRect& area = vd->GetRenderArea();

    if(pass == 0)
    {
        for(unsigned int y = area.top; y <= area.bottom; y++)
        {
            for(unsigned int x = area.left; x <= area.right; x++)
            {
                const OkLab l = OkLabOf(vd, vd->LatticeSample(x, y));
                float* dst = vd->AaLabAt(x, y);
                dst[0] = l.l; dst[1] = l.a; dst[2] = l.b; dst[3] = l.t;
                vd->Fit(x, y) = AaFit();
            }
        }
        if(o.chain)
            M5ChainFit(vd, o);
        const double pixels = double(area.GetWidth()) * double(area.GetHeight());
        vd->aaBudgetLeft = std::max<std::int64_t>(1, std::int64_t(std::ceil(pixels * vd->aaFraction)));
        vd->aaExhausted = false;
        return;
    }

    const bool probe = (pass == 2);
    const bool enabled = !o.freeOnly && !vd->aaExhausted && (probe ? (round < o.rounds) : o.noise);

    if(o.chain)
    {
        M5FoldSegments(vd);
        if(probe)
        {
            M5PlanSegments(vd, o, enabled);
            return;
        }
    }

    // a candidate's priority is what is still unknown about it, worked out before anything is traced for it
    auto bucketOf = [&](const AaFit& f) -> int
    {
        if(probe)
        {
            if(f.kind != 1)
                return -1;
            const float ext = 0.5f * (std::fabs(f.nx) + std::fabs(f.ny));
            const float gap = std::min(f.hi, ext) - std::max(f.lo, -ext);
            return (gap > o.gapMin) ? M5Bucket(f.contrast * gap) : -1;
        }
        // a noisy pixel's next sample is worth less for each it already has
        return ((f.kind == 2) && (f.probes < 250)) ? M5Bucket(f.contrast / (1.0f + float(f.probes))) : -1;
    };

    std::int64_t hist[256] = {};
    std::int64_t total = 0;
    if(enabled)
    {
        for(unsigned int y = area.top; y <= area.bottom; y++)
        {
            for(unsigned int x = area.left; x <= area.right; x++)
            {
                const int b = bucketOf(vd->Fit(x, y));
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
    int threshold = 0;
    std::int64_t admitted = total;
    bool cut = false;
    if(enabled)
    {
        std::int64_t cum = 0;
        for(int b = 255; b >= 0; b--)
        {
            cum += hist[b];
            if(cum >= vd->aaBudgetLeft)
            {
                threshold = b;
                admitted = cum;
                cut = true;
                break;
            }
        }
    }

    for(unsigned int y = area.top; y <= area.bottom; y++)
    {
        for(unsigned int x = area.left; x <= area.right; x++)
        {
            AaFit& f = vd->Fit(x, y);
            const int b = enabled ? bucketOf(f) : -1;
            f.planned = ((b >= 0) && (b >= threshold)) ? 1 : 0;
        }
    }
    vd->aaBudgetLeft -= admitted;
    if(cut)
        vd->aaExhausted = true;
}

void TraceTask::FitPixelM5(unsigned int x, unsigned int y, AaFit& fit)
{
    const M5Options& o = M5Opts();
    const POVRect& area = GetViewData()->GetRenderArea();
    const int h = o.window / 2;
    const float thr2 = float(aaThreshold * aaThreshold);
    const float* lc = GetViewData()->AaLabAt(x, y);

    struct Sample final { float dx, dy; const float* lab; int cls; };
    Sample s[49];
    int n = 0, farIndex = 0;
    float farthest = 0.0f;
    for(int dy = -h; dy <= h; dy++)
    {
        const int py = int(y) + dy;
        if((py < int(area.top)) || (py > int(area.bottom)))
            continue;
        for(int dx = -h; dx <= h; dx++)
        {
            const int px = int(x) + dx;
            if((px < int(area.left)) || (px > int(area.right)))
                continue;
            const float* l = GetViewData()->AaLabAt(px, py);
            s[n] = Sample { float(dx), float(dy), l, 0 };
            const float d = Dist2(l, lc);
            if(d > farthest)
            {
                farthest = d;
                farIndex = n;
            }
            n++;
        }
    }

    fit.kind = 0;
    if(farthest < thr2)
        return;

    // the two colours: the one farthest from the centre, then the one farthest from that
    const float* lb = s[farIndex].lab;
    const float* la = lc;
    float most = -1.0f;
    for(int i = 0; i < n; i++)
    {
        const float d = Dist2(s[i].lab, lb);
        if(d > most)
        {
            most = d;
            la = s[i].lab;
        }
    }
    const float u[4] = { lb[0] - la[0], lb[1] - la[1], lb[2] - la[2], lb[3] - la[3] };
    const float uu = u[0] * u[0] + u[1] * u[1] + u[2] * u[2] + u[3] * u[3];
    if(uu < thr2)
        return;

    // each sample's place between them, and whether it is on the line between them at all
    float gx = 0.0f, gy = 0.0f;
    int nB = 0, offLine = 0;
    for(int i = 0; i < n; i++)
    {
        const float d[4] = { s[i].lab[0] - la[0], s[i].lab[1] - la[1], s[i].lab[2] - la[2], s[i].lab[3] - la[3] };
        const float t = (d[0] * u[0] + d[1] * u[1] + d[2] * u[2] + d[3] * u[3]) / uu;
        float r2 = 0.0f;
        for(int k = 0; k < 4; k++)
            r2 += (d[k] - t * u[k]) * (d[k] - t * u[k]);
        if(r2 > std::max(thr2, 0.0625f * uu))
            offLine++;
        s[i].cls = (t >= 0.5f) ? 1 : 0;
        nB += s[i].cls;
        const float tc = std::min(1.0f, std::max(0.0f, t));
        gx += s[i].dx * tc;
        gy += s[i].dy * tc;
    }

    fit.contrast = std::sqrt(farthest);
    // more than two colours in a quarter of the window, or no direction to the change: nothing to track
    if((offLine * 4 > n) || (std::sqrt(gx * gx + gy * gy) < 0.08f * float(n)))
    {
        fit.kind = 2;
        return;
    }

    // the normal is about the direction the second colour lies in; try a few either side and keep the cleanest split
    const float theta0 = std::atan2(gy, gx);
    int bestErr = std::numeric_limits<int>::max();
    float bestGap = -1.0f, bestNx = 0.0f, bestNy = 0.0f, bestLo = 0.0f, bestHi = 0.0f;
    for(int j = -2; j <= 2; j++)
    {
        const float th = theta0 + float(j) * (3.14159265f / 16.0f);
        const float nx = std::cos(th), ny = std::sin(th);
        float sv[49];
        int sc[49];
        for(int i = 0; i < n; i++)
        {
            const float v = nx * s[i].dx + ny * s[i].dy;
            int k = i;
            while((k > 0) && (sv[k - 1] > v))
            {
                sv[k] = sv[k - 1];
                sc[k] = sc[k - 1];
                k--;
            }
            sv[k] = v;
            sc[k] = s[i].cls;
        }
        int errors = n - nB, minErr = errors, bestK = 0;
        float bestG = 0.0f;
        for(int k = 1; k <= n; k++)
        {
            errors += (sc[k - 1] == 1) ? 1 : -1;
            const float gap = (k < n) ? (sv[k] - sv[k - 1]) : 0.0f;
            if((errors < minErr) || ((errors == minErr) && (gap > bestG)))
            {
                minErr = errors;
                bestK = k;
                bestG = gap;
            }
        }
        if((minErr < bestErr) || ((minErr == bestErr) && (bestG > bestGap)))
        {
            bestErr = minErr;
            bestGap = bestG;
            bestNx = nx;
            bestNy = ny;
            bestLo = (bestK > 0) ? sv[bestK - 1] : sv[0] - 1.0f;
            bestHi = (bestK < n) ? sv[bestK] : sv[n - 1] + 1.0f;
        }
    }
    if(float(bestErr) > o.emax * float(n) / 25.0f)
    {
        fit.kind = 2;
        return;
    }

    // the colour of each side: near samples count most, and the pixel's own side is the pixel's own sample
    RGBTColour ca, cb;
    ca.Clear();
    cb.Clear();
    double wa = 0.0, wb = 0.0;
    int ownClass = 0;
    RGBTColour own = GetViewData()->LatticeSample(x, y);
    for(int i = 0; i < n; i++)
    {
        const double d2 = double(s[i].dx * s[i].dx + s[i].dy * s[i].dy);
        const double w = 1.0 / ((1.0 + d2) * (1.0 + d2));
        const RGBTColour col = GetViewData()->LatticeSample(unsigned(int(x) + int(s[i].dx)), unsigned(int(y) + int(s[i].dy)));
        if(s[i].dx == 0.0f && s[i].dy == 0.0f)
            ownClass = s[i].cls;
        if(s[i].cls == 1) { cb += col * w; wb += w; }
        else              { ca += col * w; wa += w; }
    }
    ca = ca * (1.0 / wa);
    cb = cb * (1.0 / wb);
    if(ownClass == 1)
        cb = own;
    else
        ca = own;

    fit.kind = 1;
    fit.nx = bestNx;
    fit.ny = bestNy;
    fit.lo = bestLo;
    fit.hi = bestHi;
    fit.a = ca;
    fit.b = cb;
}

void TraceTask::ProbePixelM5(unsigned int x, unsigned int y, AaFit& fit)
{
    const float ext = 0.5f * (std::fabs(fit.nx) + std::fabs(fit.ny));
    const float lo = std::max(fit.lo, -ext), hi = std::min(fit.hi, ext);
    const float sm = 0.5f * (lo + hi);
    const float tx = -fit.ny, ty = fit.nx;

    // the part of the line at that offset which lies inside the pixel; successive probes walk along it
    float ulo = -2.0f, uhi = 2.0f;
    const float along[2][2] = { { sm * fit.nx, tx }, { sm * fit.ny, ty } };
    for(const auto& axis : along)
    {
        if(std::fabs(axis[1]) < 1.0e-6f)
            continue;
        float u1 = (-0.5f - axis[0]) / axis[1], u2 = (0.5f - axis[0]) / axis[1];
        if(u1 > u2)
            std::swap(u1, u2);
        ulo = std::max(ulo, u1);
        uhi = std::min(uhi, u2);
    }
    if(uhi < ulo)
        ulo = uhi = 0.0f;
    // van der Corput, so any number of probes stay spread along the chord
    float vdc = 0.0f, scale = 0.5f;
    for(unsigned k = unsigned(fit.probes) + 1; k != 0; k >>= 1, scale *= 0.5f)
        if(k & 1)
            vdc += scale;
    const float u = 0.5f * (ulo + uhi) + (vdc - 0.5f) * (uhi - ulo);

    RGBTColour col;
    TraceSample(DBL(x) + 0.5 + sm * fit.nx + u * tx, DBL(y) + 0.5 + sm * fit.ny + u * ty, x, y, col);

    // a probe on the first colour's side puts the edge beyond it, otherwise before it
    const OkLab pa = ToOkLab(fit.a), pb = ToOkLab(fit.b), pc = ToOkLab(col);
    const float v[4] = { pb.l - pa.l, pb.a - pa.a, pb.b - pa.b, pb.t - pa.t };
    const float vv = v[0] * v[0] + v[1] * v[1] + v[2] * v[2] + v[3] * v[3];
    const float t = (vv > 0.0f) ? ((pc.l - pa.l) * v[0] + (pc.a - pa.a) * v[1] + (pc.b - pa.b) * v[2] + (pc.t - pa.t) * v[3]) / vv : 0.0f;
    if(t < 0.5f)
        fit.lo = sm;
    else
        fit.hi = sm;
    if(fit.lo > fit.hi)
        fit.lo = fit.hi = sm;
    fit.probes++;
}

void TraceTask::ProbeSegmentsM5()
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
        TraceSample(px, py, unsigned(px), unsigned(py), col);

        // which side of the line the point is on, by whether it is nearer the colour of one side or the other
        RGBTColour ca, cb;
        M5LocalColours(s, s.probeU, ca, cb);
        const OkLab pa = ToOkLab(ca), pb = ToOkLab(cb), pc = ToOkLab(col);
        const float v[4] = { pb.l - pa.l, pb.a - pa.a, pb.b - pa.b, pb.t - pa.t };
        const float vv = v[0] * v[0] + v[1] * v[1] + v[2] * v[2] + v[3] * v[3];
        const float score = (vv > 0.0f) ? ((pc.l - pa.l) * v[0] + (pc.a - pa.a) * v[1] + (pc.b - pa.b) * v[2] + (pc.t - pa.t) * v[3]) / vv : 0.0f;
        s.fresh.push_back(AaSeg::Pt { s.probeU, s.probeV, std::uint8_t(score >= 0.5f ? 1 : 0) });
        s.probes++;
        Cooperate();
    }
    radiosity.AfterTile();
    GetViewDataPtr()->AfterTile();
}

void TraceTask::ProgressiveM5()
{
    if((aaPass == 2) && M5Opts().chain)
    {
        ProbeSegmentsM5();
        return;
    }
    const bool traces = (aaPass == 2) || (aaPass == 3);
    POVRect rect;
    vector<RGBTColour> pixels;
    unsigned int serial;

    while(GetViewData()->GetNextRectangle(rect, serial) == true)
    {
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
                switch(aaPass)
                {
                    case 1:
                        if(!M5Opts().chain)
                            FitPixelM5(x, y, fit);
                        break;
                    case 2:
                        if(fit.planned)
                            ProbePixelM5(x, y, fit);
                        break;
                    case 3:
                        if(fit.planned)
                        {
                            // Halton 2,3 from this pixel's own count, shifted by a fixed amount per pixel so neighbours differ
                            auto halton = [](unsigned index, unsigned base)
                            {
                                float r = 0.0f, f = 1.0f / float(base);
                                for(unsigned i = index; i > 0; i /= base, f /= float(base))
                                    r += f * float(i % base);
                                return r;
                            };
                            const unsigned k = unsigned(fit.probes) + 1;
                            const unsigned h = (x * 73856093u) ^ (y * 19349663u);
                            const float jx = float(h & 1023u) / 1024.0f, jy = float((h >> 10) & 1023u) / 1024.0f;
                            float ox = halton(k, 2) + jx, oy = halton(k, 3) + jy;
                            ox -= std::floor(ox);
                            oy -= std::floor(oy);
                            RGBTColour extra;
                            TraceSample(DBL(x) + ox, DBL(y) + oy, x, y, extra);
                            GetViewData()->AaExtra(x, y) += extra;
                            fit.probes++;
                        }
                        break;
                    default:
                    {
                        RGBTColour col = GetViewData()->LatticeSample(x, y);
                        float mapProbes = float(fit.probes);
                        if(fit.kind == 1)
                        {
                            float nx = fit.nx, ny = fit.ny, s;
                            int probes = fit.probes;
                            if(fit.seg >= 0)
                            {
                                // the segment's line now, after any probes, seen from this pixel's centre
                                const AaSeg& sg = GetViewData()->aaSegs[size_t(fit.seg)];
                                const float inv = 1.0f / std::sqrt(1.0f + sg.b * sg.b);
                                nx = (sg.nx - sg.b * sg.tx) * inv;
                                ny = (sg.ny - sg.b * sg.ty) * inv;
                                s = sg.a * inv - (nx * (float(x) + 0.5f - sg.cx) + ny * (float(y) + 0.5f - sg.cy));
                                probes = sg.probes;
                                mapProbes = float(sg.probes);
                            }
                            else
                                s = 0.5f * (fit.lo + fit.hi);
                            const double f = M5Coverage(nx, ny, s);
                            // a pixel the final line does not cross stays as it was sampled
                            if((fit.seg < 0) || ((f > 0.0) && (f < 1.0)))
                            {
                                col = fit.a * (1.0 - f);
                                col += fit.b * f;
                            }
                            if(probes > 0)
                                GetViewDataPtr()->Stats()[Number_Of_Pixels_Supersampled]++;
                        }
                        else if((fit.kind == 2) && (fit.probes > 0))
                        {
                            col += GetViewData()->AaExtra(x, y);
                            col = col * (1.0 / double(fit.probes + 1));
                            GetViewDataPtr()->Stats()[Number_Of_Pixels_Supersampled]++;
                        }
                        if(M5Opts().map)
                        {
                            // red: probes on a fitted edge, green: fitted, blue: noisy pixel and the samples it got
                            const float p = mapProbes;
                            col = RGBTColour((fit.kind == 1) ? std::min(1.0f, p / 8.0f) : 0.0f, (fit.kind == 1) ? 0.25f : 0.0f,
                                             (fit.kind == 2) ? 0.15f + std::min(0.85f, p / 4.0f) : 0.0f, 0.0f);
                        }
                        pixels.push_back(col);
                    }
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
            GetViewData()->CompletedRectangle(rect, serial, pixels, 1, true, true, 1.0f, nullptr, progressLevel);
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
