# BK7258 scripts/ 职责收敛 — 迁移前清单 (Pre-Inventory)

- 分支：`refactor/bk7258-scripts-convergence` @ `56e574c` (= origin/dev-ai-contest-2026)
- 目的：仅作职责收敛/可维护性重构，非修复 OpenVela 违规（doc id=602 目录树为典型结构，非排他清单）。
- 隔离：未提交的 boot-policy/acceptance WIP 已 `git stash` → `stash@{0}`（可恢复，未混入本分支）。

## 1. scripts/ 受管文件（102 个，SHA-256 全量见 `2026-08-16-bk7258-scripts-preinv-sha256.txt`）

按扩展名分布：
- 49 json
- 30 py
- 6 sha256
- 6 sh
- 5 provenance
- 2 script (ld.script, ld_ap.script)
- 2 ps1
- 1 md
- 1 defs (Make.defs)

合计 = 102（与用户所述一致，`git ls-files board/bk7258/scripts`）。

## 2. 迁移后 scripts/ 最终 allowlist（仅 6 个，直接构建钩子）

```
board/bk7258/scripts/Make.defs
board/bk7258/scripts/ld.script
board/bk7258/scripts/ld_ap.script
board/bk7258/scripts/postbuild.sh
board/bk7258/scripts/gen_bk7258_partitions.py
board/bk7258/scripts/bk7258_crc_expand.py
```

其余 96 个文件迁出：
- host-only 框架/打包/验证/串口工具/JSON 合同 → `tools/bk7258/`（平铺，保留同目录 import）
- `scripts/sdk-manifests/` → `board/bk7258/bk_idk/manifests/`
- `scripts/research/` → 现有 `docs/.../research/`

## 3. 活动消费者路径（迁移分支中，scripts/ 之外、指向 scripts/ 的活动引用）

代码/构建/配置类（P3 必须改线）：
- `board/bk7258/tests/test_bk7258_aidk.py`
- `board/bk7258/tests/test_bk7258_framework.py`
- `board/bk7258/tests/test_bk7258_isolated_executor.py`
- `board/bk7258/tests/test_legacy_profile_freeze.py` （`SCRIPT_DIR = REPO_ROOT / "board/bk7258/scripts"`）
- `app/hello_app/bkvalidate_main.c`（C 校验工具，引用 scripts 路径字符串）
- 活动 README：`board/bk7258/bk_idk/README.md`、`board/bk7258/bootloader/README.md`、`board/bk7258/configs/README.md`、`board/bk7258/chip/ap/PWM_BLOCKED_ROOT_CAUSE.md`、`app/hello_app/README.md`

框架内部自引用（随文件移动到 tools/bk7258 后，P3 批量 `board/bk7258/scripts → tools/bk7258`；sdk-manifests / research 例外）：
- `bk7258_framework.py` 38 处、`bk7258_boot_policy.py` 4 处、`bk7258_resource_graph.py` 3 处、`verify_legacy_profile_freeze.py` 4 处等。

说明：`Make.defs` / `CMakeLists.txt` / BL1/BL2 Makefile 未硬编码 `board/bk7258/scripts` 路径（grep 未命中），直接 Make/CMake 链路不调用框架 Python；框架由 host 编排层（isolated_executor）独立调用。postbuild.sh 保留在 scripts/，仅调用同目录的 gen_bk7258_partitions.py / bk7258_crc_expand.py（两者均保留在 scripts/）。

注意：`legacy_profile_consumers.json` 内部引用 `board/bk7258/scripts/Make.defs`（legacy 死重，保留即可，迁移后由审计统一处理，不在本次修正）。

## 4. 当前 focused host tests 基线（迁移前，当前树含 boot-policy WIP）

- `test_bk7258_boot_policy.py` ：16 PASS
- `test_bk7258_isolated_executor.py` ：28 PASS
- `test_bk7258_framework.py` ：19 PASS
- 合计 **63 PASS**（无失败）

## 5. SDK 状态

- `board/bk7258/bk_idk/armino_as_lib/versions` 缺失、gitignored；全仓无 `*.a`。
- registry 声明 `metadata-only` + `redistribution_authorized: false` + `sdk_bytes_tracked: false`。
- 结论：**SDK 缺失 → TARGET_BUILD_BLOCKED(SDK)**。本次只能判定 STRUCTURE_PASS / HOST_PASS，不得写"彻底重构完成"或"构建验证 PASS"。

## 6. 开始条件核对

- [x] 不混入未提交 boot-policy/acceptance 改动（已 stash@{0} 隔离）
- [x] 从最新 origin/dev-ai-contest-2026(56e574c) 建干净分支
- [x] 迁移前清单已保存（本文件 + SHA-256 全量）
- [x] 保留所有用户未跟踪文件（logs/、progress/ 文档、bl2_crc.bin.json、bootloader.tmp 均保留，未 reset/clean/删除）
- [x] 当前 focused host tests 基线已记录（63 PASS）
- [x] SDK 状态已记录（缺失 → BLOCKED）
