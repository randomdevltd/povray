#ifndef POVRAY_CORE_PHOTONCAMERACACHE_H
#define POVRAY_CORE_PHOTONCAMERACACHE_H
#include "base/colour.h"
#include "core/coretypes.h"
#include "core/math/vector.h"
#include <cstdint>
#include <vector>
namespace pov {
struct PhotonCameraQuery final {
    Vector3d point, normal, rawNormal, eye;
    MathColour pigment, coefficient = MathColour(1.0);
    const FINISH* finish = nullptr;
    ConstObjectPtr receiver = nullptr;
    PreparedSetId set = 0;
    std::uint64_t key = 0;
    double attenuation = 1.0, eta = 1.0;
    bool subsurface = false;
};
struct PhotonCameraCache final {
    std::vector<PhotonCameraQuery> queries;
    MathColour firstPhotons;
    RGBTColour base;
    bool valid = true, complete = false;
};
}
#endif
