#include "core/bounding/boundingbox.h"
#include "core/shape/csg.h"
#include "core/shape/disc.h"
#include "core/shape/mesh.h"
#include "core/shape/plane.h"
#include "core/shape/portal.h"
#include "core/shape/sphere.h"
#include "base/platformbase.h"
#include "base/stringutilities.h"
#include "parser/parser.h"
#include <cassert>
#include <cstdio>
#include <iostream>
#include <thread>

using namespace pov;

static Sphere *Ball(double centre, double radius)
{
    Sphere *sphere = new Sphere();
    sphere->Center = Vector3d(centre, 0, 0);
    sphere->Radius = radius;
    sphere->Compute_BBox();
    return sphere;
}

static Plane *Halfspace(double x, double y, double z, double offset)
{
    Plane *plane = new Plane();
    plane->Normal_Vector = Vector3d(x, y, z);
    plane->Distance = -offset;
    plane->Compute_BBox();
    return plane;
}

static void Tetrahedron(Mesh& mesh, bool closed, bool duplicatePositions)
{
    const std::array<Vector3d, 4> positions = {{Vector3d(0, 0, 0), Vector3d(1, 0, 0), Vector3d(0, 1, 0), Vector3d(0, 0, 1)}};
    const int faces[4][3] = {{0, 2, 1}, {0, 1, 3}, {0, 3, 2}, {1, 2, 3}};
    MeshBuilder builder(mesh, false);
    std::array<MeshIndex, 4> shared;
    if (!duplicatePositions)
        for (int vertex = 0; vertex < 4; ++vertex)
            shared[vertex] = builder.Vertex(positions[vertex], Vector3d(0, 0, 1));
    for (int face = 0; face < (closed ? 4 : 1); ++face)
    {
        std::array<MeshIndex, 3> indices;
        for (int vertex = 0; vertex < 3; ++vertex)
            indices[vertex] = duplicatePositions ? builder.Vertex(positions[faces[face][vertex]], Vector3d(face + 1, 1, 1)) : shared[faces[face][vertex]];
        assert(builder.Triangle(indices[0], indices[1], indices[2]));
    }
    builder.Finish(true);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    pov_base::DefaultPlatformBase platform;
    for (double radius : {1e-8, 1.0, 1.7374e6, 6.957e8, 1e13, 1e25})
    {
        Sphere *sphere = Ball(radius * 215, radius);
        assert(UsableFiniteBounds(sphere->BBox) && sphere->BBox.finiteInterior);
        Update_Infinite_Flag(sphere);
        assert(UsableFiniteBounds(sphere->BBox));
        CSGUnion *group = new CSGUnion();
        group->children = {sphere, Ball(-radius * 300, radius * 2)};
        group->Compute_BBox();
        assert(UsableFiniteBounds(group->BBox) && group->BBox.finiteInterior);
        Destroy_Object(group);
    }
    CSGIntersection *bounded = new CSGIntersection(false);
    bounded->children = {Ball(1.496e11, 6.957e8), Halfspace(0, 1, 0, 0)};
    bounded->Compute_BBox();
    assert(UsableFiniteBounds(bounded->BBox) && bounded->BBox.finiteInterior);
    Destroy_Object(bounded);
    CSGIntersection *wedge = new CSGIntersection(false);
    wedge->children = {Halfspace(-1, 0, 0, 0), Halfspace(0, -1, 0, 0), Halfspace(0, 0, -1, 0),
                       Halfspace(-1, -1, 1, 1), Halfspace(-1, 1, -1, 1)};
    wedge->Compute_BBox();
    assert(!wedge->BBox.finiteInterior);
    Destroy_Object(wedge);
    Plane *clipped = Halfspace(0, 1, 0, 0);
    clipped->Clip = {Ball(0, 1)};
    clipped->Compute_BBox();
    assert(clipped->BBox.finiteInterior);
    clipped->Clip[0]->Invert();
    clipped->Compute_BBox();
    assert(!clipped->BBox.finiteInterior);
    Destroy_Object(clipped);
    Disc disc;
    disc.Compute_BBox();
    assert(disc.BBox.finite && !disc.BBox.finiteInterior);
    Portal *portal = new Portal();
    portal->body = Ball(0, 1);
    portal->Compute_BBox();
    portal->Invert();
    assert(portal->BBox.finite && !portal->BBox.finiteInterior);
    CSGUnion *surface = new CSGUnion();
    surface->children = {portal, Ball(3, 1)};
    surface->Compute_BBox();
    assert(surface->BBox.finite && !surface->BBox.finiteInterior);
    Destroy_Object(surface);
    Mesh *closed = new Mesh();
    Tetrahedron(*closed, true, true);
    Mesh *copy = static_cast<Mesh *>(Copy_Object(closed));
    std::thread first([&]() { closed->Compute_BBox(); });
    std::thread second([&]() { copy->Compute_BBox(); });
    first.join();
    second.join();
    assert(closed->Data == copy->Data && closed->BBox.finiteInterior && copy->BBox.finiteInterior);
    assert(!closed->Inside(Vector3d(1e6), nullptr));
    TRANSFORM translation;
    const Vector3d displacement(1.496e11, 0, 0);
    Compute_Translation_Transform(&translation, displacement);
    copy->Translate(displacement, &translation);
    copy->Compute_BBox();
    assert(copy->BBox.finiteInterior && copy->BBox.lowerLeft[0] <= displacement[0]);
    assert(double(copy->BBox.lowerLeft[0]) + double(copy->BBox.size[0]) >= displacement[0] + 1);
    Mesh *open = new Mesh();
    Tetrahedron(*open, false, false);
    open->Compute_BBox();
    assert(open->BBox.finite && !open->BBox.finiteInterior);
    open->has_inside_vector = false;
    open->Compute_BBox();
    assert(open->BBox.finiteInterior);
    Destroy_Object(open);
    const auto key = pov_parser::Generated_Mesh_Key(pov_base::SysToUCS2String(argv[1]), "finite geometry closed duplicate-position tetrahedron");
    pov::DeferredMeshState write, read;
    pov_parser::Write_Generated_Mesh(key, *closed, write, 0);
    const auto cached = pov_parser::Read_Generated_Mesh(key, read);
    assert(cached && cached->BBox.finiteInterior && cached->Data->ClosedTopology == 1);
    assert(!cached->Inside(Vector3d(1e6), nullptr));
    std::remove(pov_base::UCS2toSysString(key.path).c_str());
    Destroy_Object(copy);
    Destroy_Object(closed);
    std::cout << "finite geometry bounds checks passed\n";
}
