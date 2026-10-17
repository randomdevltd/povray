// sslt-reference W H Zoom Bare Walks out.ppm [scale R G B [colour R G B mfp R G B [View]]]: a physically based reference for
// the diffuse subsurface light of tools/bench/sslt-studs.pov with Fillet=0 Reference=1 (Bare=2: Blocks=1), by random walks.

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>

struct V { double x, y, z; };
static V operator+(V a, V b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
static V operator-(V a, V b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
static V operator*(V a, double s) { return { a.x * s, a.y * s, a.z * s }; }
static double Dot(V a, V b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static V Cross(V a, V b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
static V Unit(V a) { return a * (1.0 / std::sqrt(Dot(a, a))); }

// A box, or a cylinder along y (radius r > 0), the parts of one union, with their bounds.
struct Part { V lo, hi; double r; V blo, bhi; };
static std::vector<Part> parts;
static void AddPart(V lo, V hi, double r)
{
    V blo = r > 0.0 ? V{ lo.x - r, lo.y, lo.z - r } : lo, bhi = r > 0.0 ? V{ lo.x + r, hi.y, lo.z + r } : hi;
    parts.push_back({ lo, hi, r, blo, bhi });
}
static bool InBounds(const Part& p, V lo, V hi)
{
    return lo.x <= p.bhi.x && hi.x >= p.blo.x && lo.y <= p.bhi.y && hi.y >= p.blo.y && lo.z <= p.bhi.z && hi.z >= p.blo.z;
}

static bool InsidePart(const Part& p, V q)
{
    if (q.y < p.lo.y || q.y > p.hi.y)
        return false;
    if (p.r > 0.0)
        return std::pow(q.x - p.lo.x, 2) + std::pow(q.z - p.lo.z, 2) < p.r * p.r;
    return q.x > p.lo.x && q.x < p.hi.x && q.z > p.lo.z && q.z < p.hi.z;
}
static bool Inside(V q)
{
    for (const Part& p : parts)
        if (InBounds(p, q, q) && InsidePart(p, q))
            return true;
    return false;
}

// Distances along the ray where it crosses a part's surface, with that surface's normal.
struct Hit { double t; V n; };
static void Crossings(const Part& p, V o, V d, std::vector<Hit>& hits)
{
    if (p.r > 0.0)
    {
        double ox = o.x - p.lo.x, oz = o.z - p.lo.z, a = d.x * d.x + d.z * d.z, b = ox * d.x + oz * d.z, c = ox * ox + oz * oz - p.r * p.r;
        if (a > 0.0 && b * b - a * c > 0.0)
            for (double s : { -1.0, 1.0 })
            {
                double t = (-b + s * std::sqrt(b * b - a * c)) / a, y = o.y + d.y * t;
                if (y > p.lo.y && y < p.hi.y)
                    hits.push_back({ t, Unit({ ox + d.x * t, 0.0, oz + d.z * t }) });
            }
        for (double cap : { p.lo.y, p.hi.y })
            if (d.y != 0.0)
            {
                double t = (cap - o.y) / d.y, x = o.x + d.x * t - p.lo.x, z = o.z + d.z * t - p.lo.z;
                if (x * x + z * z < p.r * p.r)
                    hits.push_back({ t, { 0.0, cap == p.lo.y ? -1.0 : 1.0, 0.0 } });
            }
        return;
    }
    const double lo[3] = { p.lo.x, p.lo.y, p.lo.z }, hi[3] = { p.hi.x, p.hi.y, p.hi.z };
    const double oo[3] = { o.x, o.y, o.z }, dd[3] = { d.x, d.y, d.z };
    for (int a = 0; a < 3; a++)
        for (int side = 0; side < 2; side++)
        {
            if (dd[a] == 0.0)
                continue;
            double t = ((side ? hi[a] : lo[a]) - oo[a]) / dd[a];
            bool in = true;
            for (int b = 0; b < 3; b++)
                if (b != a && (oo[b] + dd[b] * t <= lo[b] || oo[b] + dd[b] * t >= hi[b]))
                    in = false;
            if (in)
            {
                double n[3] = { 0.0, 0.0, 0.0 };
                n[a] = side ? 1.0 : -1.0;
                hits.push_back({ t, { n[0], n[1], n[2] } });
            }
        }
}

// The first crossing of the union's surface after t = eps, out of it if inside, into it if outside.
static bool Boundary(V o, V d, double maxT, Hit& found)
{
    thread_local std::vector<Hit> hits;
    hits.clear();
    double reach = std::min(maxT, 1e6);
    V e = o + d * reach, lo = { std::min(o.x, e.x), std::min(o.y, e.y), std::min(o.z, e.z) }, hi = { std::max(o.x, e.x), std::max(o.y, e.y), std::max(o.z, e.z) };
    for (const Part& p : parts)
        if (InBounds(p, lo, hi))
            Crossings(p, o, d, hits);
    std::sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.t < b.t; });
    bool inside = Inside(o + d * 1e-9);
    double last = 0.0;
    for (const Hit& h : hits)
    {
        if (h.t <= 1e-9)
            continue;
        if (h.t > maxT)
            return false;
        if (Inside(o + d * (0.5 * (last + h.t))) == inside && Inside(o + d * (h.t + 1e-9)) != inside)
        {
            found = h;
            return true;
        }
        last = h.t;
    }
    return false;
}

static double Ft(double c, double eta)
{
    double g = std::sqrt(eta * eta + c * c - 1.0), f = 0.5 * std::pow(g - c, 2) / std::pow(g + c, 2);
    f *= 1.0 + std::pow(c * (g + c) - 1.0, 2) / std::pow(c * (g - c) + 1.0, 2);
    return 1.0 - std::min(1.0, std::max(0.0, f));
}

// Reflectance at a boundary from inside (relative index 1/eta), unpolarised.
static double FresnelInside(double c, double eta)
{
    double s2 = eta * eta * (1.0 - c * c);
    if (s2 >= 1.0)
        return 1.0;
    double ct = std::sqrt(1.0 - s2), rs = (eta * c - ct) / (eta * c + ct), rp = (c - eta * ct) / (c + eta * ct);
    return 0.5 * (rs * rs + rp * rp);
}

static double ReducedAlbedo(double rd, double eta)
{
    double fdr = ((1.0 - 1.0 / eta) + 3.0 * std::pow(1.0 - 1.0 / eta, 4.5)) / 4.0, a = (1.0 + fdr) / (1.0 - fdr), lo = 0.0, hi = 1.0;
    for (int i = 0; i < 60; i++)
    {
        double m = 0.5 * (lo + hi), root = std::sqrt(3.0 * (1.0 - m));
        ((m / 2.0) * (1.0 + std::exp(-(4.0 / 3.0) * a * root)) * std::exp(-root) < rd ? lo : hi) = m;
    }
    return 0.5 * (lo + hi);
}

struct Light { V at; double colour[3]; };
static std::vector<Light> lights;
static const double kEta = 1.5;

// Light entering at y (outward normal n) from each light, as the subsurface methods take it: colour, cosine and Fresnel.
static double Entering(V y, V n, int j)
{
    double e = 0.0;
    for (const Light& l : lights)
    {
        V to = l.at - y;
        double dist = std::sqrt(Dot(to, to)), c = Dot(to, n) / dist;
        Hit h;
        if (c <= 0.0 || Boundary(y + n * 1e-7, to * (1.0 / dist), dist, h))
            continue;
        e += l.colour[j] * c * Ft(c, kEta);
    }
    return e;
}

static uint64_t Mix(uint64_t z)
{
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}
struct Rng { uint64_t s; double operator()() { s += 0x9E3779B97F4A7C15ull; return double(Mix(s) >> 11) * (1.0 / 9007199254740992.0); } };

static V Basis(V n, double u, double v)
{
    V t = std::fabs(n.x) < 0.9 ? Unit(Cross(n, { 1, 0, 0 })) : Unit(Cross(n, { 0, 1, 0 }));
    V b = Cross(n, t);
    double r = std::sqrt(u), phi = 2.0 * M_PI * v;
    return t * (r * std::cos(phi)) + b * (r * std::sin(phi)) + n * std::sqrt(1.0 - u);
}

// Mean light entering where walks from x (outward normal n) leave the medium after two or more scatterings.
static double Walks(V x, V n, int j, double sigmaT, double albedo, int walks, Rng& rng)
{
    double sum = 0.0;
    for (int w = 0; w < walks; w++)
    {
        V p = x, d = Basis(n * -1.0, rng(), rng());
        double weight = 1.0;
        int order = 0;
        for (int step = 0; step < 100000; step++)
        {
            double s = -std::log(1.0 - rng()) / sigmaT;
            Hit h;
            if (!Boundary(p, d, s, h))
            {
                p = p + d * s;
                d = Unit({ 2.0 * rng() - 1.0, 2.0 * rng() - 1.0, 2.0 * rng() - 1.0 });
                while (Dot(d, d) > 1.0)
                    d = { 2.0 * rng() - 1.0, 2.0 * rng() - 1.0, 2.0 * rng() - 1.0 };
                d = Unit(d);
                weight *= albedo;
                order++;
                if (weight < 0.05)
                {
                    if (rng() < 0.5)
                        break;
                    weight *= 2.0;
                }
                continue;
            }
            V y = p + d * h.t, nn = h.n;
            if (Dot(nn, d) < 0.0)
                nn = nn * -1.0;
            double c = Dot(nn, d);
            if (rng() < FresnelInside(c, kEta))
            {
                p = y - nn * 1e-9;
                d = d - nn * (2.0 * c);
                continue;
            }
            if (order >= 2)
                sum += weight * Entering(y + nn * 1e-9, nn, j);
            break;
        }
    }
    return sum / walks;
}

int main(int argc, char **argv)
{
    if (argc < 7)
    {
        std::fprintf(stderr, "usage: sslt-reference W H Zoom Bare Walks out.ppm [scale R G B [colour R G B mfp R G B [View]]]\n");
        return 2;
    }
    int width = std::atoi(argv[1]), height = std::atoi(argv[2]), zoom = std::atoi(argv[3]), bare = std::atoi(argv[4]), walks = std::atoi(argv[5]);
    const double mm = 10.0, sink = 0.002, radius = 0.24, studHeight = 0.17;
    if (bare == 2)
    {
        AddPart({ -2.4, 0, -1.6 }, { 0, 0.6, 1.6 }, 0.0);
        AddPart({ 0.05, 0, -1.6 }, { 2.4, 0.35, 1.6 }, 0.0);
    }
    else
        AddPart({ -2.4, 0, -1.6 }, { 2.4, 0.4, 1.6 }, 0.0);
    if (!bare)
    {
        auto stud = [&](double x, double z) { AddPart({ x, 0.4 - sink, z }, { x, 0.4 + studHeight, z }, radius); };
        for (int i = 0; i <= 4; i++) stud(-2.0 + i * 0.8, -1.1);
        for (int i = 0; i <= 6; i++) stud(-2.0 + i * 0.56, -0.15);
        for (int i = 0; i <= 6; i++) stud(-2.0 + i * 0.51, 0.75);
        AddPart({ -2.4, 0.4 - sink, 1.2 }, { 2.4, 0.75, 1.6 }, 0.0);
        AddPart({ 1.9, 0.4 - sink, -1.6 }, { 2.1, 0.75, 1.2 + sink }, 0.0);
        AddPart({ 2.2, 0.4 - sink, -1.6 }, { 2.4, 0.75, 1.2 + sink }, 0.0);
        AddPart({ 2.1 - 1e-9, 0.4 - sink, -1.6 }, { 2.2 + 1e-9, 0.5, 1.2 + sink }, 0.0);
    }
    lights.push_back({ { -3, 5, -2.5 }, { 1.2, 0.93 * 1.2, 0.85 * 1.2 } });
    lights.push_back({ { 3.5, 2.2, 1.0 }, { 0.35, 0.4, 0.5 } });
    double pigment[3] = { 0.85, 0.12, 0.08 }, mfp[3] = { 3.0, 1.6, 0.9 };
    for (int j = 0; j < 3 && argc > 10 + j; j++)
        pigment[j] = std::atof(argv[10 + j]);
    for (int j = 0; j < 3 && argc > 13 + j; j++)
        mfp[j] = std::atof(argv[13 + j]);
    double sigmaT[3], albedo[3];
    for (int j = 0; j < 3; j++)
    {
        albedo[j] = ReducedAlbedo(pigment[j] * 0.75, kEta);
        sigmaT[j] = mm / mfp[j] / albedo[j];
    }
    int view = argc > 16 ? std::atoi(argv[16]) : 0;
    V eye = { 0.8, 4.2, -4.2 }, target = view == 1 ? V{ 2.15, 0.55, -0.6 } : view == 2 ? V{ -1.49, 0.5, 0.75 } : view == 3 ? V{ 0.02, 0.4, -0.4 } : zoom == 1 ? V{ 0, 0.3, 0.1 } : V{ -1.7, 0.5, -0.1 };
    double angle = 42.0 / zoom * M_PI / 180.0, rightLen = 4.0 / 3.0;
    V dir = Unit(target - eye) * (0.5 * rightLen / std::tan(0.5 * angle));
    V right = Unit(Cross({ 0, 1, 0 }, dir)) * rightLen, up = Unit(Cross(dir, right));
    std::vector<double> image(size_t(width) * height * 3, 0.0);
    std::atomic<int> nextRow(0);
    auto work = [&]()
    {
        for (int row; (row = nextRow++) < height;)
            for (int col = 0; col < width; col++)
            {
                V ray = Unit(dir + right * ((col + 0.5) / width - 0.5) + up * (0.5 - (row + 0.5) / height));
                Hit h;
                if (!Boundary(eye, ray, 1e9, h))
                    continue;
                V x = eye + ray * h.t, n = h.n;
                if (Dot(n, ray) > 0.0)
                    n = n * -1.0;
                double ftOut = Ft(-Dot(n, ray), kEta);
                for (int j = 0; j < 3; j++)
                {
                    Rng rng{ Mix(uint64_t(row) * 0x9E3779B97F4A7C15ull ^ uint64_t(col) * 0xC2B2AE3D27D4EB4Full ^ uint64_t(j)) };
                    image[(size_t(row) * width + col) * 3 + j] = ftOut * Walks(x - n * 1e-9, n, j, sigmaT[j], albedo[j], walks, rng);
                }
            }
    };
    std::vector<std::thread> threads;
    unsigned count = std::max(1u, std::thread::hardware_concurrency());
    for (unsigned i = 0; i < count; i++)
        threads.emplace_back(work);
    for (std::thread& t : threads)
        t.join();
    FILE *out = std::fopen(argv[6], "wb");
    std::fprintf(out, "P6\n%d %d\n65535\n", width, height);
    double scale[3] = { 1.0, 1.0, 1.0 };
    for (int j = 0; j < 3 && argc > 7 + j; j++)
        scale[j] = std::atof(argv[7 + j]);
    for (size_t i = 0; i < image.size(); i++)
    {
        double c = std::max(0.0, std::min(1.0, image[i] * scale[i % 3]));
        c = c <= 0.0031308 ? 12.92 * c : 1.055 * std::pow(c, 1.0 / 2.4) - 0.055;
        unsigned q = unsigned(std::lround(c * 65535.0));
        std::fputc(q >> 8, out);
        std::fputc(q & 255, out);
    }
    std::fclose(out);
    return 0;
}
