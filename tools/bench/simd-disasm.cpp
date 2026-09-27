// A 16-bit block's slab test written through simd:: and in raw AVX2 intrinsics; simd-disasm.sh compares their code.
#include "core/math/simd.h"

using namespace pov;

struct QBlock
{
    float origin[3], scale[3];
    std::uint16_t lo[3][8], hi[3][8];
};

extern "C" std::uint32_t slabs_simd(const QBlock& b, const float *inv, const float *originInv, float maxDepth, float *depth)
{
    typedef simd::Vec<float, 8> Lanes;
    Lanes n[3], f[3];
    for (int d = 0; d < 3; ++d)
    {
        const Lanes at(b.origin[d] * inv[d] - originInv[d]), step(b.scale[d] * inv[d]);
        const Lanes l = simd::fma(simd::load_widened<8>(b.lo[d]), step, at);
        const Lanes h = simd::fma(simd::load_widened<8>(b.hi[d]), step, at);
        n[d] = simd::min(h, l);
        f[d] = simd::max(l, h);
    }
    const Lanes tn = simd::max(simd::max(n[0], n[1]), n[2]), tf = simd::min(simd::min(f[0], f[1]), f[2]);
    tn.store(depth);
    return simd::bits((tf >= tn) & (tf >= Lanes(1.0e-10f)) & (tn <= Lanes(maxDepth)));
}

#if defined(__AVX2__)
#include <immintrin.h>

extern "C" std::uint32_t slabs_raw(const QBlock& b, const float *inv, const float *originInv, float maxDepth, float *depth)
{
    __m256 n[3], f[3];
    for (int d = 0; d < 3; ++d)
    {
        const __m256 at = _mm256_set1_ps(b.origin[d] * inv[d] - originInv[d]), step = _mm256_set1_ps(b.scale[d] * inv[d]);
        const __m256 ql = _mm256_cvtepi32_ps(_mm256_cvtepu16_epi32(_mm_loadu_si128(reinterpret_cast<const __m128i *>(b.lo[d]))));
        const __m256 qh = _mm256_cvtepi32_ps(_mm256_cvtepu16_epi32(_mm_loadu_si128(reinterpret_cast<const __m128i *>(b.hi[d]))));
        const __m256 l = _mm256_fmadd_ps(ql, step, at), h = _mm256_fmadd_ps(qh, step, at);
        n[d] = _mm256_min_ps(h, l);
        f[d] = _mm256_max_ps(l, h);
    }
    const __m256 tn = _mm256_max_ps(_mm256_max_ps(n[0], n[1]), n[2]), tf = _mm256_min_ps(_mm256_min_ps(f[0], f[1]), f[2]);
    _mm256_storeu_ps(depth, tn);
    const __m256 ok = _mm256_and_ps(_mm256_and_ps(_mm256_cmp_ps(tf, tn, _CMP_GE_OQ), _mm256_cmp_ps(tf, _mm256_set1_ps(1.0e-10f), _CMP_GE_OQ)),
                                    _mm256_cmp_ps(tn, _mm256_set1_ps(maxDepth), _CMP_LE_OQ));
    return std::uint32_t(_mm256_movemask_ps(ok));
}
#endif
