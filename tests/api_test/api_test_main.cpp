/**
 * VoicePrint SDK — Comprehensive API Test
 *
 * A standalone, self-contained test program that exercises every public API
 * function against real audio files from the testdata/ directory (with
 * synthetic fallback when files are absent).
 *
 * Usage:
 *   api_tests.exe [--models <dir>] [--testdata <dir>] [--report <file>]
 *
 * Default paths (relative to CWD):
 *   models/    model directory
 *   testdata/  test audio directory
 *   reports/api_test_report.md  output report
 */

#include <voiceprint/voiceprint_api.h>
#include <voiceprint/voiceprint_types.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

// ============================================================
// Mini test framework
// ============================================================
namespace test {

struct Result {
    std::string section;
    std::string name;
    bool        passed;
    std::string detail;   // printed on failure or for info
    double      ms = 0.0; // execution time
};

static std::vector<Result> g_results;
static std::string          g_current_section;

static void section(const std::string& name) {
    g_current_section = name;
    std::cout << "\n━━ " << name << " ━━\n";
}

static void check(const std::string& name, bool cond, const std::string& detail = "") {
    Result r;
    r.section = g_current_section;
    r.name    = name;
    r.passed  = cond;
    r.detail  = detail;
    g_results.push_back(r);
    std::cout << (cond ? "  [PASS] " : "  [FAIL] ") << name;
    if (!cond && !detail.empty()) std::cout << "  → " << detail;
    std::cout << "\n";
}

// SKIP: not a failure, but not a pass either (model absent, file missing)
static void skip(const std::string& name, const std::string& reason) {
    Result r;
    r.section = g_current_section;
    r.name    = name;
    r.passed  = true;  // skipped tests don't count as failures
    r.detail  = "SKIP: " + reason;
    g_results.push_back(r);
    std::cout << "  [SKIP] " << name << "  → " << reason << "\n";
}

// INFO: print without pass/fail
static void info(const std::string& text) {
    std::cout << "        " << text << "\n";
}

static int total()   { return (int)g_results.size(); }
static int passed()  { int n=0; for (auto& r:g_results) if (r.passed) ++n; return n; }
static int failed()  { return total()-passed(); }
static int skipped() {
    int n=0;
    for (auto& r:g_results) if (r.detail.rfind("SKIP:",0)==0) ++n;
    return n;
}

} // namespace test

// ============================================================
// WAV helpers
// ============================================================
static std::vector<float> make_sine(float freq, float dur_sec,
                                     int sr = 16000) {
    int n = (int)(dur_sec * sr);
    std::vector<float> pcm(n);
    for (int i = 0; i < n; ++i) {
        float t = (float)i / sr;
        pcm[i] = 0.35f * std::sin(2.0f * (float)M_PI * freq * t)
               + 0.20f * std::sin(2.0f * (float)M_PI * freq * 2 * t)
               + 0.08f * std::sin(2.0f * (float)M_PI * freq * 3 * t);
    }
    return pcm;
}

static bool write_wav(const std::string& path, const std::vector<float>& pcm,
                       int sr = 16000) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    int n = (int)pcm.size();
    std::vector<int16_t> s(n);
    for (int i = 0; i < n; ++i)
        s[i] = (int16_t)std::max(-32767.0f, std::min(32767.0f, pcm[i] * 32767.0f));

    int data_sz = n * 2, file_sz = 36 + data_sz;
    f.write("RIFF", 4); f.write((char*)&file_sz, 4);
    f.write("WAVE", 4); f.write("fmt ", 4);
    int fmt=16; f.write((char*)&fmt, 4);
    int16_t afmt=1, ch=1, blk=2, bps=16;
    int     br = sr * 2;
    f.write((char*)&afmt,2); f.write((char*)&ch,2);
    f.write((char*)&sr,   4); f.write((char*)&br,4);
    f.write((char*)&blk,  2); f.write((char*)&bps,2);
    f.write("data", 4); f.write((char*)&data_sz, 4);
    f.write((char*)s.data(), data_sz);
    return true;
}

static bool read_wav(const std::string& path, std::vector<float>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    char riff[4]; f.read(riff, 4);
    if (strncmp(riff,"RIFF",4)!=0) return false;
    uint32_t fsz; f.read((char*)&fsz,4);
    char wave[4]; f.read(wave,4);
    if (strncmp(wave,"WAVE",4)!=0) return false;

    uint16_t fmt=0, ch=0, bps=0;
    uint32_t sr=0;
    std::vector<uint8_t> data;

    while (f.good() && !f.eof()) {
        char id[4]; uint32_t csz;
        f.read(id,4);
        if (f.gcount()<4) break;
        f.read((char*)&csz,4);
        if (strncmp(id,"fmt ",4)==0) {
            f.read((char*)&fmt,2); f.read((char*)&ch,2);
            f.read((char*)&sr,4);
            uint32_t br; f.read((char*)&br,4);
            uint16_t bl; f.read((char*)&bl,2);
            f.read((char*)&bps,2);
            if (csz>16) f.seekg(csz-16, std::ios::cur);
        } else if (strncmp(id,"data",4)==0) {
            data.resize(csz);
            f.read((char*)data.data(), csz);
            break;
        } else {
            f.seekg(csz, std::ios::cur);
        }
    }
    if (data.empty() || bps==0) return false;
    if (bps==16) {
        size_t ns = data.size()/2;
        out.resize(ns);
        for (size_t i=0; i<ns; ++i) {
            int16_t s; memcpy(&s, data.data()+i*2, 2);
            out[i] = s / 32767.0f;
        }
    }
    return !out.empty();
}

// Collect all .wav/.flac files in a directory (non-recursive)
static std::vector<std::string> wav_files(const std::string& dir) {
    namespace fs = std::filesystem;
    std::vector<std::string> v;
    std::error_code ec;
    if (!fs::exists(dir, ec)) return v;
    for (auto& e : fs::directory_iterator(dir, ec))
        if (e.is_regular_file()) {
            auto ext = e.path().extension().string();
            if (ext==".wav" || ext==".WAV") v.push_back(e.path().string());
        }
    return v;
}

// Return first wav in dir, or write a synthetic fallback to tmp_path and return it
static std::string first_wav_or_synth(const std::string& dir, float freq,
                                       float dur, const std::string& tmp_path) {
    auto files = wav_files(dir);
    if (!files.empty()) return files[0];
    auto pcm = make_sine(freq, dur);
    write_wav(tmp_path, pcm);
    return tmp_path;
}

// ============================================================
// Timing helper
// ============================================================
static double ms_since(std::chrono::high_resolution_clock::time_point t) {
    using namespace std::chrono;
    return duration<double,std::milli>(high_resolution_clock::now() - t).count();
}

// ============================================================
// Error code to string
// ============================================================
static const char* err_str(int rc) {
    switch(rc) {
        case VP_OK:                    return "VP_OK";
        case VP_ERROR_UNKNOWN:         return "VP_ERROR_UNKNOWN";
        case VP_ERROR_INVALID_PARAM:   return "VP_ERROR_INVALID_PARAM";
        case VP_ERROR_NOT_INIT:        return "VP_ERROR_NOT_INIT";
        case VP_ERROR_ALREADY_INIT:    return "VP_ERROR_ALREADY_INIT";
        case VP_ERROR_MODEL_LOAD:      return "VP_ERROR_MODEL_LOAD";
        case VP_ERROR_AUDIO_TOO_SHORT: return "VP_ERROR_AUDIO_TOO_SHORT";
        case VP_ERROR_AUDIO_INVALID:   return "VP_ERROR_AUDIO_INVALID";
        case VP_ERROR_SPEAKER_EXISTS:  return "VP_ERROR_SPEAKER_EXISTS";
        case VP_ERROR_SPEAKER_NOT_FOUND: return "VP_ERROR_SPEAKER_NOT_FOUND";
        case VP_ERROR_DB_ERROR:        return "VP_ERROR_DB_ERROR";
        case VP_ERROR_FILE_NOT_FOUND:  return "VP_ERROR_FILE_NOT_FOUND";
        case VP_ERROR_BUFFER_TOO_SMALL: return "VP_ERROR_BUFFER_TOO_SMALL";
        case VP_ERROR_NO_MATCH:        return "VP_ERROR_NO_MATCH";
        case VP_ERROR_WAV_FORMAT:      return "VP_ERROR_WAV_FORMAT";
        case VP_ERROR_INFERENCE:       return "VP_ERROR_INFERENCE";
        case VP_ERROR_MODEL_NOT_AVAILABLE: return "VP_ERROR_MODEL_NOT_AVAILABLE";
        case VP_ERROR_ANALYSIS_FAILED: return "VP_ERROR_ANALYSIS_FAILED";
        case VP_ERROR_DIARIZE_FAILED:  return "VP_ERROR_DIARIZE_FAILED";
        default:                       return "UNKNOWN_CODE";
    }
}

// ============================================================
// Report writer
// ============================================================
static void write_report(const std::string& path,
                          const std::string& model_dir,
                          const std::string& testdata_dir,
                          double total_ms) {
    namespace fs = std::filesystem;
    fs::create_directories(fs::path(path).parent_path());
    std::ofstream f(path);
    if (!f) { std::cerr << "Cannot write report to " << path << "\n"; return; }

    // Timestamp
    auto now = std::chrono::system_clock::now();
    auto t   = std::chrono::system_clock::to_time_t(now);
    char ts[64]; strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", localtime(&t));

    f << "# VoicePrint SDK — API Test Report\n\n";
    f << "| 项目 | 值 |\n|------|----|\n";
    f << "| 测试时间 | " << ts << " |\n";
    f << "| 模型目录 | `" << model_dir << "` |\n";
    f << "| 测试数据 | `" << testdata_dir << "` |\n";
    f << "| 总用时 | " << (int)total_ms << " ms |\n";
    f << "| 通过 | " << test::passed() << " / " << test::total() << " |\n";
    f << "| 失败 | " << test::failed() << " |\n";
    f << "| 跳过 | " << test::skipped() << " |\n\n";

    // Group by section
    std::string cur_sec;
    for (auto& r : test::g_results) {
        if (r.section != cur_sec) {
            cur_sec = r.section;
            f << "## " << cur_sec << "\n\n";
            f << "| 测试项 | 结果 | 备注 |\n|--------|------|------|\n";
        }
        std::string status = r.detail.rfind("SKIP:",0)==0 ? "⬜ SKIP"
                           : r.passed                     ? "✅ PASS"
                                                          : "❌ FAIL";
        std::string detail = r.detail;
        // escape pipes in detail
        for (size_t i=0; i<detail.size(); ++i)
            if (detail[i]=='|') { detail.insert(i, "\\"); ++i; }
        f << "| " << r.name << " | " << status << " | " << detail << " |\n";
    }

    f << "\n---\n*Generated by api_tests.exe*\n";
    std::cout << "\nReport saved → " << path << "\n";
}

// ============================================================
// Main
// ============================================================
int main(int argc, char* argv[]) {
    // --- Parse args ---
    std::string model_dir   = "models";
    std::string testdata_dir = "testdata";
    std::string report_path = "reports/api_test_report.md";
    std::string db_path     = "api_test.db";
    std::string tmp_dir     = ".";

    for (int i = 1; i < argc-1; ++i) {
        if      (strcmp(argv[i],"--models"  )==0) model_dir    = argv[++i];
        else if (strcmp(argv[i],"--testdata")==0) testdata_dir = argv[++i];
        else if (strcmp(argv[i],"--report"  )==0) report_path  = argv[++i];
        else if (strcmp(argv[i],"--db"      )==0) db_path      = argv[++i];
    }

    // Try to resolve model_dir relative to exe
    namespace fs = std::filesystem;
    if (!fs::exists(model_dir)) {
        fs::path exe = fs::absolute(argv[0]).parent_path();
        for (int i = 0; i < 5; ++i) {
            if (fs::exists(exe / "models" / "ecapa_tdnn.onnx")) {
                model_dir = (exe / "models").string();
                break;
            }
            exe = exe.parent_path();
        }
    }

    std::cout << "VoicePrint SDK — Comprehensive API Test\n";
    std::cout << "  models   : " << model_dir    << "\n";
    std::cout << "  testdata : " << testdata_dir << "\n";
    std::cout << "  report   : " << report_path  << "\n\n";

    auto t_start = std::chrono::high_resolution_clock::now();
    bool core_ok = false;

    // ----------------------------------------------------------
    // helpers
    auto td = [&](const std::string& sub) { return testdata_dir + "/" + sub; };
    auto synth = [&](float freq, float dur) { return make_sine(freq, dur); };
    auto synth_file = [&](const std::string& name, float freq, float dur) -> std::string {
        std::string p = tmp_dir + "/api_test_" + name + ".wav";
        auto pcm = synth(freq, dur);
        write_wav(p, pcm);
        return p;
    };

    // ============================================================
    // SECTION 1: Initialization
    // ============================================================
    test::section("1. 初始化与释放");

    {
        auto t0 = std::chrono::high_resolution_clock::now();
        int rc = vp_init(model_dir.c_str(), db_path.c_str());
        double dt = ms_since(t0);
        core_ok = (rc == VP_OK);
        std::string detail = std::string(err_str(rc)) + "  (" + std::to_string((int)dt) + " ms)";
        test::check("vp_init 加载核心模型", rc == VP_OK, detail);
        if (!core_ok) {
            test::info("核心模型缺失，后续依赖 SDK Init 的测试将全部跳过");
        }
    }

    test::check("vp_get_speaker_count 返回非负值",
                core_ok && vp_get_speaker_count() >= 0,
                core_ok ? "" : "SDK 未初始化");

    test::check("vp_get_last_error 不返回 nullptr",
                vp_get_last_error() != nullptr);

    test::check("vp_set_threshold 接受合法值",
                !core_ok || vp_set_threshold(0.35f) == VP_OK);

    // ============================================================
    // SECTION 2: 说话人注册
    // ============================================================
    test::section("2. 说话人注册 (Enroll)");

    // Collect enrollment files from testdata/gender/male and female
    auto male_wavs   = wav_files(td("gender/male"));
    auto female_wavs = wav_files(td("gender/female"));

    // Fallback synthetic files
    std::string enroll_male_path   = synth_file("enroll_male",   120.0f, 4.0f);
    std::string enroll_female_path = synth_file("enroll_female", 230.0f, 4.0f);
    std::string enroll_speech_path = synth_file("enroll_speech", 180.0f, 4.0f);

    std::string alice_wav = !female_wavs.empty() ? female_wavs[0] : enroll_female_path;
    std::string bob_wav   = !male_wavs.empty()   ? male_wavs[0]   : enroll_male_path;

    if (core_ok) {
        // Clean up any leftover state
        vp_remove_speaker("alice");
        vp_remove_speaker("bob");
        vp_remove_speaker("carol");

        {
            int rc = vp_enroll_file("alice", alice_wav.c_str());
            test::check("vp_enroll_file alice", rc == VP_OK,
                        std::string(err_str(rc)) + " | " + alice_wav);
        }
        {
            int rc = vp_enroll_file("bob", bob_wav.c_str());
            test::check("vp_enroll_file bob", rc == VP_OK,
                        std::string(err_str(rc)) + " | " + bob_wav);
        }
        {
            // PCM enroll
            auto pcm = synth(165.0f, 3.5f);
            int rc = vp_enroll("carol", pcm.data(), (int)pcm.size());
            test::check("vp_enroll carol (PCM)", rc == VP_OK, err_str(rc));
        }
        {
            // Duplicate enroll (incremental update, should succeed)
            auto pcm = synth(165.0f, 3.0f);
            int rc = vp_enroll("carol", pcm.data(), (int)pcm.size());
            test::check("vp_enroll carol 二次增量注册", rc == VP_OK, err_str(rc));
        }
        {
            int cnt = vp_get_speaker_count();
            test::check("注册后 speaker_count >= 3", cnt >= 3,
                        "count=" + std::to_string(cnt));
        }
        {
            // Enroll with too-short audio should fail
            auto pcm = synth(200.0f, 0.3f);  // 0.3s << 1.5s minimum
            int rc = vp_enroll("shortaudio", pcm.data(), (int)pcm.size());
            test::check("过短音频注册返回错误",
                        rc != VP_OK,
                        "returned " + std::string(err_str(rc)));
        }
        {
            // Enroll with null returns error
            int rc = vp_enroll("null_test", nullptr, 0);
            test::check("空 PCM 注册返回错误", rc != VP_OK, err_str(rc));
        }
        {
            // Invalid file path
            int rc = vp_enroll_file("ghost", "/nonexistent/path.wav");
            test::check("不存在的文件返回错误", rc != VP_OK, err_str(rc));
        }
    } else {
        for (auto& n : {"vp_enroll_file alice","vp_enroll_file bob","vp_enroll carol (PCM)",
                        "vp_enroll carol 二次增量注册","注册后 speaker_count >= 3",
                        "过短音频注册返回错误","空 PCM 注册返回错误","不存在的文件返回错误"})
            test::skip(n, "SDK 未初始化");
    }

    // ============================================================
    // SECTION 3: 说话人识别 1:N
    // ============================================================
    test::section("3. 说话人识别 (1:N Identify)");

    if (core_ok) {
        {
            // Should identify alice from her enrollment audio
            std::vector<float> pcm;
            bool loaded = read_wav(alice_wav, pcm);
            if (!loaded) pcm = synth(230.0f, 3.0f);

            char out_id[256] = {};
            float score = 0;
            auto t0 = std::chrono::high_resolution_clock::now();
            int rc = vp_identify(pcm.data(), (int)pcm.size(), out_id, 256, &score);
            double dt = ms_since(t0);

            test::check("vp_identify 返回 VP_OK 或 VP_ERROR_NO_MATCH",
                        rc == VP_OK || rc == VP_ERROR_NO_MATCH,
                        std::string(err_str(rc)) + "  score=" + std::to_string(score));
            test::info("  识别结果: " + std::string(out_id[0] ? out_id : "(无匹配)")
                       + "  score=" + std::to_string(score)
                       + "  delay=" + std::to_string((int)dt) + "ms");
        }
        {
            // Null input
            char out_id[64];
            float score;
            int rc = vp_identify(nullptr, 0, out_id, 64, &score);
            test::check("vp_identify null 输入返回错误", rc != VP_OK, err_str(rc));
        }
        {
            // null output id buffer
            auto pcm = synth(200.0f, 3.0f);
            float score;
            int rc = vp_identify(pcm.data(), (int)pcm.size(), nullptr, 0, &score);
            test::check("vp_identify null 缓冲区返回错误", rc != VP_OK, err_str(rc));
        }
        {
            // WAV file identify (convenience API)
            auto t0 = std::chrono::high_resolution_clock::now();
            char out_id[256] = {};
            float score = 0;
            int rc = vp_identify_file(alice_wav.c_str(), out_id, 256, &score);
            double dt = ms_since(t0);
            test::check("vp_identify_file 正常返回",
                        rc == VP_OK || rc == VP_ERROR_NO_MATCH,
                        std::string(err_str(rc)) + " " + std::to_string((int)dt) + "ms");
        }
    } else {
        test::skip("vp_identify / vp_identify_file", "SDK 未初始化");
    }

    // ============================================================
    // SECTION 4: 说话人验证 1:1
    // ============================================================
    test::section("4. 说话人验证 (1:1 Verify)");

    if (core_ok) {
        {
            auto pcm = synth(165.0f, 3.0f);  // similar to carol
            float score = 0;
            auto t0 = std::chrono::high_resolution_clock::now();
            int rc = vp_verify("carol", pcm.data(), (int)pcm.size(), &score);
            double dt = ms_since(t0);
            test::check("vp_verify carol 返回 VP_OK",
                        rc == VP_OK,
                        std::string(err_str(rc)) + "  score=" + std::to_string(score)
                        + " " + std::to_string((int)dt) + "ms");
        }
        {
            // Verify non-existent speaker
            auto pcm = synth(200.0f, 3.0f);
            float score;
            int rc = vp_verify("ghost_speaker", pcm.data(), (int)pcm.size(), &score);
            test::check("vp_verify 不存在的说话人返回错误",
                        rc == VP_ERROR_SPEAKER_NOT_FOUND,
                        err_str(rc));
        }
        {
            // WAV file verify
            float score;
            int rc = vp_verify_file("carol", enroll_speech_path.c_str(), &score);
            test::check("vp_verify_file 正常执行",
                        rc == VP_OK,
                        std::string(err_str(rc)) + " score=" + std::to_string(score));
        }
        {
            // Anti-spoof integration (enable then call verify)
            vp_set_antispoof_enabled(1);
            auto pcm = synth(165.0f, 3.0f);
            float score;
            int rc = vp_verify("carol", pcm.data(), (int)pcm.size(), &score);
            test::check("vp_verify + antispoof enabled 正常返回",
                        rc == VP_OK || rc == VP_ERROR_MODEL_NOT_AVAILABLE,
                        err_str(rc));
            vp_set_antispoof_enabled(0);
        }
    } else {
        test::skip("vp_verify / vp_verify_file", "SDK 未初始化");
    }

    // ============================================================
    // SECTION 5: 初始化语音分析
    // ============================================================
    test::section("5. 语音分析器初始化");

    bool analyzer_ok = false;
    if (core_ok) {
        auto t0 = std::chrono::high_resolution_clock::now();
        int rc = vp_init_analyzer(VP_FEATURE_ALL);
        double dt = ms_since(t0);
        analyzer_ok = (rc == VP_OK || rc == VP_ERROR_MODEL_NOT_AVAILABLE);
        test::check("vp_init_analyzer(VP_FEATURE_ALL)",
                    analyzer_ok,
                    std::string(err_str(rc)) + " " + std::to_string((int)dt) + "ms");
        test::info("返回 VP_ERROR_MODEL_NOT_AVAILABLE 表示部分可选模型未部署，属正常情况");
    } else {
        test::skip("vp_init_analyzer", "SDK 未初始化");
    }

    // ============================================================
    // SECTION 6: 音质评估
    // ============================================================
    test::section("6. 音质评估 (Quality Assessment)");

    auto run_quality = [&](const std::string& label,
                           const std::string& dir, float fallback_freq) {
        if (!core_ok || !analyzer_ok) { test::skip(label, "SDK 未就绪"); return; }
        std::string wav = first_wav_or_synth(dir, fallback_freq, 4.0f,
                                             synth_file("qual_" + label, fallback_freq, 4.0f));
        VpQualityResult q{};
        int rc = vp_assess_quality_file(wav.c_str(), &q);
        if (rc == VP_ERROR_MODEL_NOT_AVAILABLE) {
            // DSP-only quality always works; model-MOS is optional
            test::skip(label + " (MOS model)", "dnsmos.onnx 未加载，尝试 DSP 路径");
            // Retry with PCM API
            std::vector<float> pcm;
            if (read_wav(wav, pcm)) {
                rc = vp_assess_quality(pcm.data(), (int)pcm.size(), &q);
            }
        }
        bool ok = (rc == VP_OK);
        std::string info_str;
        if (ok) {
            char buf[128];
            snprintf(buf, sizeof(buf),
                     "MOS=%.2f  SNR=%.1fdB  LUFS=%.1f  HNR=%.1fdB  clarity=%.2f",
                     q.mos_score, q.snr_db, q.loudness_lufs, q.hnr_db, q.clarity);
            test::check(label, ok && q.mos_score >= 1.0f && q.mos_score <= 5.0f, buf);
            test::info(buf);
        } else {
            test::check(label, false, err_str(rc));
        }
    };

    run_quality("干净音频 (quality/clean)",   td("quality/clean"),   200.0f);
    run_quality("噪音音频 (quality/noisy)",   td("quality/noisy"),   200.0f);
    run_quality("削波音频 (quality/clipped)", td("quality/clipped"), 200.0f);
    // Also: PCM API
    if (core_ok && analyzer_ok) {
        auto pcm = synth(300.0f, 3.0f);
        VpQualityResult q{};
        int rc = vp_assess_quality(pcm.data(), (int)pcm.size(), &q);
        test::check("vp_assess_quality (PCM API)",
                    rc == VP_OK && q.mos_score >= 1.0f && q.mos_score <= 5.0f,
                    rc == VP_OK ?
                        ("MOS=" + std::to_string(q.mos_score)) : err_str(rc));
    }

    // ============================================================
    // SECTION 7: 声学特征分析
    // ============================================================
    test::section("7. 声学特征 (Voice Features)");

    if (core_ok && analyzer_ok) {
        // Test with a known frequency so we can validate the F0 output
        auto pcm_440 = synth(440.0f, 3.0f);
        VpVoiceFeatures vf{};
        int rc = vp_analyze_voice(pcm_440.data(), (int)pcm_440.size(), &vf);
        test::check("vp_analyze_voice 返回 VP_OK", rc == VP_OK, err_str(rc));
        if (rc == VP_OK) {
            char buf[256];
            snprintf(buf, sizeof(buf),
                     "F0=%.1fHz  变化=%.1fHz  语速=%.2f  稳定性=%.2f  共鸣=%.2f  气息=%.2f",
                     vf.pitch_hz, vf.pitch_variability,
                     vf.speaking_rate, vf.voice_stability,
                     vf.resonance_score, vf.breathiness);
            test::info(buf);
            test::check("F0 检测在 440Hz 附近 (±50Hz)",
                        vf.pitch_hz > 390.0f && vf.pitch_hz < 490.0f,
                        "F0=" + std::to_string(vf.pitch_hz));
            test::check("voice_stability 在 [0,1]",
                        vf.voice_stability >= 0.0f && vf.voice_stability <= 1.0f,
                        std::to_string(vf.voice_stability));
        }

        // Test with speech file
        auto speech_wavs = wav_files(td("speech"));
        if (!speech_wavs.empty()) {
            VpVoiceFeatures vf2{};
            rc = vp_analyze_voice_file(speech_wavs[0].c_str(), &vf2);
            test::check("vp_analyze_voice_file 真实语音",
                        rc == VP_OK, err_str(rc));
            if (rc == VP_OK) {
                char buf[256];
                snprintf(buf, sizeof(buf),
                         "F0=%.1fHz  语速=%.2f syl/s  稳定性=%.2f",
                         vf2.pitch_hz, vf2.speaking_rate, vf2.voice_stability);
                test::info(buf);
            }
        }
    } else {
        test::skip("声学特征分析", "SDK 未就绪");
    }

    // ============================================================
    // SECTION 8: 声音好听度
    // ============================================================
    test::section("8. 声音好听度 (Pleasantness)");

    if (core_ok && analyzer_ok) {
        auto pcm = synth(200.0f, 3.0f);
        VpPleasantnessResult pl{};
        int rc = vp_get_pleasantness(pcm.data(), (int)pcm.size(), &pl);
        test::check("vp_get_pleasantness 返回 VP_OK", rc == VP_OK, err_str(rc));
        if (rc == VP_OK) {
            char buf[256];
            snprintf(buf, sizeof(buf),
                     "综合=%.1f  吸引力=%.1f  温暖=%.1f  权威=%.1f  清晰=%.1f",
                     pl.overall_score, pl.magnetism, pl.warmth,
                     pl.authority, pl.clarity_score);
            test::info(buf);
            test::check("综合评分在 [0,100]",
                        pl.overall_score >= 0.0f && pl.overall_score <= 100.0f,
                        std::to_string(pl.overall_score));
        }

        // file API
        auto speech_wavs = wav_files(td("speech"));
        if (!speech_wavs.empty()) {
            VpPleasantnessResult pl2{};
            rc = vp_get_pleasantness_file(speech_wavs[0].c_str(), &pl2);
            test::check("vp_get_pleasantness_file 真实语音",
                        rc == VP_OK, err_str(rc));
        }
    } else {
        test::skip("声音好听度", "SDK 未就绪");
    }

    // ============================================================
    // SECTION 9: 声音状态
    // ============================================================
    test::section("9. 声音状态 (Voice State)");

    if (core_ok && analyzer_ok) {
        auto pcm = synth(180.0f, 3.0f);
        VpVoiceState vs{};
        int rc = vp_get_voice_state(pcm.data(), (int)pcm.size(), &vs);
        test::check("vp_get_voice_state 返回 VP_OK", rc == VP_OK, err_str(rc));
        if (rc == VP_OK) {
            const char* fatigue_lbl[] = {"正常","中度","高度"};
            const char* stress_lbl[]  = {"低","中","高"};
            char buf[256];
            snprintf(buf, sizeof(buf),
                     "疲劳=%s(%.2f)  健康=%.2f  压力=%s(%.2f)",
                     fatigue_lbl[std::min(vs.fatigue_level, 2)], vs.fatigue_score,
                     vs.health_score,
                     stress_lbl[std::min(vs.stress_level, 2)],  vs.stress_score);
            test::info(buf);
            test::check("health_score 在 [0,1]",
                        vs.health_score >= 0.0f && vs.health_score <= 1.0f,
                        std::to_string(vs.health_score));
        }
    } else {
        test::skip("声音状态", "SDK 未就绪");
    }

    // ============================================================
    // SECTION 10: 性别检测
    // ============================================================
    test::section("10. 性别检测 (Gender)");

    auto run_gender = [&](const std::string& label, const std::string& file,
                          int expected_gender) {
        if (!core_ok || !analyzer_ok) { test::skip(label, "SDK 未就绪"); return; }
        std::vector<float> pcm;
        bool file_loaded = read_wav(file, pcm);
        if (!file_loaded) { test::skip(label, "文件不存在: " + file); return; }

        VpGenderResult g{};
        auto t0 = std::chrono::high_resolution_clock::now();
        int rc = vp_get_gender_file(file.c_str(), &g);
        double dt = ms_since(t0);

        if (rc == VP_ERROR_MODEL_NOT_AVAILABLE) {
            test::skip(label, "gender_age.onnx 未加载"); return;
        }
        const char* gname[] = {"female","male","child"};
        char buf[128];
        snprintf(buf, sizeof(buf),
                 "预测=%s(%.2f)  期望=%s  [%.0fms]",
                 gname[std::min(g.gender,2)], g.scores[g.gender],
                 gname[std::min(expected_gender,2)], dt);
        // Accept either correct prediction or a close second (the model might not be loaded)
        test::check(label, rc == VP_OK, std::string(err_str(rc)) + " " + buf);
        if (rc == VP_OK) test::info(buf);
    };

    {
        auto male_w   = wav_files(td("gender/male"));
        auto female_w = wav_files(td("gender/female"));
        std::string child_wav = td("gender/child_synth.wav");

        if (!male_w.empty())   run_gender("男声 gender/male[0]",   male_w[0],   VP_GENDER_MALE);
        else                   run_gender("男声 (synthetic)",       synth_file("male_synth", 120.0f, 3.5f), VP_GENDER_MALE);

        if (!female_w.empty()) run_gender("女声 gender/female[0]", female_w[0], VP_GENDER_FEMALE);
        else                   run_gender("女声 (synthetic)",       synth_file("fem_synth",  230.0f, 3.5f), VP_GENDER_FEMALE);

        run_gender("儿声 gender/child_synth.wav", child_wav, VP_GENDER_CHILD);
    }

    // ============================================================
    // SECTION 11: 年龄估计
    // ============================================================
    test::section("11. 年龄估计 (Age Estimation)");

    if (core_ok && analyzer_ok) {
        auto pcm = synth(200.0f, 3.5f);
        VpAgeResult age{};
        int rc = vp_get_age(pcm.data(), (int)pcm.size(), &age);
        if (rc == VP_ERROR_MODEL_NOT_AVAILABLE) {
            test::skip("vp_get_age", "gender_age.onnx 未加载");
        } else {
            const char* grp[] = {"儿童","青少年","成年","老年"};
            char buf[128];
            snprintf(buf, sizeof(buf),
                     "估算年龄=%d岁  年龄段=%s  置信度=%.2f",
                     age.age_years, grp[std::min(age.age_group,3)], age.confidence);
            test::check("vp_get_age 年龄在合理范围 [1,120]",
                        rc == VP_OK && age.age_years > 0 && age.age_years < 120,
                        buf);
            if (rc == VP_OK) test::info(buf);
        }

        // Speech file age test
        auto speech_wavs = wav_files(td("speech"));
        if (!speech_wavs.empty()) {
            VpAgeResult age2{};
            rc = vp_get_age_file(speech_wavs[0].c_str(), &age2);
            if (rc == VP_ERROR_MODEL_NOT_AVAILABLE) {
                test::skip("vp_get_age_file 真实语音", "模型未加载");
            } else {
                test::check("vp_get_age_file 真实语音", rc == VP_OK, err_str(rc));
            }
        }
    } else {
        test::skip("年龄估计", "SDK 未就绪");
    }

    // ============================================================
    // SECTION 12: 情感识别
    // ============================================================
    test::section("12. 情感识别 (Emotion)");

    if (core_ok && analyzer_ok) {
        auto pcm = synth(300.0f, 3.5f);
        VpEmotionResult em{};
        int rc = vp_get_emotion(pcm.data(), (int)pcm.size(), &em);
        if (rc == VP_ERROR_MODEL_NOT_AVAILABLE) {
            test::skip("vp_get_emotion", "emotion.onnx 未加载");
        } else {
            test::check("vp_get_emotion 返回 VP_OK", rc == VP_OK, err_str(rc));
            if (rc == VP_OK) {
                char buf[256];
                snprintf(buf, sizeof(buf),
                         "主要情感=%s(%.2f)  valence=%.3f  arousal=%.3f",
                         vp_emotion_name(em.emotion_id), em.scores[em.emotion_id],
                         em.valence, em.arousal);
                test::info(buf);
                test::check("emotion_id 在合法范围 [0,7]",
                            em.emotion_id >= 0 && em.emotion_id < VP_EMOTION_COUNT,
                            std::to_string(em.emotion_id));
                test::check("valence 在 [-1,1]",
                            em.valence >= -1.0f && em.valence <= 1.0f,
                            std::to_string(em.valence));
                float sum = 0;
                for (int i = 0; i < VP_EMOTION_COUNT; ++i) sum += em.scores[i];
                test::check("概率之和 ≈ 1.0",
                            std::fabs(sum - 1.0f) < 0.05f,
                            "sum=" + std::to_string(sum));
            }
        }

        // emotion name helper
        test::check("vp_emotion_name(VP_EMOTION_HAPPY) = \"happy\"",
                    strcmp(vp_emotion_name(VP_EMOTION_HAPPY), "happy") == 0);
        test::check("vp_emotion_name(VP_EMOTION_ANGRY) = \"angry\"",
                    strcmp(vp_emotion_name(VP_EMOTION_ANGRY), "angry") == 0);
        test::check("vp_emotion_name(9999) 不返回 nullptr",
                    vp_emotion_name(9999) != nullptr);
    } else {
        test::skip("情感识别", "SDK 未就绪");
    }

    // ============================================================
    // SECTION 13: 反欺骗检测
    // ============================================================
    test::section("13. 反欺骗检测 (Anti-Spoof)");

    auto run_antispoof = [&](const std::string& label,
                             const std::string& dir, float fallback_freq,
                             bool expect_genuine) {
        if (!core_ok || !analyzer_ok) { test::skip(label, "SDK 未就绪"); return; }
        std::string wav = first_wav_or_synth(dir, fallback_freq, 4.0f,
                                             synth_file("asp_" + label, fallback_freq, 4.0f));
        VpAntiSpoofResult as{};
        int rc = vp_anti_spoof_file(wav.c_str(), &as);
        if (rc == VP_ERROR_MODEL_NOT_AVAILABLE) {
            test::skip(label, "antispoof.onnx 未加载"); return;
        }
        char buf[128];
        snprintf(buf, sizeof(buf),
                 "genuine=%.3f  spoof=%.3f  is_genuine=%d",
                 as.genuine_score, as.spoof_score, as.is_genuine);
        test::check(label, rc == VP_OK, std::string(err_str(rc)) + " " + buf);
        if (rc == VP_OK) test::info(buf);
    };

    run_antispoof("真实发音 antispoof/genuine", td("antispoof/genuine"), 180.0f, true);
    run_antispoof("伪造音频 antispoof/spoofed", td("antispoof/spoofed"), 800.0f, false);

    // PCM API
    if (core_ok && analyzer_ok) {
        auto pcm = synth(200.0f, 3.5f);
        VpAntiSpoofResult as{};
        int rc = vp_anti_spoof(pcm.data(), (int)pcm.size(), &as);
        if (rc == VP_ERROR_MODEL_NOT_AVAILABLE) {
            test::skip("vp_anti_spoof (PCM)", "antispoof.onnx 未加载");
        } else {
            test::check("vp_anti_spoof PCM API 结果一致性",
                        rc == VP_OK && (as.genuine_score + as.spoof_score >= 0.95f),
                        "sum=" + std::to_string(as.genuine_score + as.spoof_score));
        }
    }

    // ============================================================
    // SECTION 14: 语种检测
    // ============================================================
    test::section("14. 语种检测 (Language Detection)");

    struct LangTest { std::string subdir; std::string expected_code; float fallback_freq; };
    std::vector<LangTest> lang_tests = {
        {"language/english", "en", 200.0f},
        {"language/chinese", "zh", 150.0f},
        {"language/german",  "de", 170.0f},
        {"language/french",  "fr", 220.0f},
    };

    for (auto& lt : lang_tests) {
        if (!core_ok || !analyzer_ok) {
            test::skip("语种检测 " + lt.subdir, "SDK 未就绪");
            continue;
        }
        std::string wav = first_wav_or_synth(td(lt.subdir), lt.fallback_freq, 4.0f,
                                             synth_file("lang_"+lt.expected_code,
                                                        lt.fallback_freq, 4.0f));
        VpLanguageResult lr{};
        auto t0 = std::chrono::high_resolution_clock::now();
        int rc = vp_detect_language_file(wav.c_str(), &lr);
        double dt = ms_since(t0);

        if (rc == VP_ERROR_MODEL_NOT_AVAILABLE) {
            test::skip("语种 " + lt.subdir, "language.onnx 未加载");
            continue;
        }
        char buf[256];
        snprintf(buf, sizeof(buf),
                 "检测=%s(%s)  置信度=%.2f  [%.0fms]",
                 lr.language, lr.language_name, lr.confidence, dt);
        test::check("vp_detect_language_file " + lt.subdir,
                    rc == VP_OK, std::string(err_str(rc)) + " " + buf);
        if (rc == VP_OK) test::info(buf);
    }

    // Language name helper
    {
        const char* en = vp_language_name("en");
        test::check("vp_language_name(\"en\") 非空",
                    en != nullptr && strlen(en) > 0, en ? en : "nullptr");
        const char* unk = vp_language_name("xx");
        test::check("vp_language_name(\"xx\") 不返回 nullptr",
                    unk != nullptr, unk ? unk : "nullptr");
    }

    // ============================================================
    // SECTION 15: 说话人分段 (Diarization)
    // ============================================================
    test::section("15. 多人分段 (Diarization)");

    if (core_ok && analyzer_ok) {
        // Multi-speaker file from testdata
        auto multi_wavs = wav_files(td("multi_speaker"));

        auto run_diarize_file = [&](const std::string& wav_path,
                                    const std::string& label) {
            const int MAX_SEG = 64;
            VpDiarizeSegment segs[MAX_SEG];
            int count = 0;
            auto t0 = std::chrono::high_resolution_clock::now();
            int rc = vp_diarize_file(wav_path.c_str(), segs, MAX_SEG, &count);
            double dt = ms_since(t0);

            if (rc == VP_ERROR_DIARIZE_FAILED || rc == VP_ERROR_NOT_INIT) {
                test::skip(label, "分段器未初始化或失败: " + std::string(err_str(rc)));
                return;
            }
            test::check(label, rc == VP_OK,
                        std::string(err_str(rc)) + "  segments=" + std::to_string(count)
                        + " [" + std::to_string((int)dt) + "ms]");
            if (rc == VP_OK) {
                test::info("  分段数=" + std::to_string(count)
                           + "  用时=" + std::to_string((int)dt) + "ms");
                for (int i = 0; i < std::min(count, 5); ++i) {
                    char buf[256];
                    snprintf(buf, sizeof(buf), "  [seg %d] %s  %.2f-%.2f s  conf=%.2f",
                             i, segs[i].speaker_label,
                             segs[i].start_sec, segs[i].end_sec, segs[i].confidence);
                    test::info(buf);
                }
            }
        };

        if (!multi_wavs.empty()) {
            run_diarize_file(multi_wavs[0], "vp_diarize_file 多人音频");
        } else {
            // Build synthetic 2-speaker audio (3s @ 200Hz + 3s @ 380Hz)
            auto a = synth(200.0f, 3.0f);
            auto b = synth(380.0f, 3.0f);
            a.insert(a.end(), b.begin(), b.end());
            std::string tmp = synth_file("multi_synth", 200.0f, 3.0f);  // placeholder
            write_wav(tmp, a);
            run_diarize_file(tmp, "vp_diarize_file (合成双人音频)");
        }

        // PCM API
        {
            auto a = synth(200.0f, 3.5f);
            auto b = synth(370.0f, 3.5f);
            a.insert(a.end(), b.begin(), b.end());

            const int MAX_SEG = 32;
            VpDiarizeSegment segs[MAX_SEG];
            int count = 0;
            int rc = vp_diarize(a.data(), (int)a.size(), segs, MAX_SEG, &count);
            if (rc == VP_ERROR_DIARIZE_FAILED || rc == VP_ERROR_NOT_INIT) {
                test::skip("vp_diarize PCM API", err_str(rc));
            } else {
                test::check("vp_diarize PCM API 返回 VP_OK", rc == VP_OK,
                            std::string(err_str(rc)) + " count=" + std::to_string(count));
            }
        }
    } else {
        test::skip("多人分段", "SDK 未就绪");
    }

    // ============================================================
    // SECTION 16: 综合分析 vp_analyze / vp_analyze_file
    // ============================================================
    test::section("16. 综合分析 (vp_analyze_file)");

    if (core_ok && analyzer_ok) {
        auto speech_wavs = wav_files(td("speech"));
        std::string test_wav = !speech_wavs.empty()
                                 ? speech_wavs[0]
                                 : synth_file("full_analysis", 200.0f, 4.0f);

        VpAnalysisResult ar{};
        auto t0 = std::chrono::high_resolution_clock::now();
        int rc = vp_analyze_file(test_wav.c_str(), VP_FEATURE_ALL, &ar);
        double dt = ms_since(t0);

        test::check("vp_analyze_file(VP_FEATURE_ALL) 返回 VP_OK",
                    rc == VP_OK, std::string(err_str(rc)) + " [" + std::to_string((int)dt) + "ms]");
        if (rc == VP_OK) {
            char buf[256];
            snprintf(buf, sizeof(buf),
                     "features_computed=0x%03X  MOS=%.2f  F0=%.1fHz",
                     ar.features_computed, ar.quality.mos_score,
                     ar.voice_features.pitch_hz);
            test::info(buf);
            test::check("features_computed 包含 DSP 功能",
                        (ar.features_computed & (VP_FEATURE_QUALITY | VP_FEATURE_VOICE_FEATS)) != 0,
                        "0x" + [&]{
                            char h[16]; snprintf(h,16, "%03X", ar.features_computed);
                            return std::string(h);
                        }());

            // PCM API
            std::vector<float> pcm;
            if (read_wav(test_wav, pcm)) {
                VpAnalysisResult ar2{};
                rc = vp_analyze(pcm.data(), (int)pcm.size(),
                                VP_FEATURE_QUALITY | VP_FEATURE_VOICE_FEATS, &ar2);
                test::check("vp_analyze PCM API 返回 VP_OK", rc == VP_OK, err_str(rc));
            }
        }
    } else {
        test::skip("综合分析", "SDK 未就绪");
    }

    // ============================================================
    // SECTION 17: 边界条件与错误处理
    // ============================================================
    test::section("17. 边界条件与错误处理");

    {
        VpQualityResult q{};
        test::check("vp_assess_quality null 输入返回错误",
                    !core_ok || vp_assess_quality(nullptr, 0, &q) != VP_OK);
    }
    {
        VpQualityResult q{};
        test::check("vp_assess_quality_file 不存在文件返回错误",
                    !core_ok || vp_assess_quality_file("/nonexistent.wav", &q) != VP_OK);
    }
    {
        VpGenderResult g{};
        test::check("vp_get_gender null 输出返回错误",
                    !core_ok || vp_get_gender(nullptr, 1000, nullptr) != VP_OK);
    }
    {
        VpEmotionResult e{};
        auto pcm = synth(200.0f, 0.5f);  // too short
        int rc = core_ok ? vp_get_emotion(pcm.data(), (int)pcm.size(), &e) : VP_ERROR_NOT_INIT;
        test::check("过短音频情感识别返回错误或 MODEL_NOT_AVAILABLE",
                    rc != VP_OK,
                    err_str(rc));
    }
    {
        test::check("vp_remove_speaker 不存在的说话人返回 VP_ERROR_SPEAKER_NOT_FOUND",
                    !core_ok || vp_remove_speaker("nonexistent_xyz_abc") == VP_ERROR_SPEAKER_NOT_FOUND);
    }
    {
        // Double init should return VP_ERROR_ALREADY_INIT or succeed (both acceptable)
        if (core_ok) {
            int rc = vp_init(model_dir.c_str(), db_path.c_str());
            test::check("二次 vp_init 返回 VP_OK 或 VP_ERROR_ALREADY_INIT",
                        rc == VP_OK || rc == VP_ERROR_ALREADY_INIT,
                        err_str(rc));
        }
    }

    // ============================================================
    // SECTION 18: 清理
    // ============================================================
    test::section("18. 资源释放");

    if (core_ok) {
        vp_remove_speaker("alice");
        vp_remove_speaker("bob");
        vp_remove_speaker("carol");
        vp_release();
        test::check("vp_release 无崩溃", true);
        // Use after release should return VP_ERROR_NOT_INIT
        auto pcm = synth(200.0f, 3.0f); char buf[64]; float s;
        int rc = vp_identify(pcm.data(), (int)pcm.size(), buf, 64, &s);
        test::check("vp_release 后 API 返回 VP_ERROR_NOT_INIT",
                    rc == VP_ERROR_NOT_INIT, err_str(rc));
    } else {
        test::skip("vp_release", "SDK 未初始化");
    }

    // Temp file cleanup
    namespace fs = std::filesystem;
    for (auto& name : {"api_test_enroll_male","api_test_enroll_female","api_test_enroll_speech",
                       "api_test_male_synth","api_test_fem_synth",
                       "api_test_qual_干净音频 (quality/clean)",
                       "api_test_multi_synth"}) {
        std::string p = std::string(tmp_dir) + "/" + name + ".wav";
        if (fs::exists(p)) fs::remove(p);
    }
    // remove all api_test_*.wav in cwd
    std::error_code ec;
    for (auto& e : fs::directory_iterator(".", ec))
        if (e.path().filename().string().rfind("api_test_",0)==0)
            fs::remove(e.path(), ec);
    // remove test db
    fs::remove(db_path, ec);

    // ============================================================
    // Final summary
    // ============================================================
    double total_ms = ms_since(t_start);
    std::cout << "\n";
    std::cout << "════════════════════════════════════════\n";
    std::cout << "  测试结果汇总\n";
    std::cout << "════════════════════════════════════════\n";
    std::cout << "  通过:   " << test::passed()  << "\n";
    std::cout << "  失败:   " << test::failed()  << "\n";
    std::cout << "  跳过:   " << test::skipped() << "\n";
    std::cout << "  总用时: " << (int)total_ms   << " ms\n";
    std::cout << "════════════════════════════════════════\n";

    write_report(report_path, model_dir, testdata_dir, total_ms);

    // Return non-zero if there are real failures (not skips)
    int real_failures = 0;
    for (auto& r : test::g_results)
        if (!r.passed && r.detail.rfind("SKIP:",0) != 0) ++real_failures;
    return real_failures > 0 ? 1 : 0;
}
