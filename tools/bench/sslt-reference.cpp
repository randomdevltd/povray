// sslt-reference Key=Value...: random-walk reference images of the diffuse subsurface light of tools/bench/sslt-reference.pov,
// Mode=forward measures a half-space; Mode=cylinder_point measures finite-body exits: tools/bench/sslt-cylinder-reference.md.

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <thread>
#include <vector>

struct V { double x, y, z; };
static V operator+(V a, V b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
static V operator-(V a, V b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
static V operator*(V a, double s) { return { a.x * s, a.y * s, a.z * s }; }
static double Dot(V a, V b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static V Cross(V a, V b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
static V Unit(V a) { return a * (1.0 / std::sqrt(Dot(a, a))); }
static V RotateY(V p, double degrees)
{
    double a = degrees * M_PI / 180.0, c = std::cos(a), s = std::sin(a);
    return { p.x * c + p.z * s, p.y, -p.x * s + p.z * c };
}

// Parts are signed distance bounds (negative inside): a box (turned about y), a capped cylinder along y, a sphere, a square
// rod along x turned 45° about it, and the ridged trunk of sslt-wide.pov's bark.
enum Kind { kBox, kCylinder, kSphere, kDiamond, kTrunk };
struct Part { Kind kind; V c, e; double angle; V lo, hi; };
static std::vector<Part> solids, holes;

static void Add(std::vector<Part>& to, Kind kind, V c, V e, double angle = 0.0)
{
    double reach = std::sqrt(Dot(e, e));
    V half = kind == kSphere ? V{ e.x, e.x, e.x } : kind == kCylinder ? V{ e.x, e.y, e.x } : kind == kDiamond ? V{ e.x, e.y, e.y } :
             kind == kTrunk ? V{ 1.1 * e.x, 0.5 * e.y, 1.1 * e.x } : angle != 0.0 ? V{ reach, reach, reach } : e;
    V mid = kind == kTrunk ? c + V{ 0.0, 0.5 * e.y, 0.0 } : c;
    to.push_back({ kind, c, e, angle, mid - half, mid + half });
}

static double Box(V q, V e)
{
    V d = { std::fabs(q.x) - e.x, std::fabs(q.y) - e.y, std::fabs(q.z) - e.z };
    V o = { std::max(d.x, 0.0), std::max(d.y, 0.0), std::max(d.z, 0.0) };
    return std::sqrt(Dot(o, o)) + std::min(std::max(d.x, std::max(d.y, d.z)), 0.0);
}

static double PartDistance(const Part& p, V q)
{
    V l = q - p.c;
    switch (p.kind)
    {
        case kBox:
            return Box(p.angle != 0.0 ? RotateY(l, -p.angle) : l, p.e);
        case kCylinder:
        {
            double dr = std::hypot(l.x, l.z) - p.e.x, dy = std::fabs(l.y) - p.e.y;
            return std::min(std::max(dr, dy), 0.0) + std::hypot(std::max(dr, 0.0), std::max(dy, 0.0));
        }
        case kSphere:
            return std::sqrt(Dot(l, l)) - p.e.x;
        case kDiamond:
            return std::max(std::fabs(l.x) - p.e.x, (std::fabs(l.y) + std::fabs(l.z) - p.e.y) * M_SQRT1_2);
        case kTrunk:
        {
            // r(θ, y) as the Trunk macro's vertices; the bound divides by the gradient's largest magnitude where ρ > R/2.
            double radius = p.e.x, height = p.e.y, rho = std::hypot(l.x, l.z), radial;
            if (rho < 0.5 * radius)
                radial = rho - 0.81 * radius;
            else
            {
                double t = std::atan2(l.z, l.x), y = l.y;
                double r = radius * (1.0 + 0.06 * std::sin(9.0 * t + 0.7 * y) + 0.03 * std::sin(31.0 * t - 2.3 * y) - 0.1 * y / height);
                double slope = (0.06 * 9.0 + 0.03 * 31.0) / 0.5, rise = radius * (0.06 * 0.7 + 0.03 * 2.3 + 0.1 / height);
                radial = (rho - r) / std::sqrt(1.0 + slope * slope + rise * rise);
            }
            return std::max(radial, std::max(-l.y, l.y - height));
        }
    }
    return 1e30;
}

static double Nearest(const std::vector<Part>& parts, V q)
{
    double best = 1e30;
    for (const Part& p : parts)
    {
        V o = { std::max(std::max(p.lo.x - q.x, q.x - p.hi.x), 0.0), std::max(std::max(p.lo.y - q.y, q.y - p.hi.y), 0.0),
                std::max(std::max(p.lo.z - q.z, q.z - p.hi.z), 0.0) };
        if (best > 0.0 && Dot(o, o) >= best * best)
            continue;
        best = std::min(best, PartDistance(p, q));
    }
    return best;
}

static double Distance(V q)
{
    double d = Nearest(solids, q);
    return holes.empty() ? d : std::max(d, -Nearest(holes, q));
}

static V Normal(V q)
{
    const double h = 1e-7;
    return Unit({ Distance(q + V{ h, 0, 0 }) - Distance(q - V{ h, 0, 0 }), Distance(q + V{ 0, h, 0 }) - Distance(q - V{ 0, h, 0 }),
                  Distance(q + V{ 0, 0, h }) - Distance(q - V{ 0, 0, h }) });
}

// Where the ray from p leaves the side it starts on within maxT, bracketed: tNear on that side, tFar past the surface. Steps
// are over-relaxed sphere tracing, retried plainly wherever the two bounds leave a gap (Keinert et al. 2014).
static bool Crossing(V p, V d, double maxT, bool inside, double& tNear, double& tFar)
{
    double last = 0.0, lastV = Distance(p), omega = 1.6;
    if (std::fabs(lastV) >= maxT)
        return false;
    for (int i = 0; i < 1000000; i++)
    {
        double t = std::min(maxT, last + std::max(omega * std::fabs(lastV), 1e-7)), v = Distance(p + d * t);
        if (omega > 1.0 && std::fabs(lastV) + std::fabs(v) < t - last)
        {
            omega = 1.0;
            continue;
        }
        omega = 1.6;
        if ((v < 0.0) != inside)
        {
            double lo = last, hi = t;
            for (int k = 0; k < 64 && hi - lo > 1e-10; k++)
            {
                double mid = 0.5 * (lo + hi);
                ((Distance(p + d * mid) < 0.0) == inside ? lo : hi) = mid;
            }
            tNear = lo;
            tFar = hi;
            return true;
        }
        if (t >= maxT)
            return false;
        last = t;
        lastV = v;
    }
    return false;
}

// Transmittance into a medium of relative index eta at cosine c (unpolarised), as the renderer's ComputeFt.
static double Ft(double c, double eta)
{
    double g = std::sqrt(eta * eta + c * c - 1.0), f = 0.5 * std::pow(g - c, 2) / std::pow(g + c, 2);
    f *= 1.0 + std::pow(c * (g + c) - 1.0, 2) / std::pow(c * (g - c) + 1.0, 2);
    return 1.0 - std::min(1.0, std::max(0.0, f));
}

static double FresnelInside(double c, double eta)
{
    double s2 = eta * eta * (1.0 - c * c);
    if (s2 >= 1.0)
        return 1.0;
    double ct = std::sqrt(1.0 - s2), rs = (eta * c - ct) / (eta * c + ct), rp = (c - eta * ct) / (c + eta * ct);
    return 0.5 * (rs * rs + rp * rp);
}

// The renderer's reduced albedo: its classic Fdr, a 100-entry table of the inverted Rd and linear interpolation.
static double ReducedAlbedo(double reflectance, double ior)
{
    double fdr = ((1.0 - 1.0 / ior) + 3.0 * std::pow(1.0 - 1.0 / ior, 4.5)) / 4.0, a = (1.0 + fdr) / (1.0 - fdr);
    auto node = [&](int i)
    {
        if (i <= 0 || i >= 100)
            return i <= 0 ? 0.0 : 1.0;
        double lo = 0.0, hi = 1.0;
        for (int k = 0; k < 100; k++)
        {
            double m = 0.5 * (lo + hi), root = std::sqrt(3.0 * (1.0 - m));
            ((m / 2.0) * (1.0 + std::exp(-(4.0 / 3.0) * a * root)) * std::exp(-root) < i / 100.0 ? lo : hi) = m;
        }
        return 0.5 * (lo + hi);
    };
    double rd = std::min(1.0, std::max(0.0, reflectance)) * 100.0, i0 = std::floor(rd), p = rd - i0;
    return (1.0 - p) * node(int(i0)) + p * node(int(std::ceil(rd)));
}

// Albedo=r,g,b[:r,g,b...] (one value for all channels allowed, "table" the renderer's): media by their reduced albedos.
static std::vector<std::vector<double>> albedos;
static double MediumAlbedo(size_t k, int j, double reflectance, double ior)
{
    return (albedos.empty() || albedos[k].empty()) ? ReducedAlbedo(reflectance, ior) : albedos[k][albedos[k].size() == 1 ? 0 : j];
}

static std::vector<std::string> Split(const std::string& text, char by)
{
    std::vector<std::string> parts(1);
    for (char c : text)
        if (c == by)
            parts.emplace_back();
        else
            parts.back().push_back(c);
    return parts;
}

static uint64_t Mix(uint64_t z)
{
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}
struct Rng { uint64_t s; double operator()() { s += 0x9E3779B97F4A7C15ull; return double(Mix(s) >> 11) * (1.0 / 9007199254740992.0); } };

static V Isotropic(Rng& rng)
{
    double z = 2.0 * rng() - 1.0, phi = 2.0 * M_PI * rng(), s = std::sqrt(std::max(0.0, 1.0 - z * z));
    return { s * std::cos(phi), s * std::sin(phi), z };
}

struct Light { V at; double colour[3]; };
static std::vector<Light> lights;
static double eta = 1.5;

struct Medium { double sigmaT, albedo; };
static std::atomic<long> entriesFound(0), entriesMissed(0);

// The fluence at p of each light's refracted, unscattered beam, entering where the light refracts towards p (iterated from where
// the line to the light leaves): Ft·colour·cos per unit of surface, as point lights do not fall off; see doc/PERF.md.
static double Fluence(V p, int j, const Medium& m, Rng& rng)
{
    double bound = std::exp(-m.sigmaT * std::fabs(Distance(p))), scale = 1.0;
    if (bound < 0.05)
    {
        if (rng() * 0.05 >= bound)
            return 0.0;
        scale = 0.05 / bound;
    }
    double phi = 0.0;
    for (const Light& l : lights)
    {
        V to = l.at - p, y, n, back;
        double tn, tf, cl = 0.0, ct = 0.0, k = 1.0 / eta;
        if (!Crossing(p, Unit(to), std::sqrt(Dot(to, to)), true, tn, tf))
            continue;
        y = p + Unit(to) * tf;
        bool found = false;
        for (int i = 0; i < 16 && !found; i++)
        {
            n = Normal(y);
            V lit = Unit(l.at - y);
            cl = Dot(lit, n);
            if (cl <= 0.0)
                break;
            ct = std::sqrt(std::max(0.0, 1.0 - k * k * (1.0 - cl * cl)));
            back = Unit(lit * k - n * (k * cl - ct));
            if (!Crossing(p, back, 1e3, true, tn, tf))
                break;
            V next = p + back * tf;
            found = Dot(next - y, next - y) < 1e-18;
            y = next;
        }
        if (!found)
        {
            entriesMissed.fetch_add(1, std::memory_order_relaxed);
            continue;
        }
        entriesFound.fetch_add(1, std::memory_order_relaxed);
        V lit = l.at - y;
        double dist = std::sqrt(Dot(lit, lit)), sn, sf;
        if (!Crossing(y + n * 1e-9, lit * (1.0 / dist), dist, false, sn, sf))
            phi += l.colour[j] * Ft(cl, eta) * cl / ct * std::exp(-m.sigmaT * tn);
    }
    return phi * scale;
}

// Light the surface at y (outward normal n) transmits per unit area: colour, cosine and Fresnel, as the renderer takes it.
static double Entering(V y, V n, int j)
{
    double e = 0.0;
    for (const Light& l : lights)
    {
        V to = l.at - y;
        double dist = std::sqrt(Dot(to, to)), c = Dot(to, n) / dist, tn, tf;
        if (c > 0.0 && !Crossing(y + n * 1e-9, to * (1.0 / dist), dist, false, tn, tf))
            e += l.colour[j] * c * Ft(c, eta);
    }
    return e;
}

// One walk from just inside x along d, scoring light scattered two or more times, per unit of Ft out: by default where it
// leaves, the light entering there taken as diffuse of the same transmitted flux; with beams, each beam at every collision.
static bool beams = false;
static double transmitted = 1.0;
static double Walk(V x, V d, int j, const Medium& m, Rng& rng)
{
    V p = x;
    double weight = 1.0, sum = 0.0;
    for (int order = 1, step = 0; step < 10000000; step++)
    {
        double s = -std::log(1.0 - rng()) / m.sigmaT, tn, tf;
        if (!Crossing(p, d, s, true, tn, tf))
        {
            p = p + d * s;
            if (beams && order >= 2)
                sum += weight * m.albedo * Fluence(p, j, m, rng) / (4.0 * eta * eta);
            order++;
            // Absorption ends walks by survival, one in ten collisions at least with the weight making up the rest.
            double survive = std::max(m.albedo, 0.9);
            if (rng() >= survive)
                return sum;
            d = Isotropic(rng);
            weight *= m.albedo / survive;
            if (weight < 0.05)
            {
                if (rng() < 0.5)
                    return sum;
                weight *= 2.0;
            }
            continue;
        }
        V n = Normal(p + d * (0.5 * (tn + tf)));
        double c = Dot(n, d);
        if (c < 0.0)
        {
            n = n * -1.0;
            c = -c;
        }
        if (rng() >= FresnelInside(c, eta))
            return (!beams && order > 2) ? weight * Entering(p + d * tf, n, j) / transmitted : sum;
        p = p + d * tn;
        d = d - n * (2.0 * c);
    }
    return sum;
}

// The same walk for several media of one reduced scattering coefficient at once: flights scatter only, and each medium's
// score is the walk's times exp(-σa·length), which is the analog walk's expectation for that medium (diffuse entry only).
static void WalkMedia(V x, V d, int j, double sigmaS, const std::vector<double>& sigmaA, Rng& rng, double *score)
{
    V p = x;
    double weight = 1.0, length = 0.0, least = *std::min_element(sigmaA.begin(), sigmaA.end());
    std::fill(score, score + sigmaA.size(), 0.0);
    for (int order = 1, step = 0; step < 10000000; step++)
    {
        double s = -std::log(1.0 - rng()) / sigmaS, tn, tf;
        if (!Crossing(p, d, s, true, tn, tf))
        {
            p = p + d * s;
            length += s;
            order++;
            d = Isotropic(rng);
            if (weight * std::exp(-least * length) < 0.05)
            {
                if (rng() < 0.5)
                    return;
                weight *= 2.0;
            }
            continue;
        }
        V n = Normal(p + d * (0.5 * (tn + tf)));
        double c = Dot(n, d);
        if (c < 0.0)
        {
            n = n * -1.0;
            c = -c;
        }
        if (rng() >= FresnelInside(c, eta))
        {
            if (order > 2)
            {
                double e = weight * Entering(p + d * tf, n, j) / transmitted;
                for (size_t k = 0; k < sigmaA.size(); k++)
                    score[k] = e * std::exp(-sigmaA[k] * (length + tn));
            }
            return;
        }
        p = p + d * tn;
        length += tn;
        d = d - n * (2.0 * c);
    }
}

struct Material { double pigment[3], diffuse, mfp[3], ior; };
static const Material kMaterials[] = {
    { { 0.85, 0.12, 0.08 }, 0.75, { 3.0, 1.6, 0.9 }, 1.5 },
    { { 1.2, 1.2, 1.2 }, 0.75, { 2.0, 2.0, 2.0 }, 1.5 },
    { { 0.85, 0.55, 0.12 }, 0.6, { 2.5, 1.5, 0.5 }, 1.45 },
    { { 0.55, 0.42, 0.3 }, 0.7, { 1.2, 0.8, 0.5 }, 1.4 },
    { { 0.9, 0.6, 0.3 }, 0.7, { 3.0, 2.0, 1.0 }, 1.4 },
};

static void Studs(int bare, int blocks)
{
    const double sink = 0.002, radius = 0.24, height = 0.17;
    auto box = [](V lo, V hi) { Add(solids, kBox, (lo + hi) * 0.5, (hi - lo) * 0.5); };
    if (blocks)
    {
        box({ -2.4, 0, -1.6 }, { 0, 0.6, 1.6 });
        box({ 0.05, 0, -1.6 }, { 2.4, 0.35, 1.6 });
        return;
    }
    box({ -2.4, 0, -1.6 }, { 2.4, 0.4, 1.6 });
    if (bare)
        return;
    auto stud = [&](double x, double z) { Add(solids, kCylinder, { x, 0.4 + 0.5 * (height - sink), z }, { radius, 0.5 * (height + sink), 0 }); };
    for (int i = 0; i <= 4; i++) stud(-2.0 + i * 0.8, -1.1);
    for (int i = 0; i <= 6; i++) stud(-2.0 + i * 0.56, -0.15);
    for (int i = 0; i <= 6; i++) stud(-2.0 + i * 0.51, 0.75);
    box({ -2.4, 0.4 - sink, 1.2 }, { 2.4, 0.75, 1.6 });
    box({ 1.9, 0.4 - sink, -1.6 }, { 2.1, 0.75, 1.2 + sink });
    box({ 2.2, 0.4 - sink, -1.6 }, { 2.4, 0.75, 1.2 + sink });
    box({ 2.1 - 1e-9, 0.4 - sink, -1.6 }, { 2.2 + 1e-9, 0.5, 1.2 + sink });
}

static void WritePfm(const char *path, const std::vector<double>& image, int width, int height)
{
    FILE *out = std::fopen(path, "wb");
    if (!out)
    {
        std::perror(path);
        std::exit(1);
    }
    std::fprintf(out, "PF\n%d %d\n-1.0\n", width, height);
    for (int row = height - 1; row >= 0; row--)
        for (int i = 0; i < width * 3; i++)
        {
            float f = float(image[size_t(row) * width * 3 + i]);
            std::fwrite(&f, 4, 1, out);
        }
    std::fclose(out);
}

// Photons into the half-space y < 0 (or a layer Thickness mm deep) at Angle degrees: the light leaving the top per unit irradiance,
// all of it and after two or more scatterings, and π times its radiance within acos(Cone) of the normal, as a pixel seen head on.
static int Forward(const std::map<std::string, std::string>& args, const Material& mat, double mm)
{
    long photons = std::atol(args.count("Photons") ? args.at("Photons").c_str() : "4000000");
    double angle = std::atof(args.count("Angle") ? args.at("Angle").c_str() : "0") * M_PI / 180.0, ci = std::cos(angle);
    double st = std::sin(angle) / eta, ct = std::sqrt(1.0 - st * st);
    double depth = args.count("Thickness") ? std::atof(args.at("Thickness").c_str()) / mm : 1e30;
    double cone = args.count("Cone") ? std::atof(args.at("Cone").c_str()) : 0.9;
    std::printf("{\"photons\": %ld, \"angle\": %g, \"channels\": [", photons, angle * 180.0 / M_PI);
    for (int j = 0; j < 3; j++)
    {
        double albedo = MediumAlbedo(0, j, mat.pigment[j] * mat.diffuse, mat.ior), sigmaT = mm / mat.mfp[j] / albedo;
        unsigned count = args.count("Threads") ? unsigned(std::max(1, std::atoi(args.at("Threads").c_str()))) : std::max(1u, std::thread::hardware_concurrency());
        std::vector<std::thread> threads;
        std::vector<double> tally(size_t(count) * 4, 0.0);
        for (unsigned t = 0; t < count; t++)
            threads.emplace_back([&, t]()
            {
                Rng rng{ Mix(0x5EEDull + t * 0x9E3779B97F4A7C15ull + j) };
                double *sum = &tally[size_t(t) * 4];
                for (long i = t; i < photons; i += count)
                {
                    if (rng() >= Ft(ci, eta))
                        continue;
                    V p = { 0, 0, 0 }, d = { st, -ct, 0 };
                    double weight = 1.0;
                    for (int order = 0;;)
                    {
                        double s = -std::log(1.0 - rng()) / sigmaT;
                        if (d.y > 0.0 && p.y + d.y * s >= 0.0)
                        {
                            p = p + d * (-p.y / d.y);
                            if (rng() < FresnelInside(d.y, eta))
                            {
                                d.y = -d.y;
                                continue;
                            }
                            bool headOn = std::sqrt(std::max(0.0, 1.0 - eta * eta * (1.0 - d.y * d.y))) >= cone;
                            sum[0] += weight;
                            sum[1] += order >= 2 ? weight : 0.0;
                            sum[2] += headOn ? weight : 0.0;
                            sum[3] += (headOn && order >= 2) ? weight : 0.0;
                            break;
                        }
                        if (d.y < 0.0 && p.y + d.y * s <= -depth)
                        {
                            p = p + d * ((-depth - p.y) / d.y);
                            if (rng() >= FresnelInside(-d.y, eta))
                                break;
                            d.y = -d.y;
                            continue;
                        }
                        p = p + d * s;
                        d = Isotropic(rng);
                        weight *= albedo;
                        order++;
                        if (weight < 0.05)
                        {
                            if (rng() < 0.5)
                                break;
                            weight *= 2.0;
                        }
                    }
                }
            });
        for (std::thread& t : threads)
            t.join();
        double sum[4] = {};
        for (unsigned t = 0; t < count; t++)
            for (int k = 0; k < 4; k++)
                sum[k] += tally[size_t(t) * 4 + k];
        std::printf("%s{\"albedo\": %.9g, \"all\": %.6g, \"multiple\": %.6g, \"head_on_all\": %.6g, \"head_on_multiple\": %.6g}", j ? ", " : "",
                    albedo, sum[0] / photons, sum[1] / photons, sum[2] / photons / (1.0 - cone * cone), sum[3] / photons / (1.0 - cone * cone));
    }
    std::printf("]}\n");
    return 0;
}


struct CylinderHit { double t; V n; int surface; };
static bool CylinderIntersection(V p, V d, double radius, double height, CylinderHit& hit)
{
    hit.t = 1e300;
    auto candidate = [&](double t, V n, int surface)
    {
        if (t > 1e-9 && t < hit.t) hit = { t, n, surface };
    };
    if (std::fabs(d.y) > 1e-15)
        for (int cap = 0; cap < 2; cap++)
        {
            double y = cap ? height : 0.0, t = (y - p.y) / d.y;
            V q = p + d * t;
            if (q.x * q.x + q.z * q.z <= radius * radius)
                candidate(t, { 0, cap ? 1.0 : -1.0, 0 }, cap ? 0 : 2);
        }
    double a = d.x * d.x + d.z * d.z, b = p.x * d.x + p.z * d.z;
    double disc = b * b - a * (p.x * p.x + p.z * p.z - radius * radius);
    if (a > 1e-30 && disc >= 0)
        for (double t : { (-b - std::sqrt(disc)) / a, (-b + std::sqrt(disc)) / a })
        {
            V q = p + d * t;
            if (q.y >= 0 && q.y <= height)
                candidate(t, Unit({ q.x, 0, q.z }), 1);
        }
    return hit.t < 1e299;
}

static int CylinderForward(const std::map<std::string, std::string>& args)
{
    auto number = [&](const char *key, double fallback) { auto it = args.find(key); return it == args.end() ? fallback : std::atof(it->second.c_str()); };
    long photons = long(number("Photons", 1000000));
    int threads = int(number("Threads", 2)), bins = int(number("Bins", 16)), maxSteps = int(number("MaxSteps", 1000000));
    double radius = number("Radius", 10.5), height = number("Height", 160), above = number("Above", 21);
    double flux = number("Flux", 12), scale = number("Scale", 1), relativeIor = number("Eta", 1.44 / 1.000277);
    uint64_t seed = uint64_t(number("Seed", 24301));
    if (photons < 1 || threads < 1 || threads > 256 || bins < 1 || bins > 10000 || maxSteps < 1 ||
        radius <= 0 || height <= 0 || above <= 0 || flux <= 0 || scale <= 0 || relativeIor < 1)
    {
        std::fprintf(stderr, "invalid cylinder parameters\n");
        return 2;
    }
    const double sigmaS[3] = { .125, .2, 1.0 / 3.0 }, sigmaA[3] = { .000437339, .001065426, .003168666 };
    const double coneMu[] = { .9, .95, .99 };
    const char *category[] = { "miss", "entry_reflected", "absorbed", "ballistic", "single", "multiple", "unresolved" };
    double fraction = .5 * (1 - above / std::hypot(above, radius));
    std::printf("{\"mode\":\"cylinder_point\",\"units\":\"mm\",\"provenance\":\"independent analog random walk; supplied isotropic wax coefficients; point source only\","
                "\"photons_per_channel\":%ld,\"threads\":%d,\"seed\":%llu,\"max_steps\":%d,\"radius\":%.12g,\"height\":%.12g,\"source_above_top\":%.12g,"
                "\"source_flux_lm_per_channel\":%.12g,\"scale\":%.12g,\"eta\":%.12g,\"analytic_intercept_fraction\":%.12g,\"channels\":[",
                photons, threads, (unsigned long long)seed, maxSteps, radius, height, above, flux, scale, relativeIor, fraction);
    for (int j = 0; j < 3; j++)
    {
        double sa = sigmaA[j] / scale, ss = sigmaS[j] / scale, st = sa + ss;
        size_t profileSize = size_t(3 * 3 * bins);
        std::vector<std::vector<uint64_t>> ledgers(threads, std::vector<uint64_t>(7)), profiles(threads, std::vector<uint64_t>(profileSize));
        std::vector<std::vector<uint64_t>> coneProfiles(threads, std::vector<uint64_t>(3 * profileSize));
        std::vector<std::thread> workers;
        for (int worker = 0; worker < threads; worker++)
            workers.emplace_back([&, worker]()
            {
                auto& ledger = ledgers[worker]; auto& profile = profiles[worker]; auto& cones = coneProfiles[worker];
                for (long i = worker; i < photons; i += threads)
                {
                    Rng rng{ Mix(seed ^ Mix(uint64_t(i)) ^ Mix(uint64_t(j) + 100)) };
                    V p = { 0, height + above, 0 }, d = Isotropic(rng);
                    CylinderHit hit;
                    if (!CylinderIntersection(p, d, radius, height, hit)) { ledger[0]++; continue; }
                    p = p + d * hit.t;
                    double c = -Dot(d, hit.n);
                    if (rng() >= Ft(c, relativeIor)) { ledger[1]++; continue; }
                    double ratio = 1 / relativeIor, ct = std::sqrt(1 - ratio * ratio * (1 - c * c));
                    d = Unit(d * ratio + hit.n * (ratio * c - ct));
                    int order = 0; bool complete = false;
                    for (int step = 0; step < maxSteps; step++)
                    {
                        if (!CylinderIntersection(p, d, radius, height, hit)) break;
                        double flight = -std::log1p(-rng()) / st;
                        if (flight < hit.t)
                        {
                            p = p + d * flight;
                            if (rng() < sa / st) { ledger[2]++; complete = true; break; }
                            order++; d = Isotropic(rng);
                        }
                        else
                        {
                            p = p + d * hit.t;
                            c = std::max(0.0, std::min(1.0, Dot(d, hit.n)));
                            if (rng() < FresnelInside(c, relativeIor))
                                d = Unit(d - hit.n * (2 * c));
                            else
                            {
                                int bucket = std::min(order, 2), bin = std::min(bins - 1, std::max(0, int(p.y / height * bins)));
                                size_t index = (hit.surface * 3 + bucket) * bins + bin;
                                ledger[3 + bucket]++; profile[index]++;
                                double muOut = std::sqrt(std::max(0.0, 1 - relativeIor * relativeIor * (1 - c * c)));
                                for (int cone = 0; cone < 3; cone++)
                                    if (muOut >= coneMu[cone]) cones[cone * profileSize + index]++;
                                complete = true; break;
                            }
                        }
                    }
                    if (!complete) ledger[6]++;
                }
            });
        for (auto& worker : workers) worker.join();
        std::vector<uint64_t> ledger(7), profile(profileSize), cones(3 * profileSize);
        for (int worker = 0; worker < threads; worker++)
        {
            for (int k = 0; k < 7; k++) ledger[k] += ledgers[worker][k];
            for (size_t k = 0; k < profileSize; k++) profile[k] += profiles[worker][k];
            for (size_t k = 0; k < cones.size(); k++) cones[k] += coneProfiles[worker][k];
        }
        uint64_t closure = 0; for (auto n : ledger) closure += n;
        double intercepted = double(photons - ledger[0]) / photons;
        std::printf("%s{\"channel\":%d,\"sigma_s_per_mm\":%.12g,\"sigma_a_per_mm\":%.12g,\"g\":0,\"intercepted_count\":%llu,\"intercepted_flux_lm\":%.12g,"
                    "\"intercept_fraction_se\":%.12g,\"closure_count\":%llu,\"ledger\":{", j ? "," : "", j, ss, sa,
                    (unsigned long long)(photons - ledger[0]), flux * intercepted, std::sqrt(intercepted * (1 - intercepted) / photons), (unsigned long long)closure);
        for (int k = 0; k < 7; k++)
        {
            double f = double(ledger[k]) / photons;
            std::printf("%s\"%s\":{\"count\":%llu,\"fraction_emitted\":%.12g,\"flux_lm\":%.12g,\"flux_se_lm\":%.12g}", k ? "," : "", category[k],
                        (unsigned long long)ledger[k], f, flux * f, flux * std::sqrt(f * (1 - f) / photons));
        }
        std::printf("},\"profile_order\":\"surface(top,side,bottom),order(0,1,2+),height_bin(bottom_to_top)\",\"height_bins\":%d,\"exit_counts\":[", bins);
        for (size_t k = 0; k < profileSize; k++) std::printf("%s%llu", k ? "," : "", (unsigned long long)profile[k]);
        std::printf("],\"normal_cones\":[");
        for (int cone = 0; cone < 3; cone++)
        {
            double zeroUpper = flux * -std::expm1(std::log(.05) / photons);
            std::printf("%s{\"mu_min\":%.12g,\"zero_count_flux_upper95_lm\":%.12g,\"counts\":[", cone ? "," : "", coneMu[cone], zeroUpper);
            for (size_t k = 0; k < profileSize; k++)
                std::printf("%s%llu", k ? "," : "", (unsigned long long)cones[cone * profileSize + k]);
            std::printf("],\"flux_lm\":[");
            for (size_t k = 0; k < profileSize; k++)
                std::printf("%s%.12g", k ? "," : "", flux * double(cones[cone * profileSize + k]) / photons);
            std::printf("],\"flux_se_lm\":[");
            for (size_t k = 0; k < profileSize; k++)
            {
                double f = double(cones[cone * profileSize + k]) / photons;
                std::printf("%s%.12g", k ? "," : "", flux * std::sqrt(f * (1 - f) / photons));
            }
            std::printf("]}");
        }
        std::printf("]}");
    }
    std::printf("]}\n");
    return 0;
}

int main(int argc, char **argv)
{
    std::map<std::string, std::string> args;
    for (int i = 1; i < argc; i++)
    {
        const char *eq = std::strchr(argv[i], '=');
        if (!eq)
        {
            std::fprintf(stderr, "usage: sslt-reference Shape=N Material=N [Entry=beam View= Zoom= Bare= Blocks= Depth= W= H= Walks= Threads= Out=x.pfm Err=x.pfm]"
                                 " [Albedo=r,g,b[:r,g,b...] with Out= and Err= a comma list, one per medium] (Err: standard errors, -1 off the object)\n"
                                 "       sslt-reference Mode=forward Material=N [Albedo=r,g,b Photons= Angle= Thickness=mm Cone=cos Threads=]\n"
                                 "       sslt-reference Mode=cylinder_point [Photons= Threads=2 Scale=1 Flux=12 Seed=24301 Radius=10.5 Height=160 Above=21 Bins=16 MaxSteps=1000000]\n");
            return 2;
        }
        args[std::string(argv[i], size_t(eq - argv[i]))] = eq + 1;
    }
    auto number = [&](const char *key, double fallback) { return args.count(key) ? std::atof(args[key].c_str()) : fallback; };
    if (args.count("Mode") && args["Mode"] == "cylinder_point")
        return CylinderForward(args);
    int shape = std::min(4, std::max(0, int(number("Shape", 0)))), view = int(number("View", 0)), zoom = int(number("Zoom", 1));
    const Material& mat = kMaterials[std::min(4, std::max(0, int(number("Material", 0))))];
    eta = mat.ior;
    const double mmPerUnit[] = { 10.0, 10.0, 80.0, 40.0, 10.0 };
    double mm = mmPerUnit[shape];
    if (args.count("Albedo"))
        for (const std::string& medium : Split(args["Albedo"], ':'))
        {
            albedos.emplace_back();
            for (const std::string& value : Split(medium, ','))
                if (value != "table")
                    albedos.back().push_back(std::atof(value.c_str()));
        }
    if (args.count("Mode") && args["Mode"] == "forward")
        return Forward(args, mat, mm);
    int width = int(number("W", 160)), height = int(number("H", 120)), walks = int(number("Walks", 1024));
    V eye, target;
    double angle;
    Light warm = { { -3, 5, -2.5 }, { 1.2, 0.93 * 1.2, 0.85 * 1.2 } }, cool = { { 3.5, 2.2, 1.0 }, { 0.35, 0.4, 0.5 } };
    if (shape == 0)
    {
        double depth = number("Depth", 24);
        Add(solids, kBox, { 0, -0.5 * depth, 0 }, { 12, 0.5 * depth, 12 });
        if (view == 1)
            lights.push_back({ { 0, 1000, 0 }, { 1, 1, 1 } });
        else
            lights = { warm, cool };
        eye = view == 1 ? V{ 0, 4, -0.01 } : V{ 0, 2.5, -3 };
        target = { 0, 0, 0 };
        angle = view == 1 ? 30 : 40;
    }
    else if (shape == 1)
    {
        Add(solids, kBox, { 0, -1, 0 }, { 2.4, 1, 1.6 });
        Add(holes, kDiamond, { 0, 0, -0.6 }, { 3, 0.2, 0 });
        Add(holes, kSphere, { 0.9, 0.12, 0.5 }, { 0.3, 0, 0 });
        lights = { warm, cool };
        eye = { 0.8, 3.5, -3.5 };
        target = { 0.3, -0.1, -0.05 };
        angle = 30.0 / zoom;
    }
    else if (shape == 2)
    {
        Add(solids, kTrunk, { 0, 0, 0 }, { 0.35, 2, 0 });
        lights = { { { -3, 4, -4 }, { 1.2, 1.08, 0.96 } }, { { 4, 1.5, 2 }, { 0.3, 0.35, 0.5 } } };
        eye = { 0, 1, -2 };
        target = { 0, 1, 0 };
        angle = 36.0 / zoom;
    }
    else if (shape == 3)
    {
        Add(solids, kBox, { 0, 0.6, 0 }, { 1, 0.6, 0.015 }, 30.0);
        lights = { { { -2, 3, -3 }, { 1, 0.93, 0.85 } }, { { 1.5, 2, 3 }, { 0.56, 0.64, 0.8 } } };
        eye = { 0.6, 1, -3 };
        target = { 0, 0.6, 0 };
        angle = 40.0 / zoom;
    }
    else
    {
        Studs(int(number("Bare", 0)), int(number("Blocks", 0)));
        lights = { warm, cool };
        eye = { 0.8, 4.2, -4.2 };
        target = view == 1 ? V{ 2.15, 0.55, -0.6 } : view == 2 ? V{ -1.49, 0.5, 0.75 } : view == 3 ? V{ 0.02, 0.4, -0.4 } : zoom == 1 ? V{ 0, 0.3, 0.1 } : V{ -1.7, 0.5, -0.1 };
        angle = 42.0 / zoom;
    }
    size_t kinds = std::max<size_t>(1, albedos.size());
    std::vector<Medium> media(kinds * 3);
    for (size_t k = 0; k < kinds; k++)
        for (int j = 0; j < 3; j++)
        {
            media[k * 3 + j].albedo = MediumAlbedo(k, j, mat.pigment[j] * mat.diffuse, mat.ior);
            media[k * 3 + j].sigmaT = mm / mat.mfp[j] / media[k * 3 + j].albedo;
        }
    // A pixel is π times the radiance: Ft out over η² times the radiance inside, where diffuse light of transmitted flux E is
    // η² E / (π (1 - F̄dr)) and a beam of fluence φ scatters σs φ / 4π.
    beams = args.count("Entry") && args["Entry"] == "beam";
    if (beams && kinds > 1)
    {
        std::fprintf(stderr, "several media take diffuse entry only\n");
        return 2;
    }
    transmitted = 0.0;
    for (int i = 0; i < 100000; i++)
        transmitted += Ft((i + 0.5) / 100000, eta) * 2.0 * (i + 0.5) / 100000 / 100000;
    double rightLen = 4.0 / 3.0;
    V dir = Unit(target - eye) * (0.5 * rightLen / std::tan(0.5 * angle * M_PI / 180.0));
    V right = Unit(Cross({ 0, 1, 0 }, dir)) * rightLen, up = Unit(Cross(dir, right));
    bool grey[3] = { false, true, true };
    for (int j = 1; j < 3; j++)
        for (const Light& l : lights)
            for (size_t k = 0; k < kinds; k++)
                grey[j] = grey[j] && media[k * 3 + j].sigmaT == media[k * 3].sigmaT && media[k * 3 + j].albedo == media[k * 3].albedo &&
                          l.colour[j] == l.colour[0];
    std::vector<double> image(size_t(width) * height * 3 * kinds, 0.0), error(image.size(), -1.0);
    size_t plane = size_t(width) * height * 3;
    std::atomic<int> nextRow(0);
    auto work = [&]()
    {
        for (int row; (row = nextRow++) < height;)
            for (int col = 0; col < width; col++)
            {
                V ray = Unit(dir + right * ((col + 0.5) / width - 0.5) + up * (0.5 - (row + 0.5) / height));
                double tn, tf;
                if (!Crossing(eye, ray, 1e3, Distance(eye) < 0.0, tn, tf))
                    continue;
                V x = eye + ray * tf, n = Normal(eye + ray * (0.5 * (tn + tf)));
                double c = -Dot(n, ray);
                if (c < 0.0)
                {
                    n = n * -1.0;
                    c = -c;
                }
                double k = 1.0 / eta, ct = std::sqrt(std::max(0.0, 1.0 - k * k * (1.0 - c * c)));
                V d = Unit(ray * k + n * (k * c - ct));
                double scale = Ft(c, eta);
                for (int j = 0; j < 3; j++)
                {
                    size_t at = (size_t(row) * width + col) * 3 + j;
                    if (grey[j])
                    {
                        for (size_t k = 0; k < kinds; k++)
                        {
                            image[k * plane + at] = image[k * plane + at - j];
                            error[k * plane + at] = error[k * plane + at - j];
                        }
                        continue;
                    }
                    Rng rng{ Mix(uint64_t(row) * 0x9E3779B97F4A7C15ull ^ uint64_t(col) * 0xC2B2AE3D27D4EB4Full ^ uint64_t(j)) };
                    std::vector<double> sum(kinds, 0.0), sumSq(kinds, 0.0), score(kinds), sigmaA(kinds);
                    for (size_t k = 0; k < kinds; k++)
                        sigmaA[k] = media[k * 3 + j].sigmaT * (1.0 - media[k * 3 + j].albedo);
                    for (int w = 0; w < walks; w++)
                    {
                        if (kinds == 1)
                            score[0] = Walk(x, d, j, media[j], rng);
                        else
                            WalkMedia(x, d, j, mm / mat.mfp[j], sigmaA, rng, score.data());
                        for (size_t k = 0; k < kinds; k++)
                        {
                            sum[k] += score[k];
                            sumSq[k] += score[k] * score[k];
                        }
                    }
                    for (size_t k = 0; k < kinds; k++)
                    {
                        double mean = sum[k] / walks;
                        image[k * plane + at] = scale * mean;
                        error[k * plane + at] = scale * std::sqrt(std::max(0.0, sumSq[k] / walks - mean * mean) / walks);
                    }
                }
            }
    };
    std::vector<std::thread> threads;
    unsigned count = args.count("Threads") ? unsigned(std::max(1.0, number("Threads", 1))) : std::max(1u, std::thread::hardware_concurrency());
    for (unsigned i = 0; i < count; i++)
        threads.emplace_back(work);
    for (std::thread& t : threads)
        t.join();
    std::fprintf(stderr, "beam entries found %ld, not found %ld\n", entriesFound.load(), entriesMissed.load());
    std::vector<std::string> outs = Split(args.count("Out") ? args["Out"] : "reference.pfm", ','), errs;
    if (args.count("Err"))
        errs = Split(args["Err"], ',');
    for (size_t k = 0; k < kinds; k++)
    {
        std::vector<double> one(image.begin() + k * plane, image.begin() + (k + 1) * plane), bars(error.begin() + k * plane, error.begin() + (k + 1) * plane);
        if (k < outs.size())
            WritePfm(outs[k].c_str(), one, width, height);
        if (k < errs.size())
            WritePfm(errs[k].c_str(), bars, width, height);
    }
    return 0;
}
