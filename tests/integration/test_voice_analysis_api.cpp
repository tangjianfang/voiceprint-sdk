// Integration tests for voice analysis API (vp_analyze, gender, emotion, etc.)
// These tests exercise the full DLL interface.
// Model-dependent tests are skipped gracefully when models are absent.

#include <gtest/gtest.h>
#include <voiceprint/voiceprint_api.h>
#include <voiceprint/voiceprint_types.h>
#include <vector>
#include <cmath>
#include <cstring>
#include <string>
#include <fstream>
#include <filesystem>

// ----------------------------------------------------------------
// Shared test helpers
// ----------------------------------------------------------------
namespace {

constexpr int SR = 16000;

static std::vector<float> make_sine_pcm(float freq_hz, float dur_sec) {
    int n = static_cast<int>(dur_sec * SR);
    std::vector<float> pcm(n);
    for (int i = 0; i < n; ++i)
        pcm[i] = 0.4f * std::sin(2.0f * M_PI * freq_hz * i / SR);
    return pcm;
}

static std::string make_wav_file(const std::string& path, float freq, float dur) {
    auto pcm = make_sine_pcm(freq, dur);
    int n = static_cast<int>(pcm.size());
    std::vector<int16_t> samples(n);
    for (int i = 0; i < n; ++i)
        samples[i] = static_cast<int16_t>(pcm[i] * 32767);

    std::ofstream f(path, std::ios::binary);
    int data_size = n * 2, file_size = 36 + data_size;
    f.write("RIFF", 4); f.write((char*)&file_size, 4);
    f.write("WAVE", 4); f.write("fmt ", 4);
    int fmt = 16;       f.write((char*)&fmt, 4);
    int16_t fmt_type=1, ch=1, blk=2, bps=16;
    f.write((char*)&fmt_type,2); f.write((char*)&ch,2);
    int sr=SR, br=SR*2;
    f.write((char*)&sr,4);  f.write((char*)&br,4);
    f.write((char*)&blk,2); f.write((char*)&bps,2);
    f.write("data",4);     f.write((char*)&data_size,4);
    f.write((char*)samples.data(), data_size);
    return path;
}

// Find the model directory relative to test binary
static std::string find_model_dir() {
    namespace fs = std::filesystem;
    // Walk up looking for models/
    fs::path p = fs::current_path();
    for (int i = 0; i < 5; ++i) {
        if (fs::exists(p / "models" / "ecapa_tdnn.onnx"))
            return (p / "models").string();
        p = p.parent_path();
    }
    return "models";
}

static bool models_available() {
    namespace fs = std::filesystem;
    std::string dir = find_model_dir();
    return fs::exists(dir + "/ecapa_tdnn.onnx") &&
           fs::exists(dir + "/silero_vad.onnx");
}

static bool analysis_models_available() {
    namespace fs = std::filesystem;
    std::string dir = find_model_dir();
    return fs::exists(dir + "/gender_age.onnx") ||
           fs::exists(dir + "/emotion.onnx")    ||
           fs::exists(dir + "/antispoof.onnx");
}

} // namespace

// ----------------------------------------------------------------
// Fixture: SDK initialized before each test, torn down after
// ----------------------------------------------------------------
class VoiceAnalysisTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!models_available()) {
            GTEST_SKIP() << "Core models not found, skipping analysis tests";
        }
        model_dir_ = find_model_dir();
        std::string db = (std::filesystem::temp_directory_path() /
                          "vp_analysis_test.db").string();
        int rc = vp_init(model_dir_.c_str(), db.c_str());
        ASSERT_EQ(rc, VP_OK) << "vp_init failed: " << vp_get_last_error();

        // Init analyzer with all features
        rc = vp_init_analyzer(VP_FEATURE_ALL);
        // Non-fatal: analyzer init may partially succeed
        EXPECT_TRUE(rc == VP_OK || rc == VP_ERROR_MODEL_NOT_AVAILABLE)
            << "vp_init_analyzer returned unexpected code: " << rc;
    }

    void TearDown() override {
        if (models_available()) vp_release();
    }

    std::string model_dir_;
};

// ----------------------------------------------------------------
// vp_analyze: smoke test
// ----------------------------------------------------------------
TEST_F(VoiceAnalysisTest, AnalyzeReturnsOkForValidInput) {
    auto pcm = make_sine_pcm(440.0f, 3.0f);
    VpAnalysisResult result{};
    int rc = vp_analyze(pcm.data(), static_cast<int>(pcm.size()),
                        VP_FEATURE_VOICE_FEATS | VP_FEATURE_QUALITY, &result);
    EXPECT_EQ(rc, VP_OK) << vp_get_last_error();
}

TEST_F(VoiceAnalysisTest, AnalyzeRejectsNullInput) {
    VpAnalysisResult result{};
    int rc = vp_analyze(nullptr, 0, VP_FEATURE_QUALITY, &result);
    EXPECT_NE(rc, VP_OK);
}

TEST_F(VoiceAnalysisTest, AnalyzeRejectsNullOutput) {
    auto pcm = make_sine_pcm(440.0f, 2.0f);
    int rc = vp_analyze(pcm.data(), static_cast<int>(pcm.size()),
                        VP_FEATURE_QUALITY, nullptr);
    EXPECT_NE(rc, VP_OK);
}

// ----------------------------------------------------------------
// DSP-only features: always available even without extra models
// ----------------------------------------------------------------
TEST_F(VoiceAnalysisTest, VoiceFeaturesReturnReasonablePitch) {
    // 440 Hz sine — pitch should be near 440
    auto pcm = make_sine_pcm(440.0f, 3.0f);
    VpAnalysisResult result{};
    int rc = vp_analyze(pcm.data(), static_cast<int>(pcm.size()),
                        VP_FEATURE_VOICE_FEATS, &result);
    ASSERT_EQ(rc, VP_OK);
    if (result.features_computed & VP_FEATURE_VOICE_FEATS) {
        EXPECT_GT(result.voice_features.pitch_hz, 0.0f)
            << "Should detect pitch in voiced sine";
        EXPECT_NEAR(result.voice_features.pitch_hz, 440.0f, 40.0f)
            << "Pitch should be near 440 Hz";
    }
}

TEST_F(VoiceAnalysisTest, QualityMetricsAreBounded) {
    auto pcm = make_sine_pcm(300.0f, 3.0f);
    VpAnalysisResult result{};
    int rc = vp_analyze(pcm.data(), static_cast<int>(pcm.size()),
                        VP_FEATURE_QUALITY | VP_FEATURE_VOICE_FEATS, &result);
    ASSERT_EQ(rc, VP_OK);
    if (result.features_computed & VP_FEATURE_QUALITY) {
        EXPECT_GE(result.quality.mos_score, 1.0f);
        EXPECT_LE(result.quality.mos_score, 5.0f);
        EXPECT_GE(result.quality.noise_level, 0.0f);
        EXPECT_LE(result.quality.noise_level, 1.0f);
        EXPECT_GE(result.quality.clarity, 0.0f);
        EXPECT_LE(result.quality.clarity, 1.0f);
    }
}

TEST_F(VoiceAnalysisTest, PleasantnessScoresAreBounded) {
    auto pcm = make_sine_pcm(200.0f, 3.0f);
    VpAnalysisResult result{};
    int rc = vp_analyze(pcm.data(), static_cast<int>(pcm.size()),
                        VP_FEATURE_QUALITY | VP_FEATURE_VOICE_FEATS |
                        VP_FEATURE_PLEASANTNESS, &result);
    ASSERT_EQ(rc, VP_OK);
    if (result.features_computed & VP_FEATURE_PLEASANTNESS) {
        EXPECT_GE(result.pleasantness.overall_score, 0.0f);
        EXPECT_LE(result.pleasantness.overall_score, 100.0f);
        EXPECT_GE(result.pleasantness.magnetism, 0.0f);
        EXPECT_LE(result.pleasantness.magnetism, 100.0f);
    }
}

TEST_F(VoiceAnalysisTest, VoiceStateFieldsAreValid) {
    auto pcm = make_sine_pcm(180.0f, 3.0f);
    VpAnalysisResult result{};
    int rc = vp_analyze(pcm.data(), static_cast<int>(pcm.size()),
                        VP_FEATURE_VOICE_FEATS | VP_FEATURE_QUALITY | VP_FEATURE_VOICE_STATE,
                        &result);
    ASSERT_EQ(rc, VP_OK);
    if (result.features_computed & VP_FEATURE_VOICE_STATE) {
        EXPECT_GE(result.voice_state.fatigue_level, VP_FATIGUE_NORMAL);
        EXPECT_LE(result.voice_state.fatigue_level, VP_FATIGUE_HIGH);
        EXPECT_GE(result.voice_state.stress_level,  VP_STRESS_LOW);
        EXPECT_LE(result.voice_state.stress_level,  VP_STRESS_HIGH);
        EXPECT_GE(result.voice_state.health_score,  0.0f);
        EXPECT_LE(result.voice_state.health_score,  1.0f);
    }
}

// ----------------------------------------------------------------
// File-based API
// ----------------------------------------------------------------
TEST_F(VoiceAnalysisTest, AnalyzeFileWorks) {
    std::string wav = (std::filesystem::temp_directory_path() / "analysis_test.wav").string();
    make_wav_file(wav, 440.0f, 3.0f);
    VpAnalysisResult result{};
    int rc = vp_analyze_file(wav.c_str(),
                             VP_FEATURE_VOICE_FEATS | VP_FEATURE_QUALITY, &result);
    EXPECT_EQ(rc, VP_OK) << vp_get_last_error();
    std::filesystem::remove(wav);
}

TEST_F(VoiceAnalysisTest, AnalyzeMissingFileReturnsError) {
    VpAnalysisResult result{};
    int rc = vp_analyze_file("/nonexistent/path.wav", VP_FEATURE_QUALITY, &result);
    EXPECT_NE(rc, VP_OK);
}

// ----------------------------------------------------------------
// Convenience single-feature functions
// ----------------------------------------------------------------
TEST_F(VoiceAnalysisTest, AssessQualityConvenienceFunction) {
    auto pcm = make_sine_pcm(440.0f, 3.0f);
    VpQualityResult q{};
    int rc = vp_assess_quality(pcm.data(), static_cast<int>(pcm.size()), &q);
    EXPECT_EQ(rc, VP_OK) << vp_get_last_error();
    EXPECT_GE(q.mos_score, 1.0f);
    EXPECT_LE(q.mos_score, 5.0f);
}

TEST_F(VoiceAnalysisTest, AnalyzeVoiceConvenienceFunction) {
    auto pcm = make_sine_pcm(440.0f, 3.0f);
    VpVoiceFeatures vf{};
    int rc = vp_analyze_voice(pcm.data(), static_cast<int>(pcm.size()), &vf);
    EXPECT_EQ(rc, VP_OK) << vp_get_last_error();
    EXPECT_GE(vf.voice_stability, 0.0f);
    EXPECT_LE(vf.voice_stability, 1.0f);
}

// ----------------------------------------------------------------
// Model-dependent tests (skip when ONNX models absent)
// ----------------------------------------------------------------
TEST_F(VoiceAnalysisTest, GenderResultValidWhenModelPresent) {
    auto pcm = make_sine_pcm(200.0f, 3.0f);  // low pitch → likely male
    VpGenderResult g{};
    int rc = vp_get_gender(pcm.data(), static_cast<int>(pcm.size()), &g);
    if (rc == VP_ERROR_MODEL_NOT_AVAILABLE) {
        GTEST_SKIP() << "gender_age.onnx not loaded";
    }
    ASSERT_EQ(rc, VP_OK) << vp_get_last_error();
    EXPECT_GE(g.gender, VP_GENDER_FEMALE);
    EXPECT_LE(g.gender, VP_GENDER_CHILD);
    float sum = g.scores[0] + g.scores[1] + g.scores[2];
    EXPECT_NEAR(sum, 1.0f, 0.05f) << "Gender scores should sum to ~1";
}

TEST_F(VoiceAnalysisTest, EmotionResultValidWhenModelPresent) {
    auto pcm = make_sine_pcm(440.0f, 3.0f);
    VpEmotionResult e{};
    int rc = vp_get_emotion(pcm.data(), static_cast<int>(pcm.size()), &e);
    if (rc == VP_ERROR_MODEL_NOT_AVAILABLE) {
        GTEST_SKIP() << "emotion.onnx not loaded";
    }
    ASSERT_EQ(rc, VP_OK) << vp_get_last_error();
    EXPECT_GE(e.emotion_id, 0);
    EXPECT_LT(e.emotion_id, VP_EMOTION_COUNT);
    EXPECT_GE(e.valence, -1.0f);
    EXPECT_LE(e.valence,  1.0f);
}

TEST_F(VoiceAnalysisTest, AntiSpoofResultValidWhenModelPresent) {
    auto pcm = make_sine_pcm(300.0f, 4.0f);
    VpAntiSpoofResult a{};
    int rc = vp_anti_spoof(pcm.data(), static_cast<int>(pcm.size()), &a);
    if (rc == VP_ERROR_MODEL_NOT_AVAILABLE) {
        GTEST_SKIP() << "antispoof.onnx not loaded";
    }
    ASSERT_EQ(rc, VP_OK);
    EXPECT_GE(a.genuine_score, 0.0f);
    EXPECT_LE(a.genuine_score, 1.0f);
    EXPECT_NEAR(a.genuine_score + a.spoof_score, 1.0f, 0.05f);
}

// ----------------------------------------------------------------
// Emotion name helper
// ----------------------------------------------------------------
TEST(VoiceAnalysisStatic, EmotionNameReturnsValidStrings) {
    EXPECT_STREQ(vp_emotion_name(VP_EMOTION_HAPPY),    "happy");
    EXPECT_STREQ(vp_emotion_name(VP_EMOTION_NEUTRAL),  "neutral");
    EXPECT_STREQ(vp_emotion_name(VP_EMOTION_ANGRY),    "angry");
    const char* out_of_range = vp_emotion_name(999);
    EXPECT_NE(out_of_range, nullptr);
}

TEST(VoiceAnalysisStatic, LanguageNameLookup) {
    const char* en = vp_language_name("en");
    EXPECT_NE(en, nullptr);
    EXPECT_STRNE(en, "");
    // Unknown code returns code itself
    const char* unk = vp_language_name("xx");
    EXPECT_NE(unk, nullptr);
}

// ----------------------------------------------------------------
// Diarization: basic smoke test
// ----------------------------------------------------------------
TEST_F(VoiceAnalysisTest, DiarizeReturnsSomeSegments) {
    // 6 seconds: 3s sine pitch-A, 3s sine pitch-B (simulates two speakers)
    auto pcm_a = make_sine_pcm(200.0f, 3.0f);
    auto pcm_b = make_sine_pcm(400.0f, 3.0f);
    std::vector<float> pcm;
    pcm.insert(pcm.end(), pcm_a.begin(), pcm_a.end());
    pcm.insert(pcm.end(), pcm_b.begin(), pcm_b.end());

    const int MAX_SEG = 32;
    VpDiarizeSegment segments[MAX_SEG];
    int count = 0;
    int rc = vp_diarize(pcm.data(), static_cast<int>(pcm.size()),
                        segments, MAX_SEG, &count);
    if (rc == VP_ERROR_NOT_INIT) {
        GTEST_SKIP() << "Diarizer not initialized";
    }
    ASSERT_EQ(rc, VP_OK) << vp_get_last_error();
    EXPECT_GE(count, 1)  << "Should produce at least one segment";
    // All segment times should be valid
    for (int i = 0; i < count; ++i) {
        EXPECT_GE(segments[i].start_sec, 0.0f);
        EXPECT_GT(segments[i].end_sec, segments[i].start_sec);
        EXPECT_GT(std::strlen(segments[i].speaker_label), 0u);
    }
}

TEST_F(VoiceAnalysisTest, DiarizeFileWorks) {
    std::string wav = (std::filesystem::temp_directory_path() / "diarize_test.wav").string();
    make_wav_file(wav, 300.0f, 5.0f);
    const int MAX_SEG = 16;
    VpDiarizeSegment segs[MAX_SEG]; int count = 0;
    int rc = vp_diarize_file(wav.c_str(), segs, MAX_SEG, &count);
    std::filesystem::remove(wav);
    if (rc == VP_ERROR_NOT_INIT) GTEST_SKIP() << "Diarizer not initialized";
    EXPECT_EQ(rc, VP_OK) << vp_get_last_error();
}
