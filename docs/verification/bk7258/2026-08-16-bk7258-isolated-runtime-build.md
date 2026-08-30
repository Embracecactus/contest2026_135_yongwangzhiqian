# BK7258 isolated CP/AP runtime-build verification

> **Superseded/current-state note:** This is a phase-local record from
> 2026-08-16. Subsequent work completed the 27-to-3 profile cutover, the
> four-role compile contract, and the postbuild command alias. The
> `NOT_RUN`/CP/AP-only boundaries below remain the facts of this captured
> run; do not read them as the current project state. See the authoritative
> This is a historical build record; current acceptance must be established
> from source, configuration, and the latest verification evidence.

- Date: 2026-08-16 (Asia/Shanghai)
- Base commit: `54ff505912baf4c23e2515ffa60e6c8df18933b5`
- Branch: `feat/bk7258-partition-layout-identity`
- Evidence root: `<evidence-root>/bk7258-isolated-t5-board-runtime-20260816-0700`
- Manifest: `execution.json`, identity
  `b63b9b35fb8b56b0ffc201d371c170dafe9d913e3193b5c85ef7cdbcd6448265`
- Snapshot identity: `9cbd0bf6f8be86a193c64166950cafaed972dcb60b5915e9fdd5bf9f5339a881`
- Phase: `runtime-built`; execution mode: `compile-runtime`

## Result and command contract

The materialized T5-Board entity snapshot was audited and used read-only. CP
completed before AP, and each role ran exactly this sequence; every command
returned `0` and has a role-private log:

1. Partition contract generator with layout
   `bk7258-v3119-ab-124ebfab37ca1fcd` and SHA
   `124ebfab37ca1fcd9971c5aba7b9f214f0500df74cdc394c88ec602020732d8a`:
   `/usr/bin/python3.10 <snapshot>/contest2026_135_yongwangzhiqian/board/bk7258/scripts/gen_bk7258_partitions.py --input <snapshot>/contest2026_135_yongwangzhiqian/board/bk7258/partitions/bk7258/auto_partitions.csv --header <role>/partition-contract/include/arch/board/bk7258_partition_layout.h --output-dir <role>/partition-contract/generated --expect-layout-id bk7258-v3119-ab-124ebfab37ca1fcd --expect-layout-sha256 124ebfab37ca1fcd9971c5aba7b9f214f0500df74cdc394c88ec602020732d8a`
2. CMake configure:
   `/usr/bin/cmake -S <snapshot>/nuttx -B <role>/cmake -G Ninja -DNUTTX_APPS_DIR=<snapshot>/apps -DPython3_EXECUTABLE=/usr/bin/python3.10 -DBOARD_CONFIG=<role>/config/t5_board_<role>_mcuboot`
3. `/usr/bin/cmake --build <role>/cmake --target nuttx`
4. `/usr/bin/cmake --build <role>/cmake --target nuttx-bin`

No `nuttx_post_build` command was run.

## Resolved tools

| Tool | Recorded resolution |
|---|---|
| Python | `<python-executable>`, `Python 3.10.12` |
| CMake | `<cmake-executable>`, `cmake version 4.3.1` |
| Ninja | `<ninja-executable>`, `1.11.1.git.kitware.jobserver-1` |
| arm-none-eabi-gcc | `<arm-none-eabi-gcc>`, `10.3.1 20210621 (release)` |
| olddefconfig | `<olddefconfig-executable>`, SHA-256 `05009d6302d326f1f9c75e6f90d20225913066dda1872058e888942ac9a652e0` |
| kconfiglib root | `<kconfiglib-root>`; `kconfiglib.py` SHA-256 `d81c16ca77a451e52e93c99d39ad9451d645506450766c9a6b7193d55c439103`; `olddefconfig.py` SHA-256 `cff7251553d17d531e266f683bab30703e6858677ee108c80ab1125e45255451` |
| make | `NOT_RUN` |

## Runtime artifacts

The following records are copied exactly from `execution.json`; each path is
role-private and each size is positive.

| Role | Artifact | Size | SHA-256 |
|---|---|---:|---|
| CP | `.config` | 55959 | `9d3e93d76a3885db7bfb820aae55bf1d19e246ce9921aa6af7be8b98ff7019bb` |
| CP | `nuttx` | 1723420 | `9c6e711d60391d3c0e76c04415855bee7cc065958212b3f99c713081bb15c30e` |
| CP | `nuttx.map` | 3553856 | `e0b5c363d0eb611be34d122396610743ed977649287af55dfc10806d4ca9deb7` |
| CP | `nuttx.bin` | 234428 | `955759eb8b2ca83137e7382b1135d4b957a51f1f2614899c92ae0643ade2685d` |
| CP | `arch/libarch.a` | 225186 | `90ccf9f4d37fabff86dc8cf3c9d0dd75a752354d4f7638fe2bebed1a1137ea9c` |
| CP | `boards/libboard.a` | 24794 | `a293eff794642d93b3da8117b413067996827229ce9b2841f8e9c9f796f9e99d` |
| AP | `.config` | 50499 | `0c44186c0214df0faa2933c1d7a75181b57246d598cbf3e987661fcc182ccb2f` |
| AP | `nuttx` | 1097664 | `064e557345355a92ac4642c1d198d94211315dfb66ef1f9a9e781467b1529a89` |
| AP | `nuttx.map` | 1817777 | `159a0d14f158e72cc279dbe25ce93e0c85b3fe51b4ded0bccb77293f4e3e0a61` |
| AP | `nuttx.bin` | 168652 | `b68392d0a15f2314b7a3725509332c96c6ca711c8e43b0f6924084d09401916c` |
| AP | `arch/libarch.a` | 262464 | `6f9945a1d7a4300185462f28726c3884856f80058d4afa9d7bbe9f295c9cec21` |
| AP | `boards/libboard.a` | 10470 | `4dbff0ef5a267e1b36d09ecc7d12d38b11ae1a1112daaf4aeffb6809ceac7116` |

`System.map` is optional and was absent from this build; it is recorded only
when a real backend output exists. `config_path` resolves to each role's
`cmake/.config`; the independent `artifacts/.config` archive has the matching
content hash and size.

## Verification and boundaries

- `python3 -m unittest board/bk7258/tests/test_bk7258_isolated_executor.py` —
  13 tests passed.
- `python3 -m unittest board/bk7258/tests/test_bk7258_framework.py` — 16 tests
  passed.
- Python compilation, schema JSON validation and `git diff --check` passed.
- `compile=PASS`; `sign`, `package`, `hardware` and `network` are `NOT_RUN`.
  BL1/BL2 command rows remain `NOT_RUN`; boot policy is
  `UNRESOLVED`/`BLOCKED`; `private_key_read` and `signing` are `NOT_RUN`.
- The post-command snapshot gate rejects source content or mode mutation
  before writing `runtime-built`. An mtime-only `touch` is allowed because
  snapshot identity is content-based; it does not authorize any source edit.
- This evidence does not complete boot-policy integration, BL1/BL2 execution,
  postbuild/sign/package/hardware delivery, P9b, legacy-profile migration or
  validation migration. The next action is to integrate boot policy and
  BL1/BL2 into the same executor, then separately authorize signing/package
  and hardware phases.
