# BK7258 maintainer CLI

`bk7258.py` is the only tracked BK7258 maintainer entry.  It builds, signs,
verifies, packages and deploys artifacts; command implementations live in
`_lib/`.


## Responsibility and reproduction boundaries

The public CLI parses arguments, dispatches operations and prints results. The
existing modules own their domain rules; module count is not a cleanup target.

| Responsibility | Existing owner |
| --- | --- |
| Build/configuration, layout, SDK and toolchain | `build.py`, `layout.py`, `sdk.py`, `toolchain.py`; official build backend executes dependencies |
| Image encoding, `.bkpack` and verification | `image.py`, `package.py`, `trust.py` |
| Artifact identity/name, release-directory evidence, delivery and recovery policy | `product.py`; `release_product` receives an explicit verification callback, never CLI arguments or device control |
| Transport and console control | `deploy.py`, `deploy_usb.py`, `deploy_console.py`; not prerequisites for compilation |
| Static ownership checks | `layers.py`; kernel compatibility checks stay in the existing dedicated implementation |
| Voice provisioning/KWS and display asset preparation | `voice.py`, `voice_kws.py`, `display_assets.py`; product tools, not build policy |

`build.validate_provenance` is the shared public evidence validator. Consumers
do not call a private build helper or independently recalculate the schema.
This change moves the reviewed release rules; it does not claim every CLI
operation is now free of orchestration. Eager command registration/imports
remain unchanged; command-specific lazy loading is not a measured speedup or a
prerequisite for this iteration.

Team-authored firmware stays in the team repository. The local Dolphin sources
live in `app/dolphin`, while existing Shaniu services remain in `app/bk7258`.
IndexTTS/server/phone/training components do not acquire a NuttX link merely by
being part of the product; source ownership and build registration are separate.
No new Git repository or `ttsindex` manifest project is introduced here.
The `frameworks` link is current: `app/bk7258/CMakeLists.txt` includes
`frameworks/cmake/agent_framework.cmake` and `frameworks/cmake/tflm.cmake`.
The `external` link is retired and no longer exists in the manifest; do not
restore it, and do not add a placeholder directory for it.
The historical framework/FFmpeg patch and generated-source replacement chain
is retired; do not recreate it to satisfy a stale instruction.
For the explicit development remote override and official delivery distinction,
see the root [README](../../README.md).

## Independent factory software and target-bound complete flash

The current first-build command sequence is in the root [README](../../README.md#官方源码构建当前主入口).
`identity init --development` creates and reuses the caller's own BL1 and
MCUboot signer outside the repository. `build --development-identity` uses its
public keys for the complete BL1/BL2/CP/AP build. `release full
--development-identity --factory-init` signs a factory software `.bkpack`
without a historical base; its `release.json` states that target hardware
data is still required and it is **not** an 8-MiB flash image.

A complete 8-MiB factory BIN can only be materialized with authenticated
same-unit hardware data and an accepted evidence record. The AIDK release
policy declares `device-firstboot`: only a formal factory transaction grants
initialization, and the device generates its own TLS identity. An ordinary
mount failure never grants formatting. A software build proves none of the
physical first-boot, QR, K2, or playback outcomes.

The following contest-era recovery path remains for existing devices; it is
not the independent development entry. Two artifacts must not be conflated:

- **Same-unit recovery image** — what `release full` produces when a base is supplied. The
  `flash/*.bin` operator image is materialized from an accepted base that was
  read back from the same physical unit, so it carries that unit's
  device-bound persistent data. Re-flashing it on that unit restores a working
  `/data`, its BLE identity and the wake acknowledgement. Flashing it on
  another unit would copy device-bound state into that unit, so this artifact
  stays a same-unit recovery image: it is not a published general-purpose
  first-flash package and it must not be flashed on a different board.
- **Independent factory software** — signed public software and factory
  initialization intent; the protected hardware tail and unallocated range
  still require a same-target snapshot before creating one full BIN.

The maintained paths are:

1. **Owner-side release production** (only the release owner, with the
   private key PEMs and one accepted same-device base outside this
   repository):

   ```sh
   tools/bk7258/bk7258.py build --board aidk_ai_toy --boot mcuboot \
     --bl1-public-key <bl1-public.pem> --mcuboot-public-key <mcuboot-public.pem> \
     --openssl /usr/bin/openssl --rollback-floor <counter>
   tools/bk7258/bk7258.py release full --build-manifest <manifest> \
     --bl1-key <bl1.pem> --mcuboot-key <mcuboot.pem> \
     --version <MAJOR.MINOR.PATCH+GENERATION> --product shaniu \
     --artifact-id <safe-id> --base <accepted-base.bin> \
     --base-evidence <accepted-base.json> \
     --openssl /usr/bin/openssl --output-dir <new-dir>
   ```

   `release full` signs and materializes in one step; `flash/*.bin` is the
   flash input in step 2. Security counters increase monotonically; a released
   generation never decreases. One such release, with its artifact hashes and
   the layer each claim rests on, is recorded in
   `docs/verification/bk7258/2026-09-20-shaniu-637-full-image.md`.
2. **Flash that image back onto the same unit**:

   ```text
   bk_loader.exe download -p <com> -b 460800 -s 0x0 -i <operator>.bin \
     --swrst "reset reboot" --hard-reset 0 --reboot 1 \
     --uart-type CH340 --fast-link 1
   ```

   AIDK AI Toy uses its CH340 UART0 (`--fast-link 1` is required); T5-Board
   uses UART0 at 6000000 baud with the USB-UART RTS reset instead of
   `--swrst`. Multi-segment downloads use `tools/bk7258-hil-download/`
   (`preflight` then `run`), which enforces board port, size and SHA-256.
3. **Reviewers and judges without that unit**: build from source. The unsigned
   diagnostic chain in step 4 brings up a board without private keys, and a
   signed deployment uses the reviewer's own keys. The recorded 637 result is
   bound to one physical board; it is not a transferable product image.
4. **Unsigned diagnostic chain** (no keys; for bring-up only):

   ```sh
   tools/bk7258/bk7258.py build --board aidk_ai_toy --boot direct
   tools/bk7258/bk7258.py package create --build-manifest \
     out/bk7258/aidk_ai_toy/app__openvela_ap/<layout-id>/releases/direct/build-manifest.json \
     --unsigned --output <pkg>
   tools/bk7258/bk7258.py package extract --package <pkg> --output <dir>
   ```

   The extracted `images/{boot,cp,ap,pair}.bin` are downloaded per board
   profile (`tools/bk7258-hil-download/references/SOP.zh-CN.md`). A direct
   image has no BL2/signature and no persistent-data snapshot: after the
   first direct flash, `/data` is unformatted and
   `BK7258 FINALINIT FAIL: persistent data at /data ...` is the expected
   first-boot report; the boot continues and the display/Agent/peripherals
   start, but configuration persistence and BLE provisioning stay
   unavailable. Re-flash with the signed operator image from step 2 for the
   intended product state on that same unit.

Detailed signing, layout and persistence rules: the build/flash/debug SOP at
`docs/platforms/bk7258/nuttx-port/bk7258-build-flash-debug-sop.md`.

## Model development is optional for firmware builds

The current public model and metadata live in `app/bk7258/models`; Android's
builtin WKM lives in its own assets directory. Both are versioned in the team
repository. No separate training repository or NuttX linkfile is required.
Private training recordings/candidates are excluded from public publication.
Training inputs must have an authorized local dataset manifest and preserved
source/voice split; exporting a candidate is not board or human acceptance.

Reuse the maintained commands and inspect their current arguments:

```sh
tools/bk7258/bk7258.py voice kws audit --help
tools/bk7258/bk7258.py voice kws train --help
tools/bk7258/bk7258.py voice kws evaluate --help
```

The documented training runs used TensorFlow 2.15.1 in a separate environment.
Ordinary firmware builds use the existing model and do not require TensorFlow,
a training corpus, private voice-cloning weights or cloud credentials.
See `.agents/skills/edge-wakeword-training/` for the reusable workflow and the
contest report for builtin-versus-App-activated model identities.

## Voice command support matrix

The `voice` commands target different peers; only the KWS model tooling is on
the current product path.

| Command | Peer / protocol | Current official firmware | Status |
| --- | --- | --- | --- |
| `voice kws audit` / `train` / `evaluate` | Host-side model tooling; the exported WKM1 package is consumed on the board by `app/bk7258/bk7258_voice_wake_package.c` and the trigger backend | Supported | Current |
| `voice provision` | CP console command `bkvoice provision`, waiting for `BKVOICE PROVISION READY` | The console command is not present in the current firmware sources | Historical: kept to reproduce the recorded provisioning runs; not a current step |
| `voice pairing --direct-cloud` / `--resume` | CP console `bkprov supply` (`bkprov-v1` RPC to the AP provisioning store); writes the owner activation file | Supported (added 2026-09-20) | Current |
| `voice pairing` without `--direct-cloud` | The retired Gateway console protocol | The console command is gone | Historical |
| `voice console-enrollment` | Host-only file writer for the retired Gateway console; it never opens a serial port | Not applicable | Historical Gateway-era utility |

Current device identity and network provisioning use the BLE `provision-v1`
service implemented by the Android companion App
(`app/bk7258/bk7258_provision_gatt.c`, `app/bk7258/bk7258_provision_owner.c`,
`docs/platforms/bk7258/shaniu-provision-security.md`). Do not restore the
retired console runtime to make the historical commands work again, and do not
present them as the current enrollment step.

## Source-layer gate

Every `bk7258.py build` runs the board/chip/app ownership gate before it
configures either role.  It can also be run directly:

```sh
python3 tools/bk7258/bk7258.py verify layers
```

The gate rejects raw Beken SDK headers, calls and types in `boards/bk7258` or
`app` (including `app/dolphin`); chip-to-board dependencies; physical pin/bus ownership in app;
CP-only Kconfig symbols nested in AP-only menus (and the reverse); and new
product GATT/UUID policy in the chip layer.  Product protocol belongs in app,
physical and calibration facts belong in board, and SDK/controller mechanics
belong in chip.

The current working-tree gate also scans CMake/Make and related scripts under
`boards/bk7258` and `app` for private SDK paths, libraries, symbols and linker
wrapping. It is a static lexical check, not a CMake interpreter or proof of ELF
resolution, runtime behavior or every Kconfig dependency. New app subdirectories
are enumerated; dynamic/generated dependencies still need targeted review.

`layer_exceptions.json` contains only hash-bound legacy product-protocol files.
Changing one invalidates the gate and requires a deliberate layer review; it
is not a wildcard allowlist.  `tests/host/bk7258/test_bk7258_layers.py` injects
each forbidden dependency and verifies that the gate fails closed.

## OTA deployment

`deploy` streams a signed CP/AP OTA package through the native USB CDC port,
then uses the CH340 CP console to reboot and confirm the accepted generation.
It is the host peer of the chip-level `BK7258_OTA_SOURCE_USB` source.

```sh
python3 tools/bk7258/bk7258.py deploy --inspect-only --package FILE \
  [--expected-board NAME] [--expected-version V] [--expected-counter N]

python3 tools/bk7258/bk7258.py deploy --package FILE \
  [--ota-port PORT] [--control-port PORT]
```

Use `--status-only` or `--reboot-only` with `--expected-version`,
`--expected-counter`, and a CH340 control port to check an accepted package.
`--control-port none` stages the pair without rebooting it.  Python 3 and
`pyserial` are required only when a serial port is opened.

The signed catalog may be scoped with `--expected-board`; product automation
must always supply its selected physical board.

### Historical Gateway release catalog (not used by current Shaniu)

After `release product` has produced one device-bound delivery ZIP, export the
metadata consumed by the authenticated Android console with:

```sh
python3 tools/bk7258/bk7258.py release gateway-catalog \
  --delivery /releases/aidk-v18.6.390+450.zip \
  --openssl /usr/bin/openssl \
  --output /private/shaniu/firmware-releases.json
```

Repeat `--delivery` for additional device releases. The command verifies each
complete delivery and its embedded OTA signatures in the same process, then
creates a new, fsynced mode-0600 `shaniu.firmware-release-registry/1` file. It
refuses deliveries without a verified signed OTA component, duplicate releases,
more than 32 entries, and an existing output path.

The registry contains only the accepted device ID, target/source versions,
board/layout identities, exact signed `catalog.json` SHA-256, and OTA package
size/SHA-256. It contains no package path, URL, firmware bytes, credentials, or
signing material. Supplying it to Gateway enables only release-list display;
the firmware update mutation remains disabled until the board confirmation and
progress protocol is implemented.

## Shaniu display assets

`package eye-pack` turns the reviewable logical-eye JSON into one deterministic,
bounded `.bkep` product asset.  `verify eye-pack` performs read-only structural,
CRC, and content-bound checks before that file is copied to the AIDK soldered
SD NAND.  It does not package the asset into CP/AP firmware or claim signed
publisher trust.

```sh
python3 tools/bk7258/bk7258.py package eye-pack \
  --source app/bk7258/assets/display/shaniu-default-v1.json \
  --output out/shaniu-display/shaniu-default-v1.bkep \
  --preview-dir out/shaniu-display/previews

python3 tools/bk7258/bk7258.py verify eye-pack \
  --package out/shaniu-display/shaniu-default-v1.bkep
```

The source, SD NAND layout contract, visual-state list, and binary format are
documented under `app/bk7258/assets/display/`.

## Shaniu owner console enrollment

`voice console-enrollment` only writes a private enrollment document for the
owner's phone. It does not contact a board or Gateway and never mutates an
existing Gateway registry. With `--access-output`, it also creates a matching
single-grant registry for a new one-device Gateway setup. It never creates the
independent device certificate binding. The bearer token is read only from an
existing regular POSIX mode-0600 file, never from command arguments or
environment. The command currently refuses Windows: it will remain unavailable
there until an audited owner/DACL private-file implementation exists.

```sh
python3 tools/bk7258/bk7258.py voice console-enrollment \
  --device-id aidk-1 --https-origin https://gateway.example:8443 \
  --spki-pin 'sha256/BASE64_SPKI_SHA256' \
  --token-file private/console-token --expires-at-ms 1893456000000 \
  --access-output private/aidk-1-console-access.json \
  --output private/aidk-1-console-enrollment.json
```

On POSIX, the output is an O_EXCL-created, flushed and fsynced mode-0600
`shaniu.console-enrollment/1` JSON document with the device ID, HTTPS origin,
one to eight canonical SPKI SHA-256 pins, expiry, and access token. Its exact
fields are `protocol`, `device_id`, `gateway_origin`, `certificate_pins`,
`access_token`, and `expires_at_ms`. Preserve it as private owner material;
CLI status output deliberately omits the token. Both outputs are new,
O_EXCL-created, fsynced mode-0600 files. If either target already exists, review it
instead of overwriting it.

Before using the pair, load the device's mTLS certificate binding into the
Gateway `shaniu.device-bindings/1` registry, then start the Gateway with that
registry and the generated `shaniu.console-access/1` file. The enrollment and
access files contain the same `device_id` and future `expires_at_ms`; the latter
stores only the SHA-256 digest of the former's token. For a multi-device or
rotated deployment, merge reviewed grants into an operator-owned registry
outside this command rather than asking it to overwrite live authorization.

## PC workbench control client (development)

`workbench status` and `workbench info` are the first read-only product-client
operations. They use TLS and the existing SDC1 protocol with an independently
authorized PC key. The native USB product owner and phone-to-PC credential
exchange are still being integrated; this is not a completed resource workbench
or a currently verified physical-device workflow.

```sh
python3 tools/bk7258/bk7258.py workbench status \
  --port NATIVE_PORT \
  --certificate device-certificate.pem \
  --certificate-sha256 TRUSTED_64_LOWERCASE_HEX \
  --pc-key-file independent-pc-key.bin
```

The public certificate and SHA256 must match the owner's trusted device identity,
not two unchecked values learned from the same new USB connection. The key input
is exactly 32 binary bytes for the PC principal. The phone's owner key is a
different identity and is rejected by the PC endpoint. These explicit developer
inputs are not a finished credential-export or secure desktop-storage flow.
Protect the key file; the tool reads it without writing it or printing its bytes.

The client verifies the certificate chain using only the supplied trust anchor,
then the exact negotiated leaf fingerprint before sending AUTH. USB VID/PID
classification only rejects a wrong transport; it does not authorize a device.
Only the selected native port is opened, with no UART/Shell fallback, mode switch,
reset, claim, firmware update or automatic SDC1 replay. Pyserial 3.5 is the
currently exercised adapter version. The Windows native handle path is reused
from the maintained USB opener; its opening retries are bounded by the supplied
opening deadline.

Output is public JSON, with port diagnostics on stderr. TLS/protocol errors close
the client and leave results unconfirmed. `--timeout` defaults to 10 seconds and
is checked at I/O/TLS boundaries; the shared synchronous driver still has its
existing 100-ms read and 5-second write bounds, so this is not a measured device
response SLA. No raw serial port or physical USB device is touched by the host
unit tests. Resource installation, scene operations, browser UI and task events
will use this same authenticated client after their service bindings are ready.

### 电脑端加密凭据配置（Windows / WSL→Windows）

开发者已通过独立授权流程取得 PC 凭据时，可以将 Key、可信设备证书与指纹一起保存到
当前 Windows 用户的 DPAPI 配置。此入口不生成 owner、不复制手机 Keystore、不验证
设备是否已接受授权；首次配对交换仍待接通。导入的原 Key 文件不会自动删除或修改。

```bash
python3 tools/bk7258/bk7258.py workbench save-profile \
  --profile /path/to/new-device.spc \
  --certificate /path/to/device.pem \
  --certificate-sha256 TRUSTED_LOWERCASE_SHA256 \
  --pc-key-file /path/to/independent-pc-key.bin

python3 tools/bk7258/bk7258.py workbench status \
  --port NATIVE_PORT --profile /path/to/new-device.spc
python3 tools/bk7258/bk7258.py workbench info \
  --port NATIVE_PORT --profile /path/to/new-device.spc
```

目录须存在，目标配置须不存在；不会覆盖旧配置。写入前先加密，临时文件也只有密文；
发布后解密回读核对。若发布后回读失败，文件保留且返回失败，不自动删除或重试覆盖；
该机制不承诺任意掉电下持久化。只有后续真实认证/命令成功，才获得设备接受凭据的证据。

`--profile` 不可与状态查询的明文凭据选项混用。配置过大、损坏、身份不匹配或系统
保护不可用时，命令在打开串口前失败，不回退明文，也不访问 UART/切换 USB 模式。
现有明确指定三项凭据的开发查询入口保留，未作为自动降级路径。

系统保护由 Windows PowerShell 的固定非交互脚本调用
[ProtectedData/DPAPI](https://learn.microsoft.com/en-us/dotnet/api/system.security.cryptography.protecteddata)
实现，使用 `CurrentUser` 和固定应用上下文，非 `LocalMachine`。数据只经过标准输入/
捕获的标准输出，不放命令行参数或诊断。当前无独立 Linux/macOS 系统密钥库后端；
WSL 可调用当前 Windows 用户的 PowerShell。没有 Windows 保护服务就明确失败。
这属于登录用户的保护边界，不抵御同一账户内恶意进程或管理员；Python/.NET 内部副本
不承诺完全擦除。文件系统需要支持同目录硬链接的独占发布，不支持时失败而非覆盖。

#### 离线配对交换（S77，原生入口已接线，实板待验）

Windows 或现有 WSL→Windows DPAPI 环境可生成公开请求和受保护的临时状态：

```sh
python3 tools/bk7258/bk7258.py workbench pair-start \
  --request pc-request.spq --pending pc-pending.spp --allow resources scenes
```

权限必须显式选择：`resources`、`scenes`、`tasks`、`diagnostics`。输出请求摘要供
后续手机核对；请求有效期 10 分钟，双方系统时间需一致。`pc-pending.spp` 绑定当前
Windows 用户，只留在本机；不会产生明文私钥文件。这里不打开串口、不授权设备。

把公开的 `pc-request.spq` 传到手机。在原生 App「设置 → 电脑授权」读取状态，再选择
「导入电脑配对请求」。核对手机显示的完整请求摘要与电脑输出一致，确认所列权限；
如果已有授权，本次授权会替换它。手机先保存加密响应，然后才通过当前认证会话提交。
设备持久回执和授权回读一致后，「导出加密配对响应」才可用。文件选择或页面重建导致
断线时重新查询，不会自动重发授权。使用「复制设备证书摘要」取得电脑导入需要的 pin。

将导出的响应传回电脑后执行：

```sh
python3 tools/bk7258/bk7258.py workbench pair-finish \
  --pending pc-pending.spp --response phone-response.spr --profile device.spc \
  --confirm-device-sha256 <从可信手机界面核对的64位小写十六进制证书摘要>
```

摘要不能照抄不可信响应。所有输出路径必须不存在；导入不覆盖已有配置。导入成功仅
表示受保护配置保存成功，仍需真实设备鉴权，不能据此宣称设备已授权。临时状态保留
供有效期内恢复，不承诺一次性消费或安全擦除。非 Windows 的系统密钥库尚未实现。

手机本机只保存加密响应及公开事务元数据，按设备隔离；没有明文 PC Key 持久化。
「移除本机配对记录」不撤销设备授权。若本机记录过期或丢失，应明确查询/撤销旧授权
后再配对，不能假设关掉页面或删除文件已撤销。S77未安装到真实手机；上述原生接线
仅有生产 Session/协议主机测试及模拟器文件确认/取消证据。实际授权→导出→PC导入→
原生USB鉴权仍待纵向实板验收，USB产品owner与S66 TLS门槛仍未关闭。

### Explicit task events and status (PTE1 / PTS1)

`workbench task-event` sends a caller-supplied task result through the same
pinned TLS connection and independent PC credential. The device must have granted
`tasks`; a profile alone does not grant access. This is an explicit sender, not a
background process monitor: the invoking build/training/render program determines
its real exit result. No command text, source files or raw logs are transmitted.

The native USB product owner and physical notification path are not yet bound or
verified. These commands currently have host Python-to-production-C TLS evidence;
they are not instructions to use the CH340 debug port or enable MSC.

Once that product transport is available, the intended sequence is:

```sh
python3 tools/bk7258/bk7258.py workbench task-status --port COM_NATIVE --profile pc.profile
python3 tools/bk7258/bk7258.py workbench task-event --port COM_NATIVE --profile pc.profile \
  --task-id 00112233445566778899aabbccddeeff --event-sequence 1 \
  --state start --ttl-ms 60000 --progress 0
# Only after the caller's actual task succeeds:
python3 tools/bk7258/bk7258.py workbench task-event --port COM_NATIVE --profile pc.profile \
  --task-id 00112233445566778899aabbccddeeff --event-sequence 2 \
  --state success --ttl-ms 60000 --progress 100
python3 tools/bk7258/bk7258.py workbench task-status --port COM_NATIVE --profile pc.profile
```

The example ID and sequence are illustrative. Generate a fresh 128-bit ID per
new task; read the current event sequence and choose a larger value across all
tasks of this authorization. Conflicts are rejected, not silently retried.
`--state` also accepts `progress`, `failure`, `canceled`; missing progress means
unknown (except `start`, which requires zero). TTL is remaining receiver lifetime
in milliseconds, 1..4294967295; subtract caller-side queue age before sending.
Progress updates are limited by the device to at most one per second after the
first progress. Ensure a live task is refreshed before its TTL expires; expiration
is not success and cannot be undone by late progress. Do not reuse old task IDs.

Each operation has one absolute timeout across all fragments. Failed writes close
the connection and leave the outcome unconfirmed; reconnect and query before a
manual retry. An exact duplicate last event is idempotent and does not renew TTL.
The client never replays a write automatically. Transfer cancellation and a task's
`canceled` event have different meanings. `accepted: true` is only the event ACK;
`feedback_pending` in the snapshot is not a rendered/displayed receipt. Snapshot
reads recheck the event identity and reject concurrent mixed snapshots.

## PC eye-pack jobs (RJI1 firmware required)

The existing `workbench` entry now supports file-level installation over the
native CDC connection. Use the independently authorized PC profile and a
RESOURCES grant. Replace `NATIVE_CDC_PORT` with the verified native device
port; the CH340/UART maintenance port is not this product channel. A protected
profile must be opened on its supported OS/account; no phone Keystore is copied.

```text
python tools/bk7258/bk7258.py workbench resource-upload --port NATIVE_CDC_PORT --profile pc.profile --file eyes.bkep --receipt eyes-job.json --ttl-ms 60000 --timeout 30
python tools/bk7258/bk7258.py workbench resource-status --port NATIVE_CDC_PORT --profile pc.profile --receipt eyes-job.json
python tools/bk7258/bk7258.py workbench resource-resume --port NATIVE_CDC_PORT --profile pc.profile --file eyes.bkep --receipt eyes-job.json --timeout 30
python tools/bk7258/bk7258.py workbench resource-cancel --port NATIVE_CDC_PORT --profile pc.profile --receipt eyes-job.json
```

The first command validates size/type, freezes an input copy, and saves a new
local receipt before BEGIN. The receipt contains public device/job identifiers
and a local file SHA256, never a credential. Existing receipt files are not
overwritten. Its successful write/sync is a precondition to sending; filesystem
and OS power-loss durability is not promised. Device format/CRC validation is
still authoritative; the local SHA256 is not a returned device hash.

A command's `--timeout` is an absolute host-operation budget (existing maximum
120 seconds). `--ttl-ms` applies only to a new job; resume/query never renews it.
The examples are caller budgets, not measured device throughput guarantees.
One 4-KiB chunk is queued at a time, in compatible 32-byte SDC1 APPEND frames.

`installed: true` is reported only from DONE after actual device installation;
upload does not set the default or confirm rendering. An ACK only accepts work.
On interruption, query the saved receipt, then explicitly resume the same file
if appropriate. Resume uses the device's confirmed written offset and never
sends another BEGIN. A different device, epoch, file or superseded volatile
receipt fails closed. Device reboot can lose the result: unknown is not success
and does not trigger automatic re-upload. Local program exit does not confirm
remote cancellation; query for CANCELED. A committing job may reject cancellation.

Host coverage includes the actual Python TLS client, SDC1 session, native job
worker and installer, with test TLS server/mount/scheduling boundaries. Embedded
TLS/PC-grant guard tests remain separate. Physical USB, a browser workbench,
phone default-selection UI, persistent device receipts and production throughput remain
pending; no board deployment is implied by these host commands/tests.

### Limited expression trials from the PC

The existing independently authenticated PC profile needs `scenes` permission.
`trial-start` without a filename tries an expression in the **currently selected
pack**. Add `--pack-filename shaniu-upload-v1.bkep` to temporarily try an already
installed pack returned by a completed upload. This is an installed device name,
not a local file path. Neither operation persists a new default. TTL is explicitly supplied
by the caller, positive milliseconds up to the existing u32 protocol limit;
it starts at acceptance and includes queue time. No duration is silently chosen.

```sh
python tools/bk7258/bk7258.py workbench trial-status --port NATIVE_CDC_PORT --profile pc.profile
# Use the exact latest id from that read; 0 below is only a fresh-device example.
python tools/bk7258/bk7258.py workbench trial-start --port NATIVE_CDC_PORT --profile pc.profile --expected-trial-id 0 --operation-id 0102030405060708 --expression happy --ttl-ms 5000
python tools/bk7258/bk7258.py workbench trial-status --port NATIVE_CDC_PORT --profile pc.profile
# Replace 1 with the actual active trial id; use a new nonzero operation id.
python tools/bk7258/bk7258.py workbench trial-cancel --port NATIVE_CDC_PORT --profile pc.profile --expected-trial-id 1 --operation-id 0102030405060709
```

Use a fresh nonzero 16-digit lowercase hex operation ID for each new intent;
retain it for an explicit retry of the exact same request after querying. No
request is automatically replayed. A stale expected trial ID is rejected.
`accepted` / `completion_verified: false` only acknowledges the request;
`trial-status` reports pending/rendering/active/restoring/terminal states.
`device_reports_rendered` means the service reports ACTIVE, not an independent
physical screen measurement. Only `cancel_confirmed` reports CANCELED. Closing
the local connection is not remote cancellation; the original TTL still applies.
A read rechecks the header to reject mixed snapshots and reports an unknown clock
as `remaining_ms: null`. Neither queries nor retries renew the trial's TTL.

To try an installed pack, read `trial-status` first and supply that exact latest
ID along with a fresh operation ID (the following ID0 is only an example):

```sh
python tools/bk7258/bk7258.py workbench trial-start --port NATIVE_CDC_PORT --profile pc.profile --expected-trial-id 0 --operation-id 0102030405060710 --expression happy --ttl-ms 5000 --pack-filename shaniu-upload-v1.bkep
```

This uses ETC2 on the existing authenticated scene channel. Canonical names
are validated before credentials/port access, and72 bytes are staged in32-byte
chunks. Use the same status/cancel commands as expression-only trials. Older
firmware rejection remains unconfirmed/error; the client never falls back to
the default pack, retries automatically, or activates an uploaded pack. A missing
installed name can be accepted then report failed; check status, not only ACK.
Host client-to-production-renderer tests do not prove physical USB/screens.


### Versioned default selection from the PC

A compatible device exposes ESC1/ESS1 and the independent PC profile needs
`resources` permission. `scenes` alone permits trials, not a persisted default.
These host commands do not establish that a connected board has been updated.

Start with `default-status`. It reads the **latest selection job**, without disk
I/O. Its filename may describe a pending request or an older result. To obtain
current persisted selection, explicitly submit `default-refresh`, then query
until that operation reports `refresh_complete: true`. Use its returned job ID
and revision when issuing `default-set`; a concurrent change is rejected.

Replace the uppercase placeholders below with actual returned values. Generate
a fresh nonzero32-character lowercase hex nonce per new intent (for example,
`python3 -c 'import secrets; print(secrets.token_hex(16))'`). Retain the exact
public epoch, nonce, expected ID, revision and filename before sending; these
are operation identifiers, not authentication credentials.

```sh
python tools/bk7258/bk7258.py workbench default-status --port NATIVE_CDC_PORT --profile pc.profile
python tools/bk7258/bk7258.py workbench default-refresh --port NATIVE_CDC_PORT --profile pc.profile --selection-epoch EPOCH_FROM_STATUS --selection-nonce REFRESH_NONCE_32_HEX --expected-selection-id JOB_ID_FROM_STATUS
python tools/bk7258/bk7258.py workbench default-status --port NATIVE_CDC_PORT --profile pc.profile --selection-epoch EPOCH_FROM_STATUS --selection-nonce REFRESH_NONCE_32_HEX
# Only after that refresh completes, use its returned id and revision:
python tools/bk7258/bk7258.py workbench default-set --port NATIVE_CDC_PORT --profile pc.profile --selection-epoch EPOCH_FROM_STATUS --selection-nonce SET_NONCE_32_HEX --expected-selection-id JOB_ID_FROM_REFRESH --expected-default-revision REVISION_FROM_REFRESH --pack-filename shaniu-upload-v1.bkep
python tools/bk7258/bk7258.py workbench default-status --port NATIVE_CDC_PORT --profile pc.profile --selection-epoch EPOCH_FROM_STATUS --selection-nonce SET_NONCE_32_HEX
```

`accepted` with `completion_verified: false` is only acceptance. Status separates
`device_reports_saved`, `device_reports_rendered`, `selection_complete`, and
errors. These are device reports, not independent physical screen measurements.
A completed refresh reports the observed version, not a new save or render.
The result is volatile; reboot, authorization change or a superseding operation
can make it unconfirmed. A stale expected nonce/epoch/ID closes the client and
sends no write. After transport failure, query before considering an explicit
retry of the exact same request. There is no automatic replay or fallback.

To cancel before commit, use `default-cancel` with the same epoch, the current
job ID and a **new** selection nonce; confirm `cancel_confirmed` via status.
Commit may reject cancellation. To retry a reported volume release failure,
use `default-recover` with those same target fields and a new nonce. Recovery is
close-only: it can clear `release_error` while the original job remains UNKNOWN;
it does not repeat the save or render. Neither closing the CLI nor an ACK proves
remote cancellation/recovery. Querying status never implicitly refreshes disk.

Host coverage includes real client framing -> native controller/worker/store/
renderer using a pipe in place of TLS/USB. Existing production TLS/permission
checks remain separate; browser UI, phone integration, physical USB, migration
of active/2 markers and board acceptance remain pending.


### Resource transfer integration for a local workbench

The shared Python `workbench.run(args, observe=..., cancel_requested=...)`
entry now forwards optional local hooks to resource operations. `observe`
receives a detached public device snapshot; `cancel_requested` reads a local
intent. Both callbacks must be short, nonblocking and must not call the client.
The owner remains the existing single USB/TLS client. A browser adapter must
publish snapshots and set cancellation intent without opening another port or
sending concurrent commands on this client.

Cancellation is checked before a new BEGIN and between bounded protocol
operations. Before BEGIN it sends no mutation and creates no receipt. After
BEGIN it sends at most one cancel, stops producing APPEND/FINISH, and waits for
the device's canceled state under the original deadline. A terminal installation
already observed as done wins over a late local cancel intent. Lost ACKs and
commit-time rejection remain errors/unknown; they are not reported as canceled.
The saved receipt remains available for explicit result queries.

The browser entry is described below. This does not make a 4096-byte
resource request interruptible in the middle of CONFIG staging, guarantee a
physical USB response latency, or renew the configured deadline/receiver TTL.

### 本机浏览器工作台

沿用已由手机授权并保存在本机的独立 PC profile。启动不会打开串口；页面中的
明确操作才借用 profile、校验证书并连接指定的原生 USB。不要填 CH340 调试口。

```sh
python tools/bk7258/bk7258.py workbench serve --port NATIVE_CDC_PORT --profile pc.profile --workbench-dir new-workbench-receipts
```

打开终端给出的 `http://127.0.0.1:端口/#本次访问口令`。页面支持设备状态/版本、
眼睛包导入、按回执查询/续传/取消、已安装包的限时试用与默认选择，以及导出
本次公开结果。使用原生文件选择器选择 BKep；导入不自动激活，受理也不等于
完成。设默认前先读取默认状态、提交刷新，再读取到明确版本；保存后再回读。
已安装素材可从目录选择，也可手填或从成功导入结果取得；暂不做文件版本比对。
先点“读取目录状态”，再点“刷新目录”，受理后再次读取状态。仅选择名称不会
试用或写默认；每页最多4项，有更多时可翻页。目录不是跨页冻结快照。目录任务
和默认选择共用版本化作业身份，切换操作后须重新读取，旧按钮不会沿用旧状态。
取消目录读取与重试释放资源是显式设备请求；结果未知或释放失败不显示空目录。

一次只执行一个设备作业。页面轮询只读本地快照；取消按钮只设置意图，由同一
传输线程在完整分块之间发送取消并回读。页面关闭不会自动撤销设备操作；
Ctrl+C 停止本地服务时请求取消在途导入并等待既有有界操作退出。提交阶段
拒绝取消或通信失败都保持未确认，不谎报远端已取消。

目录必须是新目录，最多受理64个不同本地作业，单包上限128KiB；同请求编号
和内容重复提交不重放，不同内容复用编号被拒绝。JSON大整数以字符串返回，
避免浏览器损坏uint64版本。导入数据及公开回执留在目录中，不自动删除。
重启工作台使用新目录；旧回执可通过既有CLI的`resource-status`/显式resume
查询恢复。不要同时用另一个程序占用该USB口。

本地访问口令仅授予这次受限页面入口，存于当前页会话，不是设备凭据。
HTTP仅监听IPv4 loopback、检查Host/Origin及口令，无CORS、任意文件读取、
Shell、OTA、清owner或电源操作入口。8个有超时的HTTP连接不共享USB客户端；
设备密钥只在原有本地profile路径使用，不传给网页。任务事件发送仍使用既有
`task-event` CLI，不把网页保活当作常驻电脑代理。

验证边界：HTTP边界、单作业/幂等/取消意图、大整数，以及HTTP→真实客户端TLS→
原生安装作业已在主机验证。浏览器布局/缺失profile错误路径单列；未声称真实
USB、板端SD、屏幕或手机共同操作通过。固件必须实际支持对应协议和独立PC授权。


### 目录命令与边界

`workbench catalog-status`仅取元数据，不扫描。`catalog-page`必须带本次状态的
`--selection-epoch`、`--expected-selection-id`和新的`--selection-nonce`；第一页
省略`--catalog-after`，下一页使用回读的`next_cursor`。`catalog-cancel`和
`catalog-recover`使用当前目录作业ID，后者只恢复释放，不把UNKNOWN改为成功。
所有命令沿用同一`--port`与`--profile`，不切MSC，也不打开调试Shell。

```sh
python tools/bk7258/bk7258.py workbench catalog-status --port NATIVE_CDC_PORT --profile pc.profile
python tools/bk7258/bk7258.py workbench catalog-page --port NATIVE_CDC_PORT --profile pc.profile --selection-epoch EPOCH_FROM_STATUS --expected-selection-id ID_FROM_STATUS --selection-nonce NEW_32_LOWERCASE_HEX
```

示例占位符须换成实际非零身份，不能照抄。受理不代表读取完成；随后用
`catalog-status`并带原epoch/nonce回查，错scope或旧请求会报未确认，不自动重放。
`source_sha256`只是包的源元数据摘要，不是文件哈希。旧固件不支持则报错，
不假装设备没有资源。手机原生App目录选择尚未随此电脑入口完成。
