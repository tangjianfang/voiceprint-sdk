#include "core/similarity.h"
#include <cmath>
#include <algorithm>

#ifdef __AVX2__
#include <immintrin.h>
#endif

namespace vp {

float SimilarityCalculator::cosine_similarity(const std::vector<float>& a,
                                               const std::vector<float>& b) {
    if (a.size() != b.size() || a.empty()) return 0.0f;
    return cosine_similarity(a.data(), b.data(), static_cast<int>(a.size()));
}

float SimilarityCalculator::cosine_similarity(const float* a, const float* b, int dim) {
    // For L2-normalized vectors, cosine similarity = dot product
    float dot = 0.0f;

#ifdef __AVX2__
    // AVX2 optimized dot product
    int i = 0;
    __m256 sum = _mm256_setzero_ps();
    for (; i + 8 <= dim; i += 8) {
        __m256 va = _mm256_loadu_ps(a + i);
        __m256 vb = _mm256_loadu_ps(b + i);
        sum = _mm256_fmadd_ps(va, vb, sum);
    }

    // Horizontal sum
    __m128 hi = _mm256_extractf128_ps(sum, 1);
    __m128 lo = _mm256_castps256_ps128(sum);
    __m128 sum128 = _mm_add_ps(lo, hi);
    sum128 = _mm_hadd_ps(sum128, sum128);
    sum128 = _mm_hadd_ps(sum128, sum128);
    dot = _mm_cvtss_f32(sum128);

    // Process remaining elements
    for (; i < dim; ++i) {
        dot += a[i] * b[i];
    }
#else
    // Scalar fallback
    for (int i = 0; i < dim; ++i) {
        dot += a[i] * b[i];
    }
#endif

    // Clamp to [-1, 1]
    return std::max(-1.0f, std::min(1.0f, dot));
}

SimilarityCalculator::MatchResult SimilarityCalculator::find_best_match(
    const std::vector<float>& query,
    const std::vector<std::pair<std::string, std::vector<float>>>& candidates) {

    MatchResult best{-1, -1.0f, ""};

    for (size_t i = 0; i < candidates.size(); ++i) {
        float score = cosine_similarity(query, candidates[i].second);
        if (score > best.score) {
            best.index = static_cast<int>(i);
            best.score = score;
            best.speaker_id = candidates[i].first;
        }
    }

    return best;
}

} // namespace vp
