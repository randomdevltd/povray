//******************************************************************************
///
/// @file core/scene/object.h
///
/// Declarations related to geometric shapes.
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

#ifndef POVRAY_CORE_OBJECT_H
#define POVRAY_CORE_OBJECT_H

// Module config header file must be the first file included within POV-Ray unit header files
#include "core/configcore.h"

// C++ variants of C standard header files
//  (none at the moment)

// C++ standard header files
#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

// POV-Ray header files (base module)
#include "base/messenger_fwd.h"

// POV-Ray header files (core module)
#include "core/bounding/boundingbox.h"
#include "core/math/matrix.h"
#include "core/material/texture.h"
#include "core/scene/tagfilter.h"

namespace pov
{

//##############################################################################
///
/// @defgroup PovCoreShape Geometric Shapes
/// @ingroup PovCore
///
/// @{

//******************************************************************************
///
/// @name Object Flags
///
/// The flag field is used to store all possible flags that are
/// used for objects (up to 32).
///
/// The flages are manipulated using the following macros:
///
///     Set_Flag    (Object, Flag) : set    specified Flag in Object
///     Clear_Flag  (Object, Flag) : clear  specified Flag in Object
///     Invert_Flag (Object, Flag) : invert specified Flag in Object
///     Test_Flag   (Object, Flag) : test   specified Flag in Object
///
///     Copy_Flag   (Object1, Object2, Flag) : Set the Flag in Object1 to the
///                                            value of the Flag in Object2.
///     Bool_Flag   (Object, Flag, Bool)     : if(Bool) Set flag else Clear flag
///
/// Object is a pointer to the object.
/// Flag is the number of the flag to test.
///
/// @{

#define NO_SHADOW_FLAG            0x00000001L ///< Object doesn't cast shadows.
#define CLOSED_FLAG               0x00000002L ///< Object is closed.
#define INVERTED_FLAG             0x00000004L ///< Object is inverted.
#define SMOOTHED_FLAG             0x00000008L ///< Object is smoothed.
#define CYLINDER_FLAG             0x00000010L ///< Object is a cylinder.
#define DEGENERATE_FLAG           0x00000020L ///< Object is degenerate.
#define STURM_FLAG                0x00000040L ///< Object should use sturmian root solver.
#define OPAQUE_FLAG               0x00000080L ///< Object is opaque.
#define MULTITEXTURE_FLAG         0x00000100L ///< Object is multi-textured primitive.
#define INFINITE_FLAG             0x00000200L ///< Object is infinite.
#define HIERARCHY_FLAG            0x00000400L ///< Object can have a bounding hierarchy.
#define HOLLOW_FLAG               0x00000800L ///< Object is hollow (atmosphere inside).
#define HOLLOW_SET_FLAG           0x00001000L ///< Hollow explicitly set in scene file.
#define UV_FLAG                   0x00002000L ///< Object uses UV mapping. Set if `uv_mapping` is specified on an object itself.
#define DOUBLE_ILLUMINATE_FLAG    0x00004000L ///< Illuminate both sides of the surface.
#define NO_IMAGE_FLAG             0x00008000L ///< Object doesn't catch camera rays.
#define NO_REFLECTION_FLAG        0x00010000L ///< Object doesn't catch reflection rays.
#define NO_GLOBAL_LIGHTS_FLAG     0x00020000L ///< Object doesn't receive light from global lights.
#define NO_GLOBAL_LIGHTS_SET_FLAG 0x00040000L ///< Object doesn't receive light from global lights explicitly set in scene file. @bug This seems to be currently broken.
/* Photon-related flags */
#define PH_TARGET_FLAG            0x00080000L ///< Object is a photons target.
#define PH_PASSTHRU_FLAG          0x00100000L ///< Object is pass through object (i.e. it may let photons pass on their way to the target).
#define PH_RFL_ON_FLAG            0x00200000L ///< Object explicitly reflects photons.
#define PH_RFL_OFF_FLAG           0x00400000L ///< Object explicitly does not reflect photons.
#define PH_RFR_ON_FLAG            0x00800000L ///< Object explicitly refracts photons.
#define PH_RFR_OFF_FLAG           0x01000000L ///< Object explicitly does not refract photons.
#define PH_IGNORE_PHOTONS_FLAG    0x02000000L ///< Object does not collect photons.
#define IGNORE_RADIOSITY_FLAG     0x04000000L ///< Object doesn't receive ambient light from radiosity.
#define NO_RADIOSITY_FLAG         0x08000000L ///< Object doesn't catch radiosity rays (i.e. is invisible to radiosity).
#define CUTAWAY_TEXTURES_FLAG     0x10000000L ///< Object (or any of its parents) has cutaway_textures set.
#define PORTAL_FLAG               0x20000000L ///< Object is a @ref Portal; it stamps itself as the CSG of its hits.
#define PH_PASSTHRU_OFF_FLAG      0x40000000L ///< `pass_through off` given, so `no_shadow` does not imply pass-through.
#define FRAME_TARGET_FLAG         0x80000000L ///< A declared object a camera frames: destroying it leaves it to the camera.

#define Set_Flag(Object, Flag)     \
    { (Object)->Flags |=  (Flag); }

#define Clear_Flag(Object, Flag)   \
    { (Object)->Flags &= ~(Flag); }

#define Invert_Flag(Object, Flag)  \
    { (Object)->Flags ^=  (Flag); }

#define Test_Flag(Object, Flag)    \
    ( (Object)->Flags & (Flag))

#define Copy_Flag(Object1, Object2, Flag) \
    { (Object1)->Flags = (((Object1)->Flags) & (~Flag)) | \
                         (((Object2)->Flags) &  (Flag)); }

#define Bool_Flag(Object, Flag, Bool) \
    { if(Bool){ (Object)->Flags |=  (Flag); } else { (Object)->Flags &= ~(Flag); }}

/// @}
///
//******************************************************************************
///
/// @name Object Type Flags
///
/// The object type encodes various properties in a bit field.
///
/// @{

#define BASIC_OBJECT                0x0000u ///< Object has no special properties.
#define PATCH_OBJECT                0x0001u ///< Object has no inside, no inverse.
#define TEXTURED_OBJECT             0x0002u ///< Object has texture, possibly in children.
#define IS_COMPOUND_OBJECT          0x0004u ///< Object has children field.
#define STURM_OK_OBJECT             0x0008u ///< Object accepts the `sturm` parameter.
#define HOLDS_PORTAL_OBJECT         0x0010u ///< Object is or holds a @ref Portal.
#define LIGHT_SOURCE_OBJECT         0x0020u ///< Object is to be linked in frame.light_sources.
#define NO_INSIDE_OBJECT            0x0040u ///< Object never contains a point, though it is not a patch.
// 0x0080u currently not used
#define IS_CHILD_OBJECT             0x0100u ///< Object is inside a compound.
#define HIERARCHY_OK_OBJECT         0x0200u ///< Object accepts the `hiararchy` parameter.
#define LT_SRC_UNION_OBJECT         0x0400u ///< Object is union of light source objects only.
#define LIGHT_GROUP_OBJECT          0x0800u ///< Object is light_group union object.
#define LIGHT_GROUP_LIGHT_OBJECT    0x1000u ///< Object is light in light_group object.
#define CSG_DIFFERENCE_OBJECT       0x2000u ///< Object is CSG difference object.
#define IS_CSG_OBJECT               0x4000u ///< Object is a CSG and not some other compound object.
#define POTENTIAL_OBJECT            0x8000u ///< Object has an intrinsic potential field associated.

#define CHILDREN_FLAGS (PATCH_OBJECT+TEXTURED_OBJECT+HOLDS_PORTAL_OBJECT)  ///< Reverse inherited flags.

/// @}
///
//******************************************************************************

class Portal;
class Emitter;
struct MediaLight;
struct PhotonTargetGroup final { BoundingBox box; }; ///< the bounds of the split union the group was

/// What a box is to an object: wholly outside it and off its surface, wholly inside it and off its surface, or not known.
enum class BoxClass : std::uint8_t { Unknown, Outside, Inside };
/// What a box is to the object's inverse, for shapes whose inside test reads INVERTED_FLAG.
inline BoxClass Inverted_Class(BoxClass c, bool inverted)
{
    return !inverted ? c : (c == BoxClass::Outside) ? BoxClass::Inside : (c == BoxClass::Inside) ? BoxClass::Outside : c;
}

/// One box of where an object's surface can be, the piece of the object it belongs to, and a sphere holding that piece if known.
struct SurfaceBox final
{
    BoundingBox box;
    std::uint32_t piece = 0;
    Vector3d sphereCentre = Vector3d(0.0);
    double sphereRadius = 0.0; ///< 0: no sphere
    std::uint64_t cell = 0;    ///< the refinement cell a whole-set box covers, for Surface_Cell; 0: none
};

/// What a caller asks of an object's surface boxes: a world region, unbounded when any bound is infinite, and the finest
/// detail it can use, a world length; 0 is the region's own largest side, and an unbounded region at 0 the coarsest answer.
struct SurfaceQuery final
{
    Vector3d low = Vector3d(-HUGE_VAL), high = Vector3d(HUGE_VAL);
    double fineness = 0.0;
    bool Bounded() const;
    /// The fineness asked, or the region's largest side.
    double Fineness() const;
};

/// Receives an object's surface boxes in the order of its structure; `Enter` may skip a group whose surface lies in the box given.
class SurfaceBoxVisitor
{
    public:
        virtual ~SurfaceBoxVisitor() {}
        virtual bool Enter(const Vector3d&, const Vector3d&) { return true; }
        virtual void Visit(const SurfaceBox& box) = 0;
};

/// The render's surface map of an object, set by surface_map::Maps; a copy starts with none.
struct SurfaceMapSlot final
{
    std::atomic<void*> map{nullptr};
    SurfaceMapSlot() = default;
    SurfaceMapSlot(const SurfaceMapSlot&) {}
    SurfaceMapSlot& operator=(const SurfaceMapSlot&) { return *this; }
};

/// Copies of one declared object (Copy_Object): one geometry, each copy under the transforms applied to it since. Copies
/// are also made while rendering (per-thread lights, portals), so `live` and `made` change only under the copy lock.
struct SurfaceLineage final
{
    std::map<std::uint64_t, const void*> live;         ///< the copies alive, by the order they were made
    std::uint64_t made = 0;
    std::atomic<void*> map{nullptr};                   ///< the render's shared surface map, set by surface_map::Maps
};

/// An object's place in its lineage: none, or its order and the transforms applied since the lineage began.
struct SurfaceCopy final
{
    std::shared_ptr<SurfaceLineage> lineage;
    std::uint64_t ordinal = 0;
    TRANSFORM since;
    SurfaceCopy() { MIdentity(since.matrix); MIdentity(since.inverse); }
};

/// Abstract base class for all geometric objects.
class ObjectBase
{
    public:

        int Type; // TODO - make obsolete
        TEXTURE *Texture;
        TEXTURE *Interior_Texture;
        InteriorPtr interior;
        std::shared_ptr<MediaLight> surfaceLight;
        std::vector<ObjectPtr> Bound;
        std::vector<ObjectPtr> Clip;
        std::vector<LightSource*> LLights;  ///< Used for light groups.
        std::vector<LightSource*> PhotonLights;
        bool photonGroupEnabled = false;
        BoundingBox BBox;
        TRANSFORM *Trans;
        SNGL Ph_Density;
        double RadiosityImportance;
        bool RadiosityImportanceSet;
        unsigned int Flags;
        std::vector<std::string> tags;
        /// whether the parent's effective tags add to `tags`: true when omitted or written with `inherit`
        bool inheritTags = true;
        /// subsurface method 2's map points for this object as a whole (subsurface { domain_cells }); 0 takes its material's density over its area, at most kSubsurfacePointsPerMM2
        double subsurfaceDomainCells = 0.0;
        /// the placed union of subsurface members this object is part of, which diffuses light as one body; set once the scene is complete
        ObjectBase *subsurfaceBody = nullptr;
        /// Shared by the members of a split union that was one photon target, so method 2 keeps them one target.
        std::shared_ptr<const PhotonTargetGroup> photonTargetGroup;
        /// Where the scene wrote it ("line N of file", with its declared name if placed by name), for messages; may be null.
        std::shared_ptr<const std::string> source;
        SurfaceMapSlot surfaceMap;
        SurfaceCopy surfaceCopy;

#ifdef OBJECT_DEBUG_HELPER
        ObjectDebugHelper Debug;
#endif

        /// Construct object from scratch.
        ObjectBase(int t) :
            Type(t),
            Texture(nullptr), Interior_Texture(nullptr), interior(), Trans(nullptr),
            Ph_Density(0), RadiosityImportance(0.0), RadiosityImportanceSet(false), Flags(0)
        {
            Make_BBox(BBox, -BOUND_HUGE/2.0, -BOUND_HUGE/2.0, -BOUND_HUGE/2.0, BOUND_HUGE, BOUND_HUGE, BOUND_HUGE, false);
        }

        /// Construct object as copy of existing one.
        ///
        /// @param[in]  t           Object type.
        /// @param[in]  o           Object to copy data from.
        /// @param[in]  transplant  Whether to move data rather than copy it.
        ///
        ObjectBase(int t, ObjectBase& o, bool transplant) :
            Type(t),
            Texture(o.Texture), Interior_Texture(o.Interior_Texture), interior(o.interior), surfaceLight(o.surfaceLight), Trans(o.Trans),
            Ph_Density(o.Ph_Density), RadiosityImportance(o.RadiosityImportance),
            RadiosityImportanceSet(o.RadiosityImportanceSet), Flags(o.Flags & ~FRAME_TARGET_FLAG),
            Bound(o.Bound), Clip(o.Clip), LLights(o.LLights), PhotonLights(o.PhotonLights), photonGroupEnabled(o.photonGroupEnabled),
            BBox(o.BBox), tags(o.tags), inheritTags(o.inheritTags), subsurfaceDomainCells(o.subsurfaceDomainCells), photonTargetGroup(o.photonTargetGroup), source(o.source)
        {
            if (transplant)
            {
                o.Texture = nullptr;
                o.Interior_Texture = nullptr;
                o.interior.reset();
                o.Trans = nullptr;
                o.Bound.clear();
                o.Clip.clear();
                o.LLights.clear();
                o.PhotonLights.clear();
            }
        }
        virtual ~ObjectBase();

        virtual ObjectPtr Copy() = 0;

        /// Test the object parameters and precompute derived values.
        ///
        /// @return True if object parameters are within reasonable limits.
        ///
        virtual bool Precompute() { return true; }

        virtual bool All_Intersections(const Ray&, IStack&, TraceThreadData *) = 0; // could be "const", if it wasn't for isosurface max_gradient estimation stuff
        /// Any hit on the part that blocked this thread's last shadow ray in a similar direction, if this ray crosses it too.
        virtual bool Shadow_Hint_Intersection(const Ray&, Intersection *, TraceThreadData *) { return false; }
        /// Whether All_Intersections() reports every admitted root along the ray, not only the nearest.
        /// A shadow walk gathers such an object's roots in one call and asks any other one root at a time,
        /// so a shape that does not override this is still shadowed correctly, only more slowly.
        virtual bool ReturnsAllRoots() const { return false; }
        virtual double GetPotential (const Vector3d&, bool subtractThreshold, TraceThreadData *) const;
        virtual bool Inside(const Vector3d&, TraceThreadData *) const = 0;
        virtual void Normal(Vector3d&, Intersection *, TraceThreadData *) const = 0;
        /// The unsmoothed unit normal of the face hit, either way round; false when Normal() already is that normal.
        virtual bool FaceNormal(Vector3d&, const Intersection *) const { return false; }
        /// How far a reported intersection may lie from the true surface, in world units.
        virtual double SurfaceTolerance() const { return 0.0; }
        virtual void UVCoord(Vector2d&, const Intersection *) const;
        virtual void Translate(const Vector3d&, const TRANSFORM *) = 0;
        virtual void Rotate(const Vector3d&, const TRANSFORM *) = 0;
        virtual void Scale(const Vector3d&, const TRANSFORM *) = 0;
        virtual void Transform(const TRANSFORM *) = 0;

        /// Invert the object.
        ///
        /// @attention  This method may return a newly constructed object and destroy itself.
        ///
        virtual ObjectPtr Invert();

        virtual void Compute_BBox() = 0;

        /// Classifies the box lo..hi of a frame that toWorld maps to world (null: the box is in world space). Outside: no
        /// point of the box is inside, plain or precise, and no hit the object reports lies in it; Inside: every point is
        /// inside under both, and again no hit lies in it; anything unproven is Unknown. Clips are not this method's: see
        /// Classify_Object. The default answers from the certified box.
        virtual BoxClass Classify_Box(const Vector3d& lo, const Vector3d& hi, const TRANSFORM *toWorld) const;
        /// Whether Classify_Box can answer more than the certified box does.
        virtual bool Has_Box_Test() const { return false; }

        /// Reports where the surface can be, one box per piece, numbering the pieces from `firstPiece` in the structure's order.
        virtual void Visit_Surface_Boxes(SurfaceBoxVisitor& visitor, std::uint32_t firstPiece, const SurfaceQuery& query = SurfaceQuery()) const;
        /// The enclosure of where the surface can be at that fineness (a world length), within the certified box; false
        /// when nothing is left. The default is the certified box.
        virtual bool Surface_Extent(double fineness, Vector3d& low, Vector3d& high) const;
        /// How many pieces Visit_Surface_Boxes numbers: a union's members each count, anything else is one piece.
        virtual std::uint32_t Surface_Pieces() const { return 1; }
        /// How many leaves (shapes with a surface of their own) the object holds, numbered depth first.
        virtual std::uint32_t Surface_Leaves() const { return 1; }
        /// Whether `leafObject`, hit at `point`, is a leaf of this object; `piece` and `leaf` come in as this object's first
        /// numbers and go out as the leaf's. `split` says whether a union met on the way gives each member its own piece.
        virtual bool Find_Surface_Leaf(ConstObjectPtr leafObject, const Vector3d&, bool, std::uint32_t&, std::uint32_t&) const
        { return leafObject == this; }
        /// The leaf with that number, or null.
        virtual ObjectPtr Surface_Leaf(std::uint32_t leaf) { return (leaf == 0) ? this : nullptr; }

        virtual void Determine_Textures(Intersection *, bool, WeightedTextureVector&, TraceThreadData *Thread); // could be "(const Intersection*...) const" if it wasn't for blob specials

        /// Checks whether a given ray intersects the object's bounding box.
        /// Primitives with low-cost intersection tests may override this to always return true
        virtual bool Intersect_BBox(BBoxDirection, const BBoxVector3d&, const BBoxVector3d&, BBoxScalar = HUGE_VAL) const;

        /// Optional post-render message dispatcher.
        ///
        /// This method will be called upon completion of rendering a view. This is the appropriate
        /// place to send messages that would traditionally have been sent at the end of a render or
        /// at object destruction - e.g. IsoSurface max_gradient warnings. (object destruction isn't
        /// the place to do that anymore since a scene may persist between views).
        ///
        virtual void DispatchShutdownMessages(GenericMessenger& messenger) {};

        /// Test texture for opacity.
        ///
        /// This method will be called by the parser as part of object post-processing,
        /// to test whether the object's material is guaranteed to be fully opaque.
        ///
        /// The default implementation reports the object as opaque if if has a texture that is
        /// guaranteed to be opaque (as determined by @ref Test_Opacity()), and it has either no
        /// explicit interior texture or that texture is also guaranteed to be opaque.
        ///
        /// Primitives with innate textures (such as blob or mesh) must override this method, and
        /// return false if any of their innate textures is potentially non-opaque.
        ///
        virtual bool IsOpaque() const;

    protected:

        explicit ObjectBase(const ObjectBase&) { }
};

/// Photon rays skip the object: `no_shadow` implies pass-through unless it is a target or has `pass_through off`.
inline bool Hidden_From_Photons(const ObjectBase *o)
{
    return (o->Flags & (NO_SHADOW_FLAG | PH_TARGET_FLAG | PH_PASSTHRU_OFF_FLAG)) == NO_SHADOW_FLAG;
}

/// Photons on their way to a target pass the object unaffected.
inline bool Photon_Pass_Through(const ObjectBase *o)
{
    return Test_Flag(o, PH_PASSTHRU_FLAG) || ((o->Flags & (NO_SHADOW_FLAG | PH_PASSTHRU_OFF_FLAG)) == NO_SHADOW_FLAG);
}

/// Convenience class to derive patch objects from.
class NonsolidObject : public ObjectBase
{
    public:
        NonsolidObject(int t) : ObjectBase(t) {}
        virtual ObjectPtr Invert() override;
};

/// Abstract base class for compound geometric objects.
///
/// @note   Some special compound objects (such as mesh or blob) do _not_ derive from this class.
///
class CompoundObject : public ObjectBase
{
    public:
        CompoundObject(int t) : ObjectBase(t) {}
        CompoundObject(int t, CompoundObject& o, bool transplant) : ObjectBase(t, o, transplant), children(o.children) { if (transplant) o.children.clear(); }
        std::vector<ObjectPtr> children;
        virtual ObjectPtr Invert() override;
        /// A compound reports its children's roots, so it reports all of them only when every child does.
        virtual bool ReturnsAllRoots() const override;
        virtual std::uint32_t Surface_Leaves() const override;
        virtual bool Find_Surface_Leaf(ConstObjectPtr leafObject, const Vector3d& point, bool split, std::uint32_t& piece,
                                       std::uint32_t& leaf) const override;
        virtual ObjectPtr Surface_Leaf(std::uint32_t leaf) override;
};


/// Light source.
/// @ingroup PovCoreLightingLightsource
class LightSource final : public CompoundObject
{
    public:
        size_t index;
        MathColour colour;
        Vector3d Direction, Center, Points_At, Axis1, Axis2;
        DBL Coeff, Radius, Falloff;
        DBL Fade_Distance, Fade_Power;
        /// an emitter light's distance law: factor A'(r) = A·(1/scale)^(1−A) over Radius², A = (Radius/max(r,Radius))^power
        DBL attenuationScale = 1.0, attenuationPower = 2.0, lawRadius = 0.0;
        DBL cutoff = 0.0; ///< an emitter light leaves out points its bound lights less than this
        /// a light's domain_cells: volume lattice cells or surface nodes
        double sourceDomainCells = 0.0;
        int Area_Size1, Area_Size2;
        int Adaptive_Level;
        ObjectPtr Projected_Through_Object;

        unsigned Light_Type : 8;
        bool Area_Light : 1;
        bool Use_Full_Area_Lighting : 1; // JN2007: Full area lighting
        bool Jitter : 1;
        bool Orient : 1;
        bool Circular : 1;
        bool Parallel : 1;
        bool Photon_Area_Light : 1;
        bool Photon_Only : 1;
        bool Media_Attenuation : 1;
        bool Media_Interaction : 1;
        bool lightGroupLight : 1;

        std::vector<const LightSource *> portalImages; ///< this light as seen through each portal; the scene owns them
        const LightSource *imageOf = nullptr;           ///< in an image, the light it shows
        const Portal *portal = nullptr;                 ///< in an image, the portal it is seen through
        std::shared_ptr<const Emitter> emitter;         ///< where it emits from, for a light drawn as sample positions
        ConstObjectPtr emitterContainer = nullptr;      ///< for a media light, the object holding its medium
        ConstObjectPtr selfContainer = nullptr;         ///< for a bounce light, the container whose media scatter only selfScale of it
        MathColour selfScale = MathColour(1.0);
        bool selfOnly = false;                          ///< in a selfLight: scattered only by selfContainer's media, 1 - selfScale of it
        std::shared_ptr<LightSource> selfLight;         ///< the light as it was before bouncing, for the rest; null if it emitted none
        ConstObjectPtr ownSurface = nullptr;            ///< for a surface bounce light, its object, which receives ownLight in its place
        std::shared_ptr<LightSource> ownLight;          ///< its primary emission and its bounce's self share; null if neither
        bool bounceFed = false;                         ///< in surface bounce preparation: holds its previous pass's field, which exports receive
        bool mediaPhotonOptions = false;

        LightSource();
        virtual ~LightSource() override;

        virtual ObjectPtr Copy() override;

        virtual bool All_Intersections(const Ray&, IStack&, TraceThreadData *) override;
        virtual bool Inside(const Vector3d&, TraceThreadData *) const override;
        virtual void Normal(Vector3d&, Intersection *, TraceThreadData *) const override;
        virtual void UVCoord(Vector2d&, const Intersection *) const override;
        virtual void Translate(const Vector3d&, const TRANSFORM *) override;
        virtual void Rotate(const Vector3d&, const TRANSFORM *) override;
        virtual void Scale(const Vector3d&, const TRANSFORM *) override;
        virtual void Transform(const TRANSFORM *) override;
        virtual void Compute_BBox() override {}
};


/// Generic abstract class used for containing inherently infinite objects (isosurface, parametric).
struct ContainedByShape
{
    virtual ~ContainedByShape() {}

    virtual void ComputeBBox(BoundingBox& rBox) const = 0;
    virtual bool Intersect(const Ray& ray, const TRANSFORM* pTrans, DBL& rDepth1, DBL& rDepth2, int& rSide1, int& sSide2) const = 0;
    virtual bool Inside(const Vector3d& IPoint) const = 0;
    virtual void Normal(const Vector3d& IPoint, const TRANSFORM* pTrans, int side, Vector3d& rNormal) const = 0;
    virtual ContainedByShape* Copy() const = 0;
};

/// Class used for containing inherently infinite objects (isosurface, parametric) in a box.
struct ContainedByBox final : public ContainedByShape
{
    Vector3d corner1;
    Vector3d corner2;

    ContainedByBox() : corner1(-1,-1,-1), corner2(1,1,1) {}

    virtual void ComputeBBox(BoundingBox& rBox) const override;
    virtual bool Intersect(const Ray& ray, const TRANSFORM* pTrans, DBL& rDepth1, DBL& rDepth2, int& rSide1, int& sSide2) const override;
    virtual bool Inside(const Vector3d& IPoint) const override;
    virtual void Normal(const Vector3d& IPoint, const TRANSFORM* pTrans, int side, Vector3d& rNormal) const override;
    virtual ContainedByShape* Copy() const override;
};

/// Class used for containing inherently infinite objects (isosurface, parametric) in a sphere.
struct ContainedBySphere final : public ContainedByShape
{
    Vector3d center;
    DBL radius;

    ContainedBySphere() : center(0,0,0), radius(1) {}

    virtual void ComputeBBox(BoundingBox& rBox) const override;
    virtual bool Intersect(const Ray& ray, const TRANSFORM* pTrans, DBL& rDepth1, DBL& rDepth2, int& rSide1, int& sSide2) const override;
    virtual bool Inside(const Vector3d& IPoint) const override;
    virtual void Normal(const Vector3d& IPoint, const TRANSFORM* pTrans, int side, Vector3d& rNormal) const override;
    virtual ContainedByShape* Copy() const override;
};

bool Find_Intersection(Intersection *Ray_Intersection, ObjectPtr Object, const Ray& ray, TraceThreadData *Thread);
bool Find_Intersection(Intersection *Ray_Intersection, ObjectPtr Object, const Ray& ray, const RayObjectCondition& postcondition, TraceThreadData *Thread);
bool Find_Intersection(Intersection *isect, ObjectPtr object, const Ray& ray, BBoxDirection variant, const BBoxVector3d& origin, const BBoxVector3d& invdir, TraceThreadData *ThreadData);
bool Find_Intersection(Intersection *isect, ObjectPtr object, const Ray& ray, BBoxDirection variant, const BBoxVector3d& origin, const BBoxVector3d& invdir, const RayObjectCondition& postcondition, TraceThreadData *ThreadData);
/// Find_Intersection for an object whose bounding box the caller has already seen the ray enter.
bool Find_Intersection_BBox_Passed(Intersection *isect, ObjectPtr object, const Ray& ray, TraceThreadData *Thread);
bool Find_Intersection_BBox_Passed(Intersection *isect, ObjectPtr object, const Ray& ray, const RayObjectCondition& postcondition, TraceThreadData *Thread);
bool Ray_In_Bound(const Ray& ray, const std::vector<ObjectPtr>& Bounding_Object, TraceThreadData *Thread);
bool Point_In_Clip(const Vector3d& IPoint, const std::vector<ObjectPtr>& Clip, TraceThreadData *Thread);
ObjectPtr Copy_Object(ObjectPtr Old);
std::vector<ObjectPtr> Copy_Objects(std::vector<ObjectPtr>& Src);
void Translate_Object(ObjectPtr Object, const Vector3d& Vector, const TRANSFORM *Trans);
void Rotate_Object(ObjectPtr Object, const Vector3d& Vector, const TRANSFORM *Trans);
void Scale_Object(ObjectPtr Object, const Vector3d& Vector, const TRANSFORM *Trans);
void Transform_Object(ObjectPtr Object, const TRANSFORM *Trans);
bool Inside_Object(const Vector3d& IPoint, ObjectPtr Object, TraceThreadData *Thread);
void Destroy_Object(std::vector<ObjectPtr>& Object);
void Destroy_Object(ObjectPtr Object);
/// Flags an object infinite exactly when its bounding box is, as parsing does.
void Update_Infinite_Flag(ObjectPtr Object);
void Destroy_Single_Object(ObjectPtr *ObjectPtr);
/// Classify_Box with the object's clips: outside any clip is outside, and inside needs every clip inside too.
BoxClass Classify_Object(ConstObjectPtr object, const Vector3d& lo, const Vector3d& hi, const TRANSFORM *toWorld);
/// Whether Classify_Object can answer more than the object's certified box does.
bool Has_Box_Test(ConstObjectPtr object);
/// A short name of an object's shape for messages.
const char *ShapeName(ConstObjectPtr object);
/// Takes an object and everything in it out of its lineage: its geometry is now its own (clipped, bounded, inverted).
void Leave_Lineage(ObjectPtr object);
/// The first live copy of a lineage, under the copy lock.
ObjectPtr First_Surface_Copy(const SurfaceLineage& lineage);
/// Which piece and leaf of `object` a hit on it belongs to; false when the hit is no surface, as a portal's window is not.
bool Surface_Part(ConstObjectPtr object, const Intersection& hit, std::uint32_t& piece, std::uint32_t& leaf);
/// Appends, in the structure's order, the surface boxes of `object` that meet the box from `low` to `high`, at `fineness`
/// (0: the box's own largest side). A bounded box gets at most one box per piece.
void Surface_Boxes(ConstObjectPtr object, const Vector3d& low, const Vector3d& high, std::vector<SurfaceBox>& out, double fineness = 0.0);

/// @}
///
//##############################################################################

}
// end of namespace pov

#endif // POVRAY_CORE_OBJECT_H
