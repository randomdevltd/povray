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
#include <limits>

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
                     int level, unsigned int ls, bool lf, bool pairs, DBL aab) :
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

TraceTask::OkLab TraceTask::ToOkLab(const RGBTColour& col)
{
    const RGBColour c = GammaCurve::Decode(GetViewData()->GetSceneData()->workingGamma, col.rgb());
    const float l = std::cbrt(0.4122214708f * c.red() + 0.5363325363f * c.green() + 0.0514459929f * c.blue());
    const float m = std::cbrt(0.2119034982f * c.red() + 0.6806995451f * c.green() + 0.1073969566f * c.blue());
    const float s = std::cbrt(0.0883024619f * c.red() + 0.2817188376f * c.green() + 0.6299787005f * c.blue());
    return OkLab { 0.2104542553f * l + 0.7936177850f * m - 0.0040720468f * s,
                   1.9779984951f * l - 2.4285922050f * m + 0.4505937099f * s,
                   0.0259040371f * l + 0.7827717662f * m - 0.8086757660f * s,
                   col.transm() };
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
        if((aaBudget > 0.0) && !GetViewData()->ReserveAASample())
            return;
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
