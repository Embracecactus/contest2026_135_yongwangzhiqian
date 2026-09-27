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

The current first-build command sequence is in the root [README](../../README.md#独立开发构建当前主入口).
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

#### 离线配对交换（S75，手机确认/导出 UI 尚未接通）

Windows 或现有 WSL→Windows DPAPI 环境可生成公开请求和受保护的临时状态：

```sh
python3 tools/bk7258/bk7258.py workbench pair-start \
  --request pc-request.spq --pending pc-pending.spp --allow resources scenes
```

权限必须显式选择：`resources`、`scenes`、`tasks`、`diagnostics`。输出请求摘要供
后续手机核对；请求有效期 10 分钟，双方系统时间需一致。`pc-pending.spp` 绑定当前
Windows 用户，只留在本机；不会产生明文私钥文件。这里不打开串口、不授权设备。

手机侧公开编解码接口已实现，但普通用户的文件选择、确认授权、持久回执及响应导出
尚未串联，因此目前不是完整首配 SOP。获得该流程正式产生的加密响应后，导入命令为：

```sh
python3 tools/bk7258/bk7258.py workbench pair-finish \
  --pending pc-pending.spp --response phone-response.spr --profile device.spc \
  --confirm-device-sha256 <从可信手机界面核对的64位小写十六进制证书摘要>
```

摘要不能照抄不可信响应。所有输出路径必须不存在；导入不覆盖已有配置。导入成功仅
表示受保护配置保存成功，仍需真实设备鉴权，不能据此宣称设备已授权。临时状态保留
供有效期内恢复，不承诺一次性消费或安全擦除。非 Windows 的系统密钥库尚未实现。
