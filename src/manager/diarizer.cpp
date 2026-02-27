#include "diarizer.h"
#include "speaker_manager.h"
#include "core/embedding_extractor.h"
#include "core/vad.h"
#include "core/clustering.h"
#include "utils/logger.h"
#include "utils/error_codes.h"
#include <voiceprint/voiceprint_api.h>

#include <cstring>
#include <cstdio>
#include <algorithm>
#include <filesystem>

namespace vp {

Diarizer::Diarizer()
    : extractor_(std::make_unique<EmbeddingExtractor>())
    , vad_(std::make_unique<VoiceActivityDetector>()) {
}

Diarizer::~Diarizer() = default;

bool Diarizer::init(const std::string& model_dir, void* ort_env,
                    SpeakerManager* manager) {
    namespace fs = std::filesystem;

    manager_ = manager;

    // Initialize VAD
    std::string vad_path = (fs::path(model_dir) / "silero_vad.onnx").string();
    if (!fs::exists(vad_path)) {
        last_error_ = "silero_vad.onnx not found in: " + model_dir;
        return false;
    }
    if (!vad_->init(vad_path, ort_env)) {
        last_error_ = "VAD init failed: " + vad_->last_error();
        return false;
    }

    // Initialize embedding extractor (re-uses ecapa_tdnn + silero_vad)
    if (!extractor_->init(model_dir, ort_env)) {
        last_error_ = "EmbeddingExtractor init failed: " + extractor_->last_error();
        return false;
    }

    initialized_ = true;
    VP_LOG_INFO("Diarizer initialized (threshold={})", threshold_);
    return true;
}

int Diarizer::diarize(const float* pcm_in, int sample_count,
                      VpDiarizeSegment* out_segments, int max_segments,
                      int* out_count) {
    if (!initialized_) {
        set_last_error(ErrorCode::NOT_INIT);
        return VP_ERROR_NOT_INIT;
    }
    if (!pcm_in || sample_count <= 0 || !out_segments || max_segments <= 0 || !out_count) {
        set_last_error(ErrorCode::INVALID_PARAM);
        return VP_ERROR_INVALID_PARAM;
    }
    *out_count = 0;

    std::vector<float> pcm(pcm_in, pcm_in + sample_count);
    constexpr int SR = 16000;

    // ----------------------------------------------------------------
    // Step 1: VAD → speech segments
    // ----------------------------------------------------------------
    auto segments = vad_->detect(pcm, SR);
    if (segments.empty()) {
        VP_LOG_WARN("Diarizer: no speech detected");
        return VP_OK;  // 0 segments is valid
    }
    VP_LOG_DEBUG("Diarizer: {} speech segments from VAD", segments.size());

    // ----------------------------------------------------------------
    // Step 2: Extract embedding per sufficiently long segment
    // ----------------------------------------------------------------
    struct SegEmb {
        int   start_sample;
        int   end_sample;
        float confidence;
        std::vector<float> embedding;
    };
    std::vector<SegEmb> segs_with_emb;

    for (auto& seg : segments) {
        float dur_sec = static_cast<float>(seg.end_sample - seg.start_sample) / SR;
        if (dur_sec < MIN_SEG_DURATION_SEC) continue;

        int start = std::max(0, seg.start_sample);
        int end   = std::min(sample_count, seg.end_sample);
        if (end <= start) continue;

        std::vector<float> seg_pcm(pcm.begin() + start, pcm.begin() + end);
        std::vector<float> emb = extractor_->extract(seg_pcm, SR);
        if (emb.empty()) continue;

        segs_with_emb.push_back({start, end, seg.confidence, std::move(emb)});
    }

    if (segs_with_emb.empty()) {
        VP_LOG_WARN("Diarizer: all segments too short for embedding");
        return VP_OK;
    }

    // ----------------------------------------------------------------
    // Step 3: Cluster embeddings
    // ----------------------------------------------------------------
    std::vector<std::vector<float>> embeddings;
    embeddings.reserve(segs_with_emb.size());
    for (auto& s : segs_with_emb) embeddings.push_back(s.embedding);

    auto cluster_result = clustering::agglomerative_cluster(embeddings, threshold_);
    VP_LOG_INFO("Diarizer: {} segments → {} speakers",
                segs_with_emb.size(), cluster_result.num_clusters);

    // ----------------------------------------------------------------
    // Step 4: Build output segments
    //         Optionally match cluster centroids to known speakers.
    // ----------------------------------------------------------------

    // Compute cluster centroids for speaker ID matching
    const int K = cluster_result.num_clusters;
    std::vector<std::vector<float>> centroids(K,
        std::vector<float>(embeddings[0].size(), 0.0f));
    std::vector<int> centroid_count(K, 0);

    for (size_t i = 0; i < embeddings.size(); ++i) {
        int lbl = cluster_result.labels[i];
        for (size_t d = 0; d < embeddings[i].size(); ++d)
            centroids[lbl][d] += embeddings[i][d];
        centroid_count[lbl]++;
    }
    for (int k = 0; k < K; ++k) {
        if (centroid_count[k] == 0) continue;
        // Normalize
        double norm = 0.0;
        for (float v : centroids[k]) norm += static_cast<double>(v)*v;
        norm = std::sqrt(norm);
        if (norm > 1e-8)
            for (float& v : centroids[k]) v /= static_cast<float>(norm);
    }

    // Try to match each centroid against registered speakers
    std::vector<std::string> cluster_speaker_id(K);
    if (manager_) {
        for (int k = 0; k < K; ++k) {
            std::string sid;
            float score = 0.0f;
            // We use identify() by passing centroid as PCM is not available here.
            // Instead directly call the manager's internal cache search
            // (exposed via a helper that accepts an embedding vector).
            // For now: use the centroid embedding as a synthetic 1-sample lookup.
            // This is a best-effort match - low confidence expected.
            (void)sid; (void)score;  // placeholder until manager exposes embedding search
        }
    }

    // Write output
    int written = 0;
    for (size_t i = 0; i < segs_with_emb.size() && written < max_segments; ++i) {
        VpDiarizeSegment& out = out_segments[written];
        std::memset(&out, 0, sizeof(out));

        out.start_sec   = static_cast<float>(segs_with_emb[i].start_sample) / SR;
        out.end_sec     = static_cast<float>(segs_with_emb[i].end_sample)   / SR;
        out.confidence  = segs_with_emb[i].confidence;

        int lbl = cluster_result.labels[i];
        std::snprintf(out.speaker_label, sizeof(out.speaker_label),
                      "SPEAKER_%d", lbl);

        if (!cluster_speaker_id[lbl].empty()) {
            std::strncpy(out.speaker_id, cluster_speaker_id[lbl].c_str(),
                         sizeof(out.speaker_id) - 1);
        }

        ++written;
    }
    *out_count = written;
    return VP_OK;
}

} // namespace vp
