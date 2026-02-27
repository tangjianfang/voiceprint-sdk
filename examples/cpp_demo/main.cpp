#include <iostream>
#include <vector>
#include <string>
#include <cmath>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <voiceprint/voiceprint_api.h>

// Get directory of current executable
static std::string get_exe_dir() {
    char path[MAX_PATH] = {};
    GetModuleFileNameA(NULL, path, MAX_PATH);
    std::string s(path);
    auto pos = s.find_last_of("\\/");
    return (pos != std::string::npos) ? s.substr(0, pos) : ".";
}

// Generate a simple sine wave for testing
std::vector<float> generate_sine_wave(float freq, float duration, int sample_rate = 16000) {
    int num_samples = static_cast<int>(duration * sample_rate);
    std::vector<float> samples(num_samples);
    for (int i = 0; i < num_samples; ++i) {
        samples[i] = 0.5f * std::sin(2.0f * 3.14159265f * freq * i / sample_rate);
    }
    return samples;
}

int main(int argc, char* argv[]) {
    std::cout << "=== VoicePrint SDK C++ Demo ===" << std::endl;

    // Auto-detect models relative to executable: exe_dir/../models
    std::string exe_dir = get_exe_dir();
    std::string model_dir = exe_dir + "\\..\\models";
    std::string db_path = "voiceprint_demo.db";

    // Check if models dir exists, fallback to "models" (CWD-relative)
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA((model_dir + "\\*.onnx").c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) {
        model_dir = "models";
    } else {
        FindClose(hFind);
    }

    if (argc >= 3) {
        model_dir = argv[1];
        db_path = argv[2];
    }

    // 1. Initialize SDK
    std::cout << "\n[1] Initializing SDK..." << std::endl;
    int ret = vp_init(model_dir.c_str(), db_path.c_str());
    if (ret != VP_OK) {
        std::cerr << "Init failed: " << vp_get_last_error() << std::endl;
        return 1;
    }
    std::cout << "SDK initialized successfully!" << std::endl;

    // 2. Enroll speakers from WAV files (if provided)
    if (argc >= 5) {
        std::string speaker1_id = "speaker_A";
        std::string speaker1_wav = argv[3];
        std::cout << "\n[2] Enrolling speaker '" << speaker1_id << "' from: " << speaker1_wav << std::endl;

        ret = vp_enroll_file(speaker1_id.c_str(), speaker1_wav.c_str());
        if (ret != VP_OK) {
            std::cerr << "Enroll failed: " << vp_get_last_error() << std::endl;
        } else {
            std::cout << "Speaker enrolled successfully!" << std::endl;
        }

        std::string speaker2_id = "speaker_B";
        std::string speaker2_wav = argv[4];
        std::cout << "\n[3] Enrolling speaker '" << speaker2_id << "' from: " << speaker2_wav << std::endl;

        ret = vp_enroll_file(speaker2_id.c_str(), speaker2_wav.c_str());
        if (ret != VP_OK) {
            std::cerr << "Enroll failed: " << vp_get_last_error() << std::endl;
        } else {
            std::cout << "Speaker enrolled successfully!" << std::endl;
        }

        std::cout << "\nTotal speakers: " << vp_get_speaker_count() << std::endl;

        // 3. Identify (use first speaker's wav again)
        if (argc >= 6) {
            std::string test_wav = argv[5];
            std::cout << "\n[4] Identifying speaker from: " << test_wav << std::endl;

            // Read the test wav and identify (using file-based approach would need vp_identify_file)
            // For now, demonstrate the API structure
        }

        // 4. Verify
        std::cout << "\n[5] Speaker verification demo..." << std::endl;

        // 5. Remove speaker
        std::cout << "\n[6] Removing speaker_B..." << std::endl;
        ret = vp_remove_speaker("speaker_B");
        if (ret == VP_OK) {
            std::cout << "Speaker removed. Count: " << vp_get_speaker_count() << std::endl;
        }
    } else {
        std::cout << "\nUsage for WAV file demo:" << std::endl;
        std::cout << "  cpp_demo <model_dir> <db_path> <speaker1.wav> <speaker2.wav> [test.wav]" << std::endl;
        std::cout << "\nRunning full API demo with synthetic audio..." << std::endl;

        // Set threshold
        vp_set_threshold(0.30f);
        std::cout << "Threshold set to 0.30" << std::endl;

        // Enroll two speakers with different frequencies
        std::cout << "\n[2] Enrolling speaker_A (440Hz)..." << std::endl;
        auto audio_a = generate_sine_wave(440.0f, 3.0f);
        ret = vp_enroll("speaker_A", audio_a.data(), static_cast<int>(audio_a.size()));
        if (ret == VP_OK) {
            std::cout << "  speaker_A enrolled successfully!" << std::endl;
        } else {
            std::cerr << "  Enroll failed: " << vp_get_last_error() << std::endl;
        }

        std::cout << "\n[3] Enrolling speaker_B (880Hz)..." << std::endl;
        auto audio_b = generate_sine_wave(880.0f, 3.0f);
        ret = vp_enroll("speaker_B", audio_b.data(), static_cast<int>(audio_b.size()));
        if (ret == VP_OK) {
            std::cout << "  speaker_B enrolled successfully!" << std::endl;
        } else {
            std::cerr << "  Enroll failed: " << vp_get_last_error() << std::endl;
        }

        std::cout << "\nTotal speakers: " << vp_get_speaker_count() << std::endl;

        // Identify with speaker_A's audio
        std::cout << "\n[4] Identifying speaker from 440Hz audio..." << std::endl;
        char identified[256] = {};
        float score = 0.0f;
        ret = vp_identify(audio_a.data(), static_cast<int>(audio_a.size()),
                          identified, sizeof(identified), &score);
        if (ret == VP_OK) {
            std::cout << "  Identified: " << identified << " (score: " << score << ")" << std::endl;
        } else {
            std::cout << "  No match found (all scores below threshold)" << std::endl;
        }

        // Verify speaker_A
        std::cout << "\n[5] Verifying speaker_A with 440Hz audio..." << std::endl;
        float verify_score = 0.0f;
        ret = vp_verify("speaker_A", audio_a.data(),
                        static_cast<int>(audio_a.size()), &verify_score);
        if (ret == VP_OK) {
            std::cout << "  Verified! Score: " << verify_score << std::endl;
        } else {
            std::cout << "  Verification failed: " << vp_get_last_error() << std::endl;
        }

        // Verify speaker_A with speaker_B's audio (should have low score)
        std::cout << "\n[6] Verifying speaker_A with 880Hz audio (cross-speaker)..." << std::endl;
        float cross_score = 0.0f;
        ret = vp_verify("speaker_A", audio_b.data(),
                        static_cast<int>(audio_b.size()), &cross_score);
        std::cout << "  Cross-speaker score: " << cross_score << std::endl;

        // Remove speaker
        std::cout << "\n[7] Removing speaker_B..." << std::endl;
        ret = vp_remove_speaker("speaker_B");
        if (ret == VP_OK) {
            std::cout << "  Speaker removed. Remaining: " << vp_get_speaker_count() << std::endl;
        }

        ret = vp_remove_speaker("speaker_A");
        if (ret == VP_OK) {
            std::cout << "  speaker_A removed. Remaining: " << vp_get_speaker_count() << std::endl;
        }
    }

    // 6. Release
    std::cout << "\n[Final] Releasing SDK..." << std::endl;
    vp_release();
    std::cout << "SDK released." << std::endl;

    return 0;
}
