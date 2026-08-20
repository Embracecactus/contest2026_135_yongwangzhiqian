# 10 构建、打包、调试与证据

BK7258 只有一个维护者入口：

```bash
tools/bk7258/bk7258.py build|sdk|package|verify
```

先阅读[当前 SOP](../nuttx-port/bk7258-build-flash-debug-sop.md)。不要执行历史文档中的
product、framework、build-plan、postbuild 或独立 packer 命令。

## 构建

构建必须明确给出 CP、AP、启动模式、分区 CSV 和并行数：

```bash
tools/bk7258/bk7258.py build \
  --cp-config board/bk7258/configs/t5ai_core_cp_base \
  --ap-config board/bk7258/configs/t5ai_core_ap_base \
  --boot direct \
  --partition board/bk7258/partitions/bk7258/bk7258_ab_onchip_persistent.csv \
  --jobs 8
```

SDK 和 ARM 工具链版本来自 team manifest。地址、大小、CRC规则、存储拓扑和写入策略
来自本次选择的 CSV；Python、Make、CMake不保存第二份数值。

## 打包与验证

`package create --unsigned` 只接收已经最终化的 Flash 字节。`--signed` 还要求显式
公私钥、BL1/BL2 ELF、版本与两个安全计数器，并在内部依次调用 trust、image 和
byte-preserving package 边界。

```bash
tools/bk7258/bk7258.py verify package --package firmware.bkpack
tools/bk7258/bk7258.py verify trust \
  --package firmware.bkpack --openssl /path/to/openssl
```

`verify` 始终只读。普通 build/package/update 不格式化持久化介质。

## 硬件

硬件调试、串口、J-Link和下载不进入 `tools/bk7258/_lib`。统一使用
[`tools/windows-hardware-debug`](../../../tools/windows-hardware-debug/README.md)。

构建通过不等于板端通过，下载成功也不等于应用通过。板端结论必须保留精确包哈希、
写入范围、UART原始记录和必要的只读 J-Link证据。Chip erase、校准/持久化数据破坏、
OTP/eFuse、生命周期和 debug lock 不属于普通授权。
