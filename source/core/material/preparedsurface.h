#ifndef POVRAY_CORE_PREPAREDSURFACE_H
#define POVRAY_CORE_PREPAREDSURFACE_H

#include "core/configcore.h"
#include <cmath>
#include <functional>
#include <memory>
#include <map>
#include <set>
#include <tuple>
#include <stdexcept>
#include <vector>
#include "core/lighting/surfacegeometry.h"
#include "core/material/pattern.h"
#include "core/material/pigment.h"
#include "core/material/texture.h"
#include "core/material/warp.h"
#include "core/support/imageutil.h"
#include "core/shape/mesh.h"
#include "core/shape/blob.h"
#include "core/shape/csg.h"
#include "core/scene/scenedata.h"

namespace pov
{

class PreparedSurfaceInput final
{
public:
    static std::shared_ptr<PreparedSurfaceInput> Prepare(ObjectPtr object,
        const SurfaceGeometrySampler& geometry, PIGMENT* pigment, bool useEmission,
        Ray& scratchRay, TraceThreadData* thread, std::size_t trials = 4096)
    {
        if (!object || !geometry.IsValid() || !thread || trials == 0)
            throw std::invalid_argument("Surface light requires finite surface geometry and preparation samples");
        std::shared_ptr<PreparedSurfaceInput> result(new PreparedSurfaceInput());
        if (!useEmission && (!pigment || pigment->Type == PLAIN_PATTERN))
        {
            result->constant = true;
            result->constantColour = pigment ? pigment->colour.colour() : MathColour(1.0);
            ValidateColour(result->constantColour);
            return result;
        }
        if (useEmission && ZeroMaterialEmission(object))
        {
            result->constant = true;
            result->constantColour = MathColour(0.0);
            return result;
        }
        if (useEmission && UniformMeshMaterial(dynamic_cast<Mesh*>(object)))
        {
            Mesh* mesh = static_cast<Mesh*>(object);
            const MESH_TRIANGLE* face = &mesh->Data->Triangles[0];
            Vector3d point = (Vector3d(mesh->Data->Vertices[face->P1()]) + Vector3d(mesh->Data->Vertices[face->P2()]) +
                              Vector3d(mesh->Data->Vertices[face->P3()])) / 3.0;
            if (mesh->Trans) MTransPoint(point, point, mesh->Trans);
            Intersection hit(0.0, point, object);
            hit.Pointer = face;
            result->constantColour = MaterialEmission(hit, thread);
            ValidateColour(result->constantColour);
            result->constant = true;
            return result;
        }
        if (useEmission && !dynamic_cast<Mesh*>(object) && !dynamic_cast<Blob*>(object) &&
            !dynamic_cast<CSG*>(object) && ConstantMaterial(object->Texture))
        {
            Intersection hit(0.0, Vector3d(0.0), object);
            result->constantColour = MaterialEmission(hit, thread);
            ValidateColour(result->constantColour);
            result->constant = true;
            return result;
        }
        ValidatePigment(pigment);
        std::set<const TEXTURE*> validatedTextures;
        auto add = [&](SurfaceGeometrySample& sample)
        {
            sample.intersection.INormal = sample.normal;
            sample.intersection.PNormal = sample.normal;
            sample.intersection.haveNormal = true;
            MathColour colour;
            if (useEmission) colour = MaterialEmission(sample.intersection, thread, &validatedTextures);
            else
            {
                TransColour evaluated;
                if (Compute_Pigment(evaluated, pigment, EvaluationPoint(sample.intersection),
                                    &sample.intersection, nullptr, thread))
                    colour = evaluated.colour();
            }
            ValidateColour(colour);
            result->charts[Chart(sample.intersection)].push_back(result->nodes.size());
            result->nodes.push_back(Node{sample.intersection.IPoint, sample.normal, colour});
        };
        std::vector<ObjectPtr> leaves;
        CollectLeaves(object, leaves);
        for (ObjectPtr leaf : leaves)
        {
            if (Mesh* mesh = dynamic_cast<Mesh*>(leaf))
            {
                mesh->Resolve();
                for (MeshIndex face = 0; face < mesh->Data->Number_Of_Triangles; ++face)
                {
                    const MESH_TRIANGLE* triangle = &mesh->Data->Triangles[face];
                    const Vector3d a(mesh->Data->Vertices[triangle->P1()]);
                    const Vector3d b(mesh->Data->Vertices[triangle->P2()]);
                    const Vector3d c(mesh->Data->Vertices[triangle->P3()]);
                    for (unsigned i = 0; i < 16; ++i)
                    {
                        const DBL root = std::sqrt(Radical(i + 1, 2));
                        const DBL v = Radical(i + 1, 3);
                        Vector3d point = (1.0 - root) * a + root * (1.0 - v) * b + root * v * c;
                        if (mesh->Trans) MTransPoint(point, point, mesh->Trans);
                        SurfaceGeometrySample sample;
                        sample.intersection = Intersection(0.0, point, leaf);
                        if (leaf != object) sample.intersection.Csg = object;
                        sample.intersection.Pointer = triangle;
                        mesh->Normal(sample.normal, &sample.intersection, thread);
                        sample.intersection.INormal = sample.normal;
                        sample.intersection.haveNormal = true;
                        sample.areaWeight = 1.0;
                        add(sample);
                    }
                }
            }
            else
            {
                const SurfaceGeometrySampler leafGeometry(leaf, leaf->BBox.finite ? nullptr : &object->BBox);
                const SurfaceGeometrySampler& sampler = leaf == object ? geometry : leafGeometry;
                if (!sampler.IsValid())
                    throw std::invalid_argument("Surface light input requires finite preparation coordinates");
                const std::size_t before = result->nodes.size();
                for (std::size_t i = 0; i < trials; ++i)
                {
                    SurfaceGeometrySample sample;
                    if (sampler.SampleArea(scratchRay, thread, Radical(i + 1, 2), Radical(i + 1, 3),
                                            Radical(i + 1, 5), Radical(i + 1, 7), sample))
                    {
                        if (leaf != object) sample.intersection.Csg = object;
                        add(sample);
                    }
                }
                if (leaf == object && result->nodes.size() == before)
                    throw std::invalid_argument("Surface light preparation could not cover a geometry leaf");
            }
        }
        if (result->nodes.empty())
            throw std::invalid_argument("Surface light preparation found no surface samples");
        return result;
    }

    struct DiffuseLayer final
    {
        MathColour colour;
        double front, back, brilliance, adjust, outgoing;
    };

    static std::vector<DiffuseLayer> DiffuseLayersAt(Intersection intersection, const Vector3d& normal, TraceThreadData* thread)
    {
        intersection.INormal = intersection.PNormal = normal;
        intersection.haveNormal = true;
        WeightedTextureVector textures;
        intersection.Object->Determine_Textures(&intersection, false, textures, thread);
        if (textures.empty()) throw std::invalid_argument("Surface bounce requires a surface material");
        std::set<const TEXTURE*> visited;
        std::set<const FINISH*> finishes;
        std::function<void(const TEXTURE*)> collect = [&](const TEXTURE* texture)
        {
            if (!texture || !visited.insert(texture).second) return;
            if (texture->Type == PLAIN_PATTERN && texture->Finish) finishes.insert(texture->Finish);
            if (texture->Blend_Map)
                for (const auto& entry : texture->Blend_Map->Blend_Map_Entries) collect(entry.Vals);
            for (const TEXTURE* material : texture->Materials) collect(material);
            collect(texture->Next);
        };
        for (const auto& weighted : textures) collect(weighted.texture);
        std::vector<DiffuseLayer> result;
        for (const FINISH* finish : finishes)
        {
            if (thread->GetSceneData()->useSubsurface && finish->UseSubsurface) continue;
            if (finish->Crand > 0.0)
                throw std::invalid_argument("Surface bounce cannot prepare stochastic crand diffuse finishes");
            double outgoing = 1.0;
#if POV_EXPERIMENTAL_BRILLIANCE_OUT
            outgoing = finish->BrillianceOut;
#endif
            if (!std::isfinite(finish->Brilliance) || finish->Brilliance < 0.0 || !std::isfinite(outgoing) || outgoing <= -1.0)
                throw std::invalid_argument("Surface bounce requires finite integrable brilliance");
            MathColour colour;
            std::set<const TEXTURE*> validated;
            for (const auto& weighted : textures)
            {
                ValidateTexture(weighted.texture, validated, true, thread->GetSceneData()->useSubsurface);
                const Vector3d point = ConstantMaterial(weighted.texture) ? intersection.IPoint : EvaluationPoint(intersection);
                colour += TextureEmission(weighted.texture, point, intersection, thread, 0, true, finish) * weighted.weight;
            }
            ValidateColour(colour);
            const double back = Test_Flag(intersection.Object, DOUBLE_ILLUMINATE_FLAG) ? finish->Diffuse : finish->DiffuseBack;
            if (!colour.IsZero()) result.push_back(DiffuseLayer{colour, finish->Diffuse, back,
                finish->Brilliance, finish->BrillianceAdjust, outgoing});
        }
        return result;
    }

    static bool UniformMeshMaterial(const Mesh* mesh)
    {
        if (!mesh || Test_Flag(mesh, UV_FLAG)) return false;
        std::set<const TEXTURE*> textures;
        if (!MeshMaterials(mesh, textures) || textures.size() != 1) return false;
        const TEXTURE* texture = *textures.begin();
        if (!ConstantMaterial(texture)) return false;
        for (const TEXTURE* layer = texture; layer; layer = layer->Next)
            if (!layer->Finish || layer->Tnormal || layer->Finish->SubsurfacePigment ||
                layer->Finish->SubsurfaceEmissionPigment || layer->Finish->SubsurfaceThicknessPigment) return false;
        return true;
    }

    bool IsConstant() const { return constant; }
    const MathColour& ConstantColour() const { return constantColour; }

    MathColour Sample(const Intersection& intersection, const Vector3d& normal) const
    {
        if (constant) return constantColour;
        const auto chart = charts.find(Chart(intersection));
        if (chart == charts.end())
            throw std::runtime_error("Surface light preparation did not cover this primitive chart");
        MathColour numerator;
        DBL denominator = 0.0;
        DBL nearest = HUGE_VAL;
        const Node* closest = nullptr;
        for (std::size_t index : chart->second)
        {
            const Node& node = nodes[index];
            const Vector3d delta = node.point - intersection.IPoint;
            const DBL distance2 = dot(delta, delta);
            if (distance2 == 0.0) return node.colour;
            if (distance2 < nearest) { nearest = distance2; closest = &node; }
        }
        for (std::size_t index : chart->second)
        {
            // Keep interpolation on the same primitive chart, including individual mesh faces.
            const Node& node = nodes[index];
            const Vector3d delta = node.point - intersection.IPoint;
            const DBL distance2 = dot(delta, delta);
            const DBL ratio = nearest / distance2;
            const DBL alignment = std::max(DBL(0.0), dot(normal, node.normal));
            const DBL weight = ratio * ratio * (1.0e-6 + alignment * alignment);
            numerator += node.colour * weight;
            denominator += weight;
        }
        if (denominator > 0.0) return numerator / denominator;
        if (closest) return closest->colour;
        throw std::runtime_error("Surface light preparation did not cover this primitive chart; increase preparation samples");
    }

private:
    struct Node
    {
        Vector3d point;
        Vector3d normal;
        MathColour colour;
    };
    bool constant = false;
    MathColour constantColour;
    std::vector<Node> nodes;

    using ChartKey = std::tuple<ObjectPtr, const void*>;
    std::map<ChartKey, std::vector<std::size_t>> charts;
    static ChartKey Chart(const Intersection& hit)
    {
        return ChartKey(hit.Object, hit.Pointer);
    }

    static void CollectLeaves(ObjectPtr object, std::vector<ObjectPtr>& leaves)
    {
        if (CSG* csg = dynamic_cast<CSG*>(object))
            for (ObjectPtr child : csg->children) CollectLeaves(child, leaves);
        else leaves.push_back(object);
    }

    static DBL Radical(std::size_t n, unsigned base)
    {
        DBL result = 0.0, place = 1.0 / base;
        while (n) { result += (n % base) * place; n /= base; place /= base; }
        return result;
    }

    static void ValidateColour(const MathColour& colour)
    {
        for (int channel = 0; channel < 3; ++channel)
            if (!std::isfinite(colour[channel]) || colour[channel] < 0.0)
                throw std::invalid_argument("Surface light requires finite nonnegative emitted colour");
    }

    static void ValidatePigment(const PIGMENT* pigment)
    {
        if (!pigment) return;
        if (PigmentRequiresRayContext(pigment) || dynamic_cast<const AOIPattern*>(pigment->pattern.get()))
            throw std::invalid_argument("Surface light input cannot depend on ray or camera context");
        if (const PigmentPattern* nested = dynamic_cast<const PigmentPattern*>(pigment->pattern.get()))
            ValidatePigment(nested->pPigment);
        if (const PigmentBlendMap* map = dynamic_cast<const PigmentBlendMap*>(pigment->Blend_Map.get()))
            for (const PigmentBlendMapEntry& entry : map->Blend_Map_Entries)
                ValidatePigment(entry.Vals);
    }

    static Vector3d EvaluationPoint(Intersection& hit)
    {
        if (!Test_Flag(hit.Object, UV_FLAG)) return hit.IPoint;
        hit.Object->UVCoord(hit.Iuv, &hit);
        return Vector3d(hit.Iuv[U], hit.Iuv[V], 0.0);
    }

    static bool MeshMaterials(const Mesh* mesh, std::set<const TEXTURE*>& textures)
    {
        mesh->Resolve();
        if (!mesh->Data || mesh->Data->Number_Of_Triangles == 0) return false;
        auto add = [&](MeshIndex index)
        {
            const TEXTURE* texture = index < 0 ? mesh->Texture :
                (index < mesh->Number_Of_Textures ? mesh->Textures[index] : nullptr);
            if (!texture) return false;
            textures.insert(texture);
            return true;
        };
        for (MeshIndex face = 0; face < mesh->Data->Number_Of_Triangles; ++face)
        {
            const MESH_TRIANGLE& triangle = mesh->Data->Triangles[face];
            const MeshIndex first = mesh->Data->TextureInd.Get(triangle, face, 0);
            if ((triangle.ThreeTex() && first < 0) || !add(first)) return false;
            if (triangle.ThreeTex())
                for (int vertex = 0; vertex < 2; ++vertex)
                {
                    const MeshIndex index = mesh->Data->Texture23Ind.Get(triangle, face, vertex);
                    if (index < 0 || !add(index)) return false;
                }
        }
        return true;
    }

    static bool ZeroTextureEmission(const TEXTURE* texture, std::set<const TEXTURE*>& visited, unsigned depth = 0)
    {
        if (!texture || depth > 128) return false;
        if (!visited.insert(texture).second) return true;
        if (texture->Type == PLAIN_PATTERN)
        {
            if (!texture->Pigment || !texture->Finish || !texture->Finish->Emission.IsZero()) return false;
        }
        else if (texture->Type == BITMAP_PATTERN)
        {
            if (texture->Materials.empty()) return false;
        }
        else if (!texture->Blend_Map || texture->Blend_Map->Blend_Map_Entries.empty()) return false;
        if (texture->Blend_Map)
            for (const auto& entry : texture->Blend_Map->Blend_Map_Entries)
                if (!ZeroTextureEmission(entry.Vals, visited, depth + 1)) return false;
        for (const TEXTURE* material : texture->Materials)
            if (!ZeroTextureEmission(material, visited, depth + 1)) return false;
        return !texture->Next || ZeroTextureEmission(texture->Next, visited, depth + 1);
    }

    static bool ZeroMaterialEmission(ObjectPtr object)
    {
        if (dynamic_cast<Blob*>(object) || dynamic_cast<CSG*>(object)) return false;
        std::set<const TEXTURE*> textures;
        if (Mesh* mesh = dynamic_cast<Mesh*>(object))
        {
            if (!MeshMaterials(mesh, textures)) return false;
        }
        else textures.insert(object->Texture);
        std::set<const TEXTURE*> visited;
        for (const TEXTURE* texture : textures)
            if (!ZeroTextureEmission(texture, visited)) return false;
        return true;
    }

    static bool ConstantMaterial(const TEXTURE* texture)
    {
        if (!texture) return false;
        for (const TEXTURE* layer = texture; layer; layer = layer->Next)
            if (layer->Type != PLAIN_PATTERN || !layer->Pigment || layer->Pigment->Type != PLAIN_PATTERN)
                return false;
        return true;
    }

    static void ValidateTexture(const TEXTURE* texture, std::set<const TEXTURE*>& visited, bool diffuse = false, bool skipSubsurface = false)
    {
        if (!texture || !visited.insert(texture).second) return;
        PIGMENT probe{};
        probe.pattern = texture->pattern;
        ValidatePigment(&probe);
        if (texture->Type == PLAIN_PATTERN)
        {
            if (!texture->Pigment || !texture->Finish)
                throw std::invalid_argument("Surface light emission requires complete surface materials");
            const FINISH& finish = *texture->Finish;
            if (!(diffuse && skipSubsurface && finish.UseSubsurface) && (finish.Fresnel != 0.0 ||
                (finish.Conserve_Energy && (!finish.Reflection_Max.IsZero() || !finish.Reflection_Min.IsZero()))))
                throw std::invalid_argument(diffuse ? "Surface bounce cannot prepare Fresnel or energy-conserving reflection attenuation" :
                    "Surface light emission cannot bake direction-dependent finish attenuation");
            ValidatePigment(texture->Pigment);
        }
        if (texture->Type == BITMAP_PATTERN && texture->Materials.empty())
            throw std::invalid_argument("Surface light material map is empty");
        if (texture->Blend_Map)
            for (const TextureBlendMapEntry& entry : texture->Blend_Map->Blend_Map_Entries)
                ValidateTexture(entry.Vals, visited, diffuse, skipSubsurface);
        for (const TEXTURE* material : texture->Materials) ValidateTexture(material, visited, diffuse, skipSubsurface);
        ValidateTexture(texture->Next, visited, diffuse, skipSubsurface);
    }

    static MathColour TextureEmission(const TEXTURE* texture, const Vector3d& point,
                                      Intersection& intersection, TraceThreadData* thread, unsigned depth = 0, bool diffuse = false, const FINISH* selected = nullptr)
    {
        if (!texture || depth > 128)
            throw std::invalid_argument("Surface light texture map is missing or nested too deeply");
        if (texture->Type == PLAIN_PATTERN)
        {
            MathColour transmitted(1.0), emitted;
            for (const TEXTURE* layer = texture; layer; layer = layer->Next)
            {
                if (layer->Type != PLAIN_PATTERN)
                    throw std::invalid_argument("Surface light layered materials require plain texture layers");
                TransColour colour;
                if (!Compute_Pigment(colour, layer->Pigment, point, &intersection, nullptr, thread)) continue;
                const MathColour response = diffuse ?
                    MathColour((thread->GetSceneData()->useSubsurface && layer->Finish->UseSubsurface) ||
                        (selected && selected != layer->Finish) ? 0.0 : (selected ? 1.0 : layer->Finish->Diffuse) / M_PI) : layer->Finish->Emission;
                emitted += transmitted * colour.colour() * response * colour.Opacity();
                transmitted *= colour.TransmittedColour();
            }
            return emitted;
        }
        Vector3d warped;
        if (texture->Type == UV_MAP_PATTERN)
        {
            if (!texture->Blend_Map || texture->Blend_Map->Blend_Map_Entries.empty())
                throw std::invalid_argument("Surface light UV texture map is empty");
            Vector2d uv;
            intersection.Object->UVCoord(uv, &intersection);
            warped = Vector3d(uv[U], uv[V], 0.0);
            return TextureEmission(texture->Blend_Map->Blend_Map_Entries.front().Vals,
                                   warped, intersection, thread, depth + 1, diffuse, selected);
        }
        Warp_EPoint(warped, point, texture);
        if (texture->Type == BITMAP_PATTERN)
            return TextureEmission(material_map(warped, texture), warped, intersection, thread, depth + 1, diffuse, selected);
        if (!texture->Blend_Map || texture->Blend_Map->Blend_Map_Entries.empty())
            throw std::invalid_argument("Surface light texture map is empty");
        if (texture->Type == AVERAGE_PATTERN)
        {
            MathColour sum;
            DBL total = 0.0;
            for (const TextureBlendMapEntry& entry : texture->Blend_Map->Blend_Map_Entries)
            {
                sum += TextureEmission(entry.Vals, warped, intersection, thread, depth + 1, diffuse, selected) * entry.value;
                total += entry.value;
            }
            if (!std::isfinite(total) || total == 0.0)
                throw std::invalid_argument("Surface light average texture requires finite nonzero total weight");
            return sum / total;
        }
        if (texture->Type <= LAST_SPECIAL_PATTERN)
            throw std::invalid_argument("Surface light texture has an invalid pattern type");
        const DBL value = Evaluate_TPat(texture, warped, &intersection, nullptr, thread);
        if (!std::isfinite(value))
            throw std::invalid_argument("Surface light texture pattern requires finite values");
        const TextureBlendMapEntry *previous, *current;
        DBL previousWeight, currentWeight;
        texture->Blend_Map->Search(value, previous, current, previousWeight, currentWeight);
        const MathColour currentColour = TextureEmission(current->Vals, warped, intersection, thread, depth + 1, diffuse, selected);
        if (previous == current) return currentColour;
        return currentColour * currentWeight +
            TextureEmission(previous->Vals, warped, intersection, thread, depth + 1, diffuse, selected) * previousWeight;
    }

    static MathColour MaterialEmission(Intersection& intersection, TraceThreadData* thread,
                                      std::set<const TEXTURE*>* validated = nullptr)
    {
        WeightedTextureVector textures;
        intersection.Object->Determine_Textures(&intersection, false, textures, thread);
        if (textures.empty())
            throw std::invalid_argument("Surface light emission requires a surface material");
        std::set<const TEXTURE*> localValidated;
        if (!validated) validated = &localValidated;
        MathColour result;
        for (const WeightedTexture& weighted : textures)
        {
            ValidateTexture(weighted.texture, *validated);
            const Vector3d point = ConstantMaterial(weighted.texture) ? intersection.IPoint : EvaluationPoint(intersection);
            result += TextureEmission(weighted.texture, point, intersection, thread) * weighted.weight;
        }
        return result;
    }
};

}
#endif
