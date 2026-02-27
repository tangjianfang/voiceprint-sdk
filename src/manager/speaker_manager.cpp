#include "manager/speaker_manager.h"
#include "core/embedding_extractor.h"
#include "core/similarity.h"
#include "storage/sqlite_store.h"
#include "utils/logger.h"
#include "utils/error_codes.h"
#include <onnxruntime_cxx_api.h>
#include <cmath>
#include <algorithm>

namespace vp {

// Global ONNX Runtime environment (singleton)
static std::unique_ptr<Ort::Env> g_ort_env;

SpeakerManager::SpeakerManager()
    : extractor_(std::make_unique<EmbeddingExtractor>()),
      store_(std::make_unique<SqliteStore>()) {}

SpeakerManager::~SpeakerManager() {
    release();
}

bool SpeakerManager::init(const std::string& model_dir, const std::string& db_path) {
    if (initialized_) {
        last_error_ = "Already initialized";
        return false;
    }

    // Create ONNX Runtime environment
    if (!g_ort_env) {
        g_ort_env = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "voiceprint");
    }

    // Initialize embedding extractor
    if (!extractor_->init(model_dir, g_ort_env.get())) {
        last_error_ = "Failed to initialize embedding extractor: " + extractor_->last_error();
        VP_LOG_ERROR(last_error_);
        return false;
    }

    // Open database
    if (!store_->open(db_path)) {
        last_error_ = "Failed to open database: " + store_->last_error();
        VP_LOG_ERROR(last_error_);
        return false;
    }

    // Load cache from DB
    if (!load_cache_from_db()) {
        VP_LOG_WARN("Failed to load speaker cache from DB");
    }

    initialized_ = true;
    VP_LOG_INFO("SpeakerManager initialized: model_dir={}, db={}, cached_speakers={}",
                model_dir, db_path, cache_.size());
    return true;
}

void SpeakerManager::release() {
    if (!initialized_) return;

    {
        std::unique_lock lock(cache_mutex_);
        cache_.clear();
    }

    store_->close();
    extractor_.reset();
    extractor_ = std::make_unique<EmbeddingExtractor>();
    store_.reset();
    store_ = std::make_unique<SqliteStore>();

    initialized_ = false;
    VP_LOG_INFO("SpeakerManager released");
}

void* SpeakerManager::get_ort_env() {
    return g_ort_env.get();
}

bool SpeakerManager::load_cache_from_db() {
    auto speakers = store_->load_all_speakers();

    std::unique_lock lock(cache_mutex_);
    cache_.clear();
    for (auto& sp : speakers) {
        cache_[sp.speaker_id] = std::move(sp);
    }

    return true;
}

int SpeakerManager::enroll(const std::string& speaker_id, const float* pcm_data, int sample_count) {
    if (!initialized_) {
        last_error_ = error_code_to_string(ErrorCode::NOT_INIT);
        return static_cast<int>(ErrorCode::NOT_INIT);
    }
    if (!pcm_data || sample_count <= 0) {
        last_error_ = error_code_to_string(ErrorCode::INVALID_PARAM);
        return static_cast<int>(ErrorCode::INVALID_PARAM);
    }
    if (speaker_id.empty()) {
        last_error_ = "Speaker ID cannot be empty";
        return static_cast<int>(ErrorCode::INVALID_PARAM);
    }

    // Extract embedding
    std::vector<float> audio(pcm_data, pcm_data + sample_count);
    auto embedding = extractor_->extract(audio, 16000);
    if (embedding.empty()) {
        last_error_ = extractor_->last_error();
        // Determine specific error
        if (last_error_.find("too short") != std::string::npos) {
            return static_cast<int>(ErrorCode::AUDIO_TOO_SHORT);
        }
        if (last_error_.find("No speech") != std::string::npos) {
            return static_cast<int>(ErrorCode::AUDIO_INVALID);
        }
        return static_cast<int>(ErrorCode::INFERENCE);
    }

    // Update cache and DB
    {
        std::unique_lock lock(cache_mutex_);
        auto it = cache_.find(speaker_id);
        if (it != cache_.end()) {
            // Incremental update
            incremental_update(it->second, embedding);
            store_->save_speaker(it->second);
            VP_LOG_INFO("Updated speaker: {} (count={})", speaker_id, it->second.enroll_count);
        } else {
            // New speaker
            SpeakerProfile profile(speaker_id, embedding, 1);
            store_->save_speaker(profile);
            cache_[speaker_id] = std::move(profile);
            VP_LOG_INFO("Enrolled new speaker: {}", speaker_id);
        }
    }

    return static_cast<int>(ErrorCode::OK);
}

int SpeakerManager::enroll_file(const std::string& speaker_id, const std::string& wav_path) {
    if (!initialized_) {
        last_error_ = error_code_to_string(ErrorCode::NOT_INIT);
        return static_cast<int>(ErrorCode::NOT_INIT);
    }
    if (speaker_id.empty()) {
        last_error_ = "Speaker ID cannot be empty";
        return static_cast<int>(ErrorCode::INVALID_PARAM);
    }

    // Extract embedding from file
    auto embedding = extractor_->extract_from_file(wav_path);
    if (embedding.empty()) {
        last_error_ = extractor_->last_error();
        if (last_error_.find("Cannot open") != std::string::npos) {
            return static_cast<int>(ErrorCode::FILE_NOT_FOUND);
        }
        if (last_error_.find("too short") != std::string::npos) {
            return static_cast<int>(ErrorCode::AUDIO_TOO_SHORT);
        }
        return static_cast<int>(ErrorCode::INFERENCE);
    }

    // Update cache and DB (same as enroll)
    {
        std::unique_lock lock(cache_mutex_);
        auto it = cache_.find(speaker_id);
        if (it != cache_.end()) {
            incremental_update(it->second, embedding);
            store_->save_speaker(it->second);
            VP_LOG_INFO("Updated speaker from file: {} (count={})", speaker_id, it->second.enroll_count);
        } else {
            SpeakerProfile profile(speaker_id, embedding, 1);
            store_->save_speaker(profile);
            cache_[speaker_id] = std::move(profile);
            VP_LOG_INFO("Enrolled new speaker from file: {}", speaker_id);
        }
    }

    return static_cast<int>(ErrorCode::OK);
}

int SpeakerManager::remove_speaker(const std::string& speaker_id) {
    if (!initialized_) {
        last_error_ = error_code_to_string(ErrorCode::NOT_INIT);
        return static_cast<int>(ErrorCode::NOT_INIT);
    }

    {
        std::unique_lock lock(cache_mutex_);
        auto it = cache_.find(speaker_id);
        if (it == cache_.end()) {
            last_error_ = "Speaker not found: " + speaker_id;
            return static_cast<int>(ErrorCode::SPEAKER_NOT_FOUND);
        }
        cache_.erase(it);
    }

    if (!store_->remove_speaker(speaker_id)) {
        last_error_ = store_->last_error();
        return static_cast<int>(ErrorCode::DB_ERROR);
    }

    VP_LOG_INFO("Removed speaker: {}", speaker_id);
    return static_cast<int>(ErrorCode::OK);
}

int SpeakerManager::identify(const float* pcm_data, int sample_count,
                              std::string& out_speaker_id, float& out_score) {
    if (!initialized_) {
        last_error_ = error_code_to_string(ErrorCode::NOT_INIT);
        return static_cast<int>(ErrorCode::NOT_INIT);
    }
    if (!pcm_data || sample_count <= 0) {
        last_error_ = error_code_to_string(ErrorCode::INVALID_PARAM);
        return static_cast<int>(ErrorCode::INVALID_PARAM);
    }

    // Extract embedding
    std::vector<float> audio(pcm_data, pcm_data + sample_count);
    auto embedding = extractor_->extract(audio, 16000);
    if (embedding.empty()) {
        last_error_ = extractor_->last_error();
        return static_cast<int>(ErrorCode::INFERENCE);
    }

    // Search in cache
    std::string best_id;
    float best_score = -1.0f;

    {
        std::shared_lock lock(cache_mutex_);
        for (const auto& [id, profile] : cache_) {
            float score = SimilarityCalculator::cosine_similarity(
                embedding.data(), profile.embedding.data(),
                static_cast<int>(embedding.size()));
            if (score > best_score) {
                best_score = score;
                best_id = id;
            }
        }
    }

    out_score = best_score;

    if (best_score >= threshold_) {
        out_speaker_id = best_id;
        VP_LOG_INFO("Identified speaker: {} (score={:.4f})", best_id, best_score);
        return static_cast<int>(ErrorCode::OK);
    }

    last_error_ = "No matching speaker found (best score: " + std::to_string(best_score) + ")";
    VP_LOG_INFO("No match found (best={:.4f}, threshold={:.4f})", best_score, threshold_);
    return static_cast<int>(ErrorCode::NO_MATCH);
}

int SpeakerManager::verify(const std::string& speaker_id,
                            const float* pcm_data, int sample_count, float& out_score) {
    if (!initialized_) {
        last_error_ = error_code_to_string(ErrorCode::NOT_INIT);
        return static_cast<int>(ErrorCode::NOT_INIT);
    }
    if (!pcm_data || sample_count <= 0) {
        last_error_ = error_code_to_string(ErrorCode::INVALID_PARAM);
        return static_cast<int>(ErrorCode::INVALID_PARAM);
    }

    // Check if speaker exists
    std::vector<float> ref_embedding;
    {
        std::shared_lock lock(cache_mutex_);
        auto it = cache_.find(speaker_id);
        if (it == cache_.end()) {
            last_error_ = "Speaker not found: " + speaker_id;
            return static_cast<int>(ErrorCode::SPEAKER_NOT_FOUND);
        }
        ref_embedding = it->second.embedding;
    }

    // Extract embedding
    std::vector<float> audio(pcm_data, pcm_data + sample_count);
    auto embedding = extractor_->extract(audio, 16000);
    if (embedding.empty()) {
        last_error_ = extractor_->last_error();
        return static_cast<int>(ErrorCode::INFERENCE);
    }

    out_score = SimilarityCalculator::cosine_similarity(embedding, ref_embedding);

    VP_LOG_INFO("Verify speaker {}: score={:.4f}, threshold={:.4f}, match={}",
                speaker_id, out_score, threshold_, out_score >= threshold_ ? "yes" : "no");
    return static_cast<int>(ErrorCode::OK);
}

void SpeakerManager::set_threshold(float threshold) {
    threshold_ = std::max(0.0f, std::min(1.0f, threshold));
    VP_LOG_INFO("Threshold set to {:.4f}", threshold_);
}

int SpeakerManager::get_speaker_count() const {
    std::shared_lock lock(cache_mutex_);
    return static_cast<int>(cache_.size());
}

void SpeakerManager::incremental_update(SpeakerProfile& profile,
                                         const std::vector<float>& new_embedding) {
    int n = profile.enroll_count;
    for (size_t i = 0; i < profile.embedding.size(); ++i) {
        profile.embedding[i] = (profile.embedding[i] * n + new_embedding[i]) / (n + 1);
    }
    profile.enroll_count = n + 1;

    // L2 re-normalize
    l2_normalize(profile.embedding);
}

void SpeakerManager::l2_normalize(std::vector<float>& vec) {
    float norm = 0.0f;
    for (float v : vec) {
        norm += v * v;
    }
    norm = std::sqrt(norm);
    if (norm > 1e-10f) {
        for (float& v : vec) {
            v /= norm;
        }
    }
}

} // namespace vp
