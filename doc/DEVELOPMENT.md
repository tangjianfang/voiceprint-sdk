# VoicePrint SDK — 开发参考文档

本文档面向项目开发者，记录架构设计、模块职责、实现细节及历史任务。

---

## 1. 整体分层架构

```
┌──────────────────────────────────────────────────────┐
│           调用方（C/C++/C#/Delphi/Python 等）          │
├──────────────────────────────────────────────────────┤
│          C ABI 导出层（DLL 公开接口）                   │
│      extern "C" __declspec(dllexport)                │
├──────────────────────────────────────────────────────┤
│                    业务逻辑层                          │
│  ┌──────────────┬──────────────┬──────────────────┐  │
│  │  注册管理器   │  识别引擎     │  验证引擎          │  │
│  └──────────────┴──────────────┴──────────────────┘  │
├──────────────────────────────────────────────────────┤
│                    核心引擎层                          │
│  ┌──────────────┬──────────────┬──────────────────┐  │
│  │   音频预处理  │  声纹提取     │  VAD 检测         │  │
│  │  (重采样+     │  (ONNX       │  (Silero-VAD     │  │
│  │   FBank)     │   Runtime)   │   ONNX)          │  │
│  └──────────────┴──────────────┴──────────────────┘  │
├──────────────────────────────────────────────────────┤
│                    基础设施层                          │
│  ┌──────────────┬──────────────┬──────────────────┐  │
│  │  特征存储     │  日志系统     │  线程安全管理      │  │
│  │ (SQLite/     │  (spdlog)    │  (读写锁)         │  │
│  │  内存索引)    │              │                   │  │
│  └──────────────┴──────────────┴──────────────────┘  │
└──────────────────────────────────────────────────────┘
```

---

## 2. 核心模块说明

### 2.1 音频预处理模块（`src/core/`）

**输入：** PCM raw data 或 WAV 文件路径

**处理流程：**
1. 格式归一化（16kHz / 16bit / Mono）
2. VAD 静音过滤（Silero-VAD ONNX 模型）
3. 有效性校验（最短 ≥ 1.5s 去静音后）
4. 80 维 FBank 特征提取

**依赖：** kaldi-native-fbank（纯 C++，无额外动态库）

### 2.2 声纹提取模块（`src/core/`）

- **模型：** ECAPA-TDNN（WeSpeaker 预训练，ONNX 格式）
- **推理引擎：** ONNX Runtime C++ API，`Ort::Env` 全局单例（通过 `SpeakerManager::get_ort_env()` 共享）
- **输出：** 256 维 L2 归一化 Embedding 向量

### 2.3 相似度计算模块（`src/manager/`）

- 使用余弦相似度（L2 归一化后等价于点积）
- 默认阈值：0.30，支持通过 `vp_set_threshold()` 动态调整
- 暴力检索（≤ 10000 人），预留向量索引扩展接口

### 2.4 存储模块（`src/storage/`）

| 层 | 实现 | 说明 |
|----|------|------|
| 内存层 | `unordered_map<string, SpeakerProfile>` + `shared_mutex` | 并发读写安全 |
| 持久化层 | SQLite3 WAL 模式 | 单文件数据库，断电安全 |
| 序列化格式 | BLOB 二进制 | 256 维 float32 直接存储 |

### 2.5 语音分析模块（`src/core/voice_analyzer.h/.cpp`）

通过 `vp_init_analyzer(feature_flags)` 按需加载，支持以下功能：

| 功能 | 实现方式 | 可选模型 |
|------|---------|---------|
| 性别/年龄 | ONNX 分类器 | `gender_age.onnx` |
| 情感识别 | ONNX 分类器（8 类 + Valence/Arousal） | `emotion.onnx` |
| 反欺骗 | ONNX 二分类器 | `antispoof.onnx` |
| 音质评估（MOS） | DNSMOS ONNX 模型 + DSP | `dnsmos.onnx` |
| 声学特征 | YIN 基频算法 + ITU-R BS.1770-4 LUFS | 纯 DSP，无模型 |
| 声音好听度 | 综合声学特征加权评分 | 纯 DSP，无模型 |
| 声音状态 | 基于 LUFS/F0 稳定性的规则引擎 | 纯 DSP，无模型 |
| 语种检测 | ONNX 分类器（99 种语言） | `language.onnx` |
| 多人分段 | VAD + ECAPA-TDNN + 层次聚类 | 复用核心模型 |

**DSP 工具类（头文件）：**
- `src/core/loudness.h` — ITU-R BS.1770-4 LUFS、SNR、HNR、清晰度
- `src/core/pitch_analyzer.h` — YIN F0、语速、稳定性、气息感
- `src/core/clustering.h` — 凝聚层次聚类、余弦距离

### 2.6 说话人分段模块（`src/manager/diarizer.h/.cpp`）

**流程：**
1. Silero-VAD 检测活跃语音段
2. 对每个语音段提取 ECAPA-TDNN Embedding
3. 凝聚层次聚类（cosine 距离，阈值 0.4）
4. 输出 `VpDiarizeSegment[]` 标注（speaker_id + start_ms + end_ms + score）

---

## 3. 构建系统

### 3.1 CMake 变量

| 变量 | 说明 | 默认值 |
|------|------|--------|
| `CMAKE_BUILD_TYPE` | 构建类型 | Release |
| `VP_BUILD_TESTS` | 编译测试 | ON |
| `VP_BUILD_BENCHMARKS` | 编译性能测试 | ON |

### 3.2 编译标志

```cmake
# 核心 DLL
target_compile_options(voiceprint PRIVATE /W4 /arch:AVX2 /O2)

# 测试
target_compile_options(unit_tests PRIVATE /W3)
```

### 3.3 目标产物

| 目标 | 输出路径 | 说明 |
|------|---------|------|
| `voiceprint` | `build/bin/Release/voiceprint.dll` | 主 DLL |
| `voiceprint_core` | `build/lib/Release/voiceprint_core.lib` | 静态核心库（链接进 DLL） |
| `cpp_demo` | `build/bin/Release/cpp_demo.exe` | C++ 演示程序 |
| `unit_tests` | `build/bin/Release/unit_tests.exe` | 单元测试 |
| `integration_tests` | `build/bin/Release/integration_tests.exe` | 集成测试 |
| `benchmark_tests` | `build/bin/Release/benchmark_tests.exe` | 性能基准 |
| `evaluation_tests` | `build/bin/Release/evaluation_tests.exe` | EER/minDCF 评估 |

---

## 4. 测试体系

### 4.1 单元测试（`tests/unit/`）

覆盖：
- DSP 算法（LUFS 计算、YIN 基频、SNR/HNR）
- 凝聚聚类正确性
- 音频预处理（重采样、VAD 集成）
- 各 VP_FEATURE_* 分析结果格式校验

### 4.2 集成测试（`tests/integration/`）

覆盖完整 API 流程：
- `vp_init` → `vp_enroll_file` → `vp_identify` → `vp_verify` → `vp_release`
- `vp_init_analyzer` → `vp_analyze_file` 各 feature flag 组合
- `vp_diarize_file` 多说话人场景
- 错误码边界（无效输入、模型缺失时降级）

### 4.3 性能基准（`tests/benchmark/`）

测量：
- 单次 Embedding 提取 P50/P95（目标 ≤ 200ms，CPU）
- 1:1000 检索延迟（目标 < 50ms）
- 1000 次循环内存稳定性（RSS 增长 < 1MB）
- 冷启动时间（< 1s）

### 4.4 效果评估（`tests/evaluation/`）

- 计算 EER（等错误率，目标 ≤ 3%）
- 计算 minDCF
- 需提供足够规模的试验数据集（VoxCeleb1/AISHELL，参见 `testdata/README.md`）

---

## 5. 错误码参考

| 值 | 常量 | 含义 |
|----|------|------|
| 0 | `VP_OK` | 成功 |
| -1 | `VP_ERROR_INIT_FAILED` | 初始化失败 |
| -2 | `VP_ERROR_INVALID_PARAM` | 参数无效 |
| -3 | `VP_ERROR_NOT_INITIALIZED` | 未调用 vp_init |
| -4 | `VP_ERROR_SPEAKER_NOT_FOUND` | 说话人不存在 |
| -5 | `VP_ERROR_AUDIO_TOO_SHORT` | 有效音频过短（< 1.5s） |
| -6 | `VP_ERROR_INFER_FAILED` | ONNX 推理失败 |
| -7 | `VP_ERROR_DB_WRITE` | 数据库写入失败 |
| -8 | `VP_ERROR_DB_READ` | 数据库读取失败 |
| -9 | `VP_ERROR_FILE_NOT_FOUND` | 文件不存在 |
| -10 | `VP_ERROR_ENCODE_FAILED` | WAV 解码失败 |
| -11 | `VP_ERROR_NO_SPEECH` | 未检测到语音 |
| -12 | `VP_ERROR_SPEAKER_EXISTS` | 说话人已存在 |
| -13 | `VP_ERROR_BUFFER_TOO_SMALL` | 输出缓冲区不足 |
| -14 | `VP_ERROR_THREAD_LOCK` | 线程锁竞争超时 |
| -15 | `VP_ERROR_UNKNOWN` | 未知错误 |
| -16 | `VP_ERROR_MODEL_NOT_AVAILABLE` | 可选模型未加载 |
| -17 | `VP_ERROR_ANALYSIS_FAILED` | 语音分析失败 |
| -18 | `VP_ERROR_DIARIZE_FAILED` | 说话人分段失败 |

---

## 6. 线程安全设计

- DLL 所有全局状态通过 `std::shared_mutex` 保护
- 读操作（identify/verify/analyze）→ `shared_lock`（多线程并发）
- 写操作（enroll/remove）→ `unique_lock`（排他）
- ONNX Runtime `Ort::Env` 为 SDK 内全局单例，本身线程安全
- SQLite 使用 WAL 模式，允许多读一写并发

---

## 7. 部署说明

### 必须文件

```
voiceprint.dll
models/
  ecapa_tdnn.onnx        # 声纹 Embedding（必须）
  silero_vad.onnx        # VAD（必须）
onnxruntime.dll          # 随 DLL 一起部署
```

### 可选模型（缺失时对应 API 返回 VP_ERROR_MODEL_NOT_AVAILABLE）

```
models/
  gender_age.onnx
  emotion.onnx
  antispoof.onnx
  dnsmos.onnx
  language.onnx
```

---

## 8. 实现历史（任务记录）

本节记录 SDK 从零出发的完整实现任务，供后续维护参考。

### 阶段 1 — 核心声纹识别（TASK-01 ~ TASK-10）

| 任务 | 内容 |
|------|------|
| TASK-01 | CMake 项目结构搭建、第三方依赖自动下载 |
| TASK-02 | WAV 文件解析器（PCM float32 标准化） |
| TASK-03 | FBank 特征提取集成（kaldi-native-fbank） |
| TASK-04 | Silero-VAD ONNX 推理集成，静音过滤 |
| TASK-05 | ECAPA-TDNN ONNX 推理，256 维 Embedding 提取 |
| TASK-06 | 余弦相似度计算、阈值判断逻辑 |
| TASK-07 | SQLite3 WAL 模式存储层，Embedding BLOB 序列化 |
| TASK-08 | SpeakerManager（注册/删除/内存索引）+ shared_mutex |
| TASK-09 | DLL 导出层（C ABI），vp_init/release/enroll/identify/verify |
| TASK-10 | Google Test 单元测试框架集成，基础测试用例 |

### 阶段 2 — 基础质量保障（TASK-11 ~ TASK-15）

| 任务 | 内容 |
|------|------|
| TASK-11 | 集成测试（完整 enroll → identify → verify 流程） |
| TASK-12 | 性能基准测试（P95 Embedding 提取、1:N 检索延迟） |
| TASK-13 | EER/minDCF 效果评估框架 |
| TASK-14 | C# P/Invoke 演示程序 + 完整绑定声明 |
| TASK-15 | spdlog 日志集成，错误码完善（-1 ~ -15） |

### 阶段 3 — 10 项语音分析扩展（TASK-16 ~ TASK-20）

| 任务 | 内容 |
|------|------|
| TASK-16 | 语音分析基础架构（VoiceAnalyzer、feature flags、voiceprint_types.h） |
| TASK-17 | 性别检测（vp_get_gender）+ 年龄估计（vp_get_age） |
| TASK-18 | 情感识别（vp_get_emotion，8 类 + Valence/Arousal） |
| TASK-19 | 反欺骗（vp_anti_spoof） + 音质评估（vp_assess_quality） |
| TASK-20 | 声学特征（vp_analyze_voice）+ 好听度（vp_get_pleasantness）+ 声音状态（vp_get_voice_state）+ 语种检测（vp_detect_language）+ 多人分段（vp_diarize） |

---

## 9. 关键设计决策

### 为什么选 ECAPA-TDNN？

WeSpeaker 开源预训练模型在 VoxCeleb1 上 EER < 1%，ONNX 导出后在 CPU（无 GPU）环境下单次推理 P95 ≤ 180ms，满足实时嵌入式场景需求。

### 为什么使用 C ABI？

C ABI（`extern "C"` + `__declspec(dllexport)`）可以被任意语言通过 FFI/P/Invoke 调用，避免 C++ name mangling，不依赖调用方的 MSVC 版本或 STL 实现。

### 为什么 SQLite WAL 模式？

WAL（Write-Ahead Log）模式允许多读一写并发而无需独占锁，在边缘设备（嵌入式、小服务器）场景下无需部署独立数据库服务。

### 可选模型设计

语音分析的 7 个 ONNX 模型均为可选，缺失时对应 API 返回 `VP_ERROR_MODEL_NOT_AVAILABLE`（-16），而不是 crash。这允许用户按需部署，最小安装仅需 2 个必须模型（< 50MB）。
