---
name: dependency-framework-migration
description: Implement a migration from product-owned mechanisms to an existing dependency framework, including real callers, build integration, resource ownership, and retirement of replaced code. Use for embedded or application framework migrations already in scope. Not for architecture-only advice, bulk renaming, or adding a parallel framework without switching the product path.
---

# Dependency framework migration

## Inputs and boundaries

Obtain the repository root, applicable instructions, target configuration,
dependency lock/manifest, existing build command, accepted product behavior,
and available runtime target from the authorized project. Read known files
directly. For unknown call chains use the project's indexed explorer first
when required; fall back to scoped source search if unavailable or stale.

This workflow implements an agreed migration; use an existing architecture
clarification skill for unresolved product requirements. Do not restart an
accepted design review. Keep hardware, credentials and publication within
the current task's authorization; this skill grants none of those actions.

## Execute one vertical slice

1. Establish relevant source identity with `git rev-parse --show-toplevel`,
   `git status --short`, `git rev-parse HEAD`, target config and actual checked
   out dependency revisions. Preserve unrelated changes. Inspect generated
   build inputs as well as declared manifest versions.
2. Trace **product entry → current owner → framework implementation → target
   backend**. A symbol, README claim or similarly named API is insufficient:
   check implementation, enabled backend and current callers. Select the
   smallest complete product chain that can run after this change.
3. Record a short contract: current responsibility → target implementation →
   retained product policy → deletion scope → build/runtime acceptance. Assign
   exactly one owner for sessions, audio devices, requests and cancellation.
   Hardware mechanisms belong in the platform/lower-half; physical wiring
   and instances in the board; product policy remains in the application.
4. Replace real callers and make the framework the main path. Keep product
   deadline, cancellation, privacy and fresh-data policies at explicit
   extension points. Do not leave the old worker issuing the same request,
   opening the same device, retrying or playing a second response.
5. Prefer supported extension points, configuration and the project's adapter.
   Change a dependency only when the checked-out contract demonstrates a gap
   that the adapter cannot correctly bridge and the task permits that scope.
   Separate adapter mistakes, API limitations and implementation defects.
   Use the existing integration mechanism; for a patch-based build, apply to
   the exact dependency revision
   with `git apply --check`, stage a generated build view, and replace the
   intended build source. Where replacing a source list entry, require exactly
   one match. Never deliver changes only in a local dependency checkout.
6. Build the affected target incrementally with the existing entry point.
   Inspect compiler inputs and link map/symbols when a dependency changed:
   a patched file on disk does not prove its object was linked. Reconfigure
   only when build metadata requires it; reserve clean builds for dependency
   reproducibility or a final reproduction that actually needs one.
7. Run the complete slice using available authorized runtime facilities.
   Separate host, simulator and physical evidence. Exercise cleanup after
   cancellation/error and the next operation, including late callbacks.
   For NuttX/OpenVela streaming audio use
   [the Media failure branches](references/openvela-media-streams.md).
8. Remove replaced implementations by callers and build dependencies, not by
   directory name. Check references and target config again, rebuild affected
   targets, and use relevant existing checks. A retired-interface test is
   obsolete, not passed; do not revive the duplicate path to satisfy it.

## Failure decisions

- **Two edits do not improve the same symptom:** first confirm the running
  artifact changed, then inspect the earliest failing state transition. Do not
  stack another speculative patch or repeat an unchanged flash.
- **Official backend missing:** retain the smallest documented adapter at its
  extension point. State what remains custom; do not count a wrapper as reuse.
- **Cancel returns but next open is busy:** require worker quiescence and
  device release before reopening. A state flag is not a join/drain barrier.
- **Later recovery follows an earlier error:** record both. A successful next
  turn does not establish that the original failure mechanism was fixed.
- **Runtime target unavailable:** continue source/build work; mark only the
  affected runtime gate unverified. Do not announce architecture completion.

## Output and completion

Update existing project records with the contract, exact modified callers,
retired mechanisms, source/config/artifact identity, relevant verification
and remaining gaps. Use the project's normal artifact directory and release
workflow; do not create another build or deployment front door.

Complete only when the target compiles, links and calls the new implementation,
replaced code is off the product path, no parallel owner remains, and affected
product/recovery behavior has appropriate evidence. This method is portable;
API details and physical success are specific to each target and must be
verified there.
