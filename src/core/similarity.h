#ifndef VP_SIMILARITY_H
#define VP_SIMILARITY_H

#include <vector>
#include <string>
#include <utility>

namespace vp {

class SimilarityCalculator {
public:
    // Cosine similarity between two vectors (assumes L2-normalized)
    static float cosine_similarity(const std::vector<float>& a, const std::vector<float>& b);

    // Cosine similarity using raw pointers (for performance)
    static float cosine_similarity(const float* a, const float* b, int dim);

    // Find best match from a set of embeddings
    // Returns (index, score) pair, or (-1, 0) if empty
    struct MatchResult {
        int index;
        float score;
        std::string speaker_id;
    };

    static MatchResult find_best_match(
        const std::vector<float>& query,
        const std::vector<std::pair<std::string, std::vector<float>>>& candidates);
};

} // namespace vp

#endif // VP_SIMILARITY_H
