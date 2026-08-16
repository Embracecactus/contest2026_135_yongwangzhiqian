# Scripts responsibility convergence: tools/bk7258 migration acceptance

- Date: 2026-08-16
- Owner: Codex (takeover from CodeBuddy)
- Branch: \`refactor/bk7258-scripts-convergence\`
- Base:
  \`origin/dev-ai-contest-2026@56e574caf9b0fd46cd2e8a701b0120b94e51ff9b\`
- P1 commit:
  \`a630d5218a7088b06a60f2b3fabffe686f346faa\`
- Verdict: **STRUCTURE_PASS / HOST_PASS / TARGET_BUILD_PASS**

## Scope

This is a responsibility/layout refactor. Host-only framework, verification,
packaging, transport and SDK tools move from \`board/bk7258/scripts/\` to
\`tools/bk7258/\`; direct build hooks remain board-owned. SDK manifests move to
\`board/bk7258/bk_idk/manifests/\`, and the ADR-003 research note moves under
\`docs/\`. The change does not alter driver, partition, trust-root or hardware
policy.

The OpenVela manifest maps both logical roots:

- board source -> \`vendor/openvela/boards/contest2026_135_bk7258\`
- host tools -> \`vendor/openvela/tools/contest2026_135_bk7258\`

## Structure result

- \`board/bk7258/scripts/\` contains exactly:
  \`Make.defs\`, \`ld.script\`, \`ld_ap.script\`, \`postbuild.sh\`,
  \`gen_bk7258_partitions.py\`, and \`bk7258_crc_expand.py\`.
- P2 (\`2d7f70b\`) is 96 pure renames with zero added/deleted content:
  84 host-only files to \`tools/bk7258/\`, 11 SDK manifest/provenance files to
  \`bk_idk/manifests/\`, and one research note to \`docs/\`.
- Including the committed P1 resolver, \`tools/bk7258/\` contains 85 tracked
  files.
- The retained scripts are real build inputs: Make/CMake linker selection,
  partition generation, CRC expansion and postbuild. The official guide's
  scripts tree is a typical layout, not an exclusive two-file whitelist.

## Review corrections

1. Explicit invalid workspace roots now fail closed; auto-detection does not
   borrow a parent mapping. Manifest form requires both board and tools
   mappings.
2. \`setup_bk7258_sdk.sh\` and \`import_bk7258_sdk_role.sh\` resolve board/tools
   through \`Bk7258Layout\` in source-work and manifest-mapped forms; they no
   longer infer the board from \`SCRIPT_DIR/../..\`.
3. \`load_board_script\` allows only \`gen_bk7258_partitions\` and
   \`bk7258_crc_expand\`; it rejects absolute/traversal names, containment
   escapes, symlinks and non-regular files, never mutates \`sys.path\`, and
   removes a half-initialized module on execution failure.
4. Delivery-plan records and execution resolve board build hooks from the
   source board and host pack/trust tools from the source tools root.
   \`verify_bk7258_psram.py\` follows the same split.
5. SDK registry/manifests, active SOPs and tests use the new roots. Historical
   records remain historical; the legacy-freeze reader is the one explicit
   old-tree exception.
6. Canonical CP/AP fragments now materialize a complete custom
   architecture/chip/board CMake configuration. The Kconfig quoted-value
   validator was corrected from an expression that accidentally rejected the
   literal letter \`n\` to one that rejects only quotes/newlines.

## Host verification

- Focused path/framework/executor/scripts gate: **84 passed**.
- Complete \`board/bk7258/tests\` suite after the only discovered test-order
  isolation fix: **167 passed**, with one expected zip duplicate-name warning
  from a negative tamper test.
- SDK bundle checks passed for:
  \`legacy/{cp,ap}\`, \`v3.1.1.9/{cp,ap}\`, and
  \`v3.1.1.9-sdio4/ap\`.
- Shell syntax, Python compilation and scoped whitespace checks passed during
  the focused fixes.
- No SDK import was performed; the required local bytes were already present
  and matched their manifests/provenance.

## Fresh isolated target result

One fresh external build root was used; its host path is intentionally not
persisted.

| Phase | Result | Identity |
|---|---|---|
| prepare | PASS | \`26cdf1d8a6c33c80d57409e0d4b0836a6ab3114e49012b50bb8e6c4e63214592\` |
| materialize-sources | PASS | \`c01298a25e9358bb6dd255b2080f72a5448093ff89eda6107d7ec84ded2a95b5\` |
| compile-runtime | PASS | \`b7f00d5efe25e8afb55e64becd0a668e5552f05ff409fa235c842ee615f75fdb\` |
| prepare-delivery | PASS | \`d4a0324c4550198c252048aabc397d60f51c6711a74cef03747f2fd7cfa7d0d5\` |

The terminal manifest is \`delivery-prepared\`. BL1, BL2, CP and AP compiled.
Key artifact facts:

- BL1 raw: 64,896 bytes,
  SHA-256 \`0ba0055f3e93756f8c6a020a28ca03164d7ebc7719ecf0fa8d5cc0584241f902\`
- BL2 raw: 12,128 bytes,
  SHA-256 \`e7f14cb242208418d9a81686c3cf0f357a764d580c4fdf6fc6468f396e3b3f2d\`
- \`vela_nuttx_cp.bin\`: 128,080 bytes,
  SHA-256 \`d043e9b00a9132ede29b5b84d478f5f4c62fc098f6bdb5ed99f5bcc5ca277a18\`
- \`vela_nuttx_ap.bin\`: 59,528 bytes,
  SHA-256 \`890fc30650d9f59c775c7266c84e8765072b9cb7499e9802231db6c4965afb35\`
- layout:
  \`bk7258-v3119-ab-124ebfab37ca1fcd\` /
  \`124ebfab37ca1fcd9971c5aba7b9f214f0500df74cdc394c88ec602020732d8a\`

\`prepare-delivery\` performed only the authorized keyless CRC/postbuild
checkpoint and standard alias binding. Private-key read, signing, package
creation, network and hardware all remained \`NOT_RUN\`; no
\`firmware.bkpack\` was created.

## Publication boundary

- The branch is split into P1 \`a630d52\`, P2 \`2d7f70b\`, P3 \`4799a44\`,
  and this P4 gate/evidence checkpoint.
- No push or PR was performed during the acceptance commands; publication is
  a separate SSH step after the commit boundary is verified.
- Existing generated bootloader files, hardware logs and unrelated untracked
  handoff/postmerge drafts were neither staged nor deleted.
- Signing, production packaging, Flash, COM/J-Link and hardware validation
  require separate user authorization and are not implied by this PASS.
