// Diagnostic for a triangular OBJ mesh's surface/interior bounds in positive CSG.
// Build with check-prepared-sets.mk; run with an OBJ path (v and triangular f records).
#include "core/shape/mesh.h"
#include "core/shape/csg.h"
#include "core/shape/sphere.h"
#include "core/scene/object.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
using namespace pov;
static void Report(const char* name, ObjectPtr object)
{
    object->Compute_BBox();
    Update_Infinite_Flag(object);
    std::cout << name << " finite=" << object->BBox.finite
              << " interior=" << object->BBox.finiteInterior
              << " infinite=" << bool(Test_Flag(object, INFINITE_FLAG))
              << " low=" << object->BBox.lowerLeft[0] << ',' << object->BBox.lowerLeft[1] << ',' << object->BBox.lowerLeft[2]
              << " size=" << object->BBox.size[0] << ',' << object->BBox.size[1] << ',' << object->BBox.size[2] << '\n';
}
int main(int argc, char** argv)
{
    if (argc != 2) { std::cerr << "usage: mesh-csg-bounds mesh.obj\n"; return argc == 1 ? 0 : 2; }
    std::ifstream file(argv[1]);
    if (!file) return 2;
    auto* mesh = new Mesh;
    MeshBuilder builder(*mesh, false);
    std::vector<MeshIndex> vertices;
    std::string line;
    unsigned rejected = 0, triangles = 0;
    while (std::getline(file, line))
    {
        std::istringstream row(line);
        std::string kind; row >> kind;
        if (kind == "v")
        {
            double x,y,z;
            if (!(row >> x >> y >> z)) return 2;
            vertices.push_back(builder.Vertex(Vector3d(x,y,z), Vector3d(0,0,1)));
        }
        else if (kind == "f")
        {
            size_t a,b,c;
            if (!(row >> a >> b >> c) || !a || !b || !c || a>vertices.size() || b>vertices.size() || c>vertices.size()) return 2;
            if (!builder.Triangle(vertices[a-1], vertices[b-1], vertices[c-1])) ++rejected;
            else ++triangles;
        }
    }
    if (!triangles) return 2;
    builder.Finish(true);
    Report("mesh", mesh);
    std::cout << "topology=" << mesh->Data->ClosedTopology << " rejected_degenerate=" << rejected << '\n';
    auto* ball = new Sphere; ball->Radius = 0.25;
    Report("sphere", ball);
    auto* positive = new CSGMerge; positive->children = {ball, mesh};
    Report("merge", positive);
    auto* cutter = new Sphere; cutter->Radius=0.1; cutter->Invert(); cutter->Compute_BBox();
    CSGIntersection difference(true); difference.children={positive,cutter};
    Report("difference", &difference);
}
