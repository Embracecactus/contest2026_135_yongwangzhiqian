---
name: embedded-release-verification
description: Assemble and verify an embedded source, configuration, APK or companion artifact, firmware and evidence delivery using existing build, signing and deployment tools. Use for bounded reproducibility, exact version correspondence and device-safe release preparation. Not for a new release platform, generic UART flashing, key generation or treating a build as physical acceptance.
---

# Embedded delivery and reproducibility

## Inputs and boundaries

Obtain the actual repository root, target profile, manifest/lockfile, established
build/package commands, artifact directory, source include/exclude rules,
deployment layout, current signing/trust and rollback contract, and permitted
target. Find these in authorized project configuration and SOP; never infer
partition addresses, port, device identity or signing inputs from examples.

Use a matching existing hardware download skill for the loader stage, or the
project's established download SOP if no applicable Skill is installed. Never
assume another platform's loader applies. Use the App's actual OTA procedure
for OTA. This workflow owns artifact correspondence,
not another packager or transport. Authorization is per current task, not
inherited from a Skill example. Stop signing/deployment if mandatory inputs
are missing; continue ordinary source/build work.

## Execute

1. Record **source identity**, **configuration/artifact identity**, and
   **running device identity** as separate facts. Inspect nested Git root,
   remotes, branch/HEAD, relevant dirty changes, actual manifest checkouts and
   existing outputs. A remote historical firmware number is not a local or
   board baseline. Do not sync away local fixes to match it.
2. Define the smallest relevant source closure for this artifact using the
   existing source manifest/build inputs. Include team overlays and dependency
   patches consumed by the build. Record dirty-source provenance honestly;
   a commit ID alone does not identify an uncommitted build.
3. Reuse current build directories for ordinary affected-target verification.
   Use a clean build only when changed dependencies or final reproduction
   requires it. Save exact command/config/dependency identity and result.
   Ensure packaged objects came from that build, not a stale sibling directory.
4. Compare reconstructed source to the artifact's recorded source hashes.
   When Git EOL filters are relevant, inspect `git check-attr` and compare
   `git cat-file --filters "$revision:$path"` with the build input. Do not
   silently ignore byte differences or assume raw blobs equal checkout files.
5. Reuse unchanged valid signed components under the project's trust contract.
   Verify boot components and application/core images have compatible layout,
   signatures and anti-rollback counters. If an intended version conflicts
   with a build-time floor, rebuild the affected components through the normal
   path; do not edit metadata to bypass checks or blindly downgrade.
6. Select the deployment path from the actual change. For a full image, map
   every written byte to the verified target capacity/layout. Check that an
   existing trusted **same-device** backup covers all irreplaceable ranges
   the write affects. Reuse it if valid; read only missing ranges when needed.
   Restore unique data only to that original device. Do not rotate keys,
   change fuses, bypass signature checks or write unrelated targets.
7. Build/package with the existing entry point. Hash final artifacts and retain
   signer/verifier results without secrets. Device-bound recovery images or
   backups are not public release assets merely because their firmware source
   can be published. Separate public source from private device material.
8. Deploy when authorized and appropriate; verify loader result, actual boot,
   version/counter and product behavior separately. Full flashing does not
   establish App OTA success. Preserve tested recovery outputs and affected
   configuration/identity checks, with host/simulator/physical scope explicit.

## Failure decisions and output

- Source hash mismatch: resolve scope, EOL filters, dirty inputs and generated
  files before claiming correspondence. Document remaining unknowns.
- Build passes but expected behavior is unchanged: check running artifact
  identity and linked source before another code edit or flash.
- Missing backup coverage, ambiguous target or missing signature input: stop
  only the unsafe deployment/signing step and complete unaffected work.
- A command is absent from the current CLI: inspect help and reuse the real
  entry; do not invent a parallel public command to match old notes.

Use existing README/release records to deliver exact source and dependency
identity, configuration, artifact paths/hashes, build and run commands,
upgrade/recovery notes, actual demo steps and known limits. Include the minimal
source/asset closure needed to reproduce; do not archive virtual environments,
private logs, full dependency caches or secrets without a concrete need.

Completion requires corresponding deliverables and correctly scoped evidence,
not just an archive's existence. Physical operation and independent source
reproduction must be labeled unverified when they were not performed.
