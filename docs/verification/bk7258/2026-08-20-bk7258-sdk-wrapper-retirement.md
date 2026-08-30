# BK7258 SDK wrapper retirement verification

- Date: 2026-08-20
- Scope: host-tool metadata migration only
- Conclusion: PASS for the SDK set/lock retirement phase

## Verified outcome

- The team manifest pins the SDK fork at commit
  `cb080de1655d579c7593ecf504c440997c4c137b` under
  `vendor/beken/bk_avdk_smp`.
- Three SDK set files and three SDK lock files are removed.
- Product catalogs, build plans, execution manifests, resource resolution and
  package metadata no longer consume or publish set/lock identities.
- CP/AP bundle identity is derived directly from each tracked checksum
  manifest and provenance record. BL2 remains explicitly SDK-free.
- The transitional registry remains reachable only from `sdk-import`; it is
  not part of product, build-plan or package identity.

## Evidence

- `python3 -m unittest discover -s tools/bk7258/tests -p 'test_*.py'`:
  160 tests PASS, 2 environment-dependent skips. The known stale local
  `board/bk7258/bootloader/bl.elf` was hidden only for the test process so its
  artifact-dependent symbol check skipped; the regular file was restored.
- `bk7258_framework.py validate`, `framework-check`, `build-plan`,
  `build-plan-verify`, `pack-prepare` and `pack-verify`: PASS for
  `t5ai_core_bringup`.
- `bk7258_framework.py sdk-verify --bundle-root
  board/bk7258/bk_idk/armino_as_lib`: PASS for the real local v3.1.1.9 CP/AP
  bundles, content IDs `438c1bf1...c7be` and `09a83cc5...a196`.
- Modified JSON documents parse successfully; modified shell scripts pass
  `bash -n`; `git diff --check` passes.

## Existing unrelated test-harness limitations

- `tests/bk7258/run_tests.sh` does not create `build/bl1` before its first
  pure BL1 object and therefore fails immediately from a clean directory.
- With that directory supplied temporarily, the mailbox, PM, RPTUN and BL1
  host tests pass, while the BL2 fixture later lacks
  `flash_map_backend/flash_map_backend.h`; LeakSanitizer is also unavailable
  in the current controlled process environment.
- No file under `tests/bk7258` was changed by this SDK metadata phase, so this
  record does not convert those pre-existing harness gaps into a product
  regression or a new acceptance requirement.

## Not performed

- No SDK synchronization or SDK rebuild.
- No firmware build, signing, production packaging, Flash, J-Link or hardware
  operation.
- No commit, push or pull request.
