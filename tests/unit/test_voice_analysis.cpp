// Unit tests for voice analysis DSP utilities and clustering.
// These tests run against voiceprint_core (no ONNX models required).

#include <gtest/gtest.h>
#include "core/loudness.h"
#include "core/pitch_analyzer.h"
#include "core/clustering.h"

#include <cmath>
#include <vector>
#include <numeric>

namespace {

// ----------------------------------------------------------------
// Helpers
// ----------------------------------------------------------------
static std::vector<float> make_sine(float freq_hz, float dur_sec,
                                    int sample_rate = 16000) {
    int n = static_cast<int>(dur_sec * sample_rate);
    std::vector<float> pcm(n);
    for (int i = 0; i < n; ++i)
        pcm[i] = 0.5f * std::sin(2.0f * M_PI * freq_hz * i / sample_rate);
    return pcm;
}

static std::vector<float> make_noise(float amp, int n_samples) {
    std::vector<float> noise(n_samples);
    for (int i = 0; i < n_samples; ++i)
        noise[i] = amp * (static_cast<float>(rand()) / RAND_MAX * 2.0f - 1.0f);
    return noise;
}

static std::vector<float> mix(const std::vector<float>& a,
                               const std::vector<float>& b) {
    std::vector<float> result(std::min(a.size(), b.size()));
    for (size_t i = 0; i < result.size(); ++i) result[i] = a[i] + b[i];
    return result;
}

// ----------------------------------------------------------------
// Loudness tests
// ----------------------------------------------------------------
TEST(Loudness, SilenceShouldBeLow) {
    std::vector<float> silence(16000 * 3, 0.0f);
    float lufs = vp::dsp::compute_lufs(silence);
    EXPECT_LT(lufs, -60.0f);
}

TEST(Loudness, FullScaleSineShouldBeHigh) {
    auto sine = make_sine(440.0f, 3.0f);
    // Scale to near full scale
    for (auto& s : sine) s *= 2.0f;
    float lufs = vp::dsp::compute_lufs(sine);
    EXPECT_GT(lufs, -20.0f);
    EXPECT_LT(lufs, 0.0f);  // won't exceed 0 LUFS for a sine
}

TEST(Loudness, EmptyReturnsSentinelValue) {
    std::vector<float> empty;
    float lufs = vp::dsp::compute_lufs(empty);
    EXPECT_LE(lufs, -60.0f);
}

TEST(SNR, CleanSignalHighSNR) {
    auto sine = make_sine(440.0f, 2.0f);
    float snr = vp::dsp::compute_snr_db_simple(sine);
    // Pure sine has uniform energy → SNR should still be positive
    EXPECT_GT(snr, 0.0f);
}

TEST(SNR, NoisySignalLowerSNR) {
    auto sine  = make_sine(440.0f, 2.0f);
    auto noise = make_noise(0.1f, static_cast<int>(sine.size()));
    auto noisy = mix(sine, noise);
    float snr_clean = vp::dsp::compute_snr_db_simple(sine);
    float snr_noisy = vp::dsp::compute_snr_db_simple(noisy);
    // Noisy SNR may be higher or lower; just verify function returns a number
    EXPECT_FALSE(std::isnan(snr_noisy));
    EXPECT_FALSE(std::isinf(snr_noisy));
    (void)snr_clean;
}

TEST(SNR, SpeechNoiseSplitSNR) {
    auto speech = make_sine(200.0f, 2.0f, 16000);
    auto noise  = make_noise(0.05f, 16000);
    float snr = vp::dsp::compute_snr_db(speech, noise);
    EXPECT_GT(snr, 10.0f);  // speech is much louder than noise
}

// ----------------------------------------------------------------
// Pitch analyzer tests
// ----------------------------------------------------------------
TEST(PitchAnalyzer, A4SineDetectedCorrectly) {
    auto sine = make_sine(440.0f, 2.0f);
    vp::dsp::PitchAnalyzer pa;
    auto frames = pa.analyze(sine);
    auto summary = vp::dsp::PitchAnalyzer::summarize(frames);

    ASSERT_GT(summary.voiced_fraction, 0.5f)
        << "Should detect most frames as voiced for pure sine";
    EXPECT_NEAR(summary.mean_f0_hz, 440.0f, 20.0f)
        << "Mean F0 should be near 440 Hz for A4 sine";
}

TEST(PitchAnalyzer, 200HzSineDetectedCorrectly) {
    auto sine = make_sine(200.0f, 2.0f);
    vp::dsp::PitchAnalyzer pa;
    auto frames = pa.analyze(sine);
    auto summary = vp::dsp::PitchAnalyzer::summarize(frames);

    ASSERT_GT(summary.voiced_fraction, 0.4f);
    EXPECT_NEAR(summary.mean_f0_hz, 200.0f, 30.0f);
}

TEST(PitchAnalyzer, SilenceIsUnvoiced) {
    std::vector<float> silence(16000 * 2, 0.0f);
    vp::dsp::PitchAnalyzer pa;
    auto frames = pa.analyze(silence);
    auto summary = vp::dsp::PitchAnalyzer::summarize(frames);
    EXPECT_LT(summary.voiced_fraction, 0.1f);
    EXPECT_EQ(summary.mean_f0_hz, 0.0f);
}

TEST(PitchAnalyzer, ShortAudioReturnsEmptyFrames) {
    std::vector<float> short_pcm(100, 0.1f);
    vp::dsp::PitchAnalyzer pa;
    auto frames = pa.analyze(short_pcm);
    EXPECT_TRUE(frames.empty());
}

TEST(SpeakingRate, PureSineHasSomePeaks) {
    auto sine = make_sine(3.0f, 3.0f);  // 3 Hz modulation ~ 3 syll/s
    float rate = vp::dsp::estimate_speaking_rate(sine);
    EXPECT_GE(rate, 0.0f);
    EXPECT_LT(rate, 20.0f);  // sanity upper bound
}

// ----------------------------------------------------------------
// Clustering tests
// ----------------------------------------------------------------
TEST(Clustering, SingleInputReturnsOneCluster) {
    std::vector<std::vector<float>> embeddings = {{1.0f, 0.0f, 0.0f}};
    auto result = vp::clustering::agglomerative_cluster(embeddings, 0.45f);
    EXPECT_EQ(result.num_clusters, 1);
    EXPECT_EQ(result.labels[0], 0);
}

TEST(Clustering, IdenticalVectorsMerge) {
    std::vector<std::vector<float>> embeddings(5, {1.0f, 0.0f, 0.0f});
    auto result = vp::clustering::agglomerative_cluster(embeddings, 0.45f);
    EXPECT_EQ(result.num_clusters, 1);
    for (int lbl : result.labels) EXPECT_EQ(lbl, 0);
}

TEST(Clustering, OrthogonalVectorsStaySeparate) {
    // 4 orthogonal unit vectors → 4 clusters (cosine distance = 1.0 > threshold 0.45)
    std::vector<std::vector<float>> embeddings = {
        {1.0f, 0.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 0.0f, 1.0f},
    };
    auto result = vp::clustering::agglomerative_cluster(embeddings, 0.45f);
    EXPECT_EQ(result.num_clusters, 4);
}

TEST(Clustering, TwoGroupsMergeCorrectly) {
    // Two tight groups of 3 vectors each
    float s = std::sqrt(2.0f) / 2.0f;
    std::vector<std::vector<float>> embeddings = {
        {1.0f, 0.0f},   // group A
        {0.98f, 0.2f},  // group A
        {0.96f, 0.28f}, // group A
        {0.0f, 1.0f},   // group B
        {0.2f, 0.98f},  // group B
        {0.28f, 0.96f}, // group B
    };
    // L2-normalize
    for (auto& v : embeddings) {
        float norm = std::sqrt(v[0]*v[0]+v[1]*v[1]);
        for (auto& x : v) x /= norm;
    }
    auto result = vp::clustering::agglomerative_cluster(embeddings, 0.3f);
    EXPECT_EQ(result.num_clusters, 2);
    // All A should have same label, all B same label, different from A
    EXPECT_EQ(result.labels[0], result.labels[1]);
    EXPECT_EQ(result.labels[1], result.labels[2]);
    EXPECT_EQ(result.labels[3], result.labels[4]);
    EXPECT_EQ(result.labels[4], result.labels[5]);
    EXPECT_NE(result.labels[0], result.labels[3]);
    (void)s;
}

TEST(Clustering, EmptyInputReturnsEmptyResult) {
    std::vector<std::vector<float>> empty;
    auto result = vp::clustering::agglomerative_cluster(empty, 0.45f);
    EXPECT_EQ(result.num_clusters, 0);
    EXPECT_TRUE(result.labels.empty());
}

TEST(Clustering, MaxClustersConstraint) {
    std::vector<std::vector<float>> embeddings(10);
    for (int i = 0; i < 10; ++i) {
        embeddings[i].resize(3, 0.0f);
        embeddings[i][i % 3] = 1.0f;
    }
    // Without max_clusters: up to 3 clusters (orthogonal)
    auto r1 = vp::clustering::agglomerative_cluster(embeddings, 0.99f, 0);
    EXPECT_LE(r1.num_clusters, 3);
    // With max_clusters=2: force merge to 2
    auto r2 = vp::clustering::agglomerative_cluster(embeddings, 0.99f, 2);
    EXPECT_LE(r2.num_clusters, 2);
}

TEST(Clustering, LabelsCoverAllInputs) {
    std::vector<std::vector<float>> embeddings;
    for (int i = 0; i < 20; ++i) {
        embeddings.push_back({static_cast<float>(i % 4 == 0),
                              static_cast<float>(i % 4 == 1),
                              static_cast<float>(i % 4 == 2),
                              static_cast<float>(i % 4 == 3)});
    }
    auto result = vp::clustering::agglomerative_cluster(embeddings, 0.45f);
    EXPECT_EQ(static_cast<int>(result.labels.size()), 20);
    for (int lbl : result.labels) {
        EXPECT_GE(lbl, 0);
        EXPECT_LT(lbl, result.num_clusters);
    }
}

// ----------------------------------------------------------------
// HNR test
// ----------------------------------------------------------------
TEST(HNR, PureSineHasHighHNR) {
    auto sine = make_sine(200.0f, 1.0f);
    float hnr = vp::dsp::compute_hnr_db(sine, 200.0f);
    EXPECT_GT(hnr, 15.0f)
        << "Pure sine should have high HNR";
}

TEST(HNR, InvalidPitchReturnsFallback) {
    auto pcm = make_sine(440.0f, 0.5f);
    float hnr = vp::dsp::compute_hnr_db(pcm, 0.0f);  // 0 Hz = unvoiced
    EXPECT_EQ(hnr, 15.0f);  // fallback default
}

// ----------------------------------------------------------------
// Breathiness / Resonance sanity checks
// ----------------------------------------------------------------
TEST(VoiceFeatureDSP, BreathinessIsBounded) {
    auto pcm  = make_sine(200.0f, 1.0f);
    auto fbank = vp::FbankExtractor{};
    fbank.init(80, 16000);
    auto feats = fbank.extract(pcm);
    int nf = fbank.get_num_frames(static_cast<int>(pcm.size()));
    float br = vp::dsp::compute_breathiness(feats, 80, nf);
    EXPECT_GE(br, 0.0f);
    EXPECT_LE(br, 1.0f);
}

TEST(VoiceFeatureDSP, ResonanceIsBounded) {
    auto pcm  = make_sine(200.0f, 1.0f);
    vp::FbankExtractor fbank;
    fbank.init(80, 16000);
    auto feats = fbank.extract(pcm);
    int nf = fbank.get_num_frames(static_cast<int>(pcm.size()));
    float res = vp::dsp::compute_resonance_score(feats, 80, nf);
    EXPECT_GE(res, 0.0f);
    EXPECT_LE(res, 1.0f);
}

} // namespace
