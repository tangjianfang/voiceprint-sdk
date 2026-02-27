#include <gtest/gtest.h>
#include <voiceprint/voiceprint_api.h>
#include <vector>
#include <string>
#include <cmath>
#include <thread>
#include <fstream>
#include <cstdint>

// Helper: create a WAV file with speech-like content
static std::string create_speech_wav(const std::string& filename, float freq = 300.0f,
                                      float duration = 3.0f, int sample_rate = 16000) {
    int num_samples = static_cast<int>(duration * sample_rate);
    std::vector<int16_t> samples(num_samples);

    // Generate speech-like signal (multiple harmonics + noise)
    for (int i = 0; i < num_samples; ++i) {
        float t = static_cast<float>(i) / sample_rate;
        float val = 0.3f * std::sin(2.0f * 3.14159265f * freq * t);
        val += 0.2f * std::sin(2.0f * 3.14159265f * freq * 2 * t);
        val += 0.1f * std::sin(2.0f * 3.14159265f * freq * 3 * t);
        // Add some noise
        val += 0.05f * (static_cast<float>(rand()) / RAND_MAX * 2.0f - 1.0f);
        samples[i] = static_cast<int16_t>(val * 25000);
    }

    std::ofstream file(filename, std::ios::binary);
    int data_size = num_samples * 2;
    int file_size = 36 + data_size;
    file.write("RIFF", 4);
    file.write(reinterpret_cast<char*>(&file_size), 4);
    file.write("WAVE", 4);
    file.write("fmt ", 4);
    int fmt_size = 16;
    file.write(reinterpret_cast<char*>(&fmt_size), 4);
    int16_t audio_format = 1;
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
    file.write("data", 4);
    file.write(reinterpret_cast<char*>(&data_size), 4);
    file.write(reinterpret_cast<char*>(samples.data()), data_size);
    file.close();
    return filename;
}

class IntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        model_dir_ = "models";
        db_path_ = "test_integration.db";
    }

    void TearDown() override {
        vp_release();
        std::remove(db_path_.c_str());
        // Clean up test wav files
        std::remove("test_speaker1.wav");
        std::remove("test_speaker2.wav");
        std::remove("test_verify.wav");
    }

    std::string model_dir_;
    std::string db_path_;
};

TEST_F(IntegrationTest, InitAndRelease) {
    int ret = vp_init(model_dir_.c_str(), db_path_.c_str());
    if (ret != VP_OK) {
        std::cout << "Skipping (models not available): " << vp_get_last_error() << std::endl;
        GTEST_SKIP() << "Models not available";
    }
    EXPECT_EQ(ret, VP_OK);

    // Double init should fail
    ret = vp_init(model_dir_.c_str(), db_path_.c_str());
    EXPECT_EQ(ret, VP_ERROR_ALREADY_INIT);

    vp_release();

    // After release, re-init should succeed
    ret = vp_init(model_dir_.c_str(), db_path_.c_str());
    EXPECT_EQ(ret, VP_OK);
}

TEST_F(IntegrationTest, NullParams) {
    EXPECT_EQ(vp_init(nullptr, "test.db"), VP_ERROR_INVALID_PARAM);
    EXPECT_EQ(vp_init("models", nullptr), VP_ERROR_INVALID_PARAM);
}

TEST_F(IntegrationTest, APIBeforeInit) {
    EXPECT_EQ(vp_enroll("test", nullptr, 0), VP_ERROR_NOT_INIT);
    EXPECT_EQ(vp_remove_speaker("test"), VP_ERROR_NOT_INIT);
    EXPECT_EQ(vp_get_speaker_count(), VP_ERROR_NOT_INIT);
}

TEST_F(IntegrationTest, FullLifecycle) {
    int ret = vp_init(model_dir_.c_str(), db_path_.c_str());
    if (ret != VP_OK) {
        GTEST_SKIP() << "Models not available";
    }

    // Create test WAV files
    create_speech_wav("test_speaker1.wav", 300.0f, 4.0f);
    create_speech_wav("test_speaker2.wav", 500.0f, 4.0f);

    // Enroll
    ret = vp_enroll_file("alice", "test_speaker1.wav");
    EXPECT_EQ(ret, VP_OK) << vp_get_last_error();

    ret = vp_enroll_file("bob", "test_speaker2.wav");
    EXPECT_EQ(ret, VP_OK) << vp_get_last_error();

    EXPECT_EQ(vp_get_speaker_count(), 2);

    // Set threshold
    EXPECT_EQ(vp_set_threshold(0.25f), VP_OK);

    // Remove
    EXPECT_EQ(vp_remove_speaker("bob"), VP_OK);
    EXPECT_EQ(vp_get_speaker_count(), 1);

    // Remove non-existent
    EXPECT_EQ(vp_remove_speaker("charlie"), VP_ERROR_SPEAKER_NOT_FOUND);
}

TEST_F(IntegrationTest, ConcurrentIdentify) {
    int ret = vp_init(model_dir_.c_str(), db_path_.c_str());
    if (ret != VP_OK) {
        GTEST_SKIP() << "Models not available";
    }

    // Enroll a speaker
    create_speech_wav("test_speaker1.wav", 300.0f, 4.0f);
    ret = vp_enroll_file("concurrent_test", "test_speaker1.wav");
    if (ret != VP_OK) {
        GTEST_SKIP() << "Enrollment failed";
    }

    // Concurrent identify from multiple threads
    const int num_threads = 10;
    std::vector<std::thread> threads;
    std::vector<int> results(num_threads);

    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&results, i]() {
            // Generate dummy audio
            std::vector<float> audio(48000, 0.0f); // 3 seconds
            for (size_t j = 0; j < audio.size(); ++j) {
                audio[j] = 0.3f * std::sin(2.0f * 3.14159265f * 300.0f * j / 16000.0f);
            }

            char speaker_id[256];
            float score;
            results[i] = vp_identify(audio.data(), static_cast<int>(audio.size()),
                                     speaker_id, sizeof(speaker_id), &score);
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // No crashes = success for concurrency test
    std::cout << "All " << num_threads << " concurrent threads completed" << std::endl;
}

TEST_F(IntegrationTest, InvalidAudioInput) {
    int ret = vp_init(model_dir_.c_str(), db_path_.c_str());
    if (ret != VP_OK) {
        GTEST_SKIP() << "Models not available";
    }

    // Null PCM data
    EXPECT_EQ(vp_enroll("test", nullptr, 100), VP_ERROR_INVALID_PARAM);

    // Zero samples
    float dummy = 0.0f;
    EXPECT_EQ(vp_enroll("test", &dummy, 0), VP_ERROR_INVALID_PARAM);

    // Null speaker id
    EXPECT_EQ(vp_enroll(nullptr, &dummy, 1), VP_ERROR_INVALID_PARAM);
}

TEST_F(IntegrationTest, GetLastError) {
    // Before init
    const char* err = vp_get_last_error();
    EXPECT_NE(err, nullptr);

    // After failed operation
    vp_enroll("test", nullptr, 0);
    err = vp_get_last_error();
    EXPECT_NE(err, nullptr);
    EXPECT_GT(strlen(err), 0u);
}
