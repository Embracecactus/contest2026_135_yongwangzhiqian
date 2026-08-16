# Current Progress

Last updated: 2026-08-16
Updated by: Codex (takeover from CodeBuddy)

## Snapshot

- Active branch: \`fix/bk7258-irq-swd-rtt\`.
- Upstream base:
  \`origin/dev-ai-contest-2026@56e574caf9b0fd46cd2e8a701b0120b94e51ff9b\`.
- Commit series through P3:
  \`a630d52\` (path resolver and manifest mapping), \`2d7f70b\` (pure
  relocation), and \`4799a44\` (consumer rewiring and isolated build).
  P4 is the structural-gate checkpoint; this branch is the focused IRQ and
  SWD/RTT hardware-acceptance follow-up.
- The scripts responsibility convergence is now
  **STRUCTURE_PASS / HOST_PASS / TARGET_BUILD_PASS**. Evidence:
  [2026-08-16 scripts convergence](verification/2026-08-16-bk7258-scripts-convergence.md).

## Established result

- \`board/bk7258/scripts/\` contains exactly six direct build hooks:
  \`Make.defs\`, \`ld.script\`, \`ld_ap.script\`, \`postbuild.sh\`,
  \`gen_bk7258_partitions.py\`, and \`bk7258_crc_expand.py\`.
- Host-only framework, verification, packaging, transport and SDK tools live
  under \`tools/bk7258/\`; SDK manifests live under
  \`board/bk7258/bk_idk/manifests/\`.
- Source-work, manifest-mapped workspace and isolated snapshot paths use the
  same fail-closed resolver. A manifest workspace must provide both the board
  and tools mappings.
- Migrated host tools load the two retained Python build hooks through an
  explicit allowlisted, containment-checked loader; that loader does not add
  scripts to \`sys.path\`.
- Existing local SDK bundles were verified in place. No SDK import, upload or
  publication was required.
- One fresh \`t5_board_bringup\` isolated chain passed:
  \`prepare -> materialize-sources -> compile-runtime -> prepare-delivery\`.
  BL1, BL2, CP and AP compiled successfully; the terminal manifest is
  \`delivery-prepared\` with identity
  \`d4a0324c4550198c252048aabc397d60f51c6711a74cef03747f2fd7cfa7d0d5\`.
- The prior scripts-convergence checkpoint produced standard unsigned aliases
  (these hashes are historical and are not a signed/current-tree claim):
  \`vela_nuttx_cp.bin\` SHA-256
  \`d043e9b00a9132ede29b5b84d478f5f4c62fc098f6bdb5ed99f5bcc5ca277a18\`;
  \`vela_nuttx_ap.bin\` SHA-256
  \`890fc30650d9f59c775c7266c84e8765072b9cb7499e9802231db6c4965afb35\`.
- Layout identity remained
  \`bk7258-v3119-ab-124ebfab37ca1fcd\` /
  \`124ebfab37ca1fcd9971c5aba7b9f214f0500df74cdc394c88ec602020732d8a\`.
- Full BK7258 host suite: **167 passed**. A separately authorized signing run
  completed BL1/BL2, CP/AP, MCUboot-pair, factory-layout and
  \`firmware.bkpack\` verification.  After the first UART/SWD-mismatched
  candidate was rolled back exactly, a same-key policy-consistent SWD/RTT
  package was built and installed on the COM3 T5-Board.  All written ranges
  matched the package on readback; BL2 hold/release, the three roots, CP SWD
  trace, RTT NSH, AP READY/heartbeat and CPU2 SECONDARY_READY all passed.  The
  complete rotation, recovery and accepted replacement are recorded in
  [2026-08-16 T5 root rotation](verification/2026-08-16-bk7258-t5-root-rotation.md).
- Official top-level \`build.sh <config> --cmake\` smoke has now passed once
  for both CP and AP base roles using separate clean temporary build roots.
  CP produced \`nuttx.bin\` (SHA-256
  \`2747725c045ce948aec6044469953e6a6223b65638a4fa64d529ad022d9c8ca7\`) and
  \`app_crc.bin\` (SHA-256
  \`fdd993d324296c8f553ede3107b3666b451dce55c7df57564af4160cd86f9f70\`);
  AP produced \`nuttx.bin\` (SHA-256
  \`5a0ae5ae44ee26b599acca1c5629b16a4597d16312bbf2e2d117ca7a62e260bc\`) and
  \`app1_crc.bin\` (SHA-256
  \`84ab2920e81ee41feb670e53b05cd7c97bccca5bd6bf76168c22c35e31f6b52e\`).
  Partition generation/check and role postbuild both passed. This is an
  unsigned base-role smoke, not a MCUboot delivery build.

## Current local review

- The merged scripts-convergence checkpoint is unchanged.  This follow-up
  records an IRQ contract correction: the v3.1.1.9
  CP/AP SDK archives were checked first (both CMSIS priority helpers encode
  priorities with \`priority << 5\`; the SDK map has 64 sources and the LCD
  source-27 priority exception), then the BK7258 public IRQ macros and bridge
  checks were tightened to the verified 3-bit \`[7:5]\` contract.
- The correction has passed the SDK IRQ verifier (48/0), IRQ header
  preprocessing, 46 framework/path tests, a clean \`arch/arm/src/libarch.a\`
  compile, and the CP/AP MCUboot link.  The policy-consistent signed artifact
  containing this correction is now installed: Flash readback matched, the
  system booted, and live AIRCR/NVIC readback confirmed PRIGROUP 0 and the
  three-bit \`0x20\` priority step.

## Remaining work

- The SWD/RTT configuration disconnect is resolved and hardware-validated.
  The first rejected candidate remains historical evidence only; the board is
  now running the accepted same-key replacement.
- During that run two migration path defects were fixed locally: the dual
  builder now passes the resolved workspace \`imgtool.py\` explicitly, and the
  payload packer anchors \`board/...\` layout paths at the contest checkout.
- P9b legacy-profile retirement/equivalence and validation-descriptor migration
  remain separate work; this refactor does not silently retire them.
- AIDK GPIO binding/hardware validation remains the next driver task after this
  PR, not part of scripts convergence.

## Exact next action

The owner opens the Web PR from `fix/bk7258-irq-swd-rtt` to
`dev-ai-contest-2026`.  Do not start the next driver task on this branch;
resume from the hardware-validated T5 reference baseline after merge.

## Boundaries

- Never stage or delete \`bootloader.tmp\`, \`bl2_crc.bin.json\`,
  \`logs/driver-review-*\`, \`logs/hardware-debug/\`, or unrelated handoff
  drafts.
- Do not import or publish SDK bytes as part of this change.
- Do not publish the private key material, raw 8-MiB board images, device UID,
  calibration contents, or host-local key paths.
- Do not perform another root change or use the ordinary compatible-update
  preflight to disguise a rotation.  The accepted board currently uses the
  explicitly authorized development software roots recorded above.
