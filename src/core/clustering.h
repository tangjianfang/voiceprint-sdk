#pragma once
#ifndef VP_CLUSTERING_H
#define VP_CLUSTERING_H

// Agglomerative hierarchical clustering with average linkage.
// Used by the Diarizer to group speech segments by speaker.
// Input: N embedded vectors of dimension D (L2-normalized).
// Distance metric: cosine distance = 1 - cosine_similarity.

#include <vector>
#include <string>
#include <algorithm>
#include <limits>
#include <cmath>
#include <numeric>

namespace vp {
namespace clustering {

// ----------------------------------------------------------------
// Cosine distance between two L2-normalized vectors
// ----------------------------------------------------------------
inline float cosine_dist(const std::vector<float>& a,
                         const std::vector<float>& b) {
    if (a.size() != b.size() || a.empty()) return 1.0f;
    double dot = 0.0;
    for (size_t i = 0; i < a.size(); ++i)
        dot += static_cast<double>(a[i]) * b[i];
    float sim = static_cast<float>(std::max(-1.0, std::min(1.0, dot)));
    return 1.0f - sim;
}

// ----------------------------------------------------------------
// Agglomerative clustering result
// ----------------------------------------------------------------
struct ClusterResult {
    std::vector<int> labels;  // cluster label per input segment
    int num_clusters;
};

// ----------------------------------------------------------------
// Perform agglomerative (bottom-up) clustering.
// @param embeddings  N embeddings of dimension D each.
// @param threshold   Maximum within-cluster distance to allow merge.
//                    Typical: 0.45 (cosine distance).
// @param max_clusters  Hard cap on cluster count (0 = unlimited).
// @return ClusterResult with per-input labels.
// ----------------------------------------------------------------
inline ClusterResult agglomerative_cluster(
    const std::vector<std::vector<float>>& embeddings,
    float threshold   = 0.45f,
    int   max_clusters = 0)
{
    ClusterResult result;
    const int N = static_cast<int>(embeddings.size());
    if (N == 0) return result;
    if (N == 1) { result.labels = {0}; result.num_clusters = 1; return result; }

    // Each point starts in its own cluster
    std::vector<int> labels(N);
    std::iota(labels.begin(), labels.end(), 0);

    // Track cluster means (for average linkage)
    std::vector<std::vector<float>> means = embeddings;
    std::vector<int> counts(N, 1);
    std::vector<bool> active(N, true);
    int num_active = N;

    // Iteratively merge closest pair
    while (true) {
        // Find closest active pair
        float best_dist = std::numeric_limits<float>::max();
        int   best_i = -1, best_j = -1;

        for (int i = 0; i < N; ++i) {
            if (!active[i]) continue;
            for (int j = i+1; j < N; ++j) {
                if (!active[j]) continue;
                float d = cosine_dist(means[i], means[j]);
                if (d < best_dist) {
                    best_dist = d;
                    best_i = i; best_j = j;
                }
            }
        }

        // Stop if minimum distance exceeds threshold or hit max_clusters
        bool at_max = (max_clusters > 0 && num_active <= max_clusters);
        if (best_i < 0 || (best_dist > threshold && !at_max)) break;
        if (at_max && best_dist > threshold) break;

        // Merge cluster j into cluster i (weighted average mean)
        int ci = counts[best_i], cj = counts[best_j];
        int total = ci + cj;
        for (size_t k = 0; k < means[best_i].size(); ++k)
            means[best_i][k] = (means[best_i][k]*ci + means[best_j][k]*cj) / total;

        // L2 re-normalize mean
        double norm = 0.0;
        for (float v : means[best_i]) norm += static_cast<double>(v)*v;
        norm = std::sqrt(norm);
        if (norm > 1e-8)
            for (float& v : means[best_i]) v /= static_cast<float>(norm);

        counts[best_i] = total;
        active[best_j] = false;
        --num_active;

        // Relabel: all points in cluster best_j get label best_i
        for (int k = 0; k < N; ++k)
            if (labels[k] == best_j) labels[k] = best_i;
    }

    // Compact labels to 0..K-1
    std::vector<int> id_map(N, -1);
    int next_id = 0;
    result.labels.resize(N);
    for (int i = 0; i < N; ++i) {
        int lbl = labels[i];
        if (id_map[lbl] < 0) id_map[lbl] = next_id++;
        result.labels[i] = id_map[lbl];
    }
    result.num_clusters = next_id;
    return result;
}

} // namespace clustering
} // namespace vp

#endif // VP_CLUSTERING_H
