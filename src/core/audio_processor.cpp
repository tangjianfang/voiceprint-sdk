#include "core/audio_processor.h"
#include "utils/logger.h"
#include <fstream>
#include <cstring>
#include <cmath>
#include <algorithm>

namespace vp {

bool AudioProcessor::read_wav(const std::string& wav_path, std::vector<float>& out_samples,
                               int& out_sample_rate) {
    std::ifstream file(wav_path, std::ios::binary);
    if (!file.is_open()) {
        last_error_ = "Cannot open file: " + wav_path;
        VP_LOG_ERROR(last_error_);
        return false;
    }

    // Read RIFF header
    char riff[4];
    file.read(riff, 4);
    if (std::strncmp(riff, "RIFF", 4) != 0) {
        last_error_ = "Not a valid RIFF file";
        VP_LOG_ERROR(last_error_);
        return false;
    }

    uint32_t file_size;
    file.read(reinterpret_cast<char*>(&file_size), 4);

    char wave[4];
    file.read(wave, 4);
    if (std::strncmp(wave, "WAVE", 4) != 0) {
        last_error_ = "Not a valid WAVE file";
        VP_LOG_ERROR(last_error_);
        return false;
    }

    // Parse chunks
    uint16_t audio_format = 0;
    uint16_t num_channels = 0;
    uint32_t sample_rate = 0;
    uint16_t bits_per_sample = 0;
    std::vector<uint8_t> audio_data;

    while (file.good() && !file.eof()) {
        char chunk_id[4];
        uint32_t chunk_size;

        file.read(chunk_id, 4);
        if (file.gcount() < 4) break;
        file.read(reinterpret_cast<char*>(&chunk_size), 4);
        if (file.gcount() < 4) break;

        if (std::strncmp(chunk_id, "fmt ", 4) == 0) {
            file.read(reinterpret_cast<char*>(&audio_format), 2);
            file.read(reinterpret_cast<char*>(&num_channels), 2);
            file.read(reinterpret_cast<char*>(&sample_rate), 4);
            uint32_t byte_rate;
            file.read(reinterpret_cast<char*>(&byte_rate), 4);
            uint16_t block_align;
            file.read(reinterpret_cast<char*>(&block_align), 2);
            file.read(reinterpret_cast<char*>(&bits_per_sample), 2);
            // Skip extra fmt bytes
            if (chunk_size > 16) {
                file.seekg(chunk_size - 16, std::ios::cur);
            }
        } else if (std::strncmp(chunk_id, "data", 4) == 0) {
            audio_data.resize(chunk_size);
            file.read(reinterpret_cast<char*>(audio_data.data()), chunk_size);
            break; // We have the data
        } else {
            // Skip unknown chunks
            file.seekg(chunk_size, std::ios::cur);
        }
    }

    if (audio_data.empty()) {
        last_error_ = "No audio data found in WAV file";
        VP_LOG_ERROR(last_error_);
        return false;
    }

    if (audio_format != 1 && audio_format != 3) {
        last_error_ = "Unsupported audio format: " + std::to_string(audio_format) +
                      " (only PCM=1 and IEEE float=3 supported)";
        VP_LOG_ERROR(last_error_);
        return false;
    }

    VP_LOG_INFO("WAV: format={}, channels={}, rate={}, bits={}",
                audio_format, num_channels, sample_rate, bits_per_sample);

    // Convert to float32 mono
    std::vector<float> samples;

    if (audio_format == 1 && bits_per_sample == 16) {
        // PCM int16
        size_t num_samples = audio_data.size() / 2;
        const int16_t* int_data = reinterpret_cast<const int16_t*>(audio_data.data());
        samples = int16_to_float(int_data, num_samples);
    } else if (audio_format == 1 && bits_per_sample == 8) {
        // PCM uint8
        size_t num_samples = audio_data.size();
        samples.resize(num_samples);
        for (size_t i = 0; i < num_samples; ++i) {
            samples[i] = (static_cast<float>(audio_data[i]) - 128.0f) / 128.0f;
        }
    } else if (audio_format == 3 && bits_per_sample == 32) {
        // IEEE float32
        size_t num_samples = audio_data.size() / 4;
        samples.resize(num_samples);
        std::memcpy(samples.data(), audio_data.data(), audio_data.size());
    } else {
        last_error_ = "Unsupported bit depth: " + std::to_string(bits_per_sample);
        VP_LOG_ERROR(last_error_);
        return false;
    }

    // Convert to mono if stereo
    if (num_channels == 2) {
        std::vector<float> mono(samples.size() / 2);
        for (size_t i = 0; i < mono.size(); ++i) {
            mono[i] = (samples[i * 2] + samples[i * 2 + 1]) * 0.5f;
        }
        samples = std::move(mono);
    } else if (num_channels > 2) {
        // Take first channel
        std::vector<float> mono(samples.size() / num_channels);
        for (size_t i = 0; i < mono.size(); ++i) {
            mono[i] = samples[i * num_channels];
        }
        samples = std::move(mono);
    }

    out_sample_rate = static_cast<int>(sample_rate);
    out_samples = std::move(samples);
    return true;
}

std::vector<float> AudioProcessor::int16_to_float(const int16_t* data, size_t count) {
    std::vector<float> result(count);
    for (size_t i = 0; i < count; ++i) {
        result[i] = static_cast<float>(data[i]) / 32768.0f;
    }
    return result;
}

std::vector<float> AudioProcessor::resample(const std::vector<float>& input,
                                             int src_rate, int dst_rate) {
    if (src_rate == dst_rate) {
        return input;
    }

    double ratio = static_cast<double>(dst_rate) / static_cast<double>(src_rate);
    size_t output_size = static_cast<size_t>(std::ceil(input.size() * ratio));
    std::vector<float> output(output_size);

    for (size_t i = 0; i < output_size; ++i) {
        double src_pos = static_cast<double>(i) / ratio;
        size_t idx = static_cast<size_t>(src_pos);
        double frac = src_pos - static_cast<double>(idx);

        if (idx + 1 < input.size()) {
            output[i] = static_cast<float>(
                input[idx] * (1.0 - frac) + input[idx + 1] * frac);
        } else if (idx < input.size()) {
            output[i] = input[idx];
        } else {
            output[i] = 0.0f;
        }
    }

    return output;
}

std::vector<float> AudioProcessor::normalize(const std::vector<float>& input, int sample_rate) {
    if (sample_rate == 16000) {
        return input;
    }
    VP_LOG_INFO("Resampling from {}Hz to 16000Hz", sample_rate);
    return resample(input, sample_rate, 16000);
}

} // namespace vp
