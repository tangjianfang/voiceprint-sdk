#ifndef VP_AUDIO_PROCESSOR_H
#define VP_AUDIO_PROCESSOR_H

#include <vector>
#include <string>
#include <cstdint>

namespace vp {

struct WavHeader {
    char riff[4];           // "RIFF"
    uint32_t file_size;     // File size - 8
    char wave[4];           // "WAVE"
    char fmt[4];            // "fmt "
    uint32_t fmt_size;      // Format chunk size
    uint16_t audio_format;  // 1=PCM, 3=IEEE float
    uint16_t num_channels;  // Number of channels
    uint32_t sample_rate;   // Sample rate
    uint32_t byte_rate;     // Byte rate
    uint16_t block_align;   // Block align
    uint16_t bits_per_sample; // Bits per sample
};

class AudioProcessor {
public:
    AudioProcessor() = default;

    // Read WAV file and return float32 PCM samples normalized to [-1.0, 1.0]
    // Output is always 16kHz mono
    bool read_wav(const std::string& wav_path, std::vector<float>& out_samples,
                  int& out_sample_rate);

    // Convert int16 PCM to float32 [-1.0, 1.0]
    static std::vector<float> int16_to_float(const int16_t* data, size_t count);

    // Resample audio to target sample rate (linear interpolation)
    static std::vector<float> resample(const std::vector<float>& input,
                                        int src_rate, int dst_rate);

    // Ensure audio is 16kHz mono
    std::vector<float> normalize(const std::vector<float>& input, int sample_rate);

    // Get last error message
    const std::string& last_error() const { return last_error_; }

private:
    std::string last_error_;
};

} // namespace vp

#endif // VP_AUDIO_PROCESSOR_H
