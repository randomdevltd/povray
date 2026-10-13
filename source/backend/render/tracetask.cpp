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
        if (GetSceneData()->photonSettings.method == 2 && GetSceneData()->photonSettings.photonsEnabled)
            ProgressivePhotons();
        else if(progressLevel >= 0)
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

void TraceTask::ProgressivePhotons()
{
    POVRect rect;
    unsigned int serial;
    const unsigned int pass = GetViewData()->photonPass;
    std::vector<RGBTColour> pixels;
    while (GetViewData()->GetNextRectangle(rect, serial))
    {
        BlockTimer timer;
        pixels.clear();
        pixels.reserve(rect.GetArea());
        for (unsigned int y = rect.top; y <= rect.bottom; ++y)
            for (unsigned int x = rect.left; x <= rect.right; ++x)
            {
                const auto key = DeriveKey(GetViewDataPtr()->stochasticRandomSeedBase,
                                           kDrawPhoton, std::uint64_t(y) * GetViewData()->GetWidth() + x);
                const double sx = Draw(key, kDrawPhoton, 0) + (pass + 0.5) * 0.7548776662466927;
                const double sy = Draw(key, kDrawPhoton, 1) + (pass + 0.5) * 0.5698402909980532;
                RGBTColour sample;
                auto* data = GetViewDataPtr();
                data->progressivePixel = &GetViewData()->photonPixels[size_t(y) * GetViewData()->GetWidth() + x];
                data->progressivePass = pass;
                data->progressiveValue = data->progressiveNoise = data->progressiveLaplacian = data->progressiveDensity = 0.0;
                trace(x + sx - std::floor(sx), y + sy - std::floor(sy),
                      GetViewData()->GetWidth(), GetViewData()->GetHeight(), sample);
                data->progressivePixel->Update(pass, data->progressiveValue, data->progressiveNoise,
                                               data->progressiveLaplacian, data->progressiveDensity);
                data->progressivePixel = nullptr;
                auto& sum = GetViewData()->LatticeSample(x, y);
                sum += sample;
                pixels.push_back(sum / double(pass + 1));
                GetViewDataPtr()->Stats()[Number_Of_Pixels]++;
                Cooperate();
            }
        GetViewDataPtr()->AfterTile();
        GetViewData()->CompletedRectangle(rect, serial, pixels, 1, true, true, 1.0f, nullptr, pass, timer.Elapsed());
        Cooperate();
    }
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
        BlockTimer blockTimer;
        radiosity.BeforeTile(highReproducibility? serial : 0, RadiosityFunction::FINAL_TRACE);

        positions.clear();
        colors.clear();

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
                        GetViewData()->AaHit(x, y) = (trace.primaryObject ? ((std::uint32_t(reinterpret_cast<std::uintptr_t>(trace.primaryObject) >> 4) & 0x7fffffffu) | 1u) : 0u) |
                                                     (trace.Grainy() ? 0x80000000u : 0u);
                        if(GetViewData()->HasAaPigments())
                        {
                            float* pig = GetViewData()->AaPigmentAt(x, y);
                            pig[0] = float(trace.primaryPigment.colour().Red());
                            pig[1] = float(trace.primaryPigment.colour().Green());
                            pig[2] = float(trace.primaryPigment.colour().Blue());
                        }
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
                                              blockTimer.Elapsed());

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

namespace { bool M4KeepGrain(); }

void TraceTask::TraceSample(DBL x, DBL y, RGBTColour& col, DBL footprint, int category)
{
    ViewData* vd = GetViewData();
    if(category >= 0)
        vd->aaSpent[category]++;
    if(vd->aaOracleReplay)
    {
        const POVRect& area = vd->GetRenderArea();
        const int n = int(vd->aaOracleN), w = int(area.GetWidth()) * n, h = int(area.GetHeight()) * n;
        const int i = std::min(w - 1, std::max(0, int(std::floor((x - DBL(area.left)) * n))));
        const int j = std::min(h - 1, std::max(0, int(std::floor((y - DBL(area.top)) * n))));
        const float* s = &vd->aaOracle[4 * (size_t(j) * w + i)];
        col = RGBTColour(s[0], s[1], s[2], s[3]);
        GetViewDataPtr()->Stats()[Number_Of_Samples]++;
        return;
    }
    trace.footprintFraction = footprint;
    if(M4KeepGrain())
        trace(x, y, GetViewData()->GetWidth(), GetViewData()->GetHeight(), col, Vector2d(std::floor(x) + 0.5, std::floor(y) + 0.5));
    else
        trace(x, y, GetViewData()->GetWidth(), GetViewData()->GetHeight(), col);
    trace.footprintFraction = 1.0;
    GetViewDataPtr()->Stats()[Number_Of_Samples]++;
    Cooperate();
}

namespace
{
/// Method 4's experiment switches, read once from POV_AA4, e.g. `W=7,FREE=0,NOISE=1,EMAX=2,GAP=0.0625,ROUNDS=6`.
struct M4Options final
{
    int window = 5;
    bool freeOnly = false;
    bool noise = true;
    float emax = 2.0f;
    float gapMin = 0.001f;
    int rounds = TraceTask::kM4Rounds;
    bool chain = true;              ///< Fit whole contours instead of a window round each pixel.
    int lmin = 4;                   ///< Shortest run of crossings a chain segment may be fitted to.
    float rho = 0.35f;              ///< Share of contended links in a 5x5 window above which it is noise.
    float ridgeBridge = 2.5f;       ///< Thin lines: how far past its last dash a fitted line still gives coverage, to span gaps.
    float ceps = 0.01f;             ///< Chain mode: differences below this are rounding, whatever their neighbours.
    float kappa = 2.0f;             ///< Chain mode: a link is a contour crossing when it jumps this many times its neighbours' ramp.
    bool map = false;               ///< Write where the samples went instead of the image.
    bool join = false;              ///< Chain mode: join collinear segments through junctions (worse than not, so far).
    float reach = 4.0f;             ///< Join: how far apart two segment ends may be, in pixels.
    float turn = 0.94f;             ///< Join: how nearly opposite their outward directions must be (cosine).
    float lateral = 1.0f;           ///< Join: how far one end may sit off the other's line, in pixels.
    float minLen = 8.0f;            ///< Chain mode: fits on segments shorter than this go to the noise tier instead.
    int dense = 4;                  ///< Chain mode: fits with this many different segments in a 5x5 window go to the noise tier.
    float noiseFrac = 0.0f;         ///< Share of the budget kept for the noise tier; negative for the share of screen it covers.
    bool stats = false;             ///< Print how the pixels were classified.
    int halo = 3;                   ///< Chain mode: radius within which dense contours make flat pixels worth a sample; 0 for none.
    float haloRho = 0.12f;          ///< Share of contour links in that window above which it counts as dense.
    float haloHi = 0.30f;           ///< Above this share a flat pixel joins the noise tier; between the two it is explored with one probe.
    float rough = 0.06f;            ///< Chain mode: links differing by at least this, whatever their neighbours, count towards roughness; 0 for none.
    float roughRho = 0.3f;          ///< Share of rough links in the halo window above which flat pixels there join the noise tier.
    bool cv = true;                 ///< Resolve: keep a pixel's own samples and add the fit's coverage correction, rather than replace them.
    bool quad = true;               ///< Chain mode: fit runs of a contour with parabolas, so curves need not be cut into chords.
    bool strips = false;            ///< Resolve: two nearly parallel lines crossing a pixel bound a strip, as a thin feature does.
    bool bisect = true;             ///< Pixels no fit holds (noisy, contradicted) are bisected where their samples differ, not averaged.
    bool keepGrain = true;          ///< Sub-samples take their pixel's centre random draws, so grain alone never looks like detail.
    int ridgeReach = 6;             ///< Thin lines: how far apart, in pixels, two dashes of one line may be.
    bool ridge = true;              ///< Chain mode: fit lines a pixel or two wide, which no contour of centre samples describes.
    bool explore = false;           ///< Bisection: flat pixels in a moderately dense halo get one probe first, not bisection; a find spreads.
    float bend = 2.0f;              ///< Quad: margin a parabola must gain over a line, per pixel of its sag, to be preferred.
    float spikeK = 1.5f;            ///< Spikes: how many times the AA threshold a pixel must stand out of its neighbours' median.
    float spikeCap = 0.25f;         ///< Spikes: most of the budget they may take; 0 for none.
    int spikeCentre = 2;            ///< Spikes: the centre sample is 0 dropped for the leaves, 1 kept, 2 kept when every leaf contradicts it.
    float ridgeSides = 4.0f;        ///< Thin lines: squared distance, in AA thresholds, between the two sides above which a dash sits on an edge.
    float ridgeContrast = 16.0f;    ///< Thin lines: least squared distance, in AA thresholds, between a line's colour and its background.
    int ridgeDbg[4] = { 0, 0, -1, -1 }; ///< Thin lines: print the decisions on dashes inside this rectangle (x0:y0:x1:y1) to stderr.
};

const M4Options& M4Opts()
{
    static const M4Options options = []
    {
        M4Options o;
        if(const char* env = std::getenv("POV_AA4"))
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
                    else if(key == "MINLEN") o.minLen = float(v);
                    else if(key == "DENSE")  o.dense = int(v);
                    else if(key == "NOISEFRAC") o.noiseFrac = float(v);
                    else if(key == "STATS")  o.stats = (v != 0.0);
                    else if(key == "HALO")   o.halo = int(v);
                    else if(key == "HALOHI") o.haloHi = float(v);
                    else if(key == "HALORHO") o.haloRho = float(v);
                    else if(key == "ROUGH")  o.rough = float(v);
                    else if(key == "ROUGHRHO") o.roughRho = float(v);
                    else if(key == "CV")     o.cv = (v != 0.0);
                    else if(key == "QUAD")   o.quad = (v != 0.0);
                    else if(key == "STRIPS") o.strips = (v != 0.0);
                    else if(key == "BISECT") o.bisect = (v != 0.0);
                    else if(key == "KG")     o.keepGrain = (v != 0.0);
                    else if(key == "EXPLORE") o.explore = (v != 0.0);
                    else if(key == "RIDGE")  o.ridge = (v != 0.0);
                    else if(key == "REACHR") o.ridgeReach = int(v);
                    else if(key == "BRIDGE") o.ridgeBridge = float(v);
                    else if(key == "BEND")   o.bend = float(v);
                    else if(key == "SPK")    o.spikeK = float(v);
                    else if(key == "SPIKECAP") o.spikeCap = float(v);
                    else if(key == "SPIKECTR") o.spikeCentre = int(v);
                    else if(key == "RSIDES") o.ridgeSides = float(v);
                    else if(key == "RCON")   o.ridgeContrast = float(v);
                    else if(key == "RIDGEDBG")
                        std::sscanf(item.c_str() + eq + 1, "%d:%d:%d:%d", &o.ridgeDbg[0], &o.ridgeDbg[1], &o.ridgeDbg[2], &o.ridgeDbg[3]);
                }
                pos = end + 1;
            }
        }
        o.window = std::min(7, std::max(3, o.window)) | 1;
        return o;
    }();
    return options;
}

bool M4KeepGrain() { return M4Opts().keepGrain; }

/// Stats: this process's resident and peak resident memory in MB, from /proc where there is one, else -1.
void M4Memory(long& rss, long& peak)
{
    rss = peak = -1;
    if(std::FILE* f = std::fopen("/proc/self/status", "r"))
    {
        char line[256];
        while(std::fgets(line, sizeof(line), f))
        {
            long kb = 0;
            if(std::sscanf(line, "VmRSS: %ld", &kb) == 1)
                rss = kb / 1024;
            else if(std::sscanf(line, "VmHWM: %ld", &kb) == 1)
                peak = kb / 1024;
        }
        std::fclose(f);
    }
}

/// Stats: the method's state in bytes per pixel of the render area, and the process's memory, under a heading.
void M4PrintMemory(const ViewData* vd, const char* when)
{
    double b[8];
    vd->AaStateBytes(b);
    const double px = std::max(1.0, double(vd->GetRenderArea().GetWidth()) * double(vd->GetRenderArea().GetHeight()));
    long rss, peak;
    M4Memory(rss, peak);
    std::fprintf(stderr, "AA4 memory %s: resident %ld MB, peak %ld MB; per pixel: lattice %.1f, OkLab %.1f, fits %.1f, extra %.1f, hits %.1f, pigments %.1f, "
                 "leaves %.1f, geometry %.1f B (%zu pixels)\n", when, rss, peak, b[0] / px, b[1] / px, b[2] / px, b[3] / px, b[4] / px, b[5] / px, b[6] / px,
                 b[7] / px, vd->AaGeomCount());
}

/// Stats: this thread's processor time in milliseconds where the system keeps one, else the process's.
double M4Clock()
{
#if defined(CLOCK_THREAD_CPUTIME_ID)
    timespec ts;
    if(clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts) == 0)
        return 1000.0 * double(ts.tv_sec) + 1.0e-6 * double(ts.tv_nsec);
#endif
    return 1000.0 * double(std::clock()) / double(CLOCKS_PER_SEC);
}

/// Stats: milliseconds since t, moving t on to now.
double M4Lap(double& t)
{
    const double now = M4Clock();
    const double ms = now - t;
    t = now;
    return ms;
}

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
}

namespace
{
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

/// Keep only the points that can bound a line: the upper hull when the points must lie below it, else the lower.
void M4Hull(std::vector<AaSeg::Pt>& p, bool upper)
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
bool M4EstimateQ(AaSeg& s, float bend);

bool M4Estimate(AaSeg& s)
{
    if(s.quad)
        return M4EstimateQ(s, M4Opts().bend);
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
            // two colours do not describe this edge: bisection takes its pixels
            s.fresh.clear();
            s.state = 3;
            continue;
        }
        s.fresh.clear();
        if(!M4Opts().quad)
        {
            M4Hull(s.hullA, true);
            M4Hull(s.hullB, false);
        }
        bool ok = M4Estimate(s);
        if(!ok && M4Opts().quad && !s.quad)
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

bool M4StationOk(const AaSeg& s, float u);

/// Choose the segments to probe this round, best first, before any ray is traced for them: a probe at the end where
/// the line is least certain is worth the contrast, times how uncertain it is, times how many pixels the line crosses.
void M4PlanSegments(ViewData* vd, const M4Options& o, bool enabledIn)
{
    const POVRect& area = vd->GetRenderArea();
    // what the edge probes may still spend, once the noise tier's share is set aside
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
            if(s.state || (wavg <= o.gapMin))
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
                    found = M4StationOk(s, u);
                }
            }
        }
        if(!found)
            continue;
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

/// Whether the colours either side of a segment stay the same for a few samples each way of u, so that a probe there
/// can be told apart by them; through a checkerboard's corners they swap, and no probe should land near one.
bool M4StationOk(const AaSeg& s, float u)
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

void M4SortBrackets(AaSeg& s)
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
void M4SegEnd(const AaSeg& s, int end, float& px, float& py, float& dx, float& dy)
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
bool M4TryJoin(const AaSeg& a, const AaSeg& b, AaSeg& out)
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
    M4Hull(out.hullA, true);
    M4Hull(out.hullB, false);

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
    M4SortBrackets(out);

    const float la = a.length, lb = b.length;
    out.contrast = (a.contrast * la + b.contrast * lb) / std::max(1.0e-3f, la + lb);
    out.length = out.uHi - out.uLo;
    out.probes = std::uint8_t(std::min(255, int(a.probes) + int(b.probes)));
    return M4Estimate(out);
}

/// Lines run on through a junction: where two segments end close together heading the same way, and one line
/// fits the samples of both, they are one segment, so the whole edge is pinned down by every sample along it.
void M4JoinSegments(ViewData* vd, float reach, float turn, float lateral)
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
                M4SegEnd(segs[i], e, px, py, dx, dy);
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
                M4SegEnd(segs[i], e1, px, py, dx, dy);
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
                            M4SegEnd(segs[size_t(j)], cand.second, qx, qy, ex, ey);
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
                if(M4TryJoin(segs[i], segs[size_t(bestJ)], joined))
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

/// Method 4 without windows: find every contour in the centre samples, cut it into the longest straight runs the
/// samples allow, and give each pixel the line of the run it lies most inside of.
void M4ChainFit(ViewData* vd, const M4Options& o)
{
    vd->aaSegs.clear();
    const POVRect& area = vd->GetRenderArea();
    const int x0 = int(area.left), y0 = int(area.top), w = int(area.GetWidth()), h = int(area.GetHeight());
    const float eps2 = o.ceps * o.ceps, k2 = o.kappa * o.kappa;
    auto at = [&](int x, int y) { return size_t(x - x0) + size_t(y - y0) * size_t(w); };
    auto lab = [&](int x, int y) { return vd->AaLabAt(unsigned(x), unsigned(y)); };

    // squared distance to the east and south neighbours; with textures filtered, a link on one surface whose pigment explains
    // the change is dropped (the filter has averaged it), while a change of lighting (a shadow edge, a highlight) stays
    const bool filtered = (vd->textureFilterScale > 0.0) && vd->HasAaHits() && vd->HasAaPigments();
    auto luma = [](float r, float g, float b) { return std::max(1.0e-4f, 0.2126f * r + 0.7152f * g + 0.0722f * b); };
    auto resolved = [&](int xa, int ya, int xb, int yb)
    {
        const std::uint32_t ha = vd->AaHit(unsigned(xa), unsigned(ya)) & 0x7fffffffu, hb = vd->AaHit(unsigned(xb), unsigned(yb)) & 0x7fffffffu;
        if(!filtered || (ha != hb) || (ha == 0))
            return false;
        const float* p = vd->AaPigmentAt(unsigned(xa), unsigned(ya));
        const float* q = vd->AaPigmentAt(unsigned(xb), unsigned(yb));
        if(std::max(std::fabs(p[0] - q[0]), std::max(std::fabs(p[1] - q[1]), std::fabs(p[2] - q[2]))) < 1.0e-4f)
            return false;
        const RGBTColour& ca = vd->LatticeSample(unsigned(xa), unsigned(ya));
        const RGBTColour& cb = vd->LatticeSample(unsigned(xb), unsigned(yb));
        const float lit = std::log(luma(ca.red(), ca.green(), ca.blue()) / luma(cb.red(), cb.green(), cb.blue()));
        const float pig = std::log(luma(p[0], p[1], p[2]) / luma(q[0], q[1], q[2]));
        return std::fabs(lit - pig) < 0.1f;
    };
    std::vector<float> dE(size_t(w) * size_t(h), 0.0f), dS(size_t(w) * size_t(h), 0.0f);
    for(int y = y0; y < y0 + h; y++)
    {
        for(int x = x0; x < x0 + w; x++)
        {
            if((x + 1 < x0 + w) && !resolved(x, y, x + 1, y))
                dE[at(x, y)] = Dist2(lab(x, y), lab(x + 1, y));
            if((y + 1 < y0 + h) && !resolved(x, y, x, y + 1))
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
    // roughness: links that differ a lot in themselves, which a field of noise has everywhere and no link stands out
    std::vector<std::int32_t> rsat(size_t(w + 1) * size_t(h + 1), 0);
    if(o.rough > 0.0f)
    {
        const float r2 = o.rough * o.rough;
        for(int y = 0; y < h; y++)
        {
            for(int x = 0; x < w; x++)
            {
                const size_t k = size_t(x) + size_t(y) * size_t(w);
                rsat[size_t(x + 1) + size_t(y + 1) * size_t(w + 1)] = int(dE[k] >= r2) + int(dS[k] >= r2) +
                    rsat[size_t(x) + size_t(y + 1) * size_t(w + 1)] + rsat[size_t(x + 1) + size_t(y) * size_t(w + 1)] - rsat[size_t(x) + size_t(y) * size_t(w + 1)];
            }
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
                vd->SetContrast(f, std::sqrt(far2));
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
    std::vector<std::pair<size_t, int>> overshoot;
    std::vector<size_t> chain;
    std::vector<M4Bracket> br;
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
            M4Bracket& b = br[size_t(i)];
            const float side = (float(b.lx) + 0.5f - b.cx) * ty - (float(b.ly) + 0.5f - b.cy) * tx;
            if(side < 0.0f)
            {
                std::swap(b.lx, b.rx);
                std::swap(b.ly, b.ry);
            }
        }
        float nx, ny, s;
        auto feasible = [&](int a, int b) { return o.quad ? M4FeasibleQ(br, a, b, nx, ny, s) : (M4BestLine(br, a, b, nx, ny, s) >= 0.0f); };

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
                // a line wherever one fits the run; a parabola only for runs no line can
                const bool curved = o.quad && (M4BestLine(br, i0, good, nx, ny, s) < 0.0f);
                if(curved)
                    M4FeasibleQ(br, i0, good, nx, ny, s);
                else if(!o.quad)
                    M4BestLine(br, i0, good, nx, ny, s);
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
                    }
                    sg.quad = curved;
                    if(!o.quad)
                    {
                        M4Hull(sg.hullA, true);
                        M4Hull(sg.hullB, false);
                    }
                    const M4Bracket& bm = br[size_t((i0 + good) / 2)];
                    sg.contrast = std::sqrt(Dist2(lab(bm.lx, bm.ly), lab(bm.rx, bm.ry)));
                    for(int i = i0; i <= good; i++)
                    {
                        const M4Bracket& bk = br[size_t(i)];
                        sg.bu.push_back(tx * bk.cx + ty * bk.cy - uc);
                        sg.bA.push_back(vd->LatticeSample(unsigned(bk.rx), unsigned(bk.ry)));
                        sg.bB.push_back(vd->LatticeSample(unsigned(bk.lx), unsigned(bk.ly)));
                    }
                    M4SortBrackets(sg);
                    if(!M4Estimate(sg))
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

    std::vector<std::int32_t>().swap(nb);
    std::vector<std::uint8_t>().swap(visited);

    // contours too short or too ragged to fit, with no good run through them, are noise
    if(o.join)
        M4JoinSegments(vd, o.reach, o.turn, o.lateral);
    std::vector<float> weight(size_t(w) * size_t(h), 0.0f), weight2(size_t(w) * size_t(h), 0.0f);

    // each pixel takes the line of the segment it lies most inside of, after any joining
    for(size_t id = 0; id < vd->aaSegs.size(); id++)
    {
        const AaSeg& sg = vd->aaSegs[id];
        if(sg.state == 2)
            continue;
        float nx, ny, s;
        M4LineAt(sg, sg.cx, sg.cy, nx, ny, s);
        // probes can move the line anywhere the bounds allow, so own every pixel it might come to cross
        const float slack = 0.5f * std::max(sg.w[0], std::max(sg.w[1], sg.w[2])) + 1.0e-3f;
        const float uMid = 0.5f * (sg.uLo + sg.uHi), half = 0.5f * (sg.uHi - sg.uLo);

        auto visit = [&](int px, int py)
        {
            if((px < x0) || (px >= x0 + w) || (py < y0) || (py >= y0 + h) || noisy[at(px, py)])
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

    // a line fitted on a short run, or among many others, is a guess; those pixels are better averaged than blended
    if((o.minLen > 0.0f) || (o.dense > 0))
    {
        std::vector<std::uint8_t> demote(size_t(w) * size_t(h), 0);
        for(int y = y0; y < y0 + h; y++)
        {
            for(int x = x0; x < x0 + w; x++)
            {
                const AaFit& f = vd->Fit(unsigned(x), unsigned(y));
                if((f.kind != 1) || (f.seg < 0))
                    continue;
                bool bad = vd->aaSegs[size_t(f.seg)].length < o.minLen;
                if(!bad && (o.dense > 0))
                {
                    int ids[25], n = 0;
                    for(int dy = -2; dy <= 2; dy++)
                    {
                        for(int dx = -2; dx <= 2; dx++)
                        {
                            const int nx2 = x + dx, ny2 = y + dy;
                            if((nx2 < x0) || (nx2 >= x0 + w) || (ny2 < y0) || (ny2 >= y0 + h))
                                continue;
                            const AaFit& g = vd->Fit(unsigned(nx2), unsigned(ny2));
                            if((g.kind != 1) || (g.seg < 0))
                                continue;
                            bool seen = false;
                            for(int k = 0; k < n; k++)
                                seen = seen || (ids[k] == g.seg);
                            if(!seen)
                                ids[n++] = g.seg;
                        }
                    }
                    bad = (n >= o.dense);
                }
                demote[at(x, y)] = bad ? 1 : 0;
            }
        }
        for(int y = y0; y < y0 + h; y++)
        {
            for(int x = x0; x < x0 + w; x++)
            {
                if(!demote[at(x, y)])
                    continue;
                AaFit& f = vd->Fit(unsigned(x), unsigned(y));
                float far2 = std::max(dE[at(x, y)], dS[at(x, y)]);
                if(x > x0) far2 = std::max(far2, dE[at(x - 1, y)]);
                if(y > y0) far2 = std::max(far2, dS[at(x, y - 1)]);
                f.kind = 2;
                f.seg = -1;
                AaGeom& geom = vd->GeomFor(f);
                geom.seg2 = -1;
                geom.contrast = std::sqrt(far2);
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
                vd->SetContrast(f, std::sqrt(far2));
            }
        }
    }

    // flat pixels among dense contours may hide detail the samples alias away, such as columns gone solid in the distance
    if(o.halo > 0)
    {
        const int r = o.halo;
        for(int y = y0; y < y0 + h; y++)
        {
            for(int x = x0; x < x0 + w; x++)
            {
                AaFit& f = vd->Fit(unsigned(x), unsigned(y));
                if(f.kind != 0)
                    continue;
                const int lx = x - x0, ly = y - y0;
                const int xa = std::max(0, lx - r), xb = std::min(w, lx + r + 1), ya = std::max(0, ly - r), yb = std::min(h, ly + r + 1);
                const int sum = sat[size_t(xb) + size_t(yb) * size_t(w + 1)] - sat[size_t(xa) + size_t(yb) * size_t(w + 1)] -
                                sat[size_t(xb) + size_t(ya) * size_t(w + 1)] + sat[size_t(xa) + size_t(ya) * size_t(w + 1)];
                const float density = float(sum) / (2.0f * float((xb - xa) * (yb - ya)));
                float rdensity = 0.0f;
                if(o.rough > 0.0f)
                {
                    const int rsum = rsat[size_t(xb) + size_t(yb) * size_t(w + 1)] - rsat[size_t(xa) + size_t(yb) * size_t(w + 1)] -
                                     rsat[size_t(xb) + size_t(ya) * size_t(w + 1)] + rsat[size_t(xa) + size_t(ya) * size_t(w + 1)];
                    rdensity = float(rsum) / (2.0f * float((xb - xa) * (yb - ya)));
                }
                if((density > o.haloRho) || ((o.rough > 0.0f) && (rdensity > o.roughRho)))
                {
                    vd->SetContrast(f, 0.05f * std::max(density, rdensity));
                    if(o.bisect && o.explore && (density <= o.haloHi) && (rdensity <= o.roughRho + 0.2f))
                        f.explore = 1;
                    else
                        f.kind = 2;
                }
            }
        }
    }
}
}

namespace
{
/// Thin lines: centre samples unlike both neighbours along an axis, where those two agree, are dashes of a line under two pixels
/// wide; close dashes of one colour are grouped and fitted, and the pixels a line crosses take its strip coverage. Returns the pixels taken.
std::int64_t M4RidgeFit(ViewData* vd, int reachPx, float bridge)
{
    int why[10] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };   // lines: 0 taken, 1 too few, 2 poor line, 3 no background, 4 background varies, 5 strangers, 6 crowded, 7 sides differ, 8 too wide, 9 faint
    const POVRect& area = vd->GetRenderArea();
    const int x0 = int(area.left), y0 = int(area.top), w = int(area.GetWidth()), h = int(area.GetHeight());
    const float thr = float(vd->aaThr), thr2 = thr * thr;
    double clock = M4Clock();
    double ms[4] = { 0, 0, 0, 0 };
    auto at = [&](int x, int y) { return size_t(x - x0) + size_t(y - y0) * size_t(w); };
    auto lab = [&](int x, int y) { return vd->AaLabAt(unsigned(x), unsigned(y)); };
    auto inside = [&](int x, int y) { return (x >= x0) && (x < x0 + w) && (y >= y0) && (y < y0 + h); };

    struct Dash { int x, y; };
    std::vector<Dash> dashes;
    std::vector<std::int32_t> dashAt(size_t(w) * size_t(h), -1);
    static const int axes[4][2] = { { 1, 0 }, { 0, 1 }, { 1, 1 }, { 1, -1 } };
    for(int y = y0; y < y0 + h; y++)
    {
        for(int x = x0; x < x0 + w; x++)
        {
            if(vd->Fit(unsigned(x), unsigned(y)).seg >= 0)
                continue;
            for(const auto& a : axes)
            {
                if(!inside(x - a[0], y - a[1]) || !inside(x + a[0], y + a[1]))
                    continue;
                const float dA = Dist2(lab(x, y), lab(x - a[0], y - a[1])), dB = Dist2(lab(x, y), lab(x + a[0], y + a[1]));
                const float dN = Dist2(lab(x - a[0], y - a[1]), lab(x + a[0], y + a[1]));
                const float dm = std::min(dA, dB);
                if((dm > 4.0f * thr2) && (dN < 0.25f * dm))
                {
                    dashAt[at(x, y)] = std::int32_t(dashes.size());
                    dashes.push_back(Dash { x, y });
                    break;
                }
            }
        }
    }
    if(dashes.empty())
        return 0;
    ms[0] = M4Lap(clock);

    // group dashes of one colour that lie within three pixels of each other
    std::vector<std::int32_t> parent(dashes.size());
    for(size_t i = 0; i < parent.size(); i++)
        parent[i] = std::int32_t(i);
    std::function<std::int32_t(std::int32_t)> find = [&](std::int32_t i) { while(parent[i] != i) { parent[i] = parent[parent[i]]; i = parent[i]; } return i; };
    for(size_t i = 0; i < dashes.size(); i++)
    {
        for(int dy = -reachPx; dy <= reachPx; dy++)
        {
            for(int dx = -reachPx; dx <= reachPx; dx++)
            {
                if(!inside(dashes[i].x + dx, dashes[i].y + dy))
                    continue;
                const std::int32_t j = dashAt[at(dashes[i].x + dx, dashes[i].y + dy)];
                if((j < 0) || (size_t(j) <= i))
                    continue;
                if(Dist2(lab(dashes[i].x, dashes[i].y), lab(dashes[j].x, dashes[j].y)) < 4.0f * thr2)
                    parent[find(std::int32_t(i))] = find(j);
            }
        }
    }
    // groups in order of their first dash, so which line owns a crossing never depends on a container's layout
    std::vector<std::vector<std::int32_t>> groups;
    {
        std::vector<std::int32_t> slot(dashes.size(), -1);
        for(size_t i = 0; i < dashes.size(); i++)
        {
            std::int32_t& g = slot[size_t(find(std::int32_t(i)))];
            if(g < 0)
            {
                g = std::int32_t(groups.size());
                groups.emplace_back();
            }
            groups[size_t(g)].push_back(std::int32_t(i));
        }
    }
    size_t largest = 0;
    for(const auto& g : groups)
        largest = std::max(largest, g.size());
    ms[1] = M4Lap(clock);

    std::int64_t taken = 0;
    std::vector<std::uint8_t> done(size_t(w) * size_t(h), 0);
    const int* dbg = M4Opts().ridgeDbg;
    auto watched = [&](const std::vector<std::int32_t>& pts)
    {
        if(dbg[2] < dbg[0])
            return false;
        for(std::int32_t i : pts)
            if((dashes[i].x >= dbg[0]) && (dashes[i].x <= dbg[2]) && (dashes[i].y >= dbg[1]) && (dashes[i].y <= dbg[3]))
                return true;
        return false;
    };
    auto report = [&](const char* what, const std::vector<std::int32_t>& pts, double m1, double m2)
    {
        if(!watched(pts))
            return;
        std::fprintf(stderr, "AA4 ridge %s n=%zu [%.2f %.2f]:", what, pts.size(), m1, m2);
        for(std::int32_t i : pts)
            std::fprintf(stderr, " %d,%d", dashes[i].x, dashes[i].y);
        std::fprintf(stderr, "\n");
    };
    // the principal axis of a set of dashes, and how far they spread along and across it
    struct Axis { double cx, cy, dx, dy, lo, hi, l1, l2; };
    auto axisOf = [&](const std::vector<std::int32_t>& pts)
    {
        Axis a = { 0, 0, 1, 0, 1.0e30, -1.0e30, 0, 0 };
        const double n = double(pts.size());
        for(std::int32_t i : pts) { a.cx += (dashes[i].x + 0.5) / n; a.cy += (dashes[i].y + 0.5) / n; }
        double sxx = 0, sxy = 0, syy = 0;
        for(std::int32_t i : pts)
        {
            const double ux = dashes[i].x + 0.5 - a.cx, uy = dashes[i].y + 0.5 - a.cy;
            sxx += ux * ux / n; sxy += ux * uy / n; syy += uy * uy / n;
        }
        const double tr = sxx + syy, det = sxx * syy - sxy * sxy, disc = std::sqrt(std::max(0.0, 0.25 * tr * tr - det));
        a.l1 = 0.5 * tr + disc; a.l2 = std::max(0.0, 0.5 * tr - disc);
        double dx = sxy, dy = a.l1 - sxx;
        if(std::fabs(dx) + std::fabs(dy) < 1.0e-12) { dx = 1.0; dy = 0.0; }
        a.dx = dx / std::sqrt(dx * dx + dy * dy); a.dy = dy / std::sqrt(dx * dx + dy * dy);
        for(std::int32_t i : pts)
        {
            const double u = (dashes[i].x + 0.5 - a.cx) * a.dx + (dashes[i].y + 0.5 - a.cy) * a.dy;
            a.lo = std::min(a.lo, u); a.hi = std::max(a.hi, u);
        }
        return a;
    };
    auto straight = [](const Axis& a) { return (std::sqrt(a.l2) <= 0.6) && (a.hi - a.lo >= 3.0) && (std::sqrt(a.l1) >= 4.0 * std::sqrt(a.l2)); };
    std::vector<std::vector<std::int32_t>> lines;
    std::vector<Axis> fits;
    std::vector<std::int32_t> lineOf(dashes.size(), -1);
    auto passesThrough = [&](size_t m, size_t l)
    {
        int in = 0;
        double vLo = 1.0e30, vHi = -1.0e30;
        for(std::int32_t i : lines[m])
        {
            const double qx = dashes[i].x + 0.5 - fits[l].cx, qy = dashes[i].y + 0.5 - fits[l].cy;
            const double u = qx * fits[l].dx + qy * fits[l].dy, v = qy * fits[l].dx - qx * fits[l].dy;
            in += ((u >= fits[l].lo - 2.0) && (u <= fits[l].hi + 2.0) && (std::fabs(v) <= 4.0)) ? 1 : 0;
            vLo = std::min(vLo, v); vHi = std::max(vHi, v);
        }
        return (2 * in < int(lines[m].size())) && (vHi - vLo > 4.0);
    };
    double m1 = 0.0, m2 = 0.0;
    auto acceptLine = [&](const std::vector<std::int32_t>& pts) -> int
    {
        const size_t n = pts.size();
        m1 = m2 = 0.0;
        if(n < 4)
            return 1;
        const Axis ax = axisOf(pts);
        const double cx = ax.cx, cy = ax.cy, dx = ax.dx, dy = ax.dy, lo = ax.lo, hi = ax.hi, length = hi - lo + 1.0;
        m1 = std::sqrt(ax.l2); m2 = length;
        if(!straight(ax))
            return 2;

        const float nx = float(-dy), ny = float(dx);
        RGBTColour cc;
        cc.Clear();
        float labC[4] = { 0, 0, 0, 0 }, labB[4] = { 0, 0, 0, 0 };
        int nb = 0;
        for(std::int32_t i : pts)
        {
            cc += vd->LatticeSample(unsigned(dashes[i].x), unsigned(dashes[i].y));
            for(int k = 0; k < 4; k++)
                labC[k] += lab(dashes[i].x, dashes[i].y)[k];
            const int ax = int(std::floor(dashes[i].x + 0.5 + 2.0 * nx)), ay = int(std::floor(dashes[i].y + 0.5 + 2.0 * ny));
            const int bx = int(std::floor(dashes[i].x + 0.5 - 2.0 * nx)), by = int(std::floor(dashes[i].y + 0.5 - 2.0 * ny));
            if(inside(ax, ay) && inside(bx, by))
            {
                for(int k = 0; k < 4; k++)
                    labB[k] += 0.5f * (lab(ax, ay)[k] + lab(bx, by)[k]);
                nb++;
            }
        }
        cc = cc * (1.0 / double(n));
        if(nb == 0)
            return 3;
        for(int k = 0; k < 4; k++)
        {
            labC[k] /= float(n);
            labB[k] /= float(nb);
        }
        // one background colour all along the line: the two sides of a step edge, or a checker's seam, give a spread of them
        float spreadB = 0.0f;
        for(std::int32_t i : pts)
        {
            const int ax = int(std::floor(dashes[i].x + 0.5 + 2.0 * nx)), ay = int(std::floor(dashes[i].y + 0.5 + 2.0 * ny));
            const int bx = int(std::floor(dashes[i].x + 0.5 - 2.0 * nx)), by = int(std::floor(dashes[i].y + 0.5 - 2.0 * ny));
            if(!inside(ax, ay) || !inside(bx, by))
                continue;
            float mid[4];
            for(int k = 0; k < 4; k++)
                mid[k] = 0.5f * (lab(ax, ay)[k] + lab(bx, by)[k]);
            spreadB += Dist2(mid, labB);
        }
        m1 = spreadB / float(nb) / thr2;
        if(spreadB / float(nb) > 4.0f * thr2)
            return 4;
        // a line has one background on both sides; a seam between two different regions is an edge
        {
            int sided = 0, split = 0;
            for(std::int32_t i : pts)
            {
                const float* side[2] = { nullptr, nullptr };
                for(int sgn = 0; sgn < 2; sgn++)
                {
                    for(int k = 2; (k <= 3) && !side[sgn]; k++)
                    {
                        const int sx = int(std::floor(dashes[i].x + 0.5 + (sgn ? -k : k) * nx)), sy = int(std::floor(dashes[i].y + 0.5 + (sgn ? -k : k) * ny));
                        if(inside(sx, sy) && (dashAt[at(sx, sy)] < 0))
                            side[sgn] = lab(sx, sy);
                    }
                }
                if(!side[0] || !side[1])
                    continue;
                sided++;
                split += (Dist2(side[0], side[1]) > M4Opts().ridgeSides * thr2) ? 1 : 0;
            }
            m1 = split; m2 = sided;
            if(2 * split > sided)
                return 7;
        }
        // dashes are found at twice the AA threshold, so texture grain lines up into faint ones; a real line stands well clear
        if(Dist2(labC, labB) < M4Opts().ridgeContrast * thr2)
            return 9;
        // every pixel in the strip's neighbourhood that shows the line's colour counts towards its width, not only the dashes
        int hits = 0, strangers = 0, region = 0;
        for(int py = int(std::floor(cy - std::fabs(dy) * std::max(std::fabs(lo), std::fabs(hi)) - 2.0)); py <= int(std::ceil(cy + std::fabs(dy) * std::max(std::fabs(lo), std::fabs(hi)) + 2.0)); py++)
        {
            for(int px = int(std::floor(cx - std::fabs(dx) * std::max(std::fabs(lo), std::fabs(hi)) - 2.0)); px <= int(std::ceil(cx + std::fabs(dx) * std::max(std::fabs(lo), std::fabs(hi)) + 2.0)); px++)
            {
                if(!inside(px, py))
                    continue;
                const double qx = px + 0.5 - cx, qy = py + 0.5 - cy;
                const double u = qx * dx + qy * dy, v = qx * nx + qy * ny;
                if((u < lo - 0.5) || (u > hi + 0.5) || (std::fabs(v) > 1.5))
                    continue;
                const float dC = Dist2(lab(px, py), labC), dB = Dist2(lab(px, py), labB);
                if(dC < dB)
                    hits++;
                if(std::min(dC, dB) > 4.0f * thr2)
                    strangers++;
                region++;
            }
        }
        m1 = strangers; m2 = region;
        if(float(strangers) > 0.15f * float(region))
            return 5;
        // a line stands alone; texture is crowded with other dashes within a few pixels of it
        {
            std::vector<std::int32_t> mine(pts);
            std::sort(mine.begin(), mine.end());
            int others = 0;
            std::unordered_map<std::int32_t, int> near;
            for(int py = int(std::floor(cy - std::fabs(dy) * std::max(std::fabs(lo), std::fabs(hi)) - 5.0)); py <= int(std::ceil(cy + std::fabs(dy) * std::max(std::fabs(lo), std::fabs(hi)) + 5.0)); py++)
            {
                for(int px = int(std::floor(cx - std::fabs(dx) * std::max(std::fabs(lo), std::fabs(hi)) - 5.0)); px <= int(std::ceil(cx + std::fabs(dx) * std::max(std::fabs(lo), std::fabs(hi)) + 5.0)); px++)
                {
                    if(!inside(px, py) || (dashAt[at(px, py)] < 0) || std::binary_search(mine.begin(), mine.end(), dashAt[at(px, py)]))
                        continue;
                    const double qx = px + 0.5 - cx, qy = py + 0.5 - cy;
                    const double u = qx * dx + qy * dy, v = qx * nx + qy * ny;
                    if((u >= lo - 2.0) && (u <= hi + 2.0) && (std::fabs(v) <= 4.0))
                    {
                        others++;
                        if(lineOf[dashAt[at(px, py)]] >= 0)
                            near[lineOf[dashAt[at(px, py)]]]++;
                    }
                }
            }
            // one other line that this one crosses, each lying mostly outside the other's neighbourhood, is a junction, not clutter
            int crossing = 0;
            for(const auto& c : near)
                if(passesThrough(size_t(c.first), size_t(lineOf[pts[0]])) && passesThrough(size_t(lineOf[pts[0]]), size_t(c.first)))
                    crossing = std::max(crossing, c.second);
            others -= crossing;
            m1 = others; m2 = crossing;
            if(float(others) > 0.5f * float(n))
                return 6;
        }
        // wider than a thin line is a band between two edges, which the edge fit draws
        if(double(hits) > 1.6 * length)
            return 8;
        const float width = float(std::max(0.2, double(hits) / length));

        // past its last dash the strip goes on only towards another dash ahead, or over pixels whose sample leans its way
        auto endReach = [&](double end, double dir)
        {
            bool ahead = false;
            for(int j = 1; j <= int(bridge) + 1; j++)
            {
                const int jx = int(std::floor(cx + (end + dir * j) * dx)), jy = int(std::floor(cy + (end + dir * j) * dy));
                ahead = ahead || (inside(jx, jy) && (dashAt[at(jx, jy)] >= 0) && (lineOf[dashAt[at(jx, jy)]] >= 0) &&
                                  (lineOf[dashAt[at(jx, jy)]] != lineOf[pts[0]]) && (Dist2(lab(jx, jy), labC) < 4.0f * thr2));
            }
            if(ahead)
                return double(bridge);
            double e = 0.5;
            for(int k = 1; k < bridge; k++)
            {
                const double px = cx + (end + dir * k) * dx, py = cy + (end + dir * k) * dy;
                const int ix = int(std::floor(px)), iy = int(std::floor(py));
                const int ax = int(std::floor(px + 2.0 * nx)), ay = int(std::floor(py + 2.0 * ny));
                const int bx = int(std::floor(px - 2.0 * nx)), by = int(std::floor(py - 2.0 * ny));
                if(!inside(ix, iy) || !inside(ax, ay) || !inside(bx, by))
                    break;
                float t = 0.0f, cc2 = 0.0f;
                for(int c = 0; c < 4; c++)
                {
                    const float bg = 0.5f * (lab(ax, ay)[c] + lab(bx, by)[c]);
                    t += (lab(ix, iy)[c] - bg) * (labC[c] - bg);
                    cc2 += (labC[c] - bg) * (labC[c] - bg);
                }
                if(t < 0.25f * cc2)
                    break;
                e = k + 0.5;
            }
            return std::min(e, double(bridge));
        };
        const double uLo = lo - endReach(lo, -1.0), uHi = hi + endReach(hi, 1.0);

        // every pixel the strip, extended a pixel at each end, can touch
        const int reach = int(std::ceil(width * 0.5 + 1.0));
        const double ex = std::fabs(dx) * (std::max(std::fabs(lo), std::fabs(hi)) + bridge) + reach + 1.0;
        const double ey = std::fabs(dy) * (std::max(std::fabs(lo), std::fabs(hi)) + bridge) + reach + 1.0;
        for(int py = int(std::floor(cy - ey)); py <= int(std::ceil(cy + ey)); py++)
        {
            for(int px = int(std::floor(cx - ex)); px <= int(std::ceil(cx + ex)); px++)
            {
                if(!inside(px, py) || done[at(px, py)])
                    continue;
                const double qx = px + 0.5 - cx, qy = py + 0.5 - cy;
                const double u = qx * dx + qy * dy, v = qx * nx + qy * ny;
                if((u < uLo) || (u > uHi) || (std::fabs(v) > 0.5 * width + 0.5 * (std::fabs(nx) + std::fabs(ny)) + 1.0e-3))
                    continue;
                AaFit& f = vd->Fit(unsigned(px), unsigned(py));
                if(f.seg >= 0)
                    continue;
                // the background either side of the line, two pixels off it, must agree
                const int ax = int(std::floor(px + 0.5 + 2.0 * nx)), ay = int(std::floor(py + 0.5 + 2.0 * ny));
                const int bx = int(std::floor(px + 0.5 - 2.0 * nx)), by = int(std::floor(py + 0.5 - 2.0 * ny));
                if(!inside(ax, ay) || !inside(bx, by) || (Dist2(lab(ax, ay), lab(bx, by)) > 9.0f * thr2))
                    continue;
                AaGeom& geom = vd->GeomFor(f);
                f.kind = 3;
                f.seg = -1;
                geom.seg2 = -1;
                f.explore = 0;
                geom.nx = nx; geom.ny = ny;
                geom.lo = float(-(qx * nx + qy * ny));
                geom.hi = width;
                geom.width = width;
                geom.a = (vd->LatticeSample(unsigned(ax), unsigned(ay)) + vd->LatticeSample(unsigned(bx), unsigned(by))) * 0.5;
                geom.b = cc;
                geom.contrast = std::sqrt(Dist2(lab(ax, ay), lab(px, py)));
                done[at(px, py)] = 1;
                taken++;
            }
        }
        return 0;
    };
    // a group that is not one line may hold several, crossing or one beyond another: take the line most dashes lie on,
    // set its dashes aside and go on with the rest
    std::function<void(std::vector<std::int32_t>&)> fitGroup = [&](std::vector<std::int32_t>& pts)
    {
        if(pts.size() < 4)
        {
            why[1]++;
            report("too few", pts, double(pts.size()), 0.0);
            return;
        }
        const Axis all = axisOf(pts);
        if(straight(all))
        {
            lines.push_back(pts);
            return;
        }
        // a line with a few stray dots beside it: the dashes on its axis are the line, the strays go on alone
        std::vector<std::int32_t> core, off;
        for(std::int32_t i : pts)
            (std::fabs((dashes[i].y + 0.5 - all.cy) * all.dx - (dashes[i].x + 0.5 - all.cx) * all.dy) <= 1.0 ? core : off).push_back(i);
        if((10 * off.size() <= pts.size()) && !off.empty() && straight(axisOf(core)))
        {
            lines.push_back(core);
            fitGroup(off);
            return;
        }
        // halve whatever its size, so the pair search below, cubic in what it is given, never sees more than 64 dashes
        if(pts.size() > 64)
        {
            double minX = 1e30, maxX = -1e30, minY = 1e30, maxY = -1e30;
            for(std::int32_t i : pts)
            {
                minX = std::min(minX, double(dashes[i].x)); maxX = std::max(maxX, double(dashes[i].x));
                minY = std::min(minY, double(dashes[i].y)); maxY = std::max(maxY, double(dashes[i].y));
            }
            const bool alongX = (maxX - minX) >= (maxY - minY);
            std::vector<std::int32_t> sorted(pts);
            std::sort(sorted.begin(), sorted.end(), [&](std::int32_t a, std::int32_t b) { return alongX ? (dashes[a].x < dashes[b].x) : (dashes[a].y < dashes[b].y); });
            std::vector<std::int32_t> a(sorted.begin(), sorted.begin() + sorted.size() / 2), b(sorted.begin() + sorted.size() / 2, sorted.end());
            fitGroup(a);
            fitGroup(b);
            return;
        }
        std::vector<std::int32_t> rest(pts), in;
        while(rest.size() >= 4)
        {
            std::vector<std::int32_t> best;
            double bestSpan = 0.0;
            for(size_t a = 0; a < rest.size(); a++)
            {
                for(size_t b = a + 1; b < rest.size(); b++)
                {
                    const double ex = dashes[rest[b]].x - dashes[rest[a]].x, ey = dashes[rest[b]].y - dashes[rest[a]].y;
                    const double el = std::sqrt(ex * ex + ey * ey);
                    if(el < 3.0)
                        continue;
                    in.clear();
                    double lo = 1e30, hi = -1e30;
                    for(std::int32_t i : rest)
                    {
                        const double qx = dashes[i].x - dashes[rest[a]].x, qy = dashes[i].y - dashes[rest[a]].y;
                        if(std::fabs(qx * ey - qy * ex) / el <= 0.7)
                        {
                            in.push_back(i);
                            const double u = (qx * ex + qy * ey) / el;
                            lo = std::min(lo, u); hi = std::max(hi, u);
                        }
                    }
                    if((in.size() > best.size()) || ((in.size() == best.size()) && (hi - lo > bestSpan)))
                    {
                        best.swap(in);
                        bestSpan = hi - lo;
                    }
                }
            }
            if(best.size() < 4)
            {
                why[1]++;
                report("too few", rest, double(best.size()), 0.0);
                break;
            }
            lines.push_back(best);
            std::vector<std::int32_t> left;
            std::sort(best.begin(), best.end());
            for(std::int32_t i : rest)
                if(!std::binary_search(best.begin(), best.end(), i))
                    left.push_back(i);
            rest.swap(left);
        }
    };
    for(auto& g : groups)
    {
        report("group", g, double(g.size()), 0.0);
        fitGroup(g);
    }
    ms[2] = M4Lap(clock);
    for(size_t k = 0; k < lines.size(); k++)
    {
        fits.push_back(axisOf(lines[k]));
        for(std::int32_t i : lines[k])
            lineOf[i] = std::int32_t(k);
    }
    static const char* const names[10] = { "taken", "too few", "poor line", "no background", "background varies", "strangers", "crowded", "sides differ", "too wide", "faint" };
    for(const auto& line : lines)
    {
        const int r = acceptLine(line);
        why[r]++;
        report(names[r], line, m1, m2);
    }
    ms[3] = M4Lap(clock);
    if(M4Opts().stats)
        std::fprintf(stderr, "AA4 time thin lines: dashes %.1f ms, groups %.1f ms (largest %zu dashes), line search %.1f ms, acceptance %.1f ms\n",
                     ms[0], ms[1], largest, ms[2], ms[3]);
    if(M4Opts().stats)
        std::fprintf(stderr, "AA4 thin-line groups: %zu dashes in %zu groups; lines taken %d, too few %d, poor line %d, no background %d, background varies %d, strangers %d, crowded %d, sides differ %d, too wide %d, faint %d\n",
                     dashes.size(), groups.size(), why[0], why[1], why[2], why[3], why[4], why[5], why[6], why[7], why[8], why[9]);
    return taken;
}
}

namespace
{
/// Spikes: pixels far from the per-channel median of their neighbours that no fit explains and no neighbour continues,
/// as a one-sample miss or a thin feature caught by one centre sample is. The worst are kept within `cap` samples, four each.
template<typename ToLab>
std::int64_t M4FlagSpikes(ViewData* vd, const M4Options& o, std::int64_t cap, std::int64_t& candidates, const ToLab& toLab)
{
    const POVRect& area = vd->GetRenderArea();
    const float thr = o.spikeK * float(vd->aaThr);
    std::int64_t hist[256] = {};
    candidates = 0;
    if(cap < 4)
        return 0;
    for(unsigned int y = area.top; y <= area.bottom; y++)
    {
        for(unsigned int x = area.left; x <= area.right; x++)
        {
            AaFit& f = vd->Fit(x, y);
            const float* nb[8];
            int at[8], n = 0;
            for(int k = 0; k < 9; k++)
            {
                const int dx = k % 3 - 1, dy = k / 3 - 1;
                if((k != 4) && (int(x) + dx >= int(area.left)) && (int(x) + dx <= int(area.right)) && (int(y) + dy >= int(area.top)) && (int(y) + dy <= int(area.bottom)))
                {
                    at[n] = k;
                    nb[n++] = vd->AaLabAt(unsigned(int(x) + dx), unsigned(int(y) + dy));
                }
            }
            if(n < 5)
                continue;
            float med[4], v[8];
            for(int c = 0; c < 4; c++)
            {
                for(int i = 0; i < n; i++)
                    v[i] = nb[i][c];
                std::sort(v, v + n);
                med[c] = 0.5f * (v[(n - 1) / 2] + v[n / 2]);
            }
            const float* l = vd->AaLabAt(x, y);
            const float spike2 = Dist2(l, med);
            if(spike2 <= thr * thr)
                continue;
            // neighbours much like it on opposite sides continue a line through it, and four or more make it a region; the
            // neighbours' typical spread is texture, and grain kept is not resampled away, so a grainy one must outdo them all twice over
            int alike = 0;
            bool like[9] = {};
            float grain = 0.0f;
            for(int i = 0; i < n; i++)
            {
                v[i] = std::sqrt(Dist2(nb[i], med));
                like[at[i]] = (Dist2(nb[i], l) < 0.25f * spike2);
                alike += like[at[i]] ? 1 : 0;
                grain = like[at[i]] ? grain : std::max(grain, v[i]);
            }
            if((alike > 3) || (like[0] && like[8]) || (like[1] && like[7]) || (like[2] && like[6]) || (like[3] && like[5]))
                continue;
            std::sort(v, v + n);
            const bool grainy = vd->HasAaHits() && ((vd->AaHit(x, y) & 0x80000000u) != 0) && o.keepGrain;
            const float excess = std::sqrt(spike2) - thr - (grainy ? 2.0f * grain : v[n / 2]);
            if(excess <= 0.0f)
                continue;
            if(f.kind == 1)
            {
                const AaGeom& geom = vd->Geom(f);
                // an edge crossing the pixel whose side colour it shows accounts for it
                float nx = geom.nx, ny = geom.ny, s = 0.5f * (geom.lo + geom.hi);
                if(f.seg >= 0)
                {
                    M4LineAt(vd->aaSegs[size_t(f.seg)], float(x) + 0.5f, float(y) + 0.5f, nx, ny, s);
                    s -= nx * (float(x) + 0.5f) + ny * (float(y) + 0.5f);
                }
                const float cov = M4Coverage(nx, ny, s);
                float la[4], lb[4];
                toLab(geom.a, la);
                toLab(geom.b, lb);
                const float da = Dist2(l, la), db = Dist2(l, lb);
                if(((cov <= 0.0f) ? da : ((cov >= 1.0f) ? db : std::min(da, db))) < thr * thr)
                    continue;
            }
            f.spike = std::uint8_t(std::max(1, M4Bucket(excess)));
            hist[f.spike]++;
            candidates++;
        }
    }
    int threshold = 256;
    std::int64_t kept = 0;
    for(int b = 255; (b >= 1) && (4 * (kept + hist[b]) <= cap); b--)
    {
        kept += hist[b];
        threshold = b;
    }
    for(unsigned int y = area.top; y <= area.bottom; y++)
        for(unsigned int x = area.left; x <= area.right; x++)
            if(vd->Fit(x, y).spike < threshold)
                vd->Fit(x, y).spike = 0;
    return kept;
}

/// Every leaf of a bisected pixel differs from its centre sample, so whatever the centre hit lies between them.
bool M4LeavesContradict(const AaCell* cells, size_t n, const float* centre, float thr2)
{
    for(size_t i = 0; i < n; i++)
        if(Dist2(cells[i].lab, centre) <= thr2)
            return false;
    return true;
}

/// Spikes found at pass 0 are sampled only when the noise pass traces at all.
bool M4SpikesTraced(ViewData* vd, const M4Options& o)
{
    return o.chain && o.bisect && o.noise && !o.freeOnly && (vd->aaOracleReplay || (std::getenv("POV_AA4_REPLAY") == nullptr));
}
}

static const std::uint32_t kAaOracleMagic = 0x314F4141u;
static const unsigned int kAaOracleMaxN = 32;
static const unsigned long long kAaOracleMaxBytes = 2ull << 30;

void TraceTask::PlanM4(ViewData* vd, int pass, int round)
{
    const M4Options& o = M4Opts();
    const POVRect& area = vd->GetRenderArea();
    struct Timed final
    {
        const bool on;
        const int pass, round;
        double t0;
        ~Timed()
        {
            if(!on)
                return;
            long rss, peak;
            M4Memory(rss, peak);
            std::fprintf(stderr, "AA4 time plan pass %d round %d: %.1f ms (resident %ld MB, peak %ld MB)\n", pass, round, M4Lap(t0), rss, peak);
        }
    } timed { o.stats, pass, round, M4Clock() };
    double clock = timed.t0;
    auto lap = [&](const char* what)
    {
        const double ms = M4Lap(clock);
        if(!o.stats)
            return;
        long rss, peak;
        M4Memory(rss, peak);
        std::fprintf(stderr, "AA4 time %s: %.1f ms (resident %ld MB, peak %ld MB)\n", what, ms, rss, peak);
    };

    if((pass != 0) && !vd->aaOracleReplay && !vd->aaOracle.empty())
    {
        const char* path = std::getenv("POV_AA4_ORACLE_DUMP");
        std::FILE* f = path ? std::fopen(path, "wb") : nullptr;
        const std::uint32_t head[4] = { kAaOracleMagic, area.GetWidth(), area.GetHeight(), vd->aaOracleN };
        if(!f || (std::fwrite(head, sizeof(head), 1, f) != 1) || (std::fwrite(vd->aaOracle.data(), sizeof(float), vd->aaOracle.size(), f) != vd->aaOracle.size()))
            std::fprintf(stderr, "AA4 oracle dump %s failed\n", path ? path : "");
        if(f)
            std::fclose(f);
        std::vector<float>().swap(vd->aaOracle);
        vd->aaOracleN = 0;
    }
    if(pass == 5)
    {
        if(o.stats)
            M4PrintMemory(vd, "after the resolve");
        vd->EndAaPlan();
        if(o.stats)
            M4PrintMemory(vd, "released");
        return;
    }
    if(pass == 0)
    {
        if(o.stats)
            M4PrintMemory(vd, "before planning");
        vd->StartAaPlan(!o.bisect);
        // POV_AA4_DUMP writes the centre samples method 4 starts from; POV_AA4_REPLAY loads such a file over a render
        // of the same size, so the fit can be studied on another scene's pixels without tracing them
        const char* replay = std::getenv("POV_AA4_REPLAY");
        const char* dump = std::getenv("POV_AA4_DUMP");
        if(replay || dump)
        {
            std::FILE* f = std::fopen(replay ? replay : dump, replay ? "rb" : "wb");
            std::uint32_t head[3] = { area.GetWidth(), area.GetHeight(), vd->HasAaHits() ? 1u : 0u };
            if(f && (replay ? (std::fread(head, sizeof(head), 1, f) == 1) && (head[0] == area.GetWidth()) && (head[1] == area.GetHeight())
                            : (std::fwrite(head, sizeof(head), 1, f) == 1)))
            {
                for(unsigned int y = area.top; y <= area.bottom; y++)
                {
                    for(unsigned int x = area.left; x <= area.right; x++)
                    {
                        float rec[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
                        std::uint32_t hit = 0;
                        if(replay)
                        {
                            if((std::fread(rec, sizeof(rec), 1, f) != 1) || (std::fread(&hit, sizeof(hit), 1, f) != 1))
                                break;
                            vd->LatticeSample(x, y) = RGBTColour(rec[0], rec[1], rec[2], rec[3]);
                            if(vd->HasAaHits() && head[2])
                            {
                                vd->AaHit(x, y) = hit;
                                if(vd->HasAaPigments())
                                    std::copy(rec + 4, rec + 7, vd->AaPigmentAt(x, y));
                            }
                        }
                        else
                        {
                            const RGBTColour& c = vd->LatticeSample(x, y);
                            rec[0] = c.red(); rec[1] = c.green(); rec[2] = c.blue(); rec[3] = c.transm();
                            if(vd->HasAaHits())
                            {
                                hit = vd->AaHit(x, y);
                                if(vd->HasAaPigments())
                                    std::copy(vd->AaPigmentAt(x, y), vd->AaPigmentAt(x, y) + 3, rec + 4);
                            }
                            std::fwrite(rec, sizeof(rec), 1, f);
                            std::fwrite(&hit, sizeof(hit), 1, f);
                        }
                    }
                }
            }
            else
                std::fprintf(stderr, "AA4 %s %s failed\n", replay ? "replay" : "dump", replay ? replay : dump);
            if(f)
                std::fclose(f);
        }
        // POV_AA4_ORACLE_DUMP traces N x N sub-samples per pixel in pass 1; POV_AA4_ORACLE answers a replay's samples from them
        const char* oracle = replay ? std::getenv("POV_AA4_ORACLE") : std::getenv("POV_AA4_ORACLE_DUMP");
        const char* oracleN = std::getenv("POV_AA4_ORACLE_N");
        vd->aaOracle.clear();
        vd->aaOracleN = 0;
        vd->aaOracleReplay = false;
        if(oracle)
        {
            std::uint32_t head[3] = { area.GetWidth(), area.GetHeight(), oracleN ? std::uint32_t(std::max(1ul, std::min(std::strtoul(oracleN, nullptr, 10), 0xFFFFFFFFul))) : 8u };
            const auto bytesOf = [](const std::uint32_t* h)
                { return ((h[0] > (1u << 20)) || (h[1] > (1u << 20)) || (h[2] > kAaOracleMaxN)) ? ~0ull : 16ull * h[0] * h[1] * h[2] * h[2]; };
            if(!replay)
            {
                if((head[2] <= kAaOracleMaxN) && (bytesOf(head) <= kAaOracleMaxBytes))
                {
                    try
                    {
                        vd->aaOracle.assign(bytesOf(head) / sizeof(float), 0.0f);
                        vd->aaOracleN = head[2];
                    }
                    catch(const std::bad_alloc&) { std::vector<float>().swap(vd->aaOracle); }
                }
                if(vd->aaOracleN == 0)
                    std::fprintf(stderr, "AA4 oracle dump %s: N %u at %ux%u needs %llu bytes (limit N %u, %llu bytes); rendering without it\n",
                                 oracle, head[2], head[0], head[1], bytesOf(head), kAaOracleMaxN, kAaOracleMaxBytes);
            }
            else if(std::FILE* f = std::fopen(oracle, "rb"))
            {
                // header is magic, w, h, N; files written before the magic word start at w and are told apart by their exact size
                std::uint32_t word[4] = { 0, 0, 0, 0 };
                const bool read = (std::fread(word, sizeof(word), 1, f) == 1) && (std::fseek(f, 0, SEEK_END) == 0);
                const unsigned long long size = read ? (unsigned long long)std::ftell(f) : 0;
                const bool magic = (word[0] == kAaOracleMagic);
                std::copy(word + (magic ? 1 : 0), word + (magic ? 4 : 3), head);
                const unsigned long long payload = bytesOf(head);
                if(read && (head[0] == area.GetWidth()) && (head[1] == area.GetHeight()) && (head[2] > 0) && (head[2] <= kAaOracleMaxN) &&
                   (payload <= kAaOracleMaxBytes) && (size == (magic ? 16 : 12) + payload) && (std::fseek(f, magic ? 16 : 12, SEEK_SET) == 0))
                {
                    try
                    {
                        vd->aaOracle.resize(payload / sizeof(float));
                        vd->aaOracleReplay = (std::fread(vd->aaOracle.data(), sizeof(float), vd->aaOracle.size(), f) == vd->aaOracle.size());
                        vd->aaOracleN = head[2];
                    }
                    catch(const std::bad_alloc&) { vd->aaOracleReplay = false; }
                }
                std::fclose(f);
            }
            if(replay && !vd->aaOracleReplay)
            {
                std::fprintf(stderr, "AA4 oracle %s does not fit this %ux%u render (file %ux%u, N %u); replaying without it\n",
                             oracle, area.GetWidth(), area.GetHeight(), head[0], head[1], head[2]);
                std::vector<float>().swap(vd->aaOracle);
                vd->aaOracleN = 0;
            }
        }
        lap("centre samples loaded");
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
        vd->StartAaGeometry(!o.chain || !o.bisect);
        lap("colours");
        if(o.chain)
            M4ChainFit(vd, o);
        lap("chain fit");
        const std::int64_t nRidge = (o.chain && o.ridge) ? M4RidgeFit(vd, o.ridgeReach, o.ridgeBridge) : 0;
        lap("thin lines");
        if(o.stats)
            std::fprintf(stderr, "AA4 thin lines: %lld pixels\n", (long long)nRidge);
        const double pixels = double(area.GetWidth()) * double(area.GetHeight());
        vd->aaBudgetLeft = std::max<std::int64_t>(1, std::int64_t(std::ceil(pixels * vd->aaFraction)));
        vd->aaExhausted = (std::getenv("POV_AA4_REPLAY") != nullptr) && !vd->aaOracleReplay;
        vd->aaSpentPrinted = false;
        for(auto& c : vd->aaSpent)
            c = 0;
        vd->aaReserve = 0;
        std::int64_t nSpikeSeen = 0;
        const std::int64_t nSpike = M4FlagSpikes(vd, o, (o.bisect && o.chain) ? std::int64_t(double(o.spikeCap) * double(vd->aaBudgetLeft)) : 0, nSpikeSeen,
                                                [vd](const RGBTColour& c, float* lab) { const OkLab l = OkLabOf(vd, c); lab[0] = l.l; lab[1] = l.a; lab[2] = l.b; lab[3] = l.t; });
        // spikes are paid for here, so nothing later can take their samples
        const bool spikesTraced = M4SpikesTraced(vd, o);
        if(spikesTraced)
            vd->aaBudgetLeft -= 4 * nSpike;
        lap("spikes");
        if(o.stats)
            std::fprintf(stderr, "AA4 spikes: %lld found, %lld kept%s\n", (long long)nSpikeSeen, (long long)nSpike,
                         spikesTraced ? (", " + std::to_string(4 * nSpike) + " samples set aside").c_str() : "");
        if(o.chain)
        {
            std::int64_t nFit = 0, nNoise = 0;
            for(unsigned int y = area.top; y <= area.bottom; y++)
            {
                for(unsigned int x = area.left; x <= area.right; x++)
                {
                    const AaFit& f = vd->Fit(x, y);
                    nFit += (f.kind == 1) ? 1 : 0;
                    nNoise += (f.kind == 2) ? 1 : 0;
                }
            }
            const double frac = (o.noiseFrac >= 0.0f) ? double(o.noiseFrac) : double(nNoise) / double(std::max<std::int64_t>(1, nFit + nNoise));
            vd->aaReserve = std::int64_t(frac * double(vd->aaBudgetLeft));
            if(o.stats)
                std::fprintf(stderr, "AA4 pixels %.0f: fitted %lld (%.1f%%), noisy %lld (%.1f%%), segments %zu, budget %lld, reserved for noise %lld\n", pixels,
                             (long long)nFit, 100.0 * double(nFit) / pixels, (long long)nNoise, 100.0 * double(nNoise) / pixels, vd->aaSegs.size(),
                             (long long)vd->aaBudgetLeft, (long long)vd->aaReserve);
        }
        lap("classified");
        if(o.stats)
            M4PrintMemory(vd, "after pass 0");
        return;
    }

    const bool probe = (pass == 2);
    const bool enabled = !o.freeOnly && !vd->aaExhausted && (probe ? (round < o.rounds) : o.noise);

    if(o.chain)
    {
        M4FoldSegments(vd);
        if(probe)
        {
            M4PlanSegments(vd, o, enabled);
            return;
        }
    }

    // a candidate's priority is what is still unknown about it, worked out before anything is traced for it; bisection: every pixel
    // no fit holds starts with one leaf, its centre sample, and each round splits its leaf most unlike a touching cell, by size
    std::vector<float> bisectScore;
    const float kPrepaid = 1.0e9f;
    const bool spikes = o.bisect && !probe && M4SpikesTraced(vd, o);
    if(o.bisect && !probe)
    {
        const unsigned int w = area.GetWidth();
        bisectScore.assign(size_t(w) * area.GetHeight(), 0.0f);
        const float thr2 = float(vd->aaThr * vd->aaThr);
        const float minSize = 1.0f / float(1 << std::max(1, std::min(8, vd->aaDepth)));
        // a probe that found something makes its pixel a bisection and its flat neighbours worth a probe of their own
        for(unsigned int y = area.top; y <= area.bottom; y++)
        {
            for(unsigned int x = area.left; x <= area.right; x++)
            {
                AaFit& f = vd->Fit(x, y);
                if(f.explore != 4)
                    continue;
                f.explore = 3;
                f.kind = 2;
                for(int dy = -1; dy <= 1; dy++)
                    for(int dx = -1; dx <= 1; dx++)
                        if((int(x) + dx >= int(area.left)) && (int(x) + dx <= int(area.right)) && (int(y) + dy >= int(area.top)) && (int(y) + dy <= int(area.bottom)))
                        {
                            AaFit& g = vd->Fit(unsigned(int(x) + dx), unsigned(int(y) + dy));
                            if((g.kind == 0) && (g.explore < 2))
                                g.explore = 2;
                        }
            }
        }
        for(unsigned int y = area.top; y <= area.bottom; y++)
        {
            for(unsigned int x = area.left; x <= area.right; x++)
            {
                AaFit& f = vd->Fit(x, y);
                f.leaf = -1;
                if((f.kind == 0) && (f.spike == 0) && ((f.explore == 2) || ((f.explore == 1) && ((((x * 73856093u) ^ (y * 19349663u)) & 3u) == 0))))
                {
                    // a probe is worth less than an edge, and a probe beside a find more than a guess
                    f.leaf = 0;
                    bisectScore[(x - area.left) + (y - area.top) * w] = (f.explore == 2) ? 2.0f * std::sqrt(thr2) : 0.5f * std::sqrt(thr2);
                    continue;
                }
                const bool broken = (f.kind == 1) && (f.seg >= 0) && (vd->aaSegs[size_t(f.seg)].state == 3);
                if((f.kind != 2) && !broken && (f.spike == 0))
                    continue;
                // a pixel not yet bisected has its centre sample as its one leaf
                const AaCell centre = vd->CentreLeaf(x, y);
                const AaCell* cells = (f.cells >= 0) ? vd->Leaves(f) : &centre;
                const size_t nCells = (f.cells >= 0) ? f.nCells : 1;
                const bool grainy = vd->HasAaHits() && ((vd->AaHit(x, y) & 0x80000000u) != 0);
                float best = 0.0f;
                // a leaf index must fit the planner's 16 bits
                for(size_t i = 0; (i < nCells) && (nCells <= 32764); i++)
                {
                    const AaCell& c = cells[i];
                    const float cx = c.X(), cy = c.Y(), cs = c.Size();
                    if(cs <= minSize * 1.01f)
                        continue;
                    float far2 = 0.0f;
                    auto touch = [&](float ox, float oy, float os, const float* lab)
                    {
                        if((std::fabs(ox - cx) <= 0.5f * (os + cs) + 1.0e-4f) && (std::fabs(oy - cy) <= 0.5f * (os + cs) + 1.0e-4f))
                            far2 = std::max(far2, Dist2(lab, c.lab));
                    };
                    for(size_t j = 0; j < nCells; j++)
                        if(j != i)
                            touch(cells[j].X(), cells[j].Y(), cells[j].Size(), cells[j].lab);
                    // a grainy pixel with grain kept looks at its neighbours, whose grain differs, only for its first
                    // split; after it, leaves are judged against their siblings alone, which share its draws
                    for(int dy = -1; dy <= 1; dy++)
                        for(int dx = -1; dx <= 1; dx++)
                            if((!o.keepGrain || (nCells == 1) || !grainy) && ((dx != 0) || (dy != 0)) && (int(x) + dx >= int(area.left)) && (int(x) + dx <= int(area.right)) && (int(y) + dy >= int(area.top)) && (int(y) + dy <= int(area.bottom)))
                                touch(float(dx), float(dy), 1.0f, vd->AaLabAt(unsigned(int(x) + dx), unsigned(int(y) + dy)));
                    if(far2 < thr2)
                        continue;
                    const float score = cs * std::sqrt(far2);
                    if(score > best)
                    {
                        best = score;
                        f.leaf = std::int16_t(i);
                    }
                }
                // a kept spike's first split was paid for at pass 0 and is taken whatever is left
                if((f.spike > 0) && (nCells == 1))
                {
                    best = kPrepaid;
                    f.leaf = 0;
                }
                bisectScore[(x - area.left) + (y - area.top) * w] = best;
            }
        }
    }
    auto bucketOf = [&](const AaFit& f, unsigned int x, unsigned int y) -> int
    {
        const AaGeom& geom = vd->Geom(f);
        if(probe)
        {
            if(f.kind != 1)
                return -1;
            const float ext = 0.5f * (std::fabs(geom.nx) + std::fabs(geom.ny));
            const float gap = std::min(geom.hi, ext) - std::max(geom.lo, -ext);
            return (gap > o.gapMin) ? M4Bucket(geom.contrast * gap) : -1;
        }
        if(o.bisect)
        {
            const float score = (f.leaf >= 0) ? bisectScore[(x - area.left) + (y - area.top) * area.GetWidth()] : 0.0f;
            return ((f.leaf >= 0) && (score < kPrepaid)) ? M4Bucket(score) : -1;
        }
        // a noisy pixel's next sample is worth the drop it should make to the standard error of its mean: the spread of
        // its samples so far (what its neighbours differ by until there are three), over root n against root n + 1
        if((f.kind != 2) || (f.probes >= 250))
            return -1;
        const float n = float(f.probes) + 1.0f;
        float sd = 0.5f * geom.contrast;
        if(f.probes >= 2)
        {
            const float sum2 = geom.sumLab[0] * geom.sumLab[0] + geom.sumLab[1] * geom.sumLab[1] + geom.sumLab[2] * geom.sumLab[2] + geom.sumLab[3] * geom.sumLab[3];
            sd = std::sqrt(std::max(0.0f, geom.sumSq - sum2 / n) / (n - 1.0f));
        }
        return M4Bucket(sd * (1.0f / std::sqrt(n) - 1.0f / std::sqrt(n + 1.0f)));
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
    const int factor = (o.bisect && !probe) ? 4 : 1;
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
                        std::uint32_t h = (x * 73856093u) ^ (y * 19349663u);
                        h ^= h >> 16; h *= 0x7feb352du; h ^= h >> 15; h *= 0x846ca68bu; h ^= h >> 16;
                        order.emplace_back(h, (x - area.left) + (y - area.top) * area.GetWidth());
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
            const bool prepaid = spikes && (f.leaf >= 0) && (bisectScore[at] >= kPrepaid);
            const bool taken = (b > threshold) || ((b == threshold) && (drawn.empty() || drawn[at]));
            f.planned = (((b >= 0) && taken) || prepaid) ? 1 : 0;
            // the pixels the noise pass will bisect (not those it probes) need room for three more leaves
            if(o.bisect && !probe && f.planned && !((f.kind == 0) && (f.spike == 0) && ((f.explore == 1) || (f.explore == 2))))
                split.push_back(at);
        }
    }
    if(!split.empty())
        vd->ReserveAaSplits(split);
    vd->aaBudgetLeft -= factor * admitted;
    if(cut)
        vd->aaExhausted = true;
}

void TraceTask::FitPixelM4(unsigned int x, unsigned int y, AaFit& fit)
{
    AaGeom& geom = GetViewData()->GeomFor(fit);
    const M4Options& o = M4Opts();
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

    geom.contrast = std::sqrt(farthest);
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
    geom.nx = bestNx;
    geom.ny = bestNy;
    geom.lo = bestLo;
    geom.hi = bestHi;
    geom.a = ca;
    geom.b = cb;
}

void TraceTask::ProbePixelM4(unsigned int x, unsigned int y, AaFit& fit)
{
    AaGeom& geom = GetViewData()->GeomFor(fit);
    const float ext = 0.5f * (std::fabs(geom.nx) + std::fabs(geom.ny));
    const float lo = std::max(geom.lo, -ext), hi = std::min(geom.hi, ext);
    const float sm = 0.5f * (lo + hi);
    const float tx = -geom.ny, ty = geom.nx;

    // the part of the line at that offset which lies inside the pixel; successive probes walk along it
    float ulo = -2.0f, uhi = 2.0f;
    const float along[2][2] = { { sm * geom.nx, tx }, { sm * geom.ny, ty } };
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
    TraceSample(DBL(x) + 0.5 + sm * geom.nx + u * tx, DBL(y) + 0.5 + sm * geom.ny + u * ty, col, 0.25, 0);

    // a probe on the first colour's side puts the edge beyond it, otherwise before it
    const OkLab pa = ToOkLab(geom.a), pb = ToOkLab(geom.b), pc = ToOkLab(col);
    const float v[4] = { pb.l - pa.l, pb.a - pa.a, pb.b - pa.b, pb.t - pa.t };
    const float vv = v[0] * v[0] + v[1] * v[1] + v[2] * v[2] + v[3] * v[3];
    const float t = (vv > 0.0f) ? ((pc.l - pa.l) * v[0] + (pc.a - pa.a) * v[1] + (pc.b - pa.b) * v[2] + (pc.t - pa.t) * v[3]) / vv : 0.0f;
    if(t < 0.5f)
        geom.lo = sm;
    else
        geom.hi = sm;
    if(geom.lo > geom.hi)
        geom.lo = geom.hi = sm;
    fit.probes++;
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
        TraceSample(px, py, col, 0.25, 0);

        // which side of the line the point is on, by whether it is nearer the colour of one side or the other
        RGBTColour ca, cb;
        M4LocalColours(s, s.probeU, ca, cb);
        const OkLab pa = ToOkLab(ca), pb = ToOkLab(cb), pc = ToOkLab(col);
        const float v[4] = { pb.l - pa.l, pb.a - pa.a, pb.b - pa.b, pb.t - pa.t };
        const float vv = v[0] * v[0] + v[1] * v[1] + v[2] * v[2] + v[3] * v[3];
        const float score = (vv > 0.0f) ? ((pc.l - pa.l) * v[0] + (pc.a - pa.a) * v[1] + (pc.b - pa.b) * v[2] + (pc.t - pa.t) * v[3]) / vv : 0.0f;
        // a colour well off the way from one side's to the other's is a third colour, such as a highlight along the edge
        const float t = std::min(1.0f, std::max(0.0f, score));
        const float off[4] = { pc.l - pa.l - t * v[0], pc.a - pa.a - t * v[1], pc.b - pa.b - t * v[2], pc.t - pa.t - t * v[3] };
        const float off2 = off[0] * off[0] + off[1] * off[1] + off[2] * off[2] + off[3] * off[3];
        const bool third = M4Opts().bisect && (off2 > std::max(float(aaThreshold * aaThreshold), 0.0625f * vv));
        s.fresh.push_back(AaSeg::Pt { s.probeU, s.probeV, std::uint8_t(third ? 2 : (score >= 0.5f ? 1 : 0)) });
        s.probes++;
        Cooperate();
    }
    radiosity.AfterTile();
    GetViewDataPtr()->AfterTile();
}

void TraceTask::ProgressiveM4()
{
    if((aaPass == 2) && M4Opts().chain)
    {
        ProbeSegmentsM4();
        return;
    }
    if((aaPass == 4) && M4Opts().stats && !GetViewData()->aaSpentPrinted.exchange(true))
        std::fprintf(stderr, "AA4 spent: edge probes %lld, noisy pixels %lld, exploring %lld, spikes %lld\n", (long long)GetViewData()->aaSpent[0].load(),
                     (long long)GetViewData()->aaSpent[1].load(), (long long)GetViewData()->aaSpent[2].load(), (long long)GetViewData()->aaSpent[3].load());
    const unsigned int on = ((aaPass == 1) && !GetViewData()->aaOracleReplay) ? GetViewData()->aaOracleN : 0;
    const bool traces = (aaPass == 2) || (aaPass == 3) || (on > 0);
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
                switch(aaPass)
                {
                    case 1:
                        if(!M4Opts().chain)
                            FitPixelM4(x, y, fit);
                        for(unsigned int k = 0; k < on * on; k++)
                        {
                            const POVRect& area = GetViewData()->GetRenderArea();
                            const unsigned int i = (x - area.left) * on + k % on, j = (y - area.top) * on + k / on;
                            RGBTColour c;
                            TraceSample(DBL(x) + (DBL(k % on) + 0.5) / on, DBL(y) + (DBL(k / on) + 0.5) / on, c, 1.0 / on, -1);
                            float* dst = &GetViewData()->aaOracle[4 * (size_t(j) * area.GetWidth() * on + i)];
                            dst[0] = c.red(); dst[1] = c.green(); dst[2] = c.blue(); dst[3] = c.transm();
                        }
                        break;
                    case 2:
                        if(fit.planned)
                            ProbePixelM4(x, y, fit);
                        break;
                    case 3:
                        if(fit.planned && M4Opts().bisect && (fit.kind == 0) && (fit.spike == 0) && ((fit.explore == 1) || (fit.explore == 2)))
                        {
                            const unsigned hsh = (x * 2654435761u) ^ (y * 40503u);
                            RGBTColour probe;
                            TraceSample(DBL(x) + 0.25 + 0.5 * double(hsh & 255u) / 255.0, DBL(y) + 0.25 + 0.5 * double((hsh >> 8) & 255u) / 255.0, probe, 0.25, 2);
                            const OkLab l = ToOkLab(probe);
                            const float pl[4] = { l.l, l.a, l.b, l.t };
                            fit.explore = (Dist2(pl, GetViewData()->AaLabAt(x, y)) > float(aaThreshold * aaThreshold)) ? 4 : 3;
                            fit.probes = std::uint8_t(std::min(250, int(fit.probes) + 1));
                        }
                        else if(fit.planned && M4Opts().bisect)
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
                                TraceSample(DBL(x) + 0.5 + c.X(), DBL(y) + 0.5 + c.Y(), c.col, double(c.Size()), ((fit.spike > 0) && (parent.depth == 0)) ? 3 : 1);
                                const OkLab l = ToOkLab(c.col);
                                c.lab[0] = l.l; c.lab[1] = l.a; c.lab[2] = l.b; c.lab[3] = l.t;
                                cells[(k == 0) ? size_t(fit.leaf) : size_t(fit.nCells++)] = c;
                            }
                            fit.probes = std::uint8_t(std::min(250, int(fit.probes) + 4));
                        }
                        else if(fit.planned)
                        {
                            AaGeom& geom = GetViewData()->GeomFor(fit);
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
                            TraceSample(DBL(x) + ox, DBL(y) + oy, extra, 0.5, 1);
                            GetViewData()->AaExtra(x, y) += extra;
                            const float* centre = GetViewData()->AaLabAt(x, y);
                            if(fit.probes == 0)
                            {
                                for(int c = 0; c < 4; c++)
                                    geom.sumLab[c] = centre[c];
                                geom.sumSq = centre[0] * centre[0] + centre[1] * centre[1] + centre[2] * centre[2] + centre[3] * centre[3];
                            }
                            const OkLab le = ToOkLab(extra);
                            geom.sumLab[0] += le.l;
                            geom.sumLab[1] += le.a;
                            geom.sumLab[2] += le.b;
                            geom.sumLab[3] += le.t;
                            geom.sumSq += le.l * le.l + le.a * le.a + le.b * le.b + le.t * le.t;
                            fit.probes++;
                        }
                        break;
                    default:
                    {
                        const AaGeom& geom = GetViewData()->Geom(fit);
                        const M4Options& o4 = M4Opts();
                        RGBTColour col = GetViewData()->LatticeSample(x, y);
                        float mapProbes = float(fit.probes);
                        float mapGreen = 0.25f;
                        const AaCell* bisected = o4.bisect ? GetViewData()->Leaves(fit) : nullptr;
                        if(bisected && (fit.nCells > 1))
                        {
                            col.Clear();
                            for(size_t i = 0; i < fit.nCells; i++)
                                col += bisected[i].col * double(bisected[i].Size() * bisected[i].Size());
                            if((fit.spike > 0) && ((o4.spikeCentre == 1) || ((o4.spikeCentre == 2) && M4LeavesContradict(bisected, fit.nCells, GetViewData()->AaLabAt(x, y), float(aaThreshold * aaThreshold)))))
                                col = col * 0.8 + GetViewData()->LatticeSample(x, y) * 0.2;
                            fit.kind = 2;
                            GetViewDataPtr()->Stats()[Number_Of_Pixels_Supersampled]++;
                        }
                        else if(fit.kind == 3)
                        {
                            // a line a pixel or two wide: the pixel keeps its sample and gains the strip coverage its centre sample did not show
                            const OkLab pa = ToOkLab(geom.a), pb = ToOkLab(geom.b), po = ToOkLab(col);
                            const float v[4] = { pb.l - pa.l, pb.a - pa.a, pb.b - pa.b, pb.t - pa.t };
                            const float vv = v[0] * v[0] + v[1] * v[1] + v[2] * v[2] + v[3] * v[3];
                            const bool centreOn = (vv > 0.0f) && (((po.l - pa.l) * v[0] + (po.a - pa.a) * v[1] + (po.b - pa.b) * v[2] + (po.t - pa.t) * v[3]) >= 0.5f * vv);
                            const double f = double(M4Coverage(geom.nx, geom.ny, geom.lo - 0.5f * geom.hi)) - double(M4Coverage(geom.nx, geom.ny, geom.lo + 0.5f * geom.hi));
                            const RGBTColour own = col;
                            // a centre sample on the line beyond the strip's edge means the strip is too narrow there: at least half is covered
                            const bool beyond = centreOn && (std::fabs(geom.lo) > 0.5f * geom.hi);
                            col += (geom.b - geom.a) * ((beyond ? std::max(f, 0.5) : f) - (centreOn ? 1.0 : 0.0));
                            const RGBTColour ends[2] = { geom.a, geom.b };
                            const double ew[2] = { 1.0, 1.0 };
                            col = M4Bounded(col, own, ends, ew, 2);
                            mapGreen = 0.5f;
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
                                mapProbes = float(sg.probes);
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
                                twoLines = ((std::fabs(nx * n2x + ny * n2y) < 0.95f) || o4.strips) && (f > 0.0) && (f < 1.0) && (c2 > 0.0f) && (c2 < 1.0f);
                            }

                            if(twoLines)
                            {
                                mapGreen = 0.9f;
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
                                if(o4.cv)
                                    col = M4Bounded(own + col - centreModel, own, qc, qw);
                            }
                            // a pixel the final line does not cross stays as it was sampled
                            else if(!trusted)
                                mapGreen = 0.5f;
                            else if(o4.cv && ((fit.seg < 0) || ((f > 0.0) && (f < 1.0))))
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
                            else if((fit.seg < 0) || ((f > 0.0) && (f < 1.0)))
                            {
                                col = geom.a * (1.0 - f);
                                col += geom.b * f;
                            }
                            if(probes > 0)
                                GetViewDataPtr()->Stats()[Number_Of_Pixels_Supersampled]++;
                        }
                        else if((fit.kind == 2) && (fit.probes > 0))
                        {
                            if(GetViewData()->HasAaExtra())
                                col += GetViewData()->AaExtra(x, y);
                            col = col * (1.0 / double(fit.probes + 1));
                            GetViewDataPtr()->Stats()[Number_Of_Pixels_Supersampled]++;
                        }
                        if(M4Opts().map)
                        {
                            // red: probes on a fitted edge, green: fitted, blue: noisy pixel and the samples it got, yellow: spike
                            const float p = mapProbes;
                            col = RGBTColour((fit.kind == 1) ? std::min(1.0f, p / 8.0f) : ((fit.kind == 3) ? 1.0f : 0.0f), (fit.kind == 1) ? mapGreen : 0.0f,
                                             (fit.kind == 2) ? 0.15f + std::min(0.85f, p / 4.0f) : ((fit.kind == 3) ? 1.0f : 0.0f), 0.0f);
                            if(fit.spike > 0)
                                col = RGBTColour(1.0f, 1.0f, 0.0f, 0.0f);
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
