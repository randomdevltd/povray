//******************************************************************************
///
/// @file core/math/simd.h
///
/// Fixed-width SIMD vectors for kernels, over xsimd.
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

#ifndef POVRAY_CORE_SIMD_H
#define POVRAY_CORE_SIMD_H

// Module config header file must be the first file included within POV-Ray unit header files
#include "core/configcore.h"

// C++ variants of C standard header files
#include <cmath>
#include <cstddef>
#include <cstdint>

// C++ standard header files
#include <type_traits>

#if !POV_SIMD_DISABLED
    #include "xsimd/xsimd.hpp"
#endif
#if defined(_MSC_VER)
    #include <intrin.h>
#endif

#if defined(__GNUC__)
    #define POV_SIMD_INLINE inline __attribute__((always_inline))
#elif defined(_MSC_VER)
    #define POV_SIMD_INLINE __forceinline
#else
    #define POV_SIMD_INLINE inline
#endif

namespace pov
{

/// `Vec<T, N>` is one register where the build's `-march` has one of N lanes of T, two halves where it has fewer, and
/// plain scalars with POV_SIMD_DISABLED; kernels use it, never xsimd, so only this file knows the library.
namespace simd
{

namespace detail
{

enum Repr { SCALAR, NATIVE, SPLIT };

#if POV_SIMD_DISABLED
template<typename T, int N> struct NativeOf { typedef void type; };
#else
template<typename T, int N> struct NativeOf { typedef xsimd::make_sized_batch_t<T, std::size_t(N)> type; };
#endif

template<typename T, int N> struct ReprOf
{
    static_assert((N > 0) && ((N & (N - 1)) == 0), "lane counts are powers of two");
    static const Repr value = (N == 1) ? SCALAR : std::is_void<typename NativeOf<T, N>::type>::value ? SPLIT : NATIVE;
};

}
// end of namespace detail

template<typename T, int N, detail::Repr R = detail::ReprOf<T, N>::value> struct Vec;
template<typename T, int N, detail::Repr R = detail::ReprOf<T, N>::value> struct Mask;

template<typename T> using SVec = Vec<T, 1, detail::SCALAR>;
template<typename T> using SMask = Mask<T, 1, detail::SCALAR>;
template<typename T, int N> using NVec = Vec<T, N, detail::NATIVE>;
template<typename T, int N> using NMask = Mask<T, N, detail::NATIVE>;
template<typename T, int N> using PVec = Vec<T, N, detail::SPLIT>;
template<typename T, int N> using PMask = Mask<T, N, detail::SPLIT>;

namespace detail
{

#if defined(FP_FAST_FMA)
POV_SIMD_INLINE double fmadd(double a, double b, double c) { return std::fma(a, b, c); }
#else
POV_SIMD_INLINE double fmadd(double a, double b, double c) { return a * b + c; }
#endif
#if defined(FP_FAST_FMAF)
POV_SIMD_INLINE float fmadd(float a, float b, float c) { return std::fma(a, b, c); }
#else
POV_SIMD_INLINE float fmadd(float a, float b, float c) { return a * b + c; }
#endif

}
// end of namespace detail

/// Plain values where the target has no instructions of this width, fused like SIMD fma only where the target has FMA.
/// min and max return b on ties and NaN as x86's instructions do, so they match SIMD lane for lane on x86 only.
template<typename T>
struct Mask<T, 1, detail::SCALAR> final
{
    bool m;
};

template<typename T>
struct Vec<T, 1, detail::SCALAR> final
{
    typedef SMask<T> MaskType;
    T v;
    Vec() = default;
    POV_SIMD_INLINE Vec(T s) : v(s) {}
    static POV_SIMD_INLINE Vec load(const T *p) { return Vec(p[0]); }
    POV_SIMD_INLINE void store(T *p) const { p[0] = v; }
};

#define POV_SIMD_SCALAR_BINARY(op) \
    template<typename T> POV_SIMD_INLINE SVec<T> operator op(SVec<T> a, SVec<T> b) { return SVec<T>(a.v op b.v); }
#define POV_SIMD_SCALAR_COMPARE(op) \
    template<typename T> POV_SIMD_INLINE SMask<T> operator op(SVec<T> a, SVec<T> b) { return SMask<T>{a.v op b.v}; }
#define POV_SIMD_SCALAR_LOGIC(op) \
    template<typename T> POV_SIMD_INLINE SMask<T> operator op(SMask<T> a, SMask<T> b) { return SMask<T>{bool(a.m op b.m)}; }

POV_SIMD_SCALAR_BINARY(+) POV_SIMD_SCALAR_BINARY(-) POV_SIMD_SCALAR_BINARY(*) POV_SIMD_SCALAR_BINARY(/)
POV_SIMD_SCALAR_COMPARE(<) POV_SIMD_SCALAR_COMPARE(<=) POV_SIMD_SCALAR_COMPARE(>) POV_SIMD_SCALAR_COMPARE(>=)
POV_SIMD_SCALAR_COMPARE(==) POV_SIMD_SCALAR_COMPARE(!=)
POV_SIMD_SCALAR_LOGIC(&) POV_SIMD_SCALAR_LOGIC(|) POV_SIMD_SCALAR_LOGIC(^)

template<typename T> POV_SIMD_INLINE SVec<T> operator-(SVec<T> a) { return SVec<T>(-a.v); }
template<typename T> POV_SIMD_INLINE SMask<T> operator~(SMask<T> a) { return SMask<T>{!a.m}; }
template<typename T> POV_SIMD_INLINE SVec<T> fma(SVec<T> a, SVec<T> b, SVec<T> c) { return detail::fmadd(a.v, b.v, c.v); }
template<typename T> POV_SIMD_INLINE SVec<T> min(SVec<T> a, SVec<T> b) { return (a.v < b.v) ? a : b; }
template<typename T> POV_SIMD_INLINE SVec<T> max(SVec<T> a, SVec<T> b) { return (a.v > b.v) ? a : b; }
template<typename T> POV_SIMD_INLINE SVec<T> sqrt(SVec<T> a) { return SVec<T>(std::sqrt(a.v)); }
template<typename T> POV_SIMD_INLINE SVec<T> select(SMask<T> m, SVec<T> a, SVec<T> b) { return m.m ? a : b; }
template<typename T> POV_SIMD_INLINE bool any(SMask<T> m) { return m.m; }
template<typename T> POV_SIMD_INLINE bool all(SMask<T> m) { return m.m; }
template<typename T> POV_SIMD_INLINE std::uint32_t bits(SMask<T> m) { return std::uint32_t(m.m); }

#undef POV_SIMD_SCALAR_BINARY
#undef POV_SIMD_SCALAR_COMPARE
#undef POV_SIMD_SCALAR_LOGIC

#if !POV_SIMD_DISABLED

/// One register: an xsimd batch, and its mask type, which is the target's own (a vector, or an AVX-512 mask register).
template<typename T, int N>
struct Mask<T, N, detail::NATIVE> final
{
    typename detail::NativeOf<T, N>::type::batch_bool_type m;
};

template<typename T, int N>
struct Vec<T, N, detail::NATIVE> final
{
    typedef typename detail::NativeOf<T, N>::type Batch;
    typedef NMask<T, N> MaskType;
    Batch v;
    Vec() = default;
    POV_SIMD_INLINE Vec(T s) : v(s) {}
    POV_SIMD_INLINE Vec(Batch b) : v(b) {}
    static POV_SIMD_INLINE Vec load(const T *p) { return Vec(Batch::load_unaligned(p)); }
    POV_SIMD_INLINE void store(T *p) const { v.store_unaligned(p); }
};

#define POV_SIMD_NATIVE_BINARY(op) \
    template<typename T, int N> \
    POV_SIMD_INLINE NVec<T, N> operator op(NVec<T, N> a, NVec<T, N> b) { return NVec<T, N>(a.v op b.v); }
#define POV_SIMD_NATIVE_COMPARE(op) \
    template<typename T, int N> \
    POV_SIMD_INLINE NMask<T, N> operator op(NVec<T, N> a, NVec<T, N> b) { return NMask<T, N>{a.v op b.v}; }
#define POV_SIMD_NATIVE_LOGIC(op) \
    template<typename T, int N> \
    POV_SIMD_INLINE NMask<T, N> operator op(NMask<T, N> a, NMask<T, N> b) { return NMask<T, N>{a.m op b.m}; }

POV_SIMD_NATIVE_BINARY(+) POV_SIMD_NATIVE_BINARY(-) POV_SIMD_NATIVE_BINARY(*) POV_SIMD_NATIVE_BINARY(/)
POV_SIMD_NATIVE_COMPARE(<) POV_SIMD_NATIVE_COMPARE(<=) POV_SIMD_NATIVE_COMPARE(>) POV_SIMD_NATIVE_COMPARE(>=)
POV_SIMD_NATIVE_COMPARE(==) POV_SIMD_NATIVE_COMPARE(!=)
POV_SIMD_NATIVE_LOGIC(&) POV_SIMD_NATIVE_LOGIC(|) POV_SIMD_NATIVE_LOGIC(^)

template<typename T, int N> POV_SIMD_INLINE NVec<T, N> operator-(NVec<T, N> a) { return NVec<T, N>(-a.v); }
template<typename T, int N> POV_SIMD_INLINE NMask<T, N> operator~(NMask<T, N> a) { return NMask<T, N>{~a.m}; }
template<typename T, int N>
POV_SIMD_INLINE NVec<T, N> fma(NVec<T, N> a, NVec<T, N> b, NVec<T, N> c) { return NVec<T, N>(xsimd::fma(a.v, b.v, c.v)); }
template<typename T, int N> POV_SIMD_INLINE NVec<T, N> min(NVec<T, N> a, NVec<T, N> b) { return NVec<T, N>(xsimd::min(a.v, b.v)); }
template<typename T, int N> POV_SIMD_INLINE NVec<T, N> max(NVec<T, N> a, NVec<T, N> b) { return NVec<T, N>(xsimd::max(a.v, b.v)); }
template<typename T, int N> POV_SIMD_INLINE NVec<T, N> sqrt(NVec<T, N> a) { return NVec<T, N>(xsimd::sqrt(a.v)); }
template<typename T, int N>
POV_SIMD_INLINE NVec<T, N> select(NMask<T, N> m, NVec<T, N> a, NVec<T, N> b) { return NVec<T, N>(xsimd::select(m.m, a.v, b.v)); }
template<typename T, int N> POV_SIMD_INLINE bool any(NMask<T, N> m) { return xsimd::any(m.m); }
template<typename T, int N> POV_SIMD_INLINE bool all(NMask<T, N> m) { return xsimd::all(m.m); }
template<typename T, int N>
POV_SIMD_INLINE std::uint32_t bits(NMask<T, N> m)
{
    static_assert(N <= 32, "bits() holds 32 lanes");
    return std::uint32_t(m.m.mask());
}

#undef POV_SIMD_NATIVE_BINARY
#undef POV_SIMD_NATIVE_COMPARE
#undef POV_SIMD_NATIVE_LOGIC

#endif

/// Wider than the target: two halves, each split again or native.
template<typename T, int N>
struct Mask<T, N, detail::SPLIT> final
{
    Mask<T, N / 2> lo, hi;
};

template<typename T, int N>
struct Vec<T, N, detail::SPLIT> final
{
    typedef Vec<T, N / 2> Half;
    typedef PMask<T, N> MaskType;
    Half lo, hi;
    Vec() = default;
    POV_SIMD_INLINE Vec(T s) : lo(s), hi(s) {}
    POV_SIMD_INLINE Vec(Half l, Half h) : lo(l), hi(h) {}
    static POV_SIMD_INLINE Vec load(const T *p) { return Vec(Half::load(p), Half::load(p + N / 2)); }
    POV_SIMD_INLINE void store(T *p) const { lo.store(p); hi.store(p + N / 2); }
};

#define POV_SIMD_SPLIT_BINARY(op) \
    template<typename T, int N> \
    POV_SIMD_INLINE PVec<T, N> operator op(PVec<T, N> a, PVec<T, N> b) { return PVec<T, N>(a.lo op b.lo, a.hi op b.hi); }
#define POV_SIMD_SPLIT_COMPARE(op) \
    template<typename T, int N> \
    POV_SIMD_INLINE PMask<T, N> operator op(PVec<T, N> a, PVec<T, N> b) { return PMask<T, N>{a.lo op b.lo, a.hi op b.hi}; }
#define POV_SIMD_SPLIT_LOGIC(op) \
    template<typename T, int N> \
    POV_SIMD_INLINE PMask<T, N> operator op(PMask<T, N> a, PMask<T, N> b) { return PMask<T, N>{a.lo op b.lo, a.hi op b.hi}; }

POV_SIMD_SPLIT_BINARY(+) POV_SIMD_SPLIT_BINARY(-) POV_SIMD_SPLIT_BINARY(*) POV_SIMD_SPLIT_BINARY(/)
POV_SIMD_SPLIT_COMPARE(<) POV_SIMD_SPLIT_COMPARE(<=) POV_SIMD_SPLIT_COMPARE(>) POV_SIMD_SPLIT_COMPARE(>=)
POV_SIMD_SPLIT_COMPARE(==) POV_SIMD_SPLIT_COMPARE(!=)
POV_SIMD_SPLIT_LOGIC(&) POV_SIMD_SPLIT_LOGIC(|) POV_SIMD_SPLIT_LOGIC(^)

template<typename T, int N> POV_SIMD_INLINE PVec<T, N> operator-(PVec<T, N> a) { return PVec<T, N>(-a.lo, -a.hi); }
template<typename T, int N> POV_SIMD_INLINE PMask<T, N> operator~(PMask<T, N> a) { return PMask<T, N>{~a.lo, ~a.hi}; }
template<typename T, int N>
POV_SIMD_INLINE PVec<T, N> fma(PVec<T, N> a, PVec<T, N> b, PVec<T, N> c)
{
    return PVec<T, N>(fma(a.lo, b.lo, c.lo), fma(a.hi, b.hi, c.hi));
}
template<typename T, int N>
POV_SIMD_INLINE PVec<T, N> min(PVec<T, N> a, PVec<T, N> b) { return PVec<T, N>(min(a.lo, b.lo), min(a.hi, b.hi)); }
template<typename T, int N>
POV_SIMD_INLINE PVec<T, N> max(PVec<T, N> a, PVec<T, N> b) { return PVec<T, N>(max(a.lo, b.lo), max(a.hi, b.hi)); }
template<typename T, int N> POV_SIMD_INLINE PVec<T, N> sqrt(PVec<T, N> a) { return PVec<T, N>(sqrt(a.lo), sqrt(a.hi)); }
template<typename T, int N>
POV_SIMD_INLINE PVec<T, N> select(PMask<T, N> m, PVec<T, N> a, PVec<T, N> b)
{
    return PVec<T, N>(select(m.lo, a.lo, b.lo), select(m.hi, a.hi, b.hi));
}
template<typename T, int N> POV_SIMD_INLINE bool any(PMask<T, N> m) { return any(m.lo) || any(m.hi); }
template<typename T, int N> POV_SIMD_INLINE bool all(PMask<T, N> m) { return all(m.lo) && all(m.hi); }
template<typename T, int N>
POV_SIMD_INLINE std::uint32_t bits(PMask<T, N> m)
{
    static_assert(N <= 32, "bits() holds 32 lanes");
    return bits(m.lo) | (bits(m.hi) << (N / 2));
}

#undef POV_SIMD_SPLIT_BINARY
#undef POV_SIMD_SPLIT_COMPARE
#undef POV_SIMD_SPLIT_LOGIC

namespace detail
{

template<int N, Repr R = ReprOf<float, N>::value>
struct Widen final
{
    static POV_SIMD_INLINE Vec<float, N> load(const std::uint16_t *p)
    {
        float f[N];
        for (int k = 0; k < N; ++k)
            f[k] = float(p[k]);
        return Vec<float, N>::load(f);
    }
};

template<int N>
struct Widen<N, SPLIT> final
{
    static POV_SIMD_INLINE Vec<float, N> load(const std::uint16_t *p)
    {
        return Vec<float, N>(Widen<N / 2>::load(p), Widen<N / 2>::load(p + N / 2));
    }
};

#if !POV_SIMD_DISABLED && defined(__AVX2__)
template<>
struct Widen<8, NATIVE> final
{
    static POV_SIMD_INLINE Vec<float, 8> load(const std::uint16_t *p)
    {
        const __m128i q = _mm_loadu_si128(reinterpret_cast<const __m128i *>(p));
        return Vec<float, 8>(Vec<float, 8>::Batch(_mm256_cvtepi32_ps(_mm256_cvtepu16_epi32(q))));
    }
};
#endif

}
// end of namespace detail

/// N unsigned 16-bit values, loaded as floats.
template<int N>
POV_SIMD_INLINE Vec<float, N> load_widened(const std::uint16_t *p) { return detail::Widen<N>::load(p); }

/// The lowest set lane of a nonzero `bits()` result.
POV_SIMD_INLINE int lowest_lane(std::uint32_t lanes)
{
#if defined(__GNUC__)
    return __builtin_ctz(lanes);
#elif defined(_MSC_VER)
    unsigned long k;
    _BitScanForward(&k, lanes);
    return int(k);
#else
    int k = 0;
    while (!(lanes & 1u)) { lanes >>= 1; ++k; }
    return k;
#endif
}

}
// end of namespace simd

}
// end of namespace pov

#endif // POVRAY_CORE_SIMD_H
