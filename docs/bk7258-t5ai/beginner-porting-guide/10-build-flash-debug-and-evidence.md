# 10 构建、打包、调试与证据

BK7258 只有一个维护者入口：

```bash
tools/bk7258/bk7258.py build|sdk|package|release|verify
```

先阅读[当前 SOP](../nuttx-port/bk7258-build-flash-debug-sop.md)。不要执行历史文档中的
product、framework、build-plan、postbuild 或独立 packer 命令。

## 构建

正常开发只需明确板型和启动模式；该板的 `openvela.conf` 会选择受维护的 CP/AP
配置与分区 CSV。下面是无签名直启诊断示例，不是正式发布命令：

```bash
tools/bk7258/bk7258.py build \
  --board t5ai_core --boot direct
```

新增物理板只增加板目录、CP/AP 配置、`openvela.conf`，以及确有不同拓扑时的
分区 CSV；不修改 Python 工具。xTS、性能和 drivercheck 等特殊构建才显式使用
`--cp-config`、`--ap-config` 和 `--partition`。

SDK 和 ARM 工具链版本来自 team manifest。地址、大小、CRC规则、存储拓扑和写入策略
来自本次选择的 CSV；Python、Make、CMake不保存第二份数值。成功构建会打印唯一的
`build-manifest.json`，它绑定本次原始镜像、ELF、配置、SDK、工具链、分区和信任根。

## 打包与验证

`direct` 仅用于无签名 bring-up/诊断。`package create --unsigned` 只接收 direct
构建生成并重新校验过的 manifest，由它唯一确定物理板、分区、SDK、最终 Flash 字节和
保留分区；该入口不能签名，也不能发布：

```bash
tools/bk7258/bk7258.py package create \
  --build-manifest "$DIRECT_MANIFEST" --unsigned \
  --output "$DIAGNOSTIC_PACKAGE"
```

唯一签名发布链是 `--boot mcuboot` 构建后使用其 manifest：

```bash
tools/bk7258/bk7258.py release full \
  --build-manifest "$MANIFEST" \
  --bl1-key "$BL1_PRIVATE" --mcuboot-key "$MCUBOOT_PRIVATE" \
  --version "$VERSION" \
  --base "$ACCEPTED_BASE" --base-sha256 "$ACCEPTED_BASE_SHA256" \
  --openssl "$OPENSSL" --output-dir "$RELEASE_DIR"
```

维护者不再手工枚举镜像、ELF、包内文件名、SDK 或多个计数器。版本中的
`+GENERATION` 是统一回滚代际；full release 必须等于构建时编译的 rollback floor。
发布命令先完成公钥根匹配、签名和公开信任验证，再原子产生包、单一 BKFIL 镜像、
构建证据和 `release.json`。失败不会留下看似可发布的目录。

```bash
tools/bk7258/bk7258.py verify package --package firmware.bkpack
tools/bk7258/bk7258.py verify trust \
  --package firmware.bkpack --openssl /path/to/openssl
```

`verify` 始终只读。普通 build/package/update 不格式化持久化介质。

这里的软件签名与回滚链不等于官方文档 1594 的 TEE/HUK 架构。当前 BK7258 不宣称
OP-TEE、Hardware Unique Key provisioning 或硬件不可篡改 Secure Boot。

## 硬件

硬件调试、串口、J-Link和下载不进入 `tools/bk7258/_lib`。统一使用
[`tools/windows-hardware-debug`](../../../tools/windows-hardware-debug/README.md)。

构建通过不等于板端通过，下载成功也不等于应用通过。板端结论必须保留精确包哈希、
写入范围、UART原始记录和必要的只读 J-Link证据。Chip erase、校准/持久化数据破坏、
OTP/eFuse、生命周期和 debug lock 不属于普通授权。
