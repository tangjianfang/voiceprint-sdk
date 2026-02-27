#include <voiceprint/voiceprint_api.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <sstream>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <chrono>
#include <cstring>
#include <cstdint>

struct TrialPair {
    int label;         // 1=same, 0=different
    std::string enroll_wav;
    std::string test_wav;
};

struct EvalMetrics {
    double eer;
    double min_dcf;
    double tar_at_far_1;    // TAR @ FAR=1%
    double tar_at_far_01;   // TAR @ FAR=0.1%
    double optimal_threshold;
};

// Read WAV file and return float PCM samples
static bool read_wav_pcm(const std::string& wav_path, std::vector<float>& out_samples) {
    std::ifstream file(wav_path, std::ios::binary);
    if (!file.is_open()) return false;

    char riff[4];
    file.read(riff, 4);
    if (std::strncmp(riff, "RIFF", 4) != 0) return false;

    uint32_t file_size;
    file.read(reinterpret_cast<char*>(&file_size), 4);

    char wave[4];
    file.read(wave, 4);
    if (std::strncmp(wave, "WAVE", 4) != 0) return false;

    uint16_t audio_format = 0, num_channels = 0, bits_per_sample = 0;
    uint32_t sample_rate = 0;
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
            if (chunk_size > 16) file.seekg(chunk_size - 16, std::ios::cur);
        } else if (std::strncmp(chunk_id, "data", 4) == 0) {
            audio_data.resize(chunk_size);
            file.read(reinterpret_cast<char*>(audio_data.data()), chunk_size);
            break;
        } else {
            file.seekg(chunk_size, std::ios::cur);
        }
    }

    if (audio_data.empty() || bits_per_sample == 0) return false;

    // Convert to float [-1, 1]
    if (bits_per_sample == 16) {
        size_t num_samples = audio_data.size() / 2;
        out_samples.resize(num_samples);
        const int16_t* raw = reinterpret_cast<const int16_t*>(audio_data.data());
        for (size_t i = 0; i < num_samples; ++i) {
            out_samples[i] = static_cast<float>(raw[i]) / 32768.0f;
        }
        // Mix to mono if stereo
        if (num_channels == 2) {
            size_t mono_count = num_samples / 2;
            std::vector<float> mono(mono_count);
            for (size_t i = 0; i < mono_count; ++i) {
                mono[i] = (out_samples[i * 2] + out_samples[i * 2 + 1]) * 0.5f;
            }
            out_samples = std::move(mono);
        }
    } else if (bits_per_sample == 32 && audio_format == 3) {
        // IEEE float
        size_t num_samples = audio_data.size() / 4;
        out_samples.resize(num_samples);
        std::memcpy(out_samples.data(), audio_data.data(), audio_data.size());
        if (num_channels == 2) {
            size_t mono_count = num_samples / 2;
            std::vector<float> mono(mono_count);
            for (size_t i = 0; i < mono_count; ++i) {
                mono[i] = (out_samples[i * 2] + out_samples[i * 2 + 1]) * 0.5f;
            }
            out_samples = std::move(mono);
        }
    } else {
        return false;
    }

    return true;
}

// Compute EER and other metrics
static EvalMetrics compute_metrics(const std::vector<float>& scores,
                                    const std::vector<int>& labels) {
    EvalMetrics metrics{};

    if (scores.empty()) {
        return metrics;
    }

    // Separate positive and negative scores
    std::vector<float> pos_scores, neg_scores;
    for (size_t i = 0; i < scores.size(); ++i) {
        if (labels[i] == 1) {
            pos_scores.push_back(scores[i]);
        } else {
            neg_scores.push_back(scores[i]);
        }
    }

    if (pos_scores.empty() || neg_scores.empty()) {
        std::cerr << "Need both positive and negative pairs!" << std::endl;
        return metrics;
    }

    std::sort(pos_scores.begin(), pos_scores.end());
    std::sort(neg_scores.begin(), neg_scores.end());

    // Sweep thresholds to find EER
    double best_eer = 1.0;
    double best_threshold = 0.0;
    double best_dcf = 1.0;
    const double p_target = 0.01;
    const double c_miss = 1.0;
    const double c_fa = 1.0;

    for (float threshold = -1.0f; threshold <= 1.0f; threshold += 0.001f) {
        // FAR = fraction of negative scores >= threshold
        int fa_count = 0;
        for (float s : neg_scores) {
            if (s >= threshold) fa_count++;
        }
        double far = static_cast<double>(fa_count) / neg_scores.size();

        // FRR = fraction of positive scores < threshold
        int fr_count = 0;
        for (float s : pos_scores) {
            if (s < threshold) fr_count++;
        }
        double frr = static_cast<double>(fr_count) / pos_scores.size();

        double eer_diff = std::abs(far - frr);
        if (eer_diff < std::abs(best_eer - 0.0)) {
            double eer = (far + frr) / 2.0;
            if (std::abs(far - frr) < std::abs(best_eer * 2 - far - frr + 0.001)) {
                best_eer = eer;
                best_threshold = threshold;
            }
        }

        // minDCF
        double dcf = c_miss * frr * p_target + c_fa * far * (1.0 - p_target);
        if (dcf < best_dcf) {
            best_dcf = dcf;
        }
    }

    metrics.eer = best_eer;
    metrics.min_dcf = best_dcf;
    metrics.optimal_threshold = best_threshold;

    // TAR @ FAR=1%
    {
        float threshold_1 = -1.0f;
        for (float t = 1.0f; t >= -1.0f; t -= 0.001f) {
            int fa = 0;
            for (float s : neg_scores) {
                if (s >= t) fa++;
            }
            double far = static_cast<double>(fa) / neg_scores.size();
            if (far <= 0.01) {
                threshold_1 = t;
                break;
            }
        }
        int tp = 0;
        for (float s : pos_scores) {
            if (s >= threshold_1) tp++;
        }
        metrics.tar_at_far_1 = static_cast<double>(tp) / pos_scores.size();
    }

    // TAR @ FAR=0.1%
    {
        float threshold_01 = -1.0f;
        for (float t = 1.0f; t >= -1.0f; t -= 0.001f) {
            int fa = 0;
            for (float s : neg_scores) {
                if (s >= t) fa++;
            }
            double far = static_cast<double>(fa) / neg_scores.size();
            if (far <= 0.001) {
                threshold_01 = t;
                break;
            }
        }
        int tp = 0;
        for (float s : pos_scores) {
            if (s >= threshold_01) tp++;
        }
        metrics.tar_at_far_01 = static_cast<double>(tp) / pos_scores.size();
    }

    return metrics;
}

int main(int argc, char* argv[]) {
    std::string model_dir = argc > 1 ? argv[1] : "models";
    std::string trial_list = argc > 2 ? argv[2] : "testdata/trials.txt";
    std::string db_path = "evaluation_temp.db";

    std::cout << "=== VoicePrint SDK Evaluation ===" << std::endl;
    std::cout << "Model dir: " << model_dir << std::endl;
    std::cout << "Trial list: " << trial_list << std::endl;

    // Read trial list
    std::ifstream trial_file(trial_list);
    if (!trial_file.is_open()) {
        std::cerr << "Cannot open trial list: " << trial_list << std::endl;
        std::cerr << "Expected format: label enroll_wav test_wav" << std::endl;
        std::cerr << "Generating synthetic evaluation..." << std::endl;

        // Generate synthetic evaluation report
        std::ofstream report("reports/evaluation_report.txt");
        report << "=== VoicePrint SDK Evaluation Report ===\n\n";
        report << "NOTE: Synthetic evaluation (no real test data available)\n\n";
        report << "To run real evaluation:\n";
        report << "1. Download VoxCeleb1 test set\n";
        report << "2. Create trial list: testdata/trials.txt\n";
        report << "3. Format: <label> <enroll_wav> <test_wav>\n";
        report << "4. Run: evaluation_tests <model_dir> <trial_list>\n";
        report.close();

        std::cout << "Synthetic report saved to reports/evaluation_report.txt" << std::endl;
        return 0;
    }

    std::vector<TrialPair> trials;
    std::string line;
    while (std::getline(trial_file, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream iss(line);
        TrialPair trial;
        iss >> trial.label >> trial.enroll_wav >> trial.test_wav;
        trials.push_back(trial);
    }
    trial_file.close();

    std::cout << "Loaded " << trials.size() << " trial pairs" << std::endl;

    // Initialize SDK
    int ret = vp_init(model_dir.c_str(), db_path.c_str());
    if (ret != VP_OK) {
        std::cerr << "Init failed: " << vp_get_last_error() << std::endl;
        return 1;
    }

    // Process all trials
    std::vector<float> scores;
    std::vector<int> labels;

    auto start_time = std::chrono::high_resolution_clock::now();
    int processed = 0;

    for (const auto& trial : trials) {
        // Enroll
        std::string enroll_id = "eval_enroll";
        ret = vp_enroll_file(enroll_id.c_str(), trial.enroll_wav.c_str());
        if (ret != VP_OK) {
            std::cerr << "Enroll failed for " << trial.enroll_wav << ": " << vp_get_last_error() << std::endl;
            vp_remove_speaker(enroll_id.c_str());
            continue;
        }

        // Read test WAV and verify
        std::vector<float> test_pcm;
        if (!read_wav_pcm(trial.test_wav, test_pcm) || test_pcm.empty()) {
            std::cerr << "Cannot read test WAV: " << trial.test_wav << std::endl;
            vp_remove_speaker(enroll_id.c_str());
            continue;
        }

        float score = 0.0f;
        ret = vp_verify(enroll_id.c_str(), test_pcm.data(),
                        static_cast<int>(test_pcm.size()), &score);
        if (ret == VP_OK) {
            scores.push_back(score);
            labels.push_back(trial.label);
        } else {
            std::cerr << "Verify failed for " << trial.test_wav << ": " << vp_get_last_error() << std::endl;
        }

        // Clean up for next trial
        vp_remove_speaker(enroll_id.c_str());

        processed++;
        if (processed % 100 == 0) {
            std::cout << "Processed " << processed << "/" << trials.size() << " trials" << std::endl;
        }
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    double total_seconds = std::chrono::duration<double>(end_time - start_time).count();

    // Compute metrics
    auto metrics = compute_metrics(scores, labels);

    // Print and save report
    std::cout << "\n=== Evaluation Results ===" << std::endl;
    std::cout << "EER:            " << metrics.eer * 100 << "%" << std::endl;
    std::cout << "minDCF (p=0.01):" << metrics.min_dcf << std::endl;
    std::cout << "TAR@FAR=1%:     " << metrics.tar_at_far_1 * 100 << "%" << std::endl;
    std::cout << "TAR@FAR=0.1%:   " << metrics.tar_at_far_01 * 100 << "%" << std::endl;
    std::cout << "Optimal thresh: " << metrics.optimal_threshold << std::endl;
    std::cout << "Total time:     " << total_seconds << "s" << std::endl;
    std::cout << "Pairs processed:" << processed << std::endl;

    // Save report
    std::ofstream report("reports/evaluation_report.txt");
    report << "=== VoicePrint SDK Evaluation Report ===\n\n";
    report << "Model: " << model_dir << "\n";
    report << "Trial list: " << trial_list << "\n";
    report << "Total pairs: " << trials.size() << "\n";
    report << "Processed: " << processed << "\n\n";
    report << "--- Metrics ---\n";
    report << "EER:             " << metrics.eer * 100 << "%\n";
    report << "minDCF (p=0.01): " << metrics.min_dcf << "\n";
    report << "TAR@FAR=1%:      " << metrics.tar_at_far_1 * 100 << "%\n";
    report << "TAR@FAR=0.1%:    " << metrics.tar_at_far_01 * 100 << "%\n";
    report << "Optimal threshold: " << metrics.optimal_threshold << "\n\n";
    report << "--- Targets ---\n";
    report << "EER:             <= 3% " << (metrics.eer <= 0.03 ? "PASS" : "FAIL") << "\n";
    report << "minDCF (p=0.01): <= 0.30 " << (metrics.min_dcf <= 0.30 ? "PASS" : "FAIL") << "\n";
    report << "TAR@FAR=1%:      >= 95% " << (metrics.tar_at_far_1 >= 0.95 ? "PASS" : "FAIL") << "\n";
    report << "TAR@FAR=0.1%:    >= 90% " << (metrics.tar_at_far_01 >= 0.90 ? "PASS" : "FAIL") << "\n";
    report.close();

    std::cout << "\nReport saved to: reports/evaluation_report.txt" << std::endl;

    vp_release();
    std::remove(db_path.c_str());

    return 0;
}
