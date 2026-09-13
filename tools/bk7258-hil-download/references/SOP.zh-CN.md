# BK7258 分板型 HIL 下载 SOP

## 1. 工具分层

这不是第二套通用串口工具，而是 BK7258 专用的 Flash 适配层：

- `windows-hardware-debug`：板卡无关的 UART 原始采集、手动复位同步、
  DTR/RTS 和受控 J-Link；明确不负责烧录。
- `bk7258-hil-download`：BK Loader 单文件/多段下载、板型约束、哈希与下载
  证据；烧录后调用前者收集启动证据。

板型 profile 位于 `board-profiles.json`。COM 号不写死，必须由当次 Windows
枚举和板端日志确认。

## 2. 当前 profile

| Profile | 下载/控制台拓扑 | BK Loader 输入 | 复位规则 |
|---|---|---|---|
| `aidk_ai_toy` | 同一 CH340 UART0 | 8 MiB 单 BIN，或有界已签名 CP/AP-B 段 | BK Loader 原子发送 `reset reboot`；禁止 RTS/DTR；必要时人工 K1 |
| `t5_board` | 同一 UART0 COM | 单 BIN 或受清单约束的多段 | USB 转串口支持同口内联 RTS；不借用 AIDK 软件重启参数 |
| `t5ai_core` | 下载/复位 COM 与 UART0 console 分离 | 单 BIN 或受清单约束的多段 | 在下载/复位 COM 上发 RTS；console COM 只采集；J-Link 独立授权 |

先只读查看解析后的 profile：

```bash
python3 <skill-dir>/scripts/bk7258_hil_download.py profiles
python3 <skill-dir>/scripts/bk7258_hil_download.py profiles --board t5board
```

## 3. 授权与目标冻结

执行写入前同时确认：

1. 当前会话中仍有效的授权覆盖该物理板、本次产物、端口和写入范围。
   明确的同板迭代授权可在其约定边界内沿用；一次烧录授权仍只覆盖一次写入。
2. 板型、下载 COM、控制台 COM 及两者关系已经确认。
3. 固件来源、类型、大小、SHA256 和写入范围已知；设备绑定镜像只用于其
   `device-id` 对应设备。
4. `.zip`/`.bkpack` 已由仓库流程完成 package/trust 校验并按 manifest 提取；
   它们不能直接交给 BK Loader。
5. 没有串口终端、采集任务或第二个下载进程占用下载 COM。
6. 本次使用新的 evidence 目录，不覆盖旧结果。

目标、端口、产物语义、范围或授权任一不清楚时停止。

## 4. AIDK AI Toy 单文件流程

```bash
python3 <skill-dir>/scripts/bk7258_hil_download.py preflight \
  --board aidk_ai_toy \
  --artifact-kind direct-full \
  --loader /mnt/c/path/to/bk_loader.exe \
  --image /absolute/path/to/FILE.bin \
  --port COM8 \
  --expected-size 8388608 \
  --expected-sha256 <64-hex>
```

预检必须显示：`transport=single`、8 MiB、offset `0x0`、`CH340`、
`reset reboot`、`hard-reset=0`、`fast-link=1`，且 RTS/DTR policy 为禁止。

授权后仅执行一次：

```bash
python3 <skill-dir>/scripts/bk7258_hil_download.py run \
  --board aidk_ai_toy \
  --artifact-kind direct-full \
  --loader /mnt/c/path/to/bk_loader.exe \
  --image /absolute/path/to/FILE.bin \
  --port COM8 \
  --expected-size 8388608 \
  --expected-sha256 <64-hex> \
  --evidence-dir /absolute/path/to/new-hil-run \
  --execute
```

BK Loader 必须独占“软件 reboot → Boot ROM”窗口。不要另起延时串口发送器，
不要补做 COM8 RTS/DTR。

## 5. AIDK 有界 CP/AP-B 多段流程

仅当当前设备的 inactive-slot/layout 证据已给出待写物理范围时，才可使用
`signed-segments`。每个 `--write-bound START-LENGTH` 是本次操作员授权的物理
范围，必须为正、4 KiB 对齐、互不重叠，并完全包含对应段；工具不会内置 AIDK
分区地址。AIDK 的已知容量为 8 MiB，越界范围会在打开硬件前被拒绝。每段必须
有按顺序匹配的 SHA256。此流程不证明签名或硬件通过，仍须先完成项目 trust
校验。

```bash
python3 <skill-dir>/scripts/bk7258_hil_download.py preflight \
  --board aidk_ai_toy --transport multi --artifact-kind signed-segments \
  --loader /mnt/c/path/to/bk_loader.exe --port COM8 \
  --segment /path/cp_b.bin@<cp-b-start>-<cp-b-length> \
  --segment /path/ap_b.bin@<ap-b-start>-<ap-b-length> \
  --segment-sha256 <cp-b-sha256> --segment-sha256 <ap-b-sha256> \
  --write-bound <cp-b-start>-<cp-b-length> \
  --write-bound <ap-b-start>-<ap-b-length>
```

预检命令应保留 profile 的 `--swrst 'reset reboot'`、`--hard-reset 0` 与
`--fast-link 1`；不得以 RTS/DTR 代替。范围必须来自本次设备证据，不能由本文
示例或工具推断。

## 6. T5-Board / T5AI-Core 多段流程

只使用已经通过项目 package/trust 校验并由 manifest 声明的物理范围。每个
`--segment` 使用 `PATH@OFFSET-LENGTH`，其中 LENGTH 必须等于文件实际大小；
`--segment-sha256` 与 `--segment` 按顺序一一对应。

```bash
python3 <skill-dir>/scripts/bk7258_hil_download.py preflight \
  --board t5_board \
  --artifact-kind signed-segments \
  --loader /mnt/c/path/to/bk_loader.exe \
  --port COM3 \
  --segment /path/bl_crc.bin@0x0-0x11000 \
  --segment /path/app_crc_flash.bin@0x11000-0x39000 \
  --segment-sha256 <bl-sha256> \
  --segment-sha256 <app-sha256>
```

开发态未签名分段使用 `direct-segments`；经过签名/清单验证的分段使用
`signed-segments`。完整镜像的 `*-full` 声明只能配 `--transport single`，
分段镜像的 `*-segments` 声明只能配 `--transport multi`。T5 profile 默认生成
经历史实板流程使用的 `--mainBin-multi`、`--uart-type OTHER`、`--reboot 1`、
`--fast-link 1`。范围重叠、长度不符、哈希不符或把 package 当 BIN 会在打开
硬件前失败。

T5 单文件完整/有界镜像必须显式选择 `--transport single`，并使用实际 manifest
给出的大小和哈希；不要把 AIDK 的固定 8 MiB 规则套到 T5。

```bash
python3 <skill-dir>/scripts/bk7258_hil_download.py preflight \
  --board t5_board --transport single \
  --artifact-kind signed-full \
  --loader /mnt/c/path/to/bk_loader.exe \
  --port COM3 --image /path/full.bin \
  --expected-size <manifest-size> --expected-sha256 <64-hex>
```

## 7. 下载后的通用调试交接

`debug-plan` 只生成命令，不打开串口、不复位。生成后检查 profile、端口、
动作和 regex，再运行其 `command` 字段。

AIDK 无控制线采集：

```bash
python3 <skill-dir>/scripts/bk7258_hil_download.py debug-plan \
  --board aidk_ai_toy --console-port COM8 --action capture \
  --output-dir /path/hil/boot \
  --expected-regex 'NuttShell' \
  --fail-regex 'HardFault|ASSERT|panic'
```

T5-Board 同口 RTS 同步采集：

```bash
python3 <skill-dir>/scripts/bk7258_hil_download.py debug-plan \
  --board t5_board --console-port COM3 --action serial-pulse \
  --output-dir /path/hil/boot \
  --expected-regex 'NuttShell' \
  --fail-regex 'HardFault|ASSERT|panic'
```

T5AI-Core 分离端口 RTS 同步采集：

```bash
python3 <skill-dir>/scripts/bk7258_hil_download.py debug-plan \
  --board t5ai_core --console-port COM11 --reset-port COM7 \
  --action serial-pulse --output-dir /path/hil/boot \
  --expected-regex 'NuttShell' \
  --fail-regex 'HardFault|ASSERT|panic'
```

RTS/J-Link 都属于目标控制，实际执行前必须有当前授权。T5 的 RTS 能力不能
外推给 AIDK。

## 8. 判定与证据

下载目录包含：

- `command.json`：profile、COM 角色、产物哈希/范围、实际命令和 override。
- `bkloader.raw`：本次 BK Loader 原始字节。
- `bkloader.txt`：便于阅读的解码文本。
- `result.json`：稳定 marker 判定和下载器退出码。

成功必须同时命中：

```text
Gotten Bus
EraseFlash ->pass
WriteFlash ->pass
Writing Flash OK
{All Finished Successfully}
```

任一失败 marker 或缺少成功 marker 都判失败。BK Loader 某些版本成功后仍可能
返回码 `1`，所以退出码只记录，不单独决定通过。追加式历史日志使用
`verify-log --last-session`，只判断最后一次尝试。

最终按四层报告：

| 门槛 | 必要证据 |
|---|---|
| `FLASH_PASS` | 新 `result.json` 通过，板型/端口/范围/哈希吻合 |
| `BOOT_PASS` | 新 UART 原始日志包含目标版本和必要服务，无 fatal marker |
| `FUNCTION_PASS` | 本轮要求的外设、协议或数据路径已在实板运行 |
| `PHYSICAL_PASS` | 人工确认屏幕、声音、灯光、运动等物理效果 |

失败如果发生在擦除/写入后，停止自动化并进入明确恢复流程；不要循环重刷掩盖
根因。下载成功但不启动，应转到 `windows-hardware-debug` 的 UART/J-Link 诊断。
