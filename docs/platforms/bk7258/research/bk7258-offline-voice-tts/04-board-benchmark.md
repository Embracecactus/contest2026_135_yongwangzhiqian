# 文档 4:bk7258 板端 GOPS / RTF / SRAM 实测基准方案(Step 3 放行闸门)

> **未执行声明：**本页是待实施的测试方案和报告模板；表格中的实测栏为空，当前仓库
> 不据此声明已有 TTS 板端基准或实时合成能力。

目标:把「1 GOPS / 640KB / 实时」从**推算**变**实测**,作为方案 A 放行闸门。

## 1. 环境与构建(唯一入口)

```bash
tools/bk7258/bk7258.py build
tools/bk7258/bk7258.py package
tools/bk7258/bk7258.py verify     # 烧录 + 串口
```

## 2. GOPS 实测(CMSIS-NN INT8 GEMM 微基准)

- 构造已知 `N×K×M` INT8 矩阵乘,用 bk7258 周期计数器(DWT/CYCCNT 或平台 timer)计时:

```text
GOPS = 2 * N * K * M / time_sec
```

- 再跑**一个真实 conv block 子图**(含 TFLM 调度开销)测实际吞吐;
- 对比理论 `480MHz × 1 MAC/周期 ≈ 1 GOPS`,记录折扣系数(供后续模型选型留余量)。

## 3. RTF 实测(真实模型)

- 用 `RecordingMicroInterpreter` 载文档 3 导出的 `.tflite`;
- 喂真实音素序列 → 记 `t_infer`(从输入到 mel 输出);
- `RTF = t_infer / audio_duration`,**必须 ≤ 1**(实时);
- **钉核**:把推理任务绑到 AP 480MHz 核(CPU1 亲和,N8-C2 已验证方式),锁频,确认其余核空闲。

## 4. SRAM 峰值

- 打印 TFLM `tensor_arena_size`,确认 < 640KB;
- 前后打 `heap` / `free`(AP 堆 640KB)查峰值;
- 超标则:减通道 / 逐层流式 / 缩小模型,回到文档 2 迭代。

## 5. 全链路听感

- 模型 mel → **Griffin-Lim(kissfft ISTFT)** → 16kHz PCM → `media_player` → `pcm0p` → 听;
- 确认无爆音 / 断续 / 明显拼接痕。

## 6. 报告模板(放行判据)

| 指标 | 目标 | 实测 | 判据 |
|---|---|---|---|
| GOPS(有效) | ≥ 0.5 | __ | 达标 |
| RTF | ≤ 1 | __ | 实时 |
| 峰值 SRAM | < 640KB | __ | 不溢出 |
| 听感 | 可辨目标说话人声音 | __ | 主观 |

## 7. 参考(已板级验证的事实)

- N14:PSRAM 16MiB、AP 堆 640KB / CP 256KB(`chips/bk7258/README.md`);
- N8-C2:任务可钉到指定核(CPU1 亲和),支持专用一核跑模型;
- BK7258_AUD:`pcm0p`,16kHz/16bit/单声道(`chips/bk7258/Kconfig:1440`)。
