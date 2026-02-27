#ifndef VP_FBANK_EXTRACTOR_H
#define VP_FBANK_EXTRACTOR_H

#include <vector>
#include <string>

namespace vp {

class FbankExtractor {
public:
    FbankExtractor();
    ~FbankExtractor();

    // Initialize with parameters
    void init(int num_bins = 80, int sample_rate = 16000,
              float frame_length_ms = 25.0f, float frame_shift_ms = 10.0f);

    // Extract FBank features from audio
    // Input: float32 PCM, 16kHz
    // Output: [num_frames, num_bins] row-major
    std::vector<float> extract(const std::vector<float>& audio);

    // Get number of frames for given input
    int get_num_frames(int num_samples) const;

    int num_bins() const { return num_bins_; }

private:
    int num_bins_ = 80;
    int sample_rate_ = 16000;
    float frame_length_ms_ = 25.0f;
    float frame_shift_ms_ = 10.0f;
    int frame_length_samples_ = 400;
    int frame_shift_samples_ = 160;
    bool initialized_ = false;

    // Apply CMVN normalization
    void apply_cmvn(std::vector<float>& features, int num_frames, int num_bins);
};

} // namespace vp

#endif // VP_FBANK_EXTRACTOR_H
