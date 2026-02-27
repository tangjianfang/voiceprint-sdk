# VoicePrint SDK

声纹识别 + 语音分析 C++ DLL SDK（Windows x64）

基于深度学习的声纹识别与多维语音分析库，提供标准 C ABI，可从 C/C++、C#、Python、Delphi 等任意语言调用。

---

## 功能特性

### 声纹识别（核心模块）

| 功能 | 说明 |
|------|------|
| 说话人注册 | 从 PCM 数据或 WAV 文件注册声纹，支持增量多次注册提升精度 |
| 1:N 识别 | 在已注册的说话人库中自动匹配最相似的说话人 |
| 1:1 验证 | 验证一段音频是否属于指定说话人 |
| 动态管理 | 无需重启即可添加/删除说话人，实时生效 |

### 语音分析（扩展模块）

通过 `vp_init_analyzer()` 按需加载，所有功能均可单独或组合调用：

| 功能 | API | 说明 |
|------|-----|------|
| 性别检测 | `vp_get_gender` | 区分女性 / 男性 / 儿童 |
| 年龄估计 | `vp_get_age` | 估算年龄（岁）及年龄段 |
| 情感识别 | `vp_get_emotion` | 识别 8 种情感 + Valence/Arousal 维度 |
| 反欺骗检测 | `vp_anti_spoof` | 区分真实发音与录音/TTS 合成伪造 |
| 音质评估 | `vp_assess_quality` | MOS、SNR、LUFS、HNR、清晰度 |
| 声学特征 | `vp_analyze_voice` | 基频 F0、语速、音色稳定性、共鸣、气息感 |
| 声音好听度 | `vp_get_pleasantness` | 综合评分：吸引力、温暖感、权威感、清晰度 |
| 声音状态 | `vp_get_voice_state` | 疲劳度、健康状态、压力水平 |
| 语种检测 | `vp_detect_language` | 识别 99 种语言 |
| 多人分段 | `vp_diarize` | 自动检测多说话人并输出时间段标注 |

---

## 性能指标

| 指标 | 实测值 |
|------|--------|
| Embedding 维度 | 256（L2 归一化） |
| 单次声纹提取（3s 音频，CPU） | P95 ≤ 200ms |
| 1:1000 检索 | < 1ms |
| 冷启动（加载模型 + 初始化） | < 1s |
| 内存稳定性（1000 次循环）| RSS 增长 < 1MB |
| 线程安全 | 读写锁，支持多线程并发 |

---

## 快速开始

### 构建

```powershell
# 前提：Visual Studio 2022 Build Tools + CMake >= 3.20
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

首次构建会自动下载第三方依赖（ONNX Runtime、kaldi-native-fbank 等）。  
运行 `cmake/download_models.cmake` 下载 ONNX 模型到 `models/`。

### 运行 Demo

```powershell
# C++ 演示
.\build\bin\Release\cpp_demo.exe

# C# 演示
cd examples\csharp_demo
dotnet run -- models speakers.db alice.wav bob.wav
```

### C++ 快速集成

```cpp
#include <voiceprint/voiceprint_api.h>
#include <voiceprint/voiceprint_types.h>

// 初始化
vp_init("models", "speakers.db");
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

vp_release();
```

详见 [doc/SDK_README.md](doc/SDK_README.md) 完整 API 说明和 [doc/usage_scenarios.md](doc/usage_scenarios.md) 集成示例。

---

## 目录结构

```
voiceprint-sdk/
├── CMakeLists.txt
├── cmake/
│   ├── download_deps.cmake     # 自动下载第三方依赖
│   └── download_models.cmake   # 自动下载 ONNX 模型
├── include/voiceprint/
│   ├── voiceprint_api.h        # 公开 C API 头文件
│   └── voiceprint_types.h      # 结构体 / 常量定义
├── src/
│   ├── core/                   # 音频处理、特征提取、声纹分析引擎
│   ├── storage/                # SQLite 持久化存储
│   ├── manager/                # Speaker 管理层、说话人分段
│   ├── api/                    # DLL 导出实现
│   └── utils/                  # 错误码、日志
├── models/                     # ONNX 模型文件
│   ├── ecapa_tdnn.onnx          # 声纹 Embedding（必须）
│   ├── silero_vad.onnx          # 语音活动检测（必须）
│   ├── gender_age.onnx          # 性别/年龄（可选）
│   ├── emotion.onnx             # 情感识别（可选）
│   ├── antispoof.onnx           # 反欺骗（可选）
│   ├── dnsmos.onnx              # 音质 MOS 评估（可选）
│   └── language.onnx            # 语种检测（可选）
├── third_party/                 # 第三方依赖（自动下载）
├── tests/
│   ├── unit/                   # 单元测试（Google Test + DSP/聚类算法）
│   ├── integration/            # 集成测试（完整 API 流程）
│   ├── benchmark/              # 性能基准测试
│   └── evaluation/             # EER / minDCF 效果评估
├── examples/
│   ├── cpp_demo/               # C++ 演示程序
│   └── csharp_demo/            # C# P/Invoke 演示程序
├── testdata/                   # 测试音频（运行 testdata/download_testdata.ps1 获取）
├── reports/                    # 自动生成的评估 / 性能报告
└── doc/
    ├── SDK_README.md           # SDK 用户手册（完整 API 参考）
    ├── usage_scenarios.md      # 典型场景集成示例
    └── DEVELOPMENT.md          # 项目架构与模块开发说明
```

---

## 依赖项

| 库 | 版本 | 用途 |
|----|------|------|
| ONNX Runtime | 1.17.1 | 深度学习模型推理引擎 |
| kaldi-native-fbank | latest | 80 维 FBank 特征提取 |
| sqlite3 | 3.40+ | 声纹特征持久化存储 |
| spdlog | 1.12+ | 日志系统 |
| KissFFT | latest | 音频 DSP（FFT） |
| Google Test | 1.14+ | 单元 / 集成测试框架 |

---

## 音频输入要求

| 参数 | 要求 |
|------|------|
| 格式 | float32 PCM（-1.0 ~ 1.0）或 WAV 文件 |
| 采样率 | 16kHz（推荐）或 8kHz（自动重采样） |
| 声道 | 单声道 Mono |
| 最短时长 | 1.5 秒（去静音后） |
| 推荐时长 | 3 ~ 10 秒 |

---

## 文档

| 文件 | 内容 |
|------|------|
| [doc/SDK_README.md](doc/SDK_README.md) | 完整 API 参考、错误码、部署说明 |
| [doc/usage_scenarios.md](doc/usage_scenarios.md) | 6 大典型场景集成代码示例 |
| [doc/DEVELOPMENT.md](doc/DEVELOPMENT.md) | 项目架构、模块设计、实现细节 |
| [testdata/README.md](testdata/README.md) | 测试音频获取与格式说明 |
| [reports/README.md](reports/README.md) | 评估报告字段说明 |

---

## 许可证

MIT License — 见 [LICENSE](LICENSE)