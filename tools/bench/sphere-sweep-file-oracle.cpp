// Run the public random oracle against supplied cubic control spheres (x y z radius).
// Usage: sphere-sweep-file-oracle controls.txt scale [rays=128]
#define main RandomSweepOracleMain
#include "../../tests/source/sphere_sweep_oracle.cpp"
#undef main
#include <fstream>
#include <sstream>
int main(int argc, char** argv)
{
    if (argc < 3) { std::puts("usage: sphere-sweep-file-oracle controls.txt scale [rays=128]"); return argc == 1 ? 0 : 2; }
    Initialize_Noise();
    auto scene = std::make_shared<SceneData>();
    TraceThreadData thread(scene, 1, false);
    TraceTicket ticket(1, 0.0);
    Shape shape;
    shape.name = "supplied cubic sweep";
    shape.reference.kind = CATMULL_ROM_SPLINE_SPHERE_SWEEP;
    const double scale = std::atof(argv[2]);
    if (!(scale > 0) || !std::isfinite(scale)) return 2;
    std::ifstream input(argv[1]);
    if (!input) return 2;
    std::string line;
    Vector3d low(HUGE_VAL), high(-HUGE_VAL);
    while (std::getline(input, line))
    {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream row(line);
        double x,y,z,r;
        if (!(row >> x >> y >> z >> r)) return 2;
        SPHSWEEP_SPH s{Vector3d(x,y,z)*scale,r*scale};
        shape.reference.spheres.push_back(s);
        low=min(low,s.Center-Vector3d(std::abs(s.Radius)));
        high=max(high,s.Center+Vector3d(std::abs(s.Radius)));
    }
    if (shape.reference.spheres.size()<4) return 2;
    shape.reference.size=(high-low).length();
    shape.sweep=Build(shape.reference,1.5e-6);
    const int rays=argc>3?std::atoi(argv[3]):128;
    if (rays<=0) return 2;
    int accepted=0;
    CheckInside(shape,thread,32);
    for (int i=0; i<rays; ++i)
    {
        Vector3d point,direction;
        if (!Start(shape,point,direction)) continue;
        ++accepted;
        CheckCrossings(shape,point,direction,thread,ticket);
        CheckForms(shape,point,direction,thread,ticket);
    }
    std::printf("scale=%g rays=%d checks=%d failures=%d\n",scale,accepted,checks,failures);
    Destroy_Object(shape.sweep);
    return failures?1:0;
}
