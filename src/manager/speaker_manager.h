#ifndef VP_SPEAKER_MANAGER_H
#define VP_SPEAKER_MANAGER_H

#include "storage/speaker_profile.h"
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <shared_mutex>

namespace vp {

class EmbeddingExtractor;
class SqliteStore;

struct IdentifyResult {
    std::string speaker_id;
    float score;
};

class SpeakerManager {
public:
    SpeakerManager();
    ~SpeakerManager();

    // Initialize with model directory and database path
    bool init(const std::string& model_dir, const std::string& db_path);

    // Release all resources
    void release();

    // Enroll a speaker from PCM data
    int enroll(const std::string& speaker_id, const float* pcm_data, int sample_count);

    // Enroll a speaker from WAV file
    int enroll_file(const std::string& speaker_id, const std::string& wav_path);

    // Remove a speaker
    int remove_speaker(const std::string& speaker_id);

    // Identify speaker (1:N)
    int identify(const float* pcm_data, int sample_count,
                 std::string& out_speaker_id, float& out_score);

    // Verify speaker (1:1)
    int verify(const std::string& speaker_id,
               const float* pcm_data, int sample_count, float& out_score);

    // Set similarity threshold
    void set_threshold(float threshold);

    // Get speaker count
    int get_speaker_count() const;

    const std::string& last_error() const { return last_error_; }

private:
    // Load all speakers from DB into memory cache
    bool load_cache_from_db();

    // Update incremental mean embedding
    void incremental_update(SpeakerProfile& profile, const std::vector<float>& new_embedding);

    // L2 normalize vector
    static void l2_normalize(std::vector<float>& vec);

    std::unique_ptr<EmbeddingExtractor> extractor_;
    std::unique_ptr<SqliteStore> store_;

    // In-memory cache protected by read-write lock
    mutable std::shared_mutex cache_mutex_;
    std::unordered_map<std::string, SpeakerProfile> cache_;

    float threshold_ = 0.30f;
    bool initialized_ = false;
    std::string last_error_;
};

} // namespace vp

#endif // VP_SPEAKER_MANAGER_H
