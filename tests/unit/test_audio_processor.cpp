#include <gtest/gtest.h>
#include "core/audio_processor.h"
#include <cmath>
#include <fstream>
#include <cstdint>
#include <vector>
#include <string>

using namespace vp;

// Helper: create a test WAV file
static std::string create_test_wav(const std::string& filename, int sample_rate = 16000,
                                    float duration = 3.0f, float freq = 440.0f) {
    int num_samples = static_cast<int>(duration * sample_rate);
    std::vector<int16_t> samples(num_samples);
    for (int i = 0; i < num_samples; ++i) {
        samples[i] = static_cast<int16_t>(
            16000 * std::sin(2.0 * 3.14159265 * freq * i / sample_rate));
    }

    std::ofstream file(filename, std::ios::binary);
    // RIFF header
    int data_size = num_samples * 2;
    int file_size = 36 + data_size;
    file.write("RIFF", 4);
    file.write(reinterpret_cast<char*>(&file_size), 4);
    file.write("WAVE", 4);

    // fmt chunk
    file.write("fmt ", 4);
    int fmt_size = 16;
    file.write(reinterpret_cast<char*>(&fmt_size), 4);
    int16_t audio_format = 1; // PCM
    file.write(reinterpret_cast<char*>(&audio_format), 2);
    int16_t num_channels = 1;
    file.write(reinterpret_cast<char*>(&num_channels), 2);
    file.write(reinterpret_cast<char*>(&sample_rate), 4);
    int byte_rate = sample_rate * 2;
    file.write(reinterpret_cast<char*>(&byte_rate), 4);
    int16_t block_align = 2;
    file.write(reinterpret_cast<char*>(&block_align), 2);
    int16_t bits_per_sample = 16;
    file.write(reinterpret_cast<char*>(&bits_per_sample), 2);

    // data chunk
    file.write("data", 4);
    file.write(reinterpret_cast<char*>(&data_size), 4);
    file.write(reinterpret_cast<char*>(samples.data()), data_size);

    file.close();
    return filename;
}

TEST(AudioProcessorTest, ReadWav16k) {
    std::string wav_file = create_test_wav("test_16k.wav", 16000, 2.0f);

    AudioProcessor processor;
    std::vector<float> samples;
    int sample_rate;

    ASSERT_TRUE(processor.read_wav(wav_file, samples, sample_rate));
    EXPECT_EQ(sample_rate, 16000);
    EXPECT_EQ(samples.size(), 32000u);

    // Check range [-1.0, 1.0]
    for (float s : samples) {
        EXPECT_GE(s, -1.0f);
        EXPECT_LE(s, 1.0f);
    }

    std::remove(wav_file.c_str());
}

TEST(AudioProcessorTest, ReadWav8k) {
    std::string wav_file = create_test_wav("test_8k.wav", 8000, 2.0f);

    AudioProcessor processor;
    std::vector<float> samples;
    int sample_rate;

    ASSERT_TRUE(processor.read_wav(wav_file, samples, sample_rate));
    EXPECT_EQ(sample_rate, 8000);
    EXPECT_EQ(samples.size(), 16000u);

    std::remove(wav_file.c_str());
}

TEST(AudioProcessorTest, Int16ToFloat) {
    int16_t data[] = {0, 16384, -16384, 32767, -32768};
    auto result = AudioProcessor::int16_to_float(data, 5);

    EXPECT_NEAR(result[0], 0.0f, 1e-5f);
    EXPECT_NEAR(result[1], 0.5f, 1e-3f);
    EXPECT_NEAR(result[2], -0.5f, 1e-3f);
    EXPECT_GT(result[3], 0.99f);
    EXPECT_LE(result[4], -0.99f);
}

TEST(AudioProcessorTest, Resample8kTo16k) {
    // Create a simple signal
    std::vector<float> input(8000, 0.5f);
    auto output = AudioProcessor::resample(input, 8000, 16000);

    // Should roughly double the number of samples
    EXPECT_NEAR(output.size(), 16000u, 10u);

    // Values should be preserved
    for (float s : output) {
        EXPECT_NEAR(s, 0.5f, 0.01f);
    }
}

TEST(AudioProcessorTest, ResampleSameRate) {
    std::vector<float> input = {1.0f, 2.0f, 3.0f};
    auto output = AudioProcessor::resample(input, 16000, 16000);
    EXPECT_EQ(output.size(), input.size());
    for (size_t i = 0; i < input.size(); ++i) {
        EXPECT_FLOAT_EQ(output[i], input[i]);
    }
}

TEST(AudioProcessorTest, ReadNonExistentFile) {
    AudioProcessor processor;
    std::vector<float> samples;
    int sample_rate;

    EXPECT_FALSE(processor.read_wav("nonexistent.wav", samples, sample_rate));
}

TEST(AudioProcessorTest, Normalize) {
    AudioProcessor processor;
    std::vector<float> input(8000, 0.3f);  // 8kHz
    auto output = processor.normalize(input, 8000);

    // Should be resampled to 16kHz
    EXPECT_NEAR(output.size(), 16000u, 10u);
}

TEST(AudioProcessorTest, NormalizeAlready16k) {
    AudioProcessor processor;
    std::vector<float> input(16000, 0.3f);
    auto output = processor.normalize(input, 16000);

    // Should be unchanged
    EXPECT_EQ(output.size(), 16000u);
}
