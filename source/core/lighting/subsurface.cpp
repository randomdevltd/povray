//******************************************************************************
///
/// @file core/lighting/subsurface.cpp
///
/// Implementations related to subsurface light transport.
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
#include "core/lighting/subsurface.h"

// C++ variants of C standard header files
#include <cmath>
#include <cstdio>

// C++ standard header files
#include <algorithm>
#include <functional>
#include <limits>

// POV-Ray header files (base module)
#include "base/mathutil.h"

// POV-Ray header files (core module)
#include "core/lighting/photons.h"

// this must be the last file included
#include "base/povdebug.h"

namespace pov
{

/// Computes the BRDF approximation of the diffuse reflectance term.
inline double DiffuseReflectance(double A, double alphaPrime)
{
    double root = sqrt(3*(1-alphaPrime));
    return (alphaPrime/2) * ( 1 + exp(-(4.0/3.0)*A*root) ) * exp(-root);
}


SubsurfaceInterior::SubsurfaceInterior(double ior) :
    precomputedReducedAlbedo(ior)
{}

SubsurfaceInterior::PrecomputedReducedAlbedo::PrecomputedReducedAlbedo(float ior)
{
    reducedAlbedo[ReducedAlbedoSamples] = 1.0;
    double Fdr = FresnelDiffuseReflectance(ior);
    double A = (1+Fdr)/(1-Fdr);
    double alphaPrime = 1.0;
    double Rd = 1.0;
    int it = 0;
    for (int i = ReducedAlbedoSamples-1; i > 0; i --)
    {
        double Rd0 = 0.0;
        double Rd1 = Rd;
        double diffuseReflectance = double(i)/ReducedAlbedoSamples;
        double alphaPrime0 = 0.0;
        double alphaPrime1 = alphaPrime;
        while(abs(Rd-diffuseReflectance) >= EPSILON)
        {
            double p = (diffuseReflectance-Rd0)/(Rd1-Rd0);
            alphaPrime = alphaPrime0 + p*(alphaPrime1-alphaPrime0);
            Rd = DiffuseReflectance(A, alphaPrime);
            if (Rd < diffuseReflectance)
            {
                alphaPrime0 = alphaPrime;
                Rd0 = Rd;
            }
            else
            {
                alphaPrime1 = alphaPrime;
                Rd1 = Rd;
            }
            it ++;
        }
        reducedAlbedo[i] = alphaPrime;
    }
    reducedAlbedo[0] = 0.0;
}

PreciseColourChannel SubsurfaceInterior::PrecomputedReducedAlbedo::operator()(PreciseColourChannel diffuseReflectance) const
{
    PreciseColourChannel Rd = clip(diffuseReflectance, 0.0, 1.0);
    PreciseColourChannel i = Rd * ReducedAlbedoSamples;
    int i0 = floor(i);
    int i1 = ceil(i);
    PreciseColourChannel p = (i-i0);
    return (1-p)*reducedAlbedo[i0] + p*reducedAlbedo[i1];
}

PreciseMathColour SubsurfaceInterior::GetReducedAlbedo(const MathColour& diffuseReflectance) const
{
    PreciseMathColour result;
    for (int i = 0; i < MathColour::channels; i ++)
        result[i] = (precomputedReducedAlbedo.get())(diffuseReflectance[i]);
    return result;
}

// Unpolarised Fresnel reflectance for light inside a medium of relative index eta meeting its boundary at cosine c.
static double FresnelFromInside(double c, double eta)
{
    double s2 = Sqr(eta) * (1.0 - Sqr(c));
    if (s2 >= 1.0)
        return 1.0;
    double ct = sqrt(1.0 - s2), rs = (eta * c - ct) / (eta * c + ct), rp = (c - eta * ct) / (c + eta * ct);
    return 0.5 * (Sqr(rs) + Sqr(rp));
}

// Real sources along the beam, by the log of their depth: how many, and the depths they span.
static const int kBeamDepths = 32;
static const double kBeamShallowest = 1e-7, kBeamDeepest = 48.0;

SubsurfaceDipole::SubsurfaceDipole(double albedo, const SubsurfaceBoundary& boundary)
{
    double absorption = 1.0 - albedo, D = (2.0 * absorption + albedo) / 3.0;
    sigma = sqrt(absorption / D);
    zb = 4.0 * boundary.ab * D;
    fluence = albedo * boundary.cphi / (4.0 * M_PI * D);
    flux = albedo * boundary.ce / (4.0 * M_PI);
}

double SubsurfaceDipole::Rd(double xSqr, double depth) const
{
    double zv = depth + zb, dr = sqrt(xSqr + Sqr(depth)), dv = sqrt(xSqr + Sqr(zv));
    double er = exp(-sigma * dr), ev = exp(-sigma * dv);
    return fluence * (er / dr - ev / dv) + flux * (depth * (1.0 + sigma * dr) * er / (dr * Sqr(dr)) + zv * (1.0 + sigma * dv) * ev / (dv * Sqr(dv)));
}

// (exp(-sigma a) - exp(-sigma b)) / sigma, which is b - a without absorption.
static double ExpDifference(double sigma, double a, double b)
{
    return (sigma > 0.0) ? -exp(-sigma * a) * expm1(-sigma * (b - a)) / sigma : b - a;
}

double SubsurfaceDipole::Disc(double x) const
{
    double zv = 1.0 + zb, phi = ExpDifference(sigma, 1.0, zv), e = exp(-sigma) + exp(-sigma * zv);
    if (x < HUGE_VAL)
    {
        double dr = sqrt(Sqr(x) + 1.0), dv = sqrt(Sqr(x) + Sqr(zv));
        phi -= ExpDifference(sigma, dr, dv);
        e -= exp(-sigma * dr) / dr + zv * exp(-sigma * dv) / dv;
    }
    return 2.0 * M_PI * (fluence * phi + flux * e);
}

SubsurfaceBoundary::SubsurfaceBoundary(double e) :
    eta(e), c1(0.0), c2(0.0)
{
    // Below the critical cosine all light reflects; above it, midpoint sums.
    double critical = (eta > 1.0) ? sqrt(1.0 - 1.0 / Sqr(eta)) : 0.0;
    c1 = Sqr(critical) / 2.0;
    c2 = critical * Sqr(critical) / 3.0;
    const int steps = 8192;
    for (int i = 0; i < steps; i++)
    {
        double c = critical + (1.0 - critical) * (i + 0.5) / steps, f = FresnelFromInside(c, eta) * (1.0 - critical) / steps;
        c1 += f * c;
        c2 += f * Sqr(c);
    }
    fdr = 2.0 * c1;
    ab = (1.0 + 3.0 * c2) / (1.0 - 2.0 * c1);
    cphi = 0.25 * (1.0 - 2.0 * c1);
    ce = 0.5 * (1.0 - 3.0 * c2);

    // Beam diffusion (Habel et al. 2013): real sources of the better dipole along the refracted beam, each damped near the
    // surface, as a ratio to the dipole at one depth, per row of u and column; Simpson's rule over the log radius.
    beam.assign(size_t(kBeamRows) * kBeamColumns * 2, 1.0f);
    beamTotal.assign(kBeamRows, 1.0f);
    const double logShallowest = log(kBeamShallowest), h = (log(kBeamDeepest) - logShallowest) / kBeamDepths;
    for (int row = 0; row < kBeamRows; row++)
    {
        double u = sqrt(3.0) * row / (kBeamRows - 1), albedo = 1.0 - Sqr(u) / 3.0;
        SubsurfaceDipole dipole(albedo, *this);
        auto beamRd = [&](double x)
        {
            double sum = 0.0;
            for (int k = 0; k < kBeamDepths; k++)
            {
                double t = exp(logShallowest + (k + 0.5) * h), dr = sqrt(Sqr(x) + Sqr(t));
                sum += exp(-t) * -expm1(-2.0 * (dr + t)) * dipole.Rd(Sqr(x), t) * t * h;
            }
            return albedo * sum;
        };
        double x = 0.0, rd = 0.0, disc = 0.0, rdRatio = 1.0, discRatio = 1.0;
        for (int k = 0; k < kBeamColumns; k++)
        {
            double x0 = x, rd0 = rd;
            x = sqrt(ldexp(1.0 + (k % 4) / 4.0, kBeamFirstOctave + k / 4));
            rd = beamRd(x);
            if (k == 0)
                disc = M_PI * Sqr(x) * rd;
            else
            {
                double mid = sqrt(x0 * x);
                disc += 2.0 * M_PI * log(x / x0) / 6.0 * (Sqr(x0) * rd0 + 4.0 * Sqr(mid) * beamRd(mid) + Sqr(x) * rd);
            }
            double rdDipole = dipole.Rd(Sqr(x)), discDipole = dipole.Disc(x);
            rdRatio = ((rd > 0.0) && (rdDipole > 0.0)) ? rd / rdDipole : rdRatio;
            discRatio = (discDipole > 0.0) ? disc / discDipole : discRatio;
            beam[(size_t(row) * kBeamColumns + k) * 2] = float(rdRatio);
            beam[(size_t(row) * kBeamColumns + k) * 2 + 1] = float(discRatio);
        }
        // Past the last radius the profile is the dipole times its last ratio.
        double whole = dipole.Disc(HUGE_VAL);
        beamTotal[row] = (whole > 0.0) ? float((disc + rdRatio * (whole - dipole.Disc(x))) / whole) : float(discRatio);
    }

    for (int i = 0; i <= kSamples; i++)
    {
        double lo = 0.0, hi = 1.0, target = double(i) / kSamples;
        for (int k = 0; (i > 0) && (i < kSamples) && (k < 60); k++)
            ((Total(0.5 * (lo + hi)) < target) ? lo : hi) = 0.5 * (lo + hi);
        reducedAlbedo[i] = (i == 0) ? 0.0f : (i == kSamples) ? 1.0f : float(0.5 * (lo + hi));
    }
}

const float *SubsurfaceBoundary::BeamRow(double albedo, double& weight) const
{
    double f = sqrt(clip(1.0 - albedo, 0.0, 1.0)) * (kBeamRows - 1);
    int row = std::min(kBeamRows - 2, int(floor(f)));
    weight = f - row;
    return &beam[size_t(row) * kBeamColumns * 2];
}

double SubsurfaceBoundary::BeamTotalRatio(double albedo) const
{
    double f = sqrt(clip(1.0 - albedo, 0.0, 1.0)) * (kBeamRows - 1);
    int row = std::min(kBeamRows - 2, int(floor(f)));
    return (1.0 - (f - row)) * beamTotal[row] + (f - row) * beamTotal[row + 1];
}

double SubsurfaceBoundary::Total(double albedo) const
{
    return SubsurfaceDipole(albedo, *this).Disc(HUGE_VAL) * BeamTotalRatio(albedo);
}

// The better dipole's Rd, and its integral over a disc of radius x, from a source of sign s at signed depth z (above a
// face where z < 0): the multipole's terms.
static double LayerRd(const SubsurfaceDipole& d, double xSqr, double z, double s)
{
    double r = sqrt(xSqr + Sqr(z)), e = exp(-d.sigma * r);
    return s * e * (d.fluence / r + d.flux * z * (1.0 + d.sigma * r) / (r * Sqr(r)));
}

static double LayerDisc(const SubsurfaceDipole& d, double x, double z, double s)
{
    double a = fabs(z), r = sqrt(Sqr(x) + Sqr(z));
    double flux = (z > 0.0 ? 1.0 : -1.0) * (exp(-d.sigma * a) - a * exp(-d.sigma * r) / r);
    return s * 2.0 * M_PI * (d.fluence * ExpDifference(d.sigma, a, r) + d.flux * flux);
}

// Donner and Jensen's multipole for a layer tau deep, from one source at min(1, tau / 2) carrying the light that scatters
// in the layer, 1 - e^-tau, over the better dipole: the light leaving the near face, and the far face (over the dipole at the
// straight distance through the layer).
const float *SubsurfaceBoundary::Layer(int step, int k) const
{
    std::lock_guard<std::mutex> lock(layerMutex);
    std::unique_ptr<float[]>& rows = layers[std::make_pair(step, k)];
    if (rows)
        return rows.get();
    rows.reset(new float[2 * kLayerFar]);
    double u = double(step) / kLayerSteps;
    SubsurfaceDipole dipole(1.0 - Sqr(u) / 3.0, *this);
    for (int part = 0; part < 2; part++)
    {
        double tau = exp2(0.25 * (k + part)), z = std::min(1.0, 0.5 * tau), scatter = -expm1(-tau), period = 2.0 * (tau + dipole.zb);
        int images = (dipole.sigma * period > 0.0) ? std::min(64, int(ceil(12.0 / (dipole.sigma * period)))) : 64;
        float *near = rows.get() + part * 2 * kBeamColumns, *far = near + kLayerFar;
        double rdRatio = 1.0, discRatio = 1.0, farRatio = 1.0;
        for (int c = 0; c < kBeamColumns; c++)
        {
            double xSqr = ldexp(1.0 + (c % 4) / 4.0, kBeamFirstOctave + c / 4), x = sqrt(xSqr);
            double rd = 0.0, disc = 0.0, through = 0.0;
            for (int i = -images; i <= images; i++)
            {
                double real = i * period + z, image = i * period - z - dipole.zb;
                rd += LayerRd(dipole, xSqr, real, 1.0) + LayerRd(dipole, xSqr, image, -1.0);
                disc += LayerDisc(dipole, x, real, 1.0) + LayerDisc(dipole, x, image, -1.0);
                through += LayerRd(dipole, xSqr, tau - real, 1.0) + LayerRd(dipole, xSqr, tau - image, -1.0);
            }
            double rdDipole = dipole.Rd(xSqr), discDipole = dipole.Disc(x), farDipole = dipole.Rd(xSqr + Sqr(tau));
            rdRatio = (rdDipole > 0.0) ? std::max(0.0, scatter * rd / rdDipole) : rdRatio;
            discRatio = (discDipole > 0.0) ? std::max(0.0, scatter * disc / discDipole) : discRatio;
            farRatio = (farDipole > 0.0) ? std::max(0.0, scatter * through / farDipole) : farRatio;
            near[2 * c] = float(rdRatio);
            near[2 * c + 1] = float(discRatio);
            far[2 * c] = float(farRatio);
            far[2 * c + 1] = float(farRatio);
        }
    }
    return rows.get();
}

const SubsurfaceBoundary& SubsurfaceBoundary::For(double eta)
{
    static std::mutex mutex;
    static std::map<float, std::unique_ptr<SubsurfaceBoundary>> boundaries;
    std::lock_guard<std::mutex> lock(mutex);
    std::unique_ptr<SubsurfaceBoundary>& boundary = boundaries[float(eta)];
    if (!boundary)
        boundary.reset(new SubsurfaceBoundary(float(eta)));
    return *boundary;
}

PreciseMathColour SubsurfaceBoundary::GetReducedAlbedo(const MathColour& diffuseReflectance) const
{
    PreciseMathColour result;
    for (int j = 0; j < MathColour::channels; j++)
    {
        double i = clip(double(diffuseReflectance[j]), 0.0, 1.0) * kSamples;
        int i0 = int(floor(i)), i1 = int(ceil(i));
        result[j] = (1.0 - (i - i0)) * reducedAlbedo[i0] + (i - i0) * reducedAlbedo[i1];
    }
    return result;
}

size_t SubsurfaceCellKeyHash::operator()(const SubsurfaceCellKey& k) const
{
    size_t h = std::hash<const void*>()(k.object) ^ (std::hash<const void*>()(k.medium) * 31u) ^
               (std::hash<size_t>()(k.preparedSetId) * 65599u);
    for (std::int64_t v : { std::int64_t(k.sizeLevel), k.x, k.y, k.z, std::int64_t(k.radiosity), std::int64_t(k.photons), std::int64_t(k.depthLevel) })
        h = h * 1000003u ^ std::hash<std::int64_t>()(v);
    return h;
}

SubsurfacePhotonBoundaries& SubsurfaceCache::PhotonBoundaries(const PhotonMap& map)
{
    std::lock_guard<std::mutex> lock(photonBoundariesMutex);
    auto& entry = photonBoundaries[&map];
    if (!entry)
        entry.reset(new SubsurfacePhotonBoundaries());
    return *entry;
}

// Slots of 16 bytes: at most 64 MiB, or about 32 bytes per photon below that.
static const std::uint64_t kBoundarySlotsMin = std::uint64_t(1) << 12, kBoundarySlotsMax = std::uint64_t(1) << 22;
static const int kBoundaryProbes = 32;

static std::uint64_t MixBoundaryKey(std::uint64_t key)
{
    key = (key ^ (key >> 30)) * 0xbf58476d1ce4e5b9u;
    key = (key ^ (key >> 27)) * 0x94d049bb133111ebu;
    return key ^ (key >> 31);
}

void SubsurfacePhotonBoundaries::Allocate(const PhotonMap& map)
{
    std::uint64_t count = kBoundarySlotsMin;
    while ((count < kBoundarySlotsMax) && (count < 2 * std::uint64_t(std::max(map.numPhotons, 0))))
        count *= 2;
    slots.reset(new Slot[count]);
    mask = count - 1;
    blockSize = PhotonMap::GetBlockSize();
    for (std::uint32_t id = 0; id < map.mBlockList.size(); id++)
        blocks.emplace_back(reinterpret_cast<std::uintptr_t>(map.GetBlockStart(id)), id);
    std::sort(blocks.begin(), blocks.end());
}

bool SubsurfacePhotonBoundaries::Key(const PhotonMap& map, const Photon* photon, const void* receiver, std::uint64_t& key)
{
    std::call_once(once, [&]() { Allocate(map); });
    std::uintptr_t address = reinterpret_cast<std::uintptr_t>(photon);
    auto block = std::upper_bound(blocks.begin(), blocks.end(), std::make_pair(address, std::numeric_limits<std::uint32_t>::max()));
    if (block == blocks.begin())
        return false;
    --block;
    std::uintptr_t offset = address - block->first;
    if ((offset % sizeof(Photon) != 0) || (offset / sizeof(Photon) >= blockSize))
        return false;
    std::uint64_t index = std::uint64_t(block->second) * blockSize + offset / sizeof(Photon);
    for (int id = 0; id < kReceivers; id++)
    {
        const void* expected = receivers[id].load(std::memory_order_acquire);
        if ((expected == nullptr) && receivers[id].compare_exchange_strong(expected, receiver, std::memory_order_acq_rel))
            expected = receiver;
        if (expected == receiver)
        {
            key = (std::uint64_t(id + 1) << 32) | index;
            return true;
        }
    }
    return false;
}

bool SubsurfacePhotonBoundaries::Find(std::uint64_t key, float folded[2]) const
{
    for (std::uint64_t i = MixBoundaryKey(key), probe = 0; probe < kBoundaryProbes; i++, probe++)
    {
        const Slot& slot = slots[i & mask];
        std::uint64_t word = slot.word.load(std::memory_order_acquire);
        if ((word >> 1) == key)
        {
            if ((word & 1) == 0)
                return false;
            std::copy(slot.folded, slot.folded + 2, folded);
            return true;
        }
        if (word == 0)
            return false;
    }
    return false;
}

void SubsurfacePhotonBoundaries::Store(std::uint64_t key, const float folded[2])
{
    for (std::uint64_t i = MixBoundaryKey(key), probe = 0; probe < kBoundaryProbes; i++, probe++)
    {
        Slot& slot = slots[i & mask];
        std::uint64_t word = slot.word.load(std::memory_order_relaxed);
        if ((word == 0) && slot.word.compare_exchange_strong(word, key << 1, std::memory_order_relaxed))
        {
            std::copy(folded, folded + 2, slot.folded);
            slot.word.store((key << 1) | 1, std::memory_order_release);
            return;
        }
        if ((word >> 1) == key)
            return;
    }
}

void SubsurfacePhotonBoundaries::Fold(const Vector3d& normal, bool valid, float folded[2])
{
    double l1 = valid ? fabs(normal[X]) + fabs(normal[Y]) + fabs(normal[Z]) : 0.0;
    if (!(l1 > 0.0) || !std::isfinite(l1))
    {
        folded[0] = folded[1] = 2.0f;
        return;
    }
    double u = normal[X] / l1, v = normal[Y] / l1;
    if (normal[Z] < 0.0)
    {
        double w = u;
        u = std::copysign(1.0 - fabs(v), w);
        v = std::copysign(1.0 - fabs(w), v);
    }
    folded[0] = float(u);
    folded[1] = float(v);
}

bool SubsurfacePhotonBoundaries::Unfold(const float folded[2], Vector3d& normal)
{
    double u = folded[0], v = folded[1], z = 1.0 - fabs(u) - fabs(v);
    if (fabs(u) > 1.0)
        return false;
    if (z < 0.0)
        normal = Vector3d(std::copysign(1.0 - fabs(v), u), std::copysign(1.0 - fabs(u), v), z);
    else
        normal = Vector3d(u, v, z);
    normal.normalize();
    return true;
}

std::shared_ptr<SubsurfaceCell> SubsurfaceCache::Acquire(const SubsurfaceCellKey& key, bool& build)
{
    std::lock_guard<std::mutex> lock(mutex);
    std::shared_ptr<SubsurfaceCell>& slot = cells[key];
    build = (slot == nullptr);
    if (build)
        slot = std::make_shared<SubsurfaceCell>();
    return slot;
}

SubsurfaceCache::SubsurfaceCache(double capMB, double spacing) :
    cap(size_t(std::max(1.0, capMB) * 1048576.0)),
    spacing(spacing)
{}

void SubsurfaceCache::Charge(SubsurfaceCell& cell)
{
    size_t bytes = cell.Bytes();
    {
        std::lock_guard<std::mutex> lock(mutex);
        staged -= std::min(staged, cell.staged);
        cell.staged = 0;
        cell.settled = true;
        if ((refused == 0) && (charged + bytes <= cap))
        {
            charged += bytes;
            cell.charged = bytes;
            peak = std::max(peak, charged + staged);
            return;
        }
        if (refused == 0)
        {
            refused = charged + bytes;
            refusedAt = progress;
        }
    }
    CheckCap();
}

void SubsurfaceCache::Stage(SubsurfaceCell& cell, size_t bytes)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (cell.settled)
        return;
    staged += bytes;
    cell.staged += bytes;
    peak = std::max(peak, charged + staged);
}

void SubsurfaceCache::Refund(SubsurfaceCell& cell)
{
    std::lock_guard<std::mutex> lock(mutex);
    charged -= std::min(charged, cell.charged);
    staged -= std::min(staged, cell.staged);
    cell.charged = cell.staged = 0;
    cell.settled = true;
}

bool SubsurfaceCache::CellFits(size_t bytes)
{
    if (bytes <= cap)
        return true;
    std::lock_guard<std::mutex> lock(mutex);
    if (refused == 0)
    {
        refused = bytes;
        refusedAt = progress;
    }
    return false;
}

std::atomic<std::uint64_t>& SubsurfaceCache::UnservedCounter(const void *object, SubsurfaceUnserved reason, const std::string& description)
{
    std::lock_guard<std::mutex> lock(mutex);
    std::unique_ptr<UnservedEntry>& entry = unserved[std::make_pair(object, int(reason))];
    if (!entry)
    {
        entry.reset(new UnservedEntry());
        entry->description = description;
    }
    return entry->count;
}

std::vector<SubsurfaceCache::UnservedObject> SubsurfaceCache::Unserved() const
{
    std::lock_guard<std::mutex> lock(mutex);
    std::vector<UnservedObject> list;
    for (const auto& entry : unserved)
        list.push_back({ entry.second->description, SubsurfaceUnserved(entry.first.second), entry.second->count.load() });
    std::sort(list.begin(), list.end(), [](const UnservedObject& a, const UnservedObject& b)
              { return (a.description != b.description) ? a.description < b.description : a.reason < b.reason; });
    return list;
}

bool SubsurfaceCache::ClosedMesh(const void *mesh, const std::function<bool()>& compute)
{
    std::lock_guard<std::mutex> lock(closedMeshesMutex);
    auto found = closedMeshes.find(mesh);
    if (found != closedMeshes.end())
        return found->second;
    return closedMeshes[mesh] = compute();
}

void SubsurfaceCache::CheckCap() const
{
    size_t needed;
    double at;
    {
        std::lock_guard<std::mutex> lock(mutex);
        needed = refused;
        at = refusedAt;
    }
    if (needed == 0)
        return;
    // A cap or spacing that would hold what was built so far; cloud memory goes about as 1/spacing^2.
    auto roundUp = [](double v) { double unit = std::pow(10.0, std::floor(std::log10(v)) - 1.0); return std::ceil(v / unit) * unit; };
    double neededMB = needed / 1048576.0, capMB = cap / 1048576.0;
    double coarser = roundUp(spacing * std::sqrt(neededMB / capMB));
    char text[640];
    std::snprintf(text, sizeof(text), "Subsurface clouds need at least %.4g MB at spacing %g after %.0f%% of the image, past their memory cap "
                  "of %.4g MB, so the render stopped. Raise the cap above memory %g, or set spacing %g or more (about %.3g MB for "
                  "what was built so far), in global_settings { subsurface { ... } }; the rest of the image will need more.",
                  neededMB, spacing, 100.0 * at, capMB, roundUp(neededMB), coarser, neededMB * (spacing / coarser) * (spacing / coarser));
    throw POV_EXCEPTION(kOutOfMemoryErr, text);
}

bool SubsurfaceCache::Progress(double fraction, double& heldMB, double& projectedMB, double& capMB)
{
    std::lock_guard<std::mutex> lock(mutex);
    progress = fraction;
    if (projected || !(fraction >= 0.25))
        return false;
    // Cells already built serve the rest of the image, so the clouds grow more slowly as it goes: the projection takes
    // the rate between a quarter and a half of the image, which still overshoots, rather than the first quarter's mean.
    if (firstFraction == 0.0)
    {
        firstFraction = fraction;
        firstCharged = charged;
        return false;
    }
    if (!(fraction >= 0.5) || !(fraction > firstFraction))
        return false;
    projected = true;
    heldMB = charged / 1048576.0;
    projectedMB = heldMB + (double(charged) - double(firstCharged)) / (fraction - firstFraction) * (1.0 - fraction) / 1048576.0;
    capMB = cap / 1048576.0;
    return (projectedMB > capMB);
}

SubsurfaceCloudStatistics SubsurfaceCache::Statistics() const
{
    SubsurfaceCloudStatistics stats;
    std::vector<std::shared_ptr<SubsurfaceCell>> all;
    {
        std::lock_guard<std::mutex> lock(mutex);
        stats.spacing = spacing;
        stats.peakBytes = double(peak);
        stats.chargedBytes = double(charged);
        all.reserve(cells.size());
        for (const auto& slot : cells)
            all.push_back(slot.second);
    }
    for (const std::shared_ptr<SubsurfaceCell>& cell : all)
    {
        std::lock_guard<std::mutex> cellLock(cell->mutex);
        if (cell->usable && !cell->points.empty())
        {
            double points = double(cell->points.size());
            stats.cells += 1.0;
            stats.points += points;
            stats.read += double(cell->PointsRead());
            stats.termCells[cell->term] += 1.0;
            stats.termPoints[cell->term] += points;
        }
    }
    return stats;
}

void SubsurfaceCache::SetCamera(const Vector3d& location, double pixelSize, double pixelAngle)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (cameraSet)
        return;
    cameraLocation = location;
    cameraPixelSize = pixelSize;
    cameraPixelAngle = pixelAngle;
    cameraSet = true;
}

bool SubsurfaceCache::GetCamera(Vector3d& location, double& pixelSize, double& pixelAngle) const
{
    std::lock_guard<std::mutex> lock(mutex);
    location = cameraLocation;
    pixelSize = cameraPixelSize;
    pixelAngle = cameraPixelAngle;
    return cameraSet;
}

// Splits the points until four or fewer remain.
static int BuildSubsurfaceNode(std::vector<SubsurfacePoint>& points, std::vector<SubsurfaceNode>& nodes, int first, int count)
{
    int index = int(nodes.size());
    nodes.emplace_back();
    SubsurfaceNode node;
    node.lo = node.hi = points[first].position;
    node.centre = Vector3d(0.0);
    node.area = 0.0f;
    Vector3d normal(0.0);
    for (int j = 0; j < MathColour::channels; j++)
        node.irradiance[j] = 0.0f;
    for (int i = first; i < first + count; i++)
    {
        const SubsurfacePoint& p = points[i];
        for (int a = 0; a < 3; a++)
        {
            node.lo[a] = std::min(node.lo[a], p.position[a]);
            node.hi[a] = std::max(node.hi[a], p.position[a]);
            normal[a] += p.normal[a] * p.area;
        }
        for (int j = 0; j < MathColour::channels; j++)
            node.irradiance[j] += p.irradiance[j] * p.area;
        node.centre += p.position * p.area;
        node.area += p.area;
    }
    node.centre /= std::max(double(node.area), 1e-30);
    normal = (normal.length() > 0.0) ? normal.normalized() : Vector3d(0.0);
    node.cone = 1.0f;
    for (int i = first; i < first + count; i++)
        node.cone = std::min(node.cone, float(points[i].normal[0] * normal[X] + points[i].normal[1] * normal[Y] + points[i].normal[2] * normal[Z]));
    for (int a = 0; a < 3; a++)
        node.normal[a] = float(normal[a]);
    if (count <= 4)
    {
        node.first = first;
        node.count = count;
        nodes[index] = node;
        return index;
    }
    // Points facing opposite ways (the two faces of a thin part) are split by facing first, so each face's groups can
    // be summed as one; otherwise at the median of the longest axis.
    int half = 0;
    if (node.cone < 0.0f)
    {
        const float d[3] = { points[first].normal[0], points[first].normal[1], points[first].normal[2] };
        half = int(std::partition(points.begin() + first, points.begin() + first + count, [&d](const SubsurfacePoint& p)
                                  { return p.normal[0] * d[0] + p.normal[1] * d[1] + p.normal[2] * d[2] >= 0.0f; }) - points.begin()) - first;
    }
    if ((half <= 0) || (half >= count))
    {
        Vector3d extent = node.hi - node.lo;
        int axis = (extent[X] >= extent[Y] && extent[X] >= extent[Z]) ? X : (extent[Y] >= extent[Z] ? Y : Z);
        half = count / 2;
        std::nth_element(points.begin() + first, points.begin() + first + half, points.begin() + first + count,
                         [axis](const SubsurfacePoint& a, const SubsurfacePoint& b) { return a.position[axis] < b.position[axis]; });
    }
    node.count = 0;
    nodes[index] = node;
    BuildSubsurfaceNode(points, nodes, first, half);
    nodes[index].first = BuildSubsurfaceNode(points, nodes, first + half, count - half);
    return index;
}

double SubsurfaceCell::Spacing(const Vector3d& normal) const
{
    return SubsurfaceSpacing(step, normal);
}

double SubsurfaceSpacing(const double step[3], const Vector3d& normal)
{
    double facing = 0.0, density = 0.0;
    for (int a = 0; a < 3; a++)
    {
        facing += fabs(normal[a]);
        density += fabs(normal[a]) / (step[a] * step[a]);
    }
    return (density > 0.0) ? sqrt(facing / density) : std::max(step[0], std::max(step[1], step[2]));
}

void SubsurfaceCell::BuildHierarchy()
{
    nodes.clear();
    if (!points.empty())
    {
        nodes.reserve(points.size());
        BuildSubsurfaceNode(points, nodes, 0, int(points.size()));
    }
    size_t words = (nodes.size() + 31) / 32;
    read.reset(new std::atomic<std::uint32_t>[words]);
    for (size_t i = 0; i < words; i++)
        read[i].store(0, std::memory_order_relaxed);
}

size_t SubsurfaceCell::Bytes() const
{
    return sizeof(SubsurfaceCell) + points.capacity() * sizeof(SubsurfacePoint) + nodes.capacity() * sizeof(SubsurfaceNode) +
           (visibility.capacity() + ambient.capacity() + photonEntry.capacity() + entry.capacity()) * sizeof(float) +
           (read ? (nodes.size() + 31) / 32 * sizeof(std::uint32_t) : 0) + rows.capacity() * sizeof(std::vector<SubsurfacePoint>);
}

void SubsurfaceCell::Release()
{
    std::vector<SubsurfacePoint>().swap(points);
    std::vector<SubsurfaceNode>().swap(nodes);
    std::vector<float>().swap(visibility);
    std::vector<float>().swap(ambient);
    std::vector<float>().swap(photonEntry);
    std::vector<float>().swap(entry);
    read.reset();
}

void SubsurfaceCell::MarkRead(int node) const
{
    std::uint32_t bit = std::uint32_t(1) << (node & 31);
    std::atomic<std::uint32_t>& word = read[node >> 5];
    if ((word.load(std::memory_order_relaxed) & bit) == 0)
        word.fetch_or(bit, std::memory_order_relaxed);
}

size_t SubsurfaceCell::PointsRead() const
{
    if (nodes.empty() || !read)
        return 0;
    std::function<size_t(int, bool)> count = [&](int index, bool used) -> size_t
    {
        const SubsurfaceNode& node = nodes[index];
        used = used || ((read[index >> 5].load(std::memory_order_relaxed) >> (index & 31)) & 1);
        if (node.count > 0)
            return used ? size_t(node.count) : 0;
        return count(index + 1, used) + count(node.first, used);
    };
    return count(0, false);
}

}
// end of namespace pov
