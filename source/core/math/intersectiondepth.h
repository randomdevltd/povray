#ifndef POVRAY_CORE_INTERSECTIONDEPTH_H
#define POVRAY_CORE_INTERSECTIONDEPTH_H

#include <cstdint>
#include <cstring>
#include <limits>

namespace pov
{

// Keep finite checks effective with -ffast-math; callers retain their own lower depth tolerance.
inline bool IsFiniteIntersectionDepth(double depth)
{
    static_assert(sizeof(double) == sizeof(std::uint64_t) && std::numeric_limits<double>::is_iec559,
                  "Intersection depths require IEEE 754 binary64");
    std::uint64_t bits;
    std::memcpy(&bits, &depth, sizeof(bits));
    return (bits & UINT64_C(0x7ff0000000000000)) != UINT64_C(0x7ff0000000000000);
}

}

#endif
