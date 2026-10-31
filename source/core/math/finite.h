#ifndef POVRAY_CORE_FINITE_H
#define POVRAY_CORE_FINITE_H

#include "core/configcore.h"
#include <cstdint>
#include <cstring>
#include <limits>

namespace pov
{

inline std::uint64_t NumberBits(DBL value)
{
    static_assert(sizeof(DBL) == sizeof(std::uint64_t) && std::numeric_limits<DBL>::is_iec559,
                  "Numeric classification requires IEEE binary64");
    std::uint64_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

inline bool IsFiniteNumber(DBL value)
{
    return (NumberBits(value) & UINT64_C(0x7ff0000000000000)) != UINT64_C(0x7ff0000000000000);
}

inline bool IsInfiniteNumber(DBL value)
{
    return (NumberBits(value) & UINT64_C(0x7fffffffffffffff)) == UINT64_C(0x7ff0000000000000);
}

inline bool IsNaNNumber(DBL value)
{
    return (NumberBits(value) & UINT64_C(0x7fffffffffffffff)) > UINT64_C(0x7ff0000000000000);
}

}

#endif
