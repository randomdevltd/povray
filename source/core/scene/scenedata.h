//******************************************************************************
///
/// @file core/scene/scenedata.h
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

#ifndef POVRAY_CORE_SCENEDATA_H
#define POVRAY_CORE_SCENEDATA_H

// Module config header file must be the first file included within POV-Ray unit header files
#include "core/configcore.h"
#include "core/scene/scenedata_fwd.h"

// C++ variants of C standard header files
//  (none at the moment)

// C++ standard header files
#include <atomic>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <unordered_set>

// POV-Ray header files (base module)
#include "base/image/colourspace_fwd.h"

// POV-Ray header files (core module)
#include "core/lighting/radiosity.h"
#include "core/bounding/boundingbox_fwd.h"
#include "core/scene/atmosphere_fwd.h"
#include "core/scene/camera.h"
#include "core/scene/tagfilter.h"
#include "core/shape/truetype.h"

namespace pov
{

//##############################################################################
///
/// @addtogroup PovCore
///
/// @{

using namespace pov_base;

class BSPTree;
class SubsurfaceCache;
struct DeferredMeshState;

/// What remains of an object under a filter. An excluded object is empty space, so an excluded inverted operand, such
/// as a difference's cutter, is everything; `Part` means some descendants were removed.
enum class TagSelection { Empty, Everything, Whole, Part };

TagSelection SelectTagged(ConstObjectPtr object, const TagFilter& filter, const std::vector<std::string> *inherited = nullptr);
bool EnclosingTagsMatch(ConstObjectPtr object, const TagFilter& filter);
/// Destroys the descendants of a parsed root that the filter removes; the root itself is left to the caller.
TagSelection PruneTagged(ObjectPtr object, const TagFilter& filter, std::vector<LightSource*>& removedLights);
void ForgetLights(ObjectPtr object, const std::vector<LightSource*>& lights);

struct PreparedSet final
{
    std::vector<ObjectPtr> objects;
    /// compounds rebuilt for this set with only the children it selects; they share those children
    std::vector<ObjectPtr> views;
    std::vector<LightSource*> lights;
    /// indices into SceneData::lightSources (and each thread's copies) of the global lights in this set
    std::vector<unsigned int> globalLights;
    /// membership by LightSource::index, consulted only where a portal crossing changes set
    std::vector<bool> globalLightIn, groupLightIn;
    bool groupLightsFiltered = false;
    bool UsesGroupLight(unsigned int index) const { return !groupLightsFiltered || groupLightIn[index]; }
    std::vector<const LightSource*> globalPortalImages;
    /// by group light index: that light's images through portals in this set
    std::vector<std::vector<const LightSource*>> groupPortalImages;
    std::vector<const Portal*> portalMouths;
    PhotonMap surfacePhotonMap;
    PhotonMap mediaPhotonMap;
    std::string photonKey;
    unsigned int boundingMethod = 0;
    unsigned int numberOfFiniteObjects = 0;
    unsigned int numberOfInfiniteObjects = 0;
    BBOX_TREE *boundingSlabs = nullptr;
    FlatBBoxTree *flatSlabs = nullptr;
    BSPTree *tree = nullptr;
    unsigned int nodes = 0, splitNodes = 0, objectNodes = 0, emptyNodes = 0, maxObjects = 0, maxDepth = 0, aborts = 0;
    float averageObjects = 0.0f, averageDepth = 0.0f, averageAborts = 0.0f, averageAbortObjects = 0.0f;

    ~PreparedSet();
};

/// Class holding scene specific data.
///
/// "For private use by Scene, View and Renderer classes only!
/// This class just provides access to scene specific data
/// needed in many parts of the code. Be aware that while most
/// data is members are public, they shall **not** be modified
/// carelessly. Code from POV-Ray v3.6 and earlier does depend
/// on simple access of this data all over the old code, so
/// this provides an efficient way to reuse all the old code.
/// By no means shall this way of data access be used for any
/// other newly created classes!!!"
///
class SceneData
{
    public:
        mutable std::atomic<unsigned> mediaWarningFlags{0};

        typedef std::map<std::string, std::string> DeclaredVariablesMap;

        /// Destructor.
        virtual ~SceneData();

        /// list of all shape objects
        std::vector<ObjectPtr> objects;
        TagFilter defaultFilterTags;
        TagFilter parseFilterTags;
        /// meshes recorded during parsing, in source order, and how many of them a geometry query built then
        std::vector<std::weak_ptr<DeferredMeshState>> deferredMeshes;
        std::shared_ptr<std::atomic<bool>> meshBuildCancelled = std::make_shared<std::atomic<bool>>(false);
        unsigned int deferredMeshesBuiltForQueries = 0;
        std::vector<std::unique_ptr<PreparedSet>> preparedSets;
        std::map<TagFilter, PreparedSetId> preparedSetFilters;
        unsigned int maxPreparedSetFiniteObjects = 0;

        TagFilter EffectiveFilterTags(const TagFilter& requested) const;
        PreparedSetId RegisterPreparedSet(const TagFilter& filter);
        PreparedSetId FindPreparedSet(const TagFilter& filter) const;
        PreparedSet& GetPreparedSet(PreparedSetId index) { return *preparedSets.at(index); }
        const PreparedSet& GetPreparedSet(PreparedSetId index) const { return *preparedSets.at(index); }
        bool LightInPreparedSet(const LightSource& light, PreparedSetId index) const;
        void FinalizeScreenCameras();
        /// list of all global light sources
        std::vector<LightSource*> lightSources;
        /// list of all lights that are part of light groups
        std::vector<LightSource*> lightGroupLightSources;
        /// lights as seen through portals, owned here; empty when no light reaches through a portal
        std::vector<LightSource*> portalLights;
        /// every portal mouth with an open side; empty in a scene without portals
        std::vector<const Portal*> portalMouths;
        /// factory generating contexts for legacy VM-based functions in scene
        GenericFunctionContextFactoryIPtr functionContextFactory;
        /// atmosphere index of refraction
        DBL atmosphereIOR;
        /// atmosphere dispersion
        DBL atmosphereDispersion;
        /// atmospheric media
        std::vector<Media> atmosphere;
        /// background color - TODO - allow pattern here (useful for background image maps) [trf]
        TransColour backgroundColour; // may have a filter/transmit component (but filter is ignored)
        /// ambient light in scene
        MathColour ambientLight;
        /// TODO - what is this again? [trf]
        MathColour iridWavelengths;
        /// fog in scene
        Fog_Struct *fog;
        /// rainbow in scene
        Rainbow_Struct *rainbow;
        /// skysphere around scene
        Skysphere_Struct *skysphere;
        /// language version to assume
        int languageVersion;
        /// true if a #version statement has been encountered
        bool languageVersionSet;
        /// true if the first #version statement was found after other declarations
        bool languageVersionLate;
        /// warning level
        int warningLevel;
        /// legacy `charset` global setting
        LegacyCharset legacyCharset;
        /// default noise generator to use
        int noiseGenerator;
        /// whether or not the noise generator was explicitly set by the scene - TODO FIXME remove [trf]
        bool explicitNoiseGenerator;
        /// bounding method selector
        unsigned int boundingMethod;
        /// Working gamma.
        pov_base::SimpleGammaCurvePtr workingGamma;
        /// Working gamma to sRGB encoding/decoding.
        pov_base::GammaCurvePtr workingGammaToSRGB;
        /// Default assumed decoding gamma of input files.
        SimpleGammaCurvePtr inputFileGamma;
        /// What gamma mode to use.
        /// One of kPOVList_GammaMode_*.
        int gammaMode;

        /// distance scaling factor for subsurface scattering effects
        DBL mmPerUnit;

        /// whether the scene should be rendered with alpha channel
        bool outputAlpha;

        /// whether to use subsurface light transport
        bool useSubsurface;
        /// samples for subsurface scattering diffuse contribution
        int subsurfaceSamplesDiffuse;
        /// samples for subsurface scattering single scattering contribution
        int subsurfaceSamplesSingle;
        /// whether to compute radiosity contribution to subsurface effects
        bool subsurfaceUseRadiosity;
        /// how subsurface light is found where a finish does not say: kSubsurfaceMethodSampled or kSubsurfaceMethodPointCloud
        int subsurfaceMethod;
        bool explicitSubsurfaceMethod;

        /// whether any medium mixes other than by adding, has a priority, or an interior clears
        bool mediaBlendModes;
        /// whether a non-hollow interior on a ray hides interior media too, as before language version 4.0
        bool solidBlocksInteriorMedia;
        /// whether any object carries interior media
        bool interiorMedia;
        /// whether any interior medium refracts, so rays curve where its density varies
        bool mediaRefraction;
        /// whether surfaces refract by the innermost interior entered, as before language version 4.0
        bool legacyIorStack;
        /// the largest change of direction, in degrees, a curved ray takes in one step
        double refractionAngle;
        /// whether any interior has an ior or dispersion other than the atmosphere's, or refracting media
        bool dielectrics;
        /// whether any object is guaranteed opaque
        bool anyOpaque;
        /// the lowest Interior::precedence of any interior with media
        unsigned int firstMediaPrecedence;
        /// point-cloud method: how coarsely far groups of points may be summed as one
        double subsurfaceErrorBound;
        /// point-cloud method: point spacing relative to the automatic spacing
        double subsurfaceSpacing;

        // ********************************************************************************
        // temporary variables for BSP testing ... we may or may not keep these in future
        // releases, depending on whether or not a valid need for them has been demonstrated.
        // ********************************************************************************
        unsigned int bspMaxDepth;
        float bspObjectIsectCost;
        float bspBaseAccessCost;
        float bspChildAccessCost;
        float bspMissChance;

        /// set if real-time raytracing is enabled.
        bool realTimeRaytracing;

        // ********************************************************************************
        // Old globals from v3.6 and earlier are temporarily kept below. Please carefully
        // consider what is added and mark it accordingly if it needs to go away again
        // prior to final release! [trf]
        // ********************************************************************************

        /// number of waves to use in waves and ripples pattern
        unsigned int numberOfWaves;

        /// maximum trace recursion level allowed
        unsigned int parsedMaxTraceLevel;
        /// adc bailout
        DBL parsedAdcBailout;
        /// radiosity settings
        SceneRadiositySettings radiositySettings;

        double surfacePhotonMinGatherRad = 0.0;
        double surfacePhotonMinGatherRadMult = 1.0;
        double mediaPhotonMinGatherRad = 0.0;
        double mediaPhotonMinGatherRadMult = 1.0;

        ScenePhotonSettings photonSettings; // TODO FIXME - is modified! [trf]

        // TODO - decide if we want to keep this here
        // (we can't move it to the parser though, as part of the data needs to survive into rendering)
        std::vector<TrueTypeFont*> TTFonts;

        // name of the parsed file
        UCS2String inputFile; // TODO - handle differently
        UCS2String headerFile;

        /// Aspect ratio of the output image.
        DBL aspectRatio;

        int defaultFileType;

        FrameSettings frameSettings; // TODO - move ???
        DeclaredVariablesMap declaredVariables; // TODO - move to parser
        Camera parsedCamera; // TODO - handle differently or move to parser
        bool clocklessAnimation; // TODO - this is support for an experimental feature and may be changed or removed
        std::vector<Camera> cameras; // TODO - this is support for an experimental feature and may be changed or removed
        /// The cameras of the scene's screens, one per screen block parsed.
        std::vector<std::shared_ptr<const Camera>> screenCameras;
        std::vector<std::weak_ptr<const Camera>> screenCameraCandidates;

        // this is for fractal support
        int Fractal_Iteration_Stack_Length; // TODO - move somewhere else
        int Max_Blob_Components; // TODO - move somewhere else
        // lathe and sor support (bounding cylinders)
        unsigned int Max_Bounding_Cylinders; // TODO - move somewhere else
        BBOX_TREE *boundingSlabs;
        FlatBBoxTree *flatSlabs;

        // TODO FIXME move to parser somehow
        bool splitUnions; // INI option, defaults to false
        bool removeBounds; // INI option, defaults to true

        // experimental
        BSPTree *tree;
        unsigned int numberOfFiniteObjects;
        unsigned int numberOfInfiniteObjects;

        // BSP statistics // TODO - not sure if this is the best place for stats
        unsigned int nodes, splitNodes, objectNodes, emptyNodes, maxObjects, maxDepth, aborts;
        float averageObjects, averageDepth, averageAborts, averageAbortObjects;


        /// Convenience function to determine the effective SDL version.
        ///
        /// Given that version v3.7 and later require the first statement in the file to be
        /// a `#version` statement, the absence of such a statement can be presumed to
        /// indicate a pre-v3.7 scene; this function assumes v3.6.2 in that case
        /// (which was the latest version before v3.7.0).
        /// @note       It is recommended to use this function only where behaviour differs
        ///             significantly from pre-v3.7 versions.
        /// @return     The current language version in integer format (e.g. 370 for v3.7.0)
        ///             if explicitly specified, or 362 otherwise.
        ///
        inline unsigned int EffectiveLanguageVersion() const
        {
            if (languageVersionSet)
                return languageVersion;
            else
                return 362;
        }

        /// Create new scene specific data.
        SceneData();

    private:

        SceneData(const SceneData&) = delete;
        SceneData& operator=(const SceneData&) = delete;
};

/// @}
///
//##############################################################################

}
// end of namespace pov

#endif // POVRAY_CORE_SCENEDATA_H
