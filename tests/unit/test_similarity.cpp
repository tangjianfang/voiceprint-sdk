#include <gtest/gtest.h>
#include "core/similarity.h"
#include <cmath>
#include <vector>
#include <chrono>

using namespace vp;

TEST(SimilarityTest, IdenticalVectors) {
    std::vector<float> a = {0.5f, 0.5f, 0.5f, 0.5f};
    // L2 normalize
    float norm = std::sqrt(4 * 0.25f);
    for (auto& v : a) v /= norm;

    float sim = SimilarityCalculator::cosine_similarity(a, a);
    EXPECT_NEAR(sim, 1.0f, 1e-5f);
}

TEST(SimilarityTest, OrthogonalVectors) {
    std::vector<float> a = {1.0f, 0.0f, 0.0f, 0.0f};
    std::vector<float> b = {0.0f, 1.0f, 0.0f, 0.0f};

    float sim = SimilarityCalculator::cosine_similarity(a, b);
    EXPECT_NEAR(sim, 0.0f, 1e-5f);
}

TEST(SimilarityTest, OppositeVectors) {
    std::vector<float> a = {1.0f, 0.0f, 0.0f, 0.0f};
    std::vector<float> b = {-1.0f, 0.0f, 0.0f, 0.0f};

    float sim = SimilarityCalculator::cosine_similarity(a, b);
    EXPECT_NEAR(sim, -1.0f, 1e-5f);
}

TEST(SimilarityTest, EmptyVectors) {
    std::vector<float> a;
    std::vector<float> b;

    float sim = SimilarityCalculator::cosine_similarity(a, b);
    EXPECT_FLOAT_EQ(sim, 0.0f);
}

TEST(SimilarityTest, DifferentSizeVectors) {
    std::vector<float> a = {1.0f, 0.0f};
    std::vector<float> b = {1.0f, 0.0f, 0.0f};

    float sim = SimilarityCalculator::cosine_similarity(a, b);
    EXPECT_FLOAT_EQ(sim, 0.0f);
}

TEST(SimilarityTest, FindBestMatch) {
    std::vector<float> query = {1.0f, 0.0f, 0.0f};

    std::vector<std::pair<std::string, std::vector<float>>> candidates = {
        {"speaker_A", {0.0f, 1.0f, 0.0f}},  // orthogonal
        {"speaker_B", {0.9f, 0.1f, 0.0f}},   // similar
        {"speaker_C", {-1.0f, 0.0f, 0.0f}},  // opposite
    };

    auto result = SimilarityCalculator::find_best_match(query, candidates);
    EXPECT_EQ(result.speaker_id, "speaker_B");
    EXPECT_GT(result.score, 0.5f);
    EXPECT_EQ(result.index, 1);
}

TEST(SimilarityTest, FindBestMatchEmpty) {
    std::vector<float> query = {1.0f, 0.0f, 0.0f};
    std::vector<std::pair<std::string, std::vector<float>>> candidates;

    auto result = SimilarityCalculator::find_best_match(query, candidates);
    EXPECT_EQ(result.index, -1);
}

TEST(SimilarityTest, Performance1000Vectors192Dim) {
    const int dim = 192;
    const int num_vectors = 1000;

    // Generate random vectors
    std::vector<float> query(dim);
    for (int i = 0; i < dim; ++i) {
        query[i] = static_cast<float>(i) / dim;
    }

    std::vector<std::pair<std::string, std::vector<float>>> candidates(num_vectors);
    for (int n = 0; n < num_vectors; ++n) {
        candidates[n].first = "speaker_" + std::to_string(n);
        candidates[n].second.resize(dim);
        for (int i = 0; i < dim; ++i) {
            candidates[n].second[i] = static_cast<float>((n + i) % dim) / dim;
        }
    }

    auto start = std::chrono::high_resolution_clock::now();

    auto result = SimilarityCalculator::find_best_match(query, candidates);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    std::cout << "1:1000 search (192-dim): " << duration_us << " us" << std::endl;
    EXPECT_LT(duration_us, 1000000);  // < 1 second (should be < 1ms)

    // Should find some result
    EXPECT_GE(result.index, 0);
}
