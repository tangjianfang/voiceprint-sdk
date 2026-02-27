# Reports 目录

测试运行后自动生成报告，存放于此目录。

---

## evaluation_report.txt — 效果评估报告

由 `evaluation_tests` 执行后生成，包含：

| 字段 | 说明 | 目标 |
|------|------|------|
| EER | 等错误率（Equal Error Rate） | ≤ 3% |
| minDCF | 最小检测代价函数 | 越低越好 |
| TAR@FAR=0.01 | FAR=1% 时的真接受率 | ≥ 95% |
| TAR@FAR=0.001 | FAR=0.1% 时的真接受率 | ≥ 85% |
| 测试集规模 | 试验对数 | 建议 ≥ 10000 |

运行方式：

```powershell
.\build\bin\Release\evaluation_tests.exe
# 报告输出到 reports/evaluation_report.txt
```

> 正式评估建议使用 VoxCeleb1-O 测试集或 AISHELL-3，参见 `testdata/README.md`。

---

## benchmark_report.txt — 性能基准报告

由 `benchmark_tests` 执行后生成，包含：

| 字段 | 说明 | 目标 |
|------|------|------|
| embedding_p50_ms | Embedding 提取 P50 延迟 | ≤ 150ms |
| embedding_p95_ms | Embedding 提取 P95 延迟 | ≤ 200ms |
| identify_1k_ms | 1:1000 检索延迟 | < 1ms |
| cold_start_ms | 冷启动时间（含模型加载） | < 1000ms |
| memory_rss_delta_kb | 1000 次循环 RSS 增长 | < 1024 KB |
| analysis_gender_p95_ms | 性别检测 P95 延迟 | ≤ 100ms |
| analysis_emotion_p95_ms | 情感识别 P95 延迟 | ≤ 150ms |
| diarize_realtime_factor | 说话人分段实时率（RTF） | ≤ 0.5x |

运行方式：

```powershell
.\build\bin\Release\benchmark_tests.exe
# 报告输出到 reports/benchmark_report.txt
```
