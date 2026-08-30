# 文档 3:INT8 量化 + TFLite 导出 — 清单与坑(Step 2)

## 1. 训练 → 导出链路

```text
PyTorch 训练 → torch.onnx.export → onnx2tf → TFLite
或:TF/Keras 训练 → TFLiteConverter 直接导出(推荐,INT8 工具链最顺)
```

- 推荐 **TF/Keras** 路线,量化工具(`tfmot`)成熟。

## 2. PTQ(训练后量化,起步)

```python
converter = tf.lite.TFLiteConverter.from_saved_model(m)
converter.optimizations = [tf.lite.Optimize.DEFAULT]

def rep():
    for inp in representative_set:   # ≥100~500 条目标说话人 mel/音素输入
        yield [inp.astype(np.float32)]

converter.representative_dataset = rep
converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
converter.inference_input_type = tf.int8
converter.inference_output_type = tf.int8
tflite = converter.convert()
```

- **representative dataset 必须来自目标说话人语音分布**,否则动态范围失准。

## 3. QAT(量化感知训练,精度不够时上)

- `tfmot.quantization.keras.quantize_model` 插入 FakeQuant,**重训 2–5 epoch** 恢复音质。
- 音频对量化敏感(尤其 attention softmax outlier),QAT 几乎是必选项。

## 4. 算子覆盖(上板生死线)

- TFLM 只支持算子子集,需 `MicroMutableOpResolver` 注册;openvela 的 int8 补丁 `0001-0003` 已加常用 INT8 算子。
- **最大坑:Length Regulator(按时长上采样)** 在 TFLM 无原生算子 → 对策:
  1. 把 duration predictor 挪到 **板端 CPU 预处理**(轻量 C 实现);
  2. TFLM 内只跑 conv/attention 核心,上采样结果以**固定张量**喂入;
  3. 或用 `GATHER` + `RESHAPE` 组合替代(需确认在支持列表)。
- 用 `flatc` / tflite 可视化 inspect 算子码,逐一对照 `micro_mutable_op_resolver.h` 支持表。

## 5. 布局与加速

- Conv1d → 转 **Conv2D(NHWC)** 以命中 **CMSIS-NN INT8** 核(大幅加速);
- 减少 Transpose / Reshape 频次;激活张量尽量原地复用。

## 6. 导出校验 & 上板

- PC 端 TFLite 解释器跑同一输入,对比 float 输出在容忍误差内;
- 嵌入:`tflm_tool -C model.tflite` 生成 C 数组编进固件,或放 LittleFS `/data` 运行时加载;
- 读出 `interpreter.arena_size` 确认 < 640KB SRAM。

## 7. openvela 相关入口(供核对)

- `apps/mlearning/tflite-micro/`(含 `tflm_tool.cc`、`operators/neon`、int8 补丁 `0001-0003`)
- `docs/zh-cn/edge_ai_dev/tflite_micro_integration.md`(集成指引)
