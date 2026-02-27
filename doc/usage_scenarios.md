# VoicePrint SDK 使用场景与集成指南

## 场景一：门禁/考勤系统（C++）

员工通过语音打卡，系统自动识别身份并记录考勤。

```cpp
#include <voiceprint/voiceprint_api.h>
#include <iostream>
#include <ctime>

class AttendanceSystem {
public:
    bool init() {
        return vp_init("models", "attendance.db") == VP_OK;
    }

    // 管理员注册新员工（录入 3~5 秒语音）
    bool register_employee(const std::string& employee_id, const std::string& wav_path) {
        int ret = vp_enroll_file(employee_id.c_str(), wav_path.c_str());
        if (ret != VP_OK) {
            std::cerr << "注册失败: " << vp_get_last_error() << std::endl;
            return false;
        }
        std::cout << "员工 " << employee_id << " 注册成功" << std::endl;
        return true;
    }

    // 多次注册提高精度（建议不同时段录 3 次）
    bool enhance_enrollment(const std::string& employee_id, const std::string& wav_path) {
        // 同一 ID 多次 enroll 会自动增量平均
        return vp_enroll_file(employee_id.c_str(), wav_path.c_str()) == VP_OK;
    }

    // 语音打卡：麦克风采集 → 识别 → 记录
    void clock_in(const float* audio, int sample_count) {
        char employee_id[256] = {};
        float score = 0;

        int ret = vp_identify(audio, sample_count, employee_id, 256, &score);
        if (ret == VP_OK && score >= 0.45f) {  // 门禁场景建议提高阈值
            time_t now = time(nullptr);
            std::cout << "[打卡成功] " << employee_id
                      << " 相似度: " << score
                      << " 时间: " << ctime(&now);
            // TODO: 写入考勤数据库
        } else {
            std::cout << "[打卡失败] 未识别到已注册员工" << std::endl;
        }
    }

    // 离职删除
    void remove_employee(const std::string& employee_id) {
        vp_remove_speaker(employee_id.c_str());
    }

    ~AttendanceSystem() { vp_release(); }
};
```

---

## 场景二：客服系统来电身份验证（C#）

呼叫中心通过声纹验证来电客户身份，替代传统密码/身份证验证。

```csharp
using System;
using System.Runtime.InteropServices;
using System.Text;

public class VoicePrintAuth
{
    [DllImport("voiceprint.dll", CallingConvention = CallingConvention.Cdecl)]
    static extern int vp_init(string model_dir, string db_path);
    [DllImport("voiceprint.dll", CallingConvention = CallingConvention.Cdecl)]
    static extern void vp_release();
    [DllImport("voiceprint.dll", CallingConvention = CallingConvention.Cdecl)]
    static extern int vp_enroll(string speaker_id, float[] pcm, int count);
    [DllImport("voiceprint.dll", CallingConvention = CallingConvention.Cdecl)]
    static extern int vp_verify(string speaker_id, float[] pcm, int count, out float score);
    [DllImport("voiceprint.dll", CallingConvention = CallingConvention.Cdecl)]
    static extern int vp_set_threshold(float threshold);
    [DllImport("voiceprint.dll", CallingConvention = CallingConvention.Cdecl)]
    static extern IntPtr vp_get_last_error();

    static string GetError() => Marshal.PtrToStringAnsi(vp_get_last_error()) ?? "";

    public void Initialize()
    {
        vp_init("models", "customers.db");
        vp_set_threshold(0.50f);  // 金融场景使用高阈值
    }

    /// <summary>
    /// 客户首次注册声纹（开户时柜面采集）
    /// </summary>
    public bool RegisterCustomer(string customerId, float[] voiceSample)
    {
        return vp_enroll(customerId, voiceSample, voiceSample.Length) == 0;
    }

    /// <summary>
    /// 来电身份验证
    /// </summary>
    public AuthResult VerifyCallerIdentity(string claimedId, float[] callAudio)
    {
        int ret = vp_verify(claimedId, callAudio, callAudio.Length, out float score);

        return new AuthResult
        {
            IsVerified = (ret == 0 && score >= 0.50f),
            Confidence = score,
            RiskLevel = score switch
            {
                >= 0.70f => "低风险 - 高度匹配",
                >= 0.50f => "中风险 - 基本匹配",
                >= 0.30f => "高风险 - 需二次验证",
                _ => "极高风险 - 身份不匹配"
            }
        };
    }

    public void Shutdown() => vp_release();
}

public record AuthResult
{
    public bool IsVerified { get; init; }
    public float Confidence { get; init; }
    public string RiskLevel { get; init; }
}

// 使用示例
// var auth = new VoicePrintAuth();
// auth.Initialize();
// var result = auth.VerifyCallerIdentity("CUST_10086", callerAudio);
// if (result.IsVerified)
//     Console.WriteLine($"身份验证通过，{result.RiskLevel}");
// else
//     Console.WriteLine("验证未通过，请提供其他身份证明");
```

---

## 场景三：智能会议纪要（C++）

会议录音自动区分发言人，生成带说话人标签的纪要。

```cpp
#include <voiceprint/voiceprint_api.h>
#include <vector>
#include <string>
#include <map>

class MeetingSpeakerDiarization {
    struct Segment {
        double start_sec;
        double end_sec;
        std::string speaker;
        float confidence;
    };

public:
    bool init(const std::string& meeting_db) {
        return vp_init("models", meeting_db.c_str()) == VP_OK;
    }

    // 会前：注册与会人员（可从历史会议 DB 自动加载）
    void register_participant(const std::string& name, const float* voice, int samples) {
        vp_enroll(name.c_str(), voice, samples);
    }

    // 会中：对每个语音片段进行说话人识别
    std::vector<Segment> diarize(const float* full_audio, int total_samples,
                                  int sample_rate = 16000) {
        std::vector<Segment> timeline;
        const int window_sec = 3;   // 3 秒窗口
        const int step_sec = 1;     // 1 秒步进
        const int window = window_sec * sample_rate;
        const int step = step_sec * sample_rate;

        for (int offset = 0; offset + window <= total_samples; offset += step) {
            char speaker[256] = {};
            float score = 0;

            int ret = vp_identify(full_audio + offset, window, speaker, 256, &score);

            double start = static_cast<double>(offset) / sample_rate;
            double end = static_cast<double>(offset + window) / sample_rate;

            Segment seg;
            seg.start_sec = start;
            seg.end_sec = end;
            seg.speaker = (ret == 0) ? speaker : "Unknown";
            seg.confidence = score;
            timeline.push_back(seg);
        }

        // 合并相邻相同说话人的片段
        return merge_segments(timeline);
    }

private:
    std::vector<Segment> merge_segments(const std::vector<Segment>& raw) {
        if (raw.empty()) return {};
        std::vector<Segment> merged;
        merged.push_back(raw[0]);

        for (size_t i = 1; i < raw.size(); ++i) {
            if (raw[i].speaker == merged.back().speaker) {
                merged.back().end_sec = raw[i].end_sec;  // 延展
            } else {
                merged.push_back(raw[i]);
            }
        }
        return merged;
    }
};

// 输出示例:
// [00:00 - 00:15] 张经理: ...
// [00:15 - 00:32] 李工: ...
// [00:32 - 00:45] 张经理: ...
```

---

## 场景四：智能家居语音控制（嵌入式/IoT）

只响应主人的语音指令，防止外人操控设备。

```cpp
#include <voiceprint/voiceprint_api.h>

class SmartHomeGuard {
    float security_threshold_ = 0.55f;

public:
    bool init() {
        if (vp_init("models", "home_speakers.db") != VP_OK)
            return false;
        vp_set_threshold(security_threshold_);
        return true;
    }

    // 注册家庭成员
    void add_family_member(const std::string& name, const float* voice, int samples) {
        vp_enroll(name.c_str(), voice, samples);
    }

    // 接收语音指令前先验证身份
    struct CommandAuth {
        bool authorized;
        std::string member_name;
        float confidence;
    };

    CommandAuth authenticate_command(const float* audio, int samples) {
        char speaker[256] = {};
        float score = 0;
        int ret = vp_identify(audio, samples, speaker, 256, &score);

        CommandAuth result;
        result.authorized = (ret == 0);
        result.member_name = speaker;
        result.confidence = score;
        return result;
    }

    // 敏感操作需要更高阈值（开门、关闭安防等）
    bool authorize_sensitive_action(const std::string& member,
                                     const float* audio, int samples) {
        float score = 0;
        vp_verify(member.c_str(), audio, samples, &score);
        return score >= 0.65f;  // 敏感操作阈值更高
    }
};

// 使用:
// auto guard = SmartHomeGuard();
// guard.init();
// guard.add_family_member("爸爸", dad_voice, dad_samples);
//
// auto auth = guard.authenticate_command(mic_audio, mic_samples);
// if (auth.authorized) {
//     execute_command(command);  // 执行 "打开空调" 等
// } else {
//     play_tts("抱歉，无法识别您的身份");
// }
```

---

## 场景五：在线教育/考试防作弊（Web 后端，C++ 服务）

考生答题时定期采集语音，验证是否本人。

```cpp
#include <voiceprint/voiceprint_api.h>
#include <string>
#include <chrono>

class ExamProctor {
public:
    bool init() {
        if (vp_init("models", "exam_proctoring.db") != VP_OK)
            return false;
        vp_set_threshold(0.40f);
        return true;
    }

    // 考前注册：考生朗读指定文字
    bool enroll_student(const std::string& student_id,
                        const float* audio, int samples) {
        return vp_enroll(student_id.c_str(), audio, samples) == VP_OK;
    }

    struct ProctorResult {
        bool is_same_person;
        float score;
        std::string alert_level;  // "PASS" / "WARNING" / "ALERT"
    };

    // 考中验证：每隔 5 分钟采集一次
    ProctorResult verify_student(const std::string& student_id,
                                  const float* audio, int samples) {
        float score = 0;
        int ret = vp_verify(student_id.c_str(), audio, samples, &score);

        ProctorResult result;
        result.score = score;

        if (ret == VP_OK && score >= 0.45f) {
            result.is_same_person = true;
            result.alert_level = "PASS";
        } else if (score >= 0.30f) {
            result.is_same_person = false;
            result.alert_level = "WARNING";  // 可能换人，发预警
        } else {
            result.is_same_person = false;
            result.alert_level = "ALERT";    // 疑似替考，通知监考老师
        }
        return result;
    }

    // 考试结束清理
    void cleanup_exam(const std::vector<std::string>& student_ids) {
        for (const auto& id : student_ids)
            vp_remove_speaker(id.c_str());
    }
};
```

---

## 场景六：Delphi / VB / Python 等语言调用

SDK 导出标准 C ABI，任何支持调用 DLL 的语言都能使用。

### Python (ctypes)

```python
import ctypes
import numpy as np

# 加载 DLL
vp = ctypes.CDLL("./voiceprint.dll")

# 定义函数签名
vp.vp_init.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
vp.vp_init.restype = ctypes.c_int

vp.vp_enroll.argtypes = [ctypes.c_char_p, ctypes.POINTER(ctypes.c_float), ctypes.c_int]
vp.vp_enroll.restype = ctypes.c_int

vp.vp_identify.argtypes = [
    ctypes.POINTER(ctypes.c_float), ctypes.c_int,
    ctypes.c_char_p, ctypes.c_int, ctypes.POINTER(ctypes.c_float)
]
vp.vp_identify.restype = ctypes.c_int

vp.vp_verify.argtypes = [
    ctypes.c_char_p, ctypes.POINTER(ctypes.c_float),
    ctypes.c_int, ctypes.POINTER(ctypes.c_float)
]
vp.vp_verify.restype = ctypes.c_int

vp.vp_get_last_error.restype = ctypes.c_char_p
vp.vp_release.restype = None

# 使用
vp.vp_init(b"models", b"speakers.db")

# 加载音频（16kHz float32）
import soundfile as sf
audio, sr = sf.read("speaker1.wav", dtype="float32")
audio_ptr = audio.ctypes.data_as(ctypes.POINTER(ctypes.c_float))

# 注册
vp.vp_enroll(b"alice", audio_ptr, len(audio))

# 识别
out_id = ctypes.create_string_buffer(256)
out_score = ctypes.c_float(0)
ret = vp.vp_identify(audio_ptr, len(audio), out_id, 256, ctypes.byref(out_score))
if ret == 0:
    print(f"识别结果: {out_id.value.decode()}, 分数: {out_score.value:.4f}")

vp.vp_release()
```

### Delphi

```pascal
const
  VP_OK = 0;

function vp_init(model_dir, db_path: PAnsiChar): Integer; cdecl; external 'voiceprint.dll';
procedure vp_release; cdecl; external 'voiceprint.dll';
function vp_enroll(speaker_id: PAnsiChar; pcm: PSingle; count: Integer): Integer; cdecl; external 'voiceprint.dll';
function vp_verify(speaker_id: PAnsiChar; pcm: PSingle; count: Integer; out score: Single): Integer; cdecl; external 'voiceprint.dll';
function vp_identify(pcm: PSingle; count: Integer; out_id: PAnsiChar; buf_size: Integer; out score: Single): Integer; cdecl; external 'voiceprint.dll';
function vp_get_speaker_count: Integer; cdecl; external 'voiceprint.dll';

// 使用:
// vp_init('models', 'speakers.db');
// vp_enroll('user1', @pcm_data[0], Length(pcm_data));
// vp_release;
```

---

## 阈值选择建议

不同场景对安全性和便利性的要求不同，建议调整阈值：

| 场景 | 推荐阈值 | 说明 |
|------|----------|------|
| 门禁考勤 | 0.45 ~ 0.50 | 中等安全，日常使用 |
| 金融验证 | 0.50 ~ 0.60 | 高安全，宁可误拒不误放 |
| 智能家居 | 0.40 ~ 0.50 | 常规指令偏便利 |
| 敏感操作 | 0.60 ~ 0.70 | 开门/转账等高安全操作 |
| 会议标注 | 0.30 ~ 0.40 | 偏便利，允许模糊匹配 |
| 考试防作弊 | 0.40 ~ 0.50 | 中等，配合人工复核 |

```cpp
// 运行时动态调整阈值
vp_set_threshold(0.50f);
```

## 集成注意事项

1. **DLL 部署**：`voiceprint.dll` 和 `onnxruntime.dll` 必须在同一目录
2. **模型目录**：`models/` 下需包含 `ecapa_tdnn.onnx` 和 `silero_vad.onnx`
3. **线程安全**：所有 API 线程安全，可在多线程环境直接调用
4. **生命周期**：一个进程只需调用一次 `vp_init`，程序退出前调用 `vp_release`
5. **音频要求**：16kHz 采样率，单声道，float32 [-1.0, 1.0]，最少 1.5 秒
6. **增量注册**：同一 ID 多次 enroll 会自动融合，注册 3~5 次可显著提升精度
7. **数据库持久化**：注册信息自动保存在 SQLite DB 中，重启后无需重新注册
