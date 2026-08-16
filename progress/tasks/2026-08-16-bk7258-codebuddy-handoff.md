# Task handoff: BK7258 post-merge acceptance and driver continuation

- Created: 2026-08-16
- Owner: CodeBuddy
- Status: Ready after this documentation checkpoint is persisted

Do not reconstruct this project from chat history. Treat
`progress/CURRENT.md` as the only current-state owner and use this packet only
for the bounded next work.

## Objective and user outcome

Resume from the merged BK7258 product framework, reconcile the checked-in
boot-policy authority with the already implemented isolated-executor phases,
establish one clean post-merge keyless four-role build result, and then
continue the first AIDK board driver binding without reopening the framework
design.

The immediate deliverable is a narrowly scoped policy/authority correction
followed by a reproducible `t5_board_bringup`
`prepare -> materialize-sources -> compile-runtime` result based on the
canonical upstream merge. A keyless `prepare-delivery` may follow only if the
corrected policy authorizes that phase. If those gates pass, the next
implementation is one standard NuttX GPIO LED/key binding for AIDK; signing,
production packaging and hardware access remain separately authorized phases.

## Required context

Read in this order:

1. Repository/parent `AGENTS.md`.
2. `memory/INDEX.md`.
3. `progress/CURRENT.md`.
4. `memory/RULES.md`.
5. `memory/ARCHITECTURE.md`.
6. `memory/decisions/ADR-026-bk7258-platform-v2-configuration-and-isolation.md`.
7. `board/bk7258/configs/README.md`.
8. `progress/verification/2026-08-16-bk7258-four-role-isolated-build.md`.
9. `progress/verification/2026-08-16-bk7258-partition-layout-identity.md`.

Canonical source checkpoint:

```text
repository: contest2026_135_yongwangzhiqian
base ref:   origin/dev-ai-contest-2026
base SHA:   56e574caf9b0fd46cd2e8a701b0120b94e51ff9b
tree SHA:   1583e0ddc43aac58ebe2a1da49720809f1c3cd5c
reviewed feature commit with identical tree:
            73c0dc7069be4cc6081ccc85d63600f0fb816ca7
```

Create a new feature branch from the base ref. Because the reviewed feature
and upstream commits have identical trees, the exact docs-only handoff patch
may be carried across that switch after verifying its scope. If any other
tracked change exists, use a separate clean clone/worktree instead. Do not
continue development on `feat/bk7258-partition-layout-identity`, which is the
already-merged fork branch, and never reset or delete user files to make the
checkout appear clean.

## Allowed and forbidden scope

- Allowed:
  - read-only resume and host verification;
  - a fresh isolated build root under `/tmp`;
  - BK7258 changes inside this contest repository;
  - physical-board facts/composition under
    `board/bk7258/boards/aidk_ai_toy`;
  - product fragments, validation suites, resource graphs and focused tests;
  - updating `progress/CURRENT.md` and one phase verification record when a
    material result is established.
- Forbidden without a new explicit user instruction:
  - signing, reading private keys, packaging a production release, Flash,
    reset, COM/J-Link access or any hardware operation;
  - modifying NuttX/apps/common repositories in place; public-framework
    changes require their own fork/PR;
  - adding SDK bytes, publishing the SDK mirror, changing trust roots,
    inspecting N17 or weakening trust preflight;
  - adding per-driver directories under `board/bk7258/configs`; use a product
    fragment or validation suite;
  - putting physical pins, external sensor policy or product partitions back
    into `chip/`;
  - using a T5 package, layout or debug procedure on AIDK;
  - staging, deleting or rewriting the existing untracked
    `bootloader.tmp`, `bl2_crc.bin.json`, `logs/driver-review-*` or
    `logs/hardware-debug/`.

## Step 1: resume and verify the checkpoint

First verify the base, tree equivalence and current diff. Preserve the handoff
docs and existing unrelated untracked files:

```sh
git fetch origin dev-ai-contest-2026
git rev-parse HEAD^{tree}
git rev-parse origin/dev-ai-contest-2026^{tree}
git status --short
git switch -c feat/bk7258-postmerge-acceptance \
  origin/dev-ai-contest-2026
git rev-parse HEAD
git status --short
```

The expected HEAD is `56e574c...` and the expected tree is
`1583e0ddc43aac58ebe2a1da49720809f1c3cd5c`. Before the switch, the only
allowed tracked difference is this documentation checkpoint; a different
source tree or any unrelated tracked change is a blocker. The four known
untracked exclusions are expected and must remain untouched.

## Step 2: reconcile boot-policy and executor authority

Do this before running `prepare-delivery` and do not merely flip a status
field. Review these as one contract:

- `board/bk7258/scripts/bk7258_boot_policy_t5ai_core_bringup.json`;
- `board/bk7258/scripts/bk7258_boot_policy_t5_board_bringup.json`;
- `board/bk7258/scripts/bk7258_boot_policy_aidk_ai_toy_bringup.json`;
- `board/bk7258/scripts/bk7258_boot_policy.py`;
- `board/bk7258/scripts/bk7258_isolated_executor.py`;
- the focused boot-policy, framework and isolated-executor tests.

At the merged checkpoint, all three policy documents still declare
`metadata_only=true`, `executor_authoritative=false`,
`integration_status=BLOCKED` and
`integration_blocked_until=plan/profile-reconciliation`; their tests assert
that state. The executor nevertheless implements `prepare-delivery` and an
explicitly authorized `deliver`. Resolve that contradiction with these
invariants:

- role-isolated compile remains gated by explicit compile authorization;
- keyless `prepare-delivery` may prepare and validate a plan but grants no
  signing or packaging authority;
- `deliver` continues to require separate explicit sign and package
  authorizations, an external key and explicit version/security-counter
  inputs;
- Flash and hardware remain forbidden in this handoff;
- AIDK must not inherit T5 trust or SWD assumptions;
- wrong product/plan/profile/phase combinations continue to fail closed.

Do not promote all three products uniformly. The minimum accepted correction
is a coherent T5-Board compile/keyless-plan contract. AIDK must remain blocked
from delivery/hardware until its no-SWD trust and first-install path is
designed and authorized; change T5AI-Core authority only when its own evidence
supports the same transition.

Add focused positive and negative tests proving the transition rules and that
no authority is widened silently. If a coherent correction would require a
larger policy redesign, stop with `PARTIAL` and report it instead of weakening
the checks.

## Step 3: clean compile and keyless delivery-plan gate

Use a fresh external build root:

```sh
BK7258_HANDOFF_ROOT="$(mktemp -d -p /tmp bk7258-codebuddy.XXXXXX)"
python3 board/bk7258/scripts/bk7258_isolated_executor.py prepare \
  --product t5_board_bringup \
  --build-root "${BK7258_HANDOFF_ROOT}/build" \
  --out "${BK7258_HANDOFF_ROOT}/build/execution.json"
python3 board/bk7258/scripts/bk7258_isolated_executor.py materialize-sources \
  --manifest "${BK7258_HANDOFF_ROOT}/build/execution.json"
python3 board/bk7258/scripts/bk7258_isolated_executor.py compile-runtime \
  --manifest "${BK7258_HANDOFF_ROOT}/build/execution.json" \
  --authorize-compile
```

The compile gate passes only if the terminal manifest is
`runtime-built/compile-runtime`, all active BL1/BL2/CP/AP commands pass, and
sign/package/hardware/network/private-key-read remain `NOT_RUN`. Record the
base SHA, manifest identity, role artifact hashes/sizes and exact unrun
boundaries; do not copy raw build logs into project memory.

After Step 2 is accepted, run exactly one keyless plan transition:

```sh
python3 board/bk7258/scripts/bk7258_isolated_executor.py prepare-delivery \
  --manifest "${BK7258_HANDOFF_ROOT}/build/execution.json"
```

It must end at `delivery-prepared/prepare-delivery`, keep private-key read,
signing, packaging, network and hardware at `NOT_RUN`, and bind the standard
aliases `vela_nuttx_cp.bin`, `vela_nuttx_ap.bin` and
`vela_nuttx_manifest.json` to the runtime artifacts. Do **not** call
`deliver`; do not claim or emit a production `firmware.bkpack` in this
handoff.

## Step 4: bounded AIDK GPIO binding

Start only after Steps 2 and 3 pass and their evidence is recorded.

- Reuse the chip controller/lower-half and standard NuttX GPIO upper-half:
  `board/bk7258/chip/cp/bk7258_gpio_lowerhalf.c`. Do not create another GPIO
  driver.
- Add only typed AIDK board facts and composition in:
  - `board/bk7258/boards/aidk_ai_toy/include/bk7258_board_config.h`;
  - `board/bk7258/boards/aidk_ai_toy/src/bk7258_board_bringup.c`;
  - the AIDK product fragment/resource graph and focused tests.
- The schematic pin map currently identifies P40 as the `LED1` net, P12 as
  `KEY2`, and P13 as `KEY1`. Re-open the owner-supplied schematic and confirm
  polarity and the chosen key before encoding the descriptor; do not repeat
  the earlier P12/KEY1 label mismatch.
- Enable the board user-GPIO capability and only the CP-side IRQ bridge/GPIO
  lower-half dependencies required by the standard ABI.
- Keep P8/P9 disabled because of the unresolved 32.768 kHz crystal versus
  KEY3/motor conflict. Keep P20/P21 owned by the future SC7A20 I2C decision,
  with SWD disabled. Keep UART0 flow control disabled.
- Do not hard-code `COM9`; the host transport discovers the port. AIDK
  remains `hardware_verified=false` until an authorized physical run.

Host acceptance for this step must prove board-selection fail-closed behavior,
resource ownership, Classic/CMake source parity, and clean AIDK product
profile resolution. Hardware acceptance, when separately authorized, is
`/dev/gpio0` LED output plus one active-low key input/edge path, followed by
cold/soft restart checks.

## Acceptance criteria and evidence

- [ ] Work starts from `origin/dev-ai-contest-2026@56e574c` on a new branch.
- [ ] Existing unrelated untracked files are preserved and excluded.
- [ ] Boot-policy/executor authority is internally consistent, tested and
      still denies implicit sign/package/Flash/hardware authority.
- [ ] Fresh T5-Board isolated four-role compile reaches
      `runtime-built/compile-runtime`.
- [ ] Keyless `prepare-delivery` reaches `delivery-prepared` and validates the
      three standard aliases without emitting a production package.
- [ ] Evidence explicitly keeps sign/package/hardware/network/key-read
      at `NOT_RUN`.
- [ ] No new legacy config directory or chip-to-board dependency is added.
- [ ] If Step 4 is implemented, AIDK board/resource tests and product
      profile-check pass without claiming hardware PASS.
- [ ] `git diff --check` passes and the final diff contains no secrets,
      binary artifacts or host-private paths.
- [ ] `progress/CURRENT.md` is updated once with the actual terminal result
      and exact next action.

## Authority

- Modify files: Yes, only within the allowed scope above.
- Run host tests/builds: Yes, keyless and non-hardware only.
- Commit or push: No unless the user explicitly asks CodeBuddy to do so.
- Call `deliver`, sign, package for release, deploy or access hardware: No
  without separate explicit authorization.

## Expected return

Return:

1. base/head SHA and changed-file scope;
2. PASS/PARTIAL/FAIL conclusion for each executed step;
3. exact commands and exit results;
4. manifest/artifact identities, without raw logs or private paths;
5. tests not run and why;
6. remaining risks and the next single action;
7. links to the updated current-state and phase evidence.
