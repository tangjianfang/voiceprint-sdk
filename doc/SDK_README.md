# VoicePrint SDK v1.0.0

声纹识别 C++ DLL SDK — Windows x64

## 概述

基于深度学习的说话人识别 SDK，支持说话人注册、1:N 识别和 1:1 验证。

| 指标 | 实测值 |
|------|--------|
| Embedding 维度 | 256 维 (L2 归一化) |
| 单次提取耗时 (3s 音频, CPU) | P95 ≤ 200ms |
| 1:1000 检索耗时 | < 1ms |
| 冷启动 | < 1s |
| 内存稳定性 (1000次循环) | RSS 增长 < 1MB |
| 线程安全 | 读写锁，支持多线程并发 |

## 目录结构

```
voiceprint-sdk-v1.0.0-win-x64/
├── bin/                    # 运行时文件
│   ├── voiceprint.dll      # SDK 主 DLL
│   ├── onnxruntime.dll     # ONNX Runtime 推理引擎
│   └── cpp_demo.exe        # C++ 演示程序
├── include/voiceprint/     # C/C++ 头文件
│   └── voiceprint_api.h
├── lib/                    # 链接库
│   └── voiceprint.lib      # 导入库 (MSVC)
├── models/                 # ONNX 模型文件
│   ├── ecapa_tdnn.onnx     # 声纹 Embedding 提取模型
│   └── silero_vad.onnx     # VAD 语音活动检测模型
├── examples/               # 调用示例
│   ├── cpp/main.cpp        # C++ 示例
│   └── csharp/             # C# P/Invoke 示例
├── reports/                # 测试报告
└── doc/                    # 文档
    └── SDK_README.md       # 本文件
```

## 快速开始

### 运行 Demo

```bash
cd bin
cpp_demo.exe
```

Demo 会用合成音频演示完整流程：初始化 → 注册 → 识别 → 验证 → 删除 → 释放。

### C++ 集成

1. 将 `include/voiceprint/` 加入头文件搜索路径
2. 链接 `lib/voiceprint.lib`
3. 运行时确保 `voiceprint.dll` 和 `onnxruntime.dll` 在可执行文件同目录或 PATH 中
4. 将 `models/` 目录放在工作目录下（或通过 `vp_init` 指定路径）

```cpp
#include <voiceprint/voiceprint_api.h>

int main() {
    // 初始化（指定模型目录和数据库路径）
    int ret = vp_init("models", "speakers.db");
    if (ret != VP_OK) return 1;

    // 注册说话人（从 WAV 文件）
    vp_enroll_file("alice", "alice_voice.wav");

    // 注册说话人（从 PCM float 数据）
    // float pcm[] = {...};  // 16kHz, [-1.0, 1.0]
    // vp_enroll("bob", pcm, sample_count);

    // 1:N 识别
    char speaker_id[256];
    float score;
    ret = vp_identify(pcm_data, sample_count, speaker_id, 256, &score);
    if (ret == VP_OK) {
        printf("Identified: %s (score: %.4f)\n", speaker_id, score);
    }

    // 1:1 验证
    float verify_score;
    ret = vp_verify("alice", pcm_data, sample_count, &verify_score);

    // 释放资源
    vp_release();
    return 0;
}
```

### C# 集成 (P/Invoke)

```csharp
using System.Runtime.InteropServices;

[DllImport("voiceprint.dll", CallingConvention = CallingConvention.Cdecl)]
static extern int vp_init(string model_dir, string db_path);

[DllImport("voiceprint.dll", CallingConvention = CallingConvention.Cdecl)]
static extern int vp_enroll_file(string speaker_id, string wav_path);

// 详见 examples/csharp/Program.cs
```

### CMake 集成

```cmake
# 找到 SDK
set(VP_SDK_DIR "/path/to/voiceprint-sdk")

add_executable(my_app main.cpp)
target_include_directories(my_app PRIVATE ${VP_SDK_DIR}/include)
target_link_libraries(my_app PRIVATE ${VP_SDK_DIR}/lib/voiceprint.lib)

# 复制运行时 DLL
add_custom_command(TARGET my_app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${VP_SDK_DIR}/bin/voiceprint.dll
        ${VP_SDK_DIR}/bin/onnxruntime.dll
        $<TARGET_FILE_DIR:my_app>)
```

## API 参考

### 初始化 / 释放

| 函数 | 说明 |
|------|------|
| `int vp_init(const char* model_dir, const char* db_path)` | 初始化 SDK，加载模型，打开数据库 |
| `void vp_release()` | 释放所有资源 |

### 注册 / 删除

| 函数 | 说明 |
|------|------|
| `int vp_enroll(const char* speaker_id, const float* pcm_data, int sample_count)` | 从 PCM 数据注册说话人 |
| `int vp_enroll_file(const char* speaker_id, const char* wav_path)` | 从 WAV 文件注册说话人 |
| `int vp_remove_speaker(const char* speaker_id)` | 删除已注册的说话人 |

### 识别 / 验证

| 函数 | 说明 |
|------|------|
| `int vp_identify(const float* pcm, int count, char* out_id, int buf_size, float* out_score)` | 1:N 识别，返回最佳匹配 |
| `int vp_verify(const char* speaker_id, const float* pcm, int count, float* out_score)` | 1:1 验证 |

### 配置 / 查询

| 函数 | 说明 |
|------|------|
| `int vp_set_threshold(float threshold)` | 设置相似度阈值 (默认 0.30) |
| `int vp_get_speaker_count()` | 获取已注册说话人数量 |
| `const char* vp_get_last_error()` | 获取最后一次错误信息 |

### 错误码

| 错误码 | 值 | 说明 |
|--------|-----|------|
| VP_OK | 0 | 成功 |
| VP_ERROR_UNKNOWN | -1 | 未知错误 |
| VP_ERROR_INVALID_PARAM | -2 | 参数无效 |
| VP_ERROR_NOT_INIT | -3 | SDK 未初始化 |
| VP_ERROR_ALREADY_INIT | -4 | SDK 已初始化 |
| VP_ERROR_MODEL_LOAD | -5 | 模型加载失败 |
| VP_ERROR_AUDIO_TOO_SHORT | -6 | 音频太短 (< 1.5s) |
| VP_ERROR_AUDIO_INVALID | -7 | 音频数据无效 |
| VP_ERROR_SPEAKER_EXISTS | -8 | 说话人已存在 |
| VP_ERROR_SPEAKER_NOT_FOUND | -9 | 说话人未找到 |
| VP_ERROR_DB_ERROR | -10 | 数据库错误 |
| VP_ERROR_FILE_NOT_FOUND | -11 | 文件未找到 |
| VP_ERROR_BUFFER_TOO_SMALL | -12 | 缓冲区太小 |
| VP_ERROR_NO_MATCH | -13 | 无匹配 (低于阈值) |
| VP_ERROR_WAV_FORMAT | -14 | WAV 格式错误 |
| VP_ERROR_INFERENCE | -15 | 推理引擎错误 |

## 音频输入要求

| 参数 | 要求 |
|------|------|
| 格式 | PCM float32 ([-1.0, 1.0]) 或 WAV 文件 |
| 采样率 | 16kHz (推荐) 或 8kHz (自动重采样) |
| 通道 | 单声道 (Mono) |
| 最小时长 | 1.5 秒 (去除静音后) |
| 推荐时长 | 3~10 秒 |

## 增量注册

对同一 `speaker_id` 多次调用 `vp_enroll`，SDK 会自动进行增量平均更新 Embedding，无需删除重建。多次注册可提高识别精度。

## 线程安全

- 所有 API 都是线程安全的
- 识别/验证使用共享读锁，可多线程并发
- 注册/删除使用独占写锁
- 建议在应用退出时调用 `vp_release()`

## 运行时依赖

- Windows 10/11 x64
- Visual C++ Redistributable 2022
- 无需 GPU，CPU 即可运行

## 技术栈

| 组件 | 技术 |
|------|------|
| 声纹模型 | ResNet34-LM (Wespeaker, 256维) |
| VAD | Silero VAD v5 (ONNX) |
| 推理引擎 | ONNX Runtime 1.17.1 |
| 特征提取 | kaldi-native-fbank (80维 FBank) |
| 存储 | SQLite3 WAL 模式 |
| 日志 | spdlog (文件+控制台) |

## 许可证

本 SDK 仅供内部使用。第三方组件遵循各自许可协议。
