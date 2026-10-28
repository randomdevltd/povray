#ifndef POVRAY_CORE_INTERIORFIELD_H
#define POVRAY_CORE_INTERIORFIELD_H

#include "core/configcore.h"
#include "core/coretypes.h"
#include "core/material/preparedfield.h"
#include "core/material/preparedlightaxis.h"
#include "core/material/preparedrefractionaxis.h"

namespace pov
{

class PreparedRefractionDefinition final
{
    public:
        PreparedRefractionComponent component;
        DBL detail = HUGE_VAL;
        bool varying = false;
        std::vector<std::shared_ptr<const PreparedGridLayout>> layouts;
};

struct PreparedLightComponent final
{
    const LightSource *source = nullptr;
    std::shared_ptr<const PreparedLightAxis> payload;
};

class InteriorField final
{
    public:
        ObjectPtr owner;
        std::shared_ptr<const PreparedGridLayout> layout;
        std::vector<PreparedLightComponent> lights;
        bool mediaReady = false, lightReady = false;

        InteriorField(ObjectPtr owner, std::shared_ptr<const PreparedGridLayout> layout) :
            owner(owner), layout(std::move(layout)) {}
};

}

#endif
