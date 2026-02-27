

```md
# 声纹识别 C++ DLL SDK — AI 智能体开发任务书

## 1. 项目概述

开发一个基于声纹（Voiceprint）的说话人识别 C++ DLL SDK，通过提取语音音色特征（Speaker Embedding），实现对不同说话人的注册、识别（1:N）和验证（1:1）功能。

### 1.1 核心能力

- 说话人注册：从音频中提取声纹特征并持久化存储
- 说话人识别（1:N）：输入一段语音，从已注册的说话人库中识别出是谁
- 说话人验证（1:1）：验证一段语音是否属于指定说话人
- 支持动态增删说话人，无需重新加载模型

### 1.2 性能指标要求

| 指标 | 目标值 |
|------|--------|
| 等错误率（EER） | ≤ 3% |
| 单次 Embedding 提取耗时（CPU） | ≤ 200ms |
| 1:1000 检索耗时 | ≤ 50ms |
| 最小有效语音时长 | ≥ 1.5s（去除静音后） |
| 支持采样率 | 16kHz / 8kHz（自动重采样） |
| DLL 体积（含模型） | ≤ 150MB |
| 内存占用（1000人库） | ≤ 300MB |

### 1.3 构建环境约束

- IDE：VSCode
- 编译器：MSVC（Visual Studio 2022 Build Tools）
- 构建系统：CMake ≥ 3.20
- 平台：Windows x64
- 所有第三方依赖由 AI 智能体自动下载到项目内 `third_party/` 统一目录
- 不依赖系统级安装的 vcpkg 或 Conan，所有依赖自包含

---

## 2. 技术架构

### 2.1 整体分层架构

```text
┌──────────────────────────────────────────────────┐
│                  调用方（C/C++/C#/Delphi 等）       │
├──────────────────────────────────────────────────┤
│              C ABI 导出层（DLL 公开接口）            │
│         extern "C" __declspec(dllexport)          │
├──────────────────────────────────────────────────┤
│                   业务逻辑层                       │
│    ┌────────────┬─────────────┬───────────────┐   │
│    │ 注册管理器  │  识别引擎    │  验证引擎      │   │
│    └────────────┴─────────────┴───────────────┘   │
├──────────────────────────────────────────────────┤
│                  核心引擎层                        │
│    ┌────────────┬─────────────┬───────────────┐   │
│    │ 音频预处理  │ 声纹提取     │  VAD 检测     │   │
│    │ (重采样/    │ (ONNX       │  (Silero-VAD  │   │
│    │  FBank)    │  Runtime)   │   ONNX)       │   │
│    └────────────┴─────────────┴───────────────┘   │
├──────────────────────────────────────────────────┤
│                  基础设施层                        │
│    ┌────────────┬─────────────┬───────────────┐   │
│    │ 特征存储    │  日志系统    │  线程安全管理  │   │
│    │ (SQLite/   │  (spdlog)   │  (读写锁)     │   │
│    │  内存索引)  │             │               │   │
│    └────────────┴─────────────┴───────────────┘   │
└──────────────────────────────────────────────────┘
```

### 2.2 核心模块说明

#### 2.2.1 音频预处理模块

- 输入：PCM raw data 或 WAV 文件路径
- 处理流程：格式归一化（16kHz/16bit/Mono）→ VAD 静音过滤 → 有效性校验（≥1.5s）→ FBank 特征提取（80维）
- 依赖：kaldi-native-fbank（纯 C++）

#### 2.2.2 声纹提取模块

- 模型：ECAPA-TDNN（Wespeaker 预训练 ONNX）
- 推理引擎：ONNX Runtime C++ API
- 输出：192/256 维 L2 归一化 Embedding 向量

#### 2.2.3 特征比对模块

- Cosine Similarity（L2 归一化后等价于点积）
- 默认阈值 0.30，支持 API 动态配置
- 暴力搜索（≤10000 人），预留向量索引接口

#### 2.2.4 存储模块

- 内存层：`unordered_map<string, SpeakerProfile>` + `shared_mutex`
- 持久化层：SQLite3 WAL 模式单文件数据库
- Embedding 以 BLOB 二进制存储

### 2.3 DLL 公开接口

```cpp
#ifdef VOICEPRINT_EXPORTS
#define VP_API extern "C" __declspec(dllexport)
#else
#define VP_API extern "C" __declspec(dllimport)
#endif

VP_API int   vp_init(const char* model_dir, const char* db_path);
VP_API void  vp_release();

VP_API int   vp_enroll(const char* speaker_id, const float* pcm_data, int sample_count);
VP_API int   vp_enroll_file(const char* speaker_id, const char* wav_path);
VP_API int   vp_remove_speaker(const char* speaker_id);

VP_API int   vp_identify(const float* pcm_data, int sample_count,
                          char* out_speaker_id, int id_buf_size, float* out_score);
VP_API int   vp_verify(const char* speaker_id,
                        const float* pcm_data, int sample_count, float* out_score);

VP_API int   vp_set_threshold(float threshold);
VP_API int   vp_get_speaker_count();
VP_API const char* vp_get_last_error();
```

### 2.4 第三方依赖

| 库 | 用途 | 获取方式 |
|----|------|----------|
| onnxruntime ≥1.16 | 模型推理 | GitHub Release 预编译包下载 |
| kaldi-native-fbank | FBank 特征提取 | Git clone 源码编译 |
| sqlite3 ≥3.40 | 持久化存储 | 下载 amalgamation 源码 |
| spdlog ≥1.12 | 日志 | Git clone（header-only） |
| Google Test ≥1.14 | 单元测试 | CMake FetchContent |

### 2.5 预训练模型

| 模型 | 来源 | 用途 |
|------|------|------|
| ecapa_tdnn.onnx | Wespeaker | 声纹 Embedding 提取 |
| silero_vad.onnx | Silero GitHub | 语音活动检测 |

---

## 3. 实现方案

### 3.1 Embedding 提取流程

```text
原始音频 → 重采样16kHz → Silero-VAD过滤静音 → 有效性校验(≥1.5s)
    → FBank提取(80维×T帧) → ECAPA-TDNN推理 → L2归一化 → Embedding向量
```

### 3.2 注册流程

```text
vp_enroll() → 提取Embedding → speaker_id已存在?
    → 不存在: 创建新Profile(mean=embedding, count=1)
    → 已存在: 增量平均(new_mean = (old*n + new)/(n+1)), L2重新归一化
    → 同步写入内存缓存 + SQLite
```

### 3.3 识别/验证流程

```text
vp_identify() → 提取Embedding → 遍历所有Speaker计算Cosine Similarity
    → 最高分 ≥ threshold → 返回speaker_id + score
    → 最高分 < threshold → 返回unknown
```

### 3.4 线程安全

- ONNX Runtime Session：配置 intra/inter op 线程数，Session 本身线程安全
- 声纹库：`std::shared_mutex`，识别共享读锁，注册/删除独占写锁
- SQLite：WAL 模式 + busy_timeout

---

## 4. AI 智能体任务执行计划

> 严格按顺序执行，每个任务完成后必须通过验收条件才能进入下一个任务。
> 构建命令统一使用：`cmake -B build -G "Visual Studio 17 2022" -A x64 && cmake --build build --config Release`

---

### TASK-01：项目骨架初始化

目标：创建完整目录结构、CMake 构建系统、DLL 导出层空实现、错误码定义

验收条件：
- [ ] CMake 配置并编译成功，生成 `voiceprint.dll`
- [ ] `dumpbin /exports` 可见所有 API 符号

失败处理：检查 CMakeLists.txt 语法和 MSVC 工具链配置

---

### TASK-02：第三方依赖下载与集成

目标：编写 CMake 脚本自动下载所有依赖到 `third_party/`，创建对应 CMake targets 并链接到主项目

依赖清单：onnxruntime（预编译包）、kaldi-native-fbank（源码）、sqlite3（amalgamation）、spdlog（header-only）、Google Test（FetchContent）

验收条件：
- [ ] CMake 配置时自动拉取所有依赖
- [ ] 所有依赖存在于 `third_party/` 下
- [ ] 整体编译通过

失败处理：下载失败切换镜像 URL；编译失败检查子项目 CMake 兼容性并 patch

---

### TASK-03：下载预训练模型

目标：编写 CMake 脚本自动下载 `ecapa_tdnn.onnx` 和 `silero_vad.onnx` 到 `models/` 目录

来源：Wespeaker GitHub Release（声纹模型）、Silero GitHub（VAD 模型）

验收条件：
- [ ] 两个模型文件存在且大小合理
- [ ] ONNX Runtime 可加载不报错

失败处理：链接失效则搜索最新 Release 页面；opset 不兼容则匹配 ONNX Runtime 版本

---

### TASK-04：日志模块

目标：基于 spdlog 封装统一日志系统，支持文件+控制台双输出，提供 `VP_LOG_INFO/WARN/ERROR/DEBUG` 宏

验收条件：
- [ ] 编译通过
- [ ] 调用日志宏可在控制台和文件中看到带时间戳的输出

失败处理：头文件路径问题检查 include directories

---

### TASK-05：音频预处理模块

目标：实现 WAV 文件读取、PCM int16→float32 转换、重采样（8kHz→16kHz）

技术方向：手动解析 WAV 标准头（44字节），线性插值重采样

验收条件：
- [ ] 正确读取 16kHz/8kHz WAV 文件
- [ ] float32 输出值域 [-1.0, 1.0]
- [ ] 单元测试通过

失败处理：非标准 WAV 头增加容错；重采样质量差则升级为 sinc 插值

---

### TASK-06：VAD 语音活动检测模块

目标：集成 Silero-VAD ONNX 模型，实现静音段检测和过滤

技术方向：512 样本窗口滑动推理，维护 hidden state（h/c tensors），合并相邻语音段（间隔<300ms）

验收条件：
- [ ] 模型加载成功
- [ ] 纯静音返回空段，含语音返回正确区间
- [ ] 单元测试通过

失败处理：输出全静音检查音频归一化；hidden state 维度错误参考 Silero 官方 Python 实现核对

---

### TASK-07：FBank 特征提取模块

目标：基于 kaldi-native-fbank 提取 80 维 Log Mel-Filterbank 特征，含 CMVN 归一化

技术方向：配置 num_bins=80, frame_length=25ms, frame_shift=10ms, sample_rate=16000

验收条件：
- [ ] 3 秒音频产生约 298 帧，维度 80
- [ ] 输出无 NaN/Inf
- [ ] 单元测试通过

失败处理：帧数不符检查参数单位；CMVN 除零添加 epsilon 保护

---

### TASK-08：ONNX Runtime 推理引擎封装

目标：封装 ONNX Runtime C++ API 为通用推理类，支持模型加载、输入输出查询、tensor 推理

技术方向：全局 `Ort::Env` 单例，`SessionOptions` 配置线程数，CPU MemoryInfo，正确管理 `Ort::Value` 生命周期

验收条件：
- [ ] 可加载两个 ONNX 模型
- [ ] 输入输出名称和 shape 查询正确
- [ ] 随机输入推理不崩溃
- [ ] 单元测试通过

失败处理：DLL 找不到检查 PATH；Session 创建失败用 `std::wstring` 路径 API；输出为空检查 output_names 匹配

---

### TASK-09：声纹 Embedding 提取模块

目标：串联 FBank + ECAPA-TDNN 推理，实现音频→归一化 Embedding 向量的完整 Pipeline

技术方向：FBank 输出 reshape 为 `[1, num_frames, 80]` 输入模型，输出做 L2 归一化

验收条件：
- [ ] Embedding 维度正确（192 或 256）
- [ ] L2 范数 = 1.0（误差 < 1e-5）
- [ ] 同一人相似度 > 0.5，不同人 < 0.3
- [ ] 单次耗时 ≤ 200ms
- [ ] 单元测试通过

失败处理：shape 不匹配用 `get_input_shapes()` 核对；同一人相似度低检查 CMVN 是否遗漏

---

### TASK-10：相似度计算模块

目标：实现 Cosine Similarity 计算和 1:N 最佳匹配检索

技术方向：L2 归一化向量的 cosine similarity = dot product，启用 `/arch:AVX2` 优化

验收条件：
- [ ] 边界值数学正确（相同=1, 正交=0, 反向=-1）
- [ ] 1000 个 192 维向量全量比对 < 1ms
- [ ] 单元测试通过

失败处理：精度问题用 double 累加；性能不达标手写 SIMD intrinsics

---

### TASK-11：SQLite 存储模块

目标：实现声纹 Profile 的 CRUD 持久化，Embedding 以 BLOB 存储

技术方向：SQLite amalgamation 编译，WAL 模式，`memcpy` 序列化 float 数组为 BLOB

验收条件：
- [ ] 数据库自动创建，CRUD 正确
- [ ] Embedding 存取无精度损失
- [ ] 单元测试通过

失败处理：BLOB 数据损坏检查 `bind_blob` 的 size 参数；并发冲突启用 `busy_timeout`

---

### TASK-12：Speaker Manager 业务逻辑层

目标：实现注册（增量平均）、识别（1:N）、验证（1:1）完整业务逻辑，管理内存缓存与 DB 一致性

技术方向：`shared_mutex` 读写锁保护缓存，增量平均后 L2 重新归一化，冷启动从 DB 批量加载

验收条件：
- [ ] 完整 enroll→identify→verify→remove 流程正确
- [ ] 多线程并发无 crash
- [ ] 重启后从 DB 恢复，结果一致
- [ ] 集成测试通过

失败处理：增量平均后相似度下降检查 L2 重归一化；数据竞争用 `/fsanitize=address` 检测

---

### TASK-13：DLL API 层完整实现

目标：将所有模块串联到 C ABI 导出层，实现全部接口，添加参数校验和异常捕获

技术方向：全局 static 实例管理，try-catch 包裹转错误码，`thread_local` 错误信息，参数空指针/越界校验

验收条件：
- [ ] 所有 API 无空实现残留
- [ ] 异常输入返回正确错误码不崩溃
- [ ] 重复 init/release 无泄漏
- [ ] 集成测试通过

失败处理：异常逃逸确保 `catch(...)` 兜底；buffer 溢出检查 `id_buf_size`

---

### TASK-14：调用示例

目标：编写 C++ 和 C# (P/Invoke) 完整调用示例，覆盖初始化→注册→识别→验证→释放全流程

验收条件：
- [ ] C++ demo 编译运行成功
- [ ] C# demo 编译运行成功
- [ ] 输出正确的识别/验证结果

失败处理：DLL 加载失败检查路径和依赖 DLL 是否齐全

---

### TASK-15：准备测试数据集

目标：下载或生成用于自动化测试和效果评估的音频数据

技术方向：
- 单元测试用：代码生成正弦波 WAV + 少量真实语音样本
- 效果评估用：下载 VoxCeleb1-O 测试集（或 CN-Celeb 中文集）的 trial pairs 和对应音频

验收条件：
- [ ] `testdata/` 下存在可用的测试音频
- [ ] 评估用 trial list 文件存在（格式：`label enroll_wav test_wav`）

失败处理：公开数据集下载受限则用少量样本构造 mini 测试集

---

### TASK-16：自动化单元测试

目标：基于 Google Test 对所有核心模块编写单元测试，集成到 CMake CTest

技术方向：每个模块对应一个 test 文件，覆盖正常路径 + 边界条件 + 异常输入

验收条件：
- [ ] `cmake --build build --target test` 或 `ctest` 全部通过
- [ ] 覆盖所有核心模块

失败处理：测试失败定位到具体模块修复后重跑

---

### TASK-17：集成测试

目标：通过 DLL C API 接口执行端到端全流程测试和多线程压力测试

技术方向：完整生命周期测试、异常输入测试、并发读写测试（N 线程 identify + M 线程 enroll）

验收条件：
- [ ] 全流程测试通过
- [ ] 10 线程并发无 crash 无数据不一致
- [ ] 所有异常场景返回正确错误码

失败处理：并发问题用 sanitizer 检测；数据不一致检查锁粒度

---

### TASK-18：效果评估（EER / minDCF）

目标：在标准测试集上计算声纹识别的量化指标

技术方向：
- 遍历 trial list，对每对音频提取 Embedding 计算 Cosine Similarity
- 计算 EER（FAR=FRR 交叉点）和 minDCF（p_target=0.01）
- 计算 TAR@FAR=1% 和 TAR@FAR=0.1%
- 输出评估报告到 `reports/evaluation_report.txt`

达标标准：

| 指标 | 目标 |
|------|------|
| EER | ≤ 3% |
| minDCF (p=0.01) | ≤ 0.30 |
| TAR@FAR=1% | ≥ 95% |
| TAR@FAR=0.1% | ≥ 90% |

验收条件：
- [ ] 评估脚本可自动运行并输出报告
- [ ] 所有指标达标

失败处理：→ 触发 TASK-20 自愈流程

---

### TASK-19：性能基准测试

目标：验证推理延迟、检索耗时、内存占用、冷启动时间

技术方向：Google Benchmark 或自定义计时，循环 1000 次取 P95

达标标准：

| 测试项 | 目标 |
|--------|------|
| 单次 Embedding 提取（3s 音频，CPU） | ≤ 200ms (P95) |
| 1:1000 检索 | ≤ 50ms |
| 内存泄漏（10000 次注册/删除循环） | RSS 增长 ≤ 5MB |
| 冷启动（加载模型 + 1000 人库） | ≤ 3s |

验收条件：
- [ ] 所有性能指标达标
- [ ] 输出报告到 `reports/benchmark_report.txt`

失败处理：推理慢检查是否 Debug 构建或线程数配置不当；内存泄漏用 ASAN 定位

---

### TASK-20：自愈修复流程

目标：当 TASK-18 或 TASK-19 指标未达标时，按优先级自动执行修复并重新评估

执行策略（按优先级逐级尝试）：

```text
Level 1 [自动] 参数调优
  → 网格搜索最优阈值
  → 调整 VAD 灵敏度
  → 调整 FBank 参数
  → 重新运行 TASK-18

Level 2 [自动] 模型替换
  → 切换备选模型: ECAPA-TDNN → ResNet34-LM → CAM++
  → 重新下载 ONNX，重新运行 TASK-18

Level 3 [半自动] 后处理增强
  → 添加 Score Normalization (AS-Norm)
  → 添加 Embedding 后处理 (LDA)
  → 重新运行 TASK-18

Level 4 [人工介入] 数据增强微调
  → 收集目标场景数据
  → Fine-tune 预训练模型
  → 重新导出 ONNX
  → 重新运行 TASK-18
```

验收条件：
- [ ] 经过自愈流程后，TASK-18 所有指标达标
- [ ] 自愈过程记录到 `reports/self_healing_log.txt`

终止条件：Level 3 仍未达标则暂停，输出诊断报告等待人工介入

---

## 5. 项目目录结构

```text
voiceprint-sdk/
├── CMakeLists.txt
├── cmake/
│   ├── download_deps.cmake
│   └── download_models.cmake
├── include/voiceprint/
│   └── voiceprint_api.h
├── src/
│   ├── core/           # 音频预处理、VAD、FBank、Embedding、相似度
│   ├── storage/        # SQLite 存储
│   ├── manager/        # 业务逻辑层
│   ├── api/            # DLL 导出实现
│   └── utils/          # 错误码、日志
├── models/             # ONNX 模型文件
├── third_party/        # 所有第三方依赖
├── tests/
│   ├── unit/           # 单元测试
│   ├── integration/    # 集成测试
│   ├── benchmark/      # 性能测试
│   └── evaluation/     # 效果评估
├── examples/
│   ├── cpp_demo/
│   └── csharp_demo/
├── testdata/           # 测试音频
└── reports/            # 评估和基准测试报告
```
```

这个版本把任务拆分聚焦在技术方向和验收条件上，具体代码实现留给 AI 智能体自行完成。每个 TASK 都有明确的目标、技术方向、验收 checklist 和失败处理策略，形成闭环。