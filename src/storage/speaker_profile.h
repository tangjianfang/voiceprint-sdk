#ifndef VP_SPEAKER_PROFILE_H
#define VP_SPEAKER_PROFILE_H

#include <string>
#include <vector>

namespace vp {

struct SpeakerProfile {
    std::string speaker_id;
    std::vector<float> embedding;   // L2-normalized mean embedding
    int enroll_count = 0;           // Number of enrollment samples

    SpeakerProfile() = default;

    SpeakerProfile(const std::string& id, const std::vector<float>& emb, int count = 1)
        : speaker_id(id), embedding(emb), enroll_count(count) {}
};

} // namespace vp

#endif // VP_SPEAKER_PROFILE_H
