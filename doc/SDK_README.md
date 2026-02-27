# VoicePrint SDK — 用户手册

声纹识别 + 语音分析 C++ DLL SDK — Windows x64

---

## 概述

| 指标 | 实测值 |
|------|--------|
| Embedding 维度 | 256 维（L2 归一化） |
| 单次声纹提取（3s 音频，CPU） | P95 ≤ 200ms |
| 1:1000 检索耗时 | < 1ms |
| 冷启动（加载模型 + 初始化） | < 1s |
| 内存稳定性（1000 次循环） | RSS 增长 < 1MB |
| 线程安全 | 读写锁，支持多线程并发 |

---

## 部署结构

### 最小部署（仅声纹识别）

```
voiceprint.dll
onnxruntime.dll
models/
  ecapa_tdnn.onnx        # 声纹 Embedding 提取（必须）
  silero_vad.onnx        # 语音活动检测（必须）
```

### 完整部署（含全部语音分析）

```
voiceprint.dll
onnxruntime.dll
models/
  ecapa_tdnn.onnx        # 必须
  silero_vad.onnx        # 必须
  gender_age.onnx        # 可选：性别 / 年龄检测
  emotion.onnx           # 可选：情感识别
  antispoof.onnx         # 可选：反欺骗检测
  dnsmos.onnx            # 可选：音质 MOS 评估
  language.onnx          # 可选：语种检测
```

可选模型缺失时，对应 API 返回 `VP_ERROR_MODEL_NOT_AVAILABLE`（-16），不会 crash。

---

## 集成方式

### C++ 集成

1. 将 `include/voiceprint/` 加入头文件搜索路径
2. 链接 `lib/voiceprint.lib`
3. 确保 `voiceprint.dll` 和 `onnxruntime.dll` 在可执行文件目录或 PATH 中
4. 将 `models/` 放置在工作目录下（或通过 `vp_init` 指定路径）

```cpp
#include <voiceprint/voiceprint_api.h>
#include <voiceprint/voiceprint_types.h>

// 初始化核心（声纹识别）
vp_init("models", "speakers.db");

// 初始化语音分析（可选，按需加载）
vp_init_analyzer(VP_FEATURE_GENDER | VP_FEATURE_EMOTION | VP_FEATURE_QUALITY);

// 注册说话人
vp_enroll_file("alice", "alice.wav");

// 1:N 识别
char speaker_id[256];
float score;
vp_identify(pcm, sample_count, speaker_id, 256, &score);

// 语音分析
VpAnalysisResult result;
vp_analyze_file("test.wav", VP_FEATURE_GENDER | VP_FEATURE_QUALITY, &result);

// 释放资源
vp_release();
```

### C# 集成（P/Invoke）

```csharp
using System.Runtime.InteropServices;

// 完整绑定见 examples/csharp_demo/Program.cs

[DllImport("voiceprint.dll", CallingConvention = CallingConvention.Cdecl,
           CharSet = CharSet.Ansi)]
static extern int vp_init(string model_dir, string db_path);

[DllImport("voiceprint.dll", CallingConvention = CallingConvention.Cdecl,
           CharSet = CharSet.Ansi)]
static extern int vp_init_analyzer(uint feature_flags);

[DllImport("voiceprint.dll", CallingConvention = CallingConvention.Cdecl,
           CharSet = CharSet.Ansi)]
static extern int vp_enroll_file(string speaker_id, string wav_path);

[DllImport("voiceprint.dll", CallingConvention = CallingConvention.Cdecl,
           CharSet = CharSet.Ansi)]
static extern int vp_analyze_file(string wav_path, uint features,
                                  out VpAnalysisResult result);
```

### CMake 集成

```cmake
set(VP_SDK_DIR "/path/to/voiceprint-sdk")

add_executable(my_app main.cpp)
target_include_directories(my_app PRIVATE ${VP_SDK_DIR}/include)
target_link_libraries(my_app PRIVATE ${VP_SDK_DIR}/lib/voiceprint.lib)

add_custom_command(TARGET my_app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${VP_SDK_DIR}/bin/voiceprint.dll
        ${VP_SDK_DIR}/bin/onnxruntime.dll
        $<TARGET_FILE_DIR:my_app>)
```

---

## API 参考

### 一、声纹识别核心 API

#### 初始化 / 释放

```cpp
// 初始化 SDK，加载必须模型，打开数据库
int vp_init(const char* model_dir, const char* db_path);

// 释放所有资源（线程安全）
void vp_release();
```

#### 注册 / 删除说话人

```cpp
// 从 PCM float32 数据注册（多次调用同一 ID 时自动增量平均更新）
int vp_enroll(const char* speaker_id,
              const float* pcm_data, int sample_count);

// 从 WAV 文件注册（自动解码，支持 16kHz/8kHz）
int vp_enroll_file(const char* speaker_id, const char* wav_path);

// 删除已注册的说话人（同步更新内存和数据库）
int vp_remove_speaker(const char* speaker_id);
```

#### 识别 / 验证

```cpp
// 1:N 识别：在已注册库中找出最相似的说话人
// out_speaker_id 缓冲区建议 >= 256 字节
int vp_identify(const float* pcm_data, int sample_count,
                char* out_speaker_id, int id_buf_size,
                float* out_score);

// 1:1 验证：验证音频是否属于指定说话人
int vp_verify(const char* speaker_id,
              const float* pcm_data, int sample_count,
              float* out_score);

// WAV 文件版本
int vp_identify_file(const char* wav_path,
                     char* out_speaker_id, int id_buf_size,
                     float* out_score);
int vp_verify_file(const char* speaker_id, const char* wav_path,
                   float* out_score);
```

#### 配置 / 查询

```cpp
int vp_set_threshold(float threshold);   // 默认 0.30
int vp_get_speaker_count();
const char* vp_get_last_error();
```

---

### 二、语音分析扩展 API

#### 功能标志（Feature Flags）

| 标志常量 | 值 | 说明 |
|---------|-----|------|
| `VP_FEATURE_GENDER` | 0x001 | 性别检测 |
| `VP_FEATURE_AGE` | 0x002 | 年龄估计 |
| `VP_FEATURE_EMOTION` | 0x004 | 情感识别 |
| `VP_FEATURE_ANTISPOOF` | 0x008 | 反欺骗检测 |
| `VP_FEATURE_QUALITY` | 0x010 | 音质评估 |
| `VP_FEATURE_VOICE` | 0x020 | 声学特征分析 |
| `VP_FEATURE_PLEASANT` | 0x040 | 声音好听度 |
| `VP_FEATURE_STATE` | 0x080 | 声音状态 |
| `VP_FEATURE_LANGUAGE` | 0x100 | 语种检测 |
| `VP_FEATURE_ALL` | 0x1FF | 全部功能 |

#### 初始化分析器

```cpp
// 按需加载语音分析模型，必须在 vp_init 之后调用
int vp_init_analyzer(unsigned int feature_flags);

// 动态开关反欺骗检测（默认开启）
int vp_set_antispoof_enabled(int enabled);
```

#### PCM 数据版本

```cpp
int vp_analyze(const float* pcm, int count, unsigned int features, VpAnalysisResult* out);
int vp_get_gender(const float* pcm, int count, VpGenderResult* out);
int vp_get_age(const float* pcm, int count, VpAgeResult* out);
int vp_get_emotion(const float* pcm, int count, VpEmotionResult* out);
int vp_anti_spoof(const float* pcm, int count, VpAntiSpoofResult* out);
int vp_assess_quality(const float* pcm, int count, VpQualityResult* out);
int vp_analyze_voice(const float* pcm, int count, VpVoiceFeatures* out);
int vp_get_pleasantness(const float* pcm, int count, VpPleasantnessResult* out);
int vp_get_voice_state(const float* pcm, int count, VpVoiceState* out);
int vp_detect_language(const float* pcm, int count, VpLanguageResult* out);
int vp_diarize(const float* pcm, int count,
               VpDiarizeSegment* out_segments, int max_segments, int* out_count);
```

#### WAV 文件版本

函数命名规则：在上述 PCM 版本函数名后加 `_file`，并将 PCM 参数替换为 `const char* path`：

```cpp
int vp_analyze_file(const char* path, unsigned int features, VpAnalysisResult* out);
int vp_get_gender_file(const char* path, VpGenderResult* out);
int vp_get_age_file(const char* path, VpAgeResult* out);
int vp_get_emotion_file(const char* path, VpEmotionResult* out);
int vp_anti_spoof_file(const char* path, VpAntiSpoofResult* out);
int vp_assess_quality_file(const char* path, VpQualityResult* out);
int vp_analyze_voice_file(const char* path, VpVoiceFeatures* out);
int vp_get_pleasantness_file(const char* path, VpPleasantnessResult* out);
int vp_get_voice_state_file(const char* path, VpVoiceState* out);
int vp_detect_language_file(const char* path, VpLanguageResult* out);
int vp_diarize_file(const char* path,
                    VpDiarizeSegment* out_segments, int max_segments, int* out_count);
```

#### 辅助函数

```cpp
const char* vp_emotion_name(int emotion_id);      // 如: "happy", "sad", "neutral"
const char* vp_language_name(const char* lang_code); // 如: "Chinese", "English"
```

---

### 三、结果结构体

```cpp
typedef struct {
    int   gender;        // 0=女性, 1=男性, 2=儿童
    float confidence;
    float female_prob;
    float male_prob;
    float child_prob;
} VpGenderResult;

typedef struct {
    float estimated_age;    // 估算年龄（岁）
    int   age_group;        // 0=儿童, 1=青少年, 2=青年, 3=中年, 4=老年
    float confidence;
} VpAgeResult;

typedef struct {
    int   dominant_emotion;  // 主要情感 ID (0-7)
    float confidence;
    float valence;           // 效价 [-1,1]（正面程度）
    float arousal;           // 激活度 [-1,1]（兴奋程度）
    float probs[8];          // neutral/happy/sad/angry/fearful/disgusted/surprised/calm
} VpEmotionResult;

typedef struct {
    int   is_genuine;    // 1=真实, 0=伪造
    float genuine_score; // 真实得分 [0, 1]
    float spoof_score;
} VpAntiSpoofResult;

typedef struct {
    float mos_score;        // MOS 质量分 [1.0, 5.0]
    float snr_db;           // 信噪比（dB）
    float loudness_lufs;    // 响度 LUFS（ITU-R BS.1770-4）
    float hnr_db;           // 谐噪比（dB）
    float clarity;          // 清晰度 [0, 1]
} VpQualityResult;

typedef struct {
    float f0_mean_hz;         // 平均基频（Hz）
    float f0_std_hz;          // 基频标准差
    float speaking_rate;      // 语速（音节/秒）
    float timbre_stability;   // 音色稳定性 [0, 1]
    float resonance;          // 共鸣度 [0, 1]
    float breathiness;        // 气息感 [0, 1]
} VpVoiceFeatures;

typedef struct {
    float overall_score;
    float attractiveness;   // 吸引力
    float warmth;           // 温暖感
    float authority;        // 权威感
    float clarity;          // 清晰度
} VpPleasantnessResult;

typedef struct {
    int   overall_state;    // 0=正常, 1=轻度疲劳, 2=疲劳, 3=极度疲劳
    float fatigue_level;    // [0, 1]
    float health_score;     // [0, 1]
    float stress_level;     // [0, 1]
} VpVoiceState;

typedef struct {
    char  lang_code[8];     // BCP-47 代码（如 "zh", "en", "de"）
    float confidence;
    char  second_lang[8];
    float second_conf;
} VpLanguageResult;

typedef struct {
    char  speaker_label[32]; // 如 "Speaker_0", "Speaker_1"
    int   start_ms;
    int   end_ms;
    float confidence;
} VpDiarizeSegment;

typedef struct {
    unsigned int      analyzed_features;  // 实际分析的功能标志
    VpGenderResult    gender;
    VpAgeResult       age;
    VpEmotionResult   emotion;
    VpAntiSpoofResult antispoof;
    VpQualityResult   quality;
    VpVoiceFeatures   voice;
    VpPleasantnessResult pleasantness;
    VpVoiceState      state;
    VpLanguageResult  language;
} VpAnalysisResult;
```

---

### 四、错误码

| 常量 | 值 | 说明 |
|------|-----|------|
| `VP_OK` | 0 | 成功 |
| `VP_ERROR_INIT_FAILED` | -1 | 初始化失败（模型加载、数据库打开） |
| `VP_ERROR_INVALID_PARAM` | -2 | 参数无效（空指针、非法值） |
| `VP_ERROR_NOT_INITIALIZED` | -3 | 未调用 `vp_init` |
| `VP_ERROR_SPEAKER_NOT_FOUND` | -4 | 说话人不存在 |
| `VP_ERROR_AUDIO_TOO_SHORT` | -5 | 有效音频过短（去静音后 < 1.5s） |
| `VP_ERROR_INFER_FAILED` | -6 | ONNX 推理异常 |
| `VP_ERROR_DB_WRITE` | -7 | 数据库写入失败 |
| `VP_ERROR_DB_READ` | -8 | 数据库读取失败 |
| `VP_ERROR_FILE_NOT_FOUND` | -9 | 文件不存在 |
| `VP_ERROR_ENCODE_FAILED` | -10 | WAV 解码失败 |
| `VP_ERROR_NO_SPEECH` | -11 | 未检测到语音活动 |
| `VP_ERROR_SPEAKER_EXISTS` | -12 | 说话人已存在 |
| `VP_ERROR_BUFFER_TOO_SMALL` | -13 | 输出缓冲区不足 |
| `VP_ERROR_THREAD_LOCK` | -14 | 线程锁竞争超时 |
| `VP_ERROR_UNKNOWN` | -15 | 未知错误 |
| `VP_ERROR_MODEL_NOT_AVAILABLE` | -16 | 可选模型未加载 |
| `VP_ERROR_ANALYSIS_FAILED` | -17 | 语音分析失败 |
| `VP_ERROR_DIARIZE_FAILED` | -18 | 说话人分段失败 |

---

## 音频输入要求

| 参数 | 要求 |
|------|------|
| 格式 | PCM float32（-1.0 ~ 1.0）或 WAV 文件 |
| 采样率 | 16kHz（推荐）或 8kHz（自动重采样） |
| 声道 | 单声道 Mono |
| 最短时长 | 1.5 秒（去静音后） |
| 推荐时长 | 3 ~ 10 秒（声纹） / 2 秒以上（语音分析） |

---

## 增量注册

对同一 `speaker_id` 多次调用 `vp_enroll`，SDK 自动增量平均更新 Embedding，无需删除重建，多次注册可提升识别精度。

---

## 线程安全

- 识别 / 验证 / 分析：共享读锁，多线程并发无阻塞
- 注册 / 删除：独占写锁
- ONNX Runtime `Ort::Env` 全局单例，推理会话可并发

---

## 运行时依赖

| 依赖 | 说明 |
|------|------|
| Windows 10/11 x64 | 最低系统要求 |
| Visual C++ Redistributable 2022 | MSVC 运行时 |
| `onnxruntime.dll` | 随 SDK 一起部署，无需系统安装 |
| GPU | 非必须，CPU 即可运行全部功能 |

---

## 技术栈

| 组件 | 技术 |
|------|------|
| 声纹模型 | ECAPA-TDNN（WeSpeaker 预训练，256 维） |
| VAD | Silero VAD v5（ONNX） |
| 推理引擎 | ONNX Runtime 1.17.1（CPU EP） |
| 特征提取 | kaldi-native-fbank（80 维 FBank） |
| DSP | YIN 基频，ITU-R BS.1770-4 LUFS，KissFFT |
| 聚类 | 凝聚层次聚类（余弦距离） |
| 存储 | SQLite3 WAL 模式 |
| 日志 | spdlog |
| 线程安全 | C++17 `std::shared_mutex` |