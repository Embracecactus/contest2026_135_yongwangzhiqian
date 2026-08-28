# BK7258 SDK profiles

The team manifest is the only source/version authority. Each maintained
profile is a tracked SDK config with one accepted bundle-tree hash comment:

```text
sdk-profiles/<manifest-version>/
  cp.config
  cp-aidk.config   # AIDK internal-ROSC overlay; no external 32.768 kHz crystal
  ap.config
  ap-sdio4.config  # optional four-line AP capability; install/rebuild on demand
```

Local proprietary bundles remain ignored and are never redistributed:

```text
armino_as_lib/versions/<manifest-version>/<profile>/
  config/
  include/
  libs/       # profile-resolved NuttX link closure
```

The only maintainer interface is:

```bash
tools/bk7258/bk7258.py sdk list
tools/bk7258/bk7258.py sdk verify --profile cp
tools/bk7258/bk7258.py sdk install \
  --profile cp --bundle PATH [--replace]
tools/bk7258/bk7258.py sdk rebuild \
  --profile cp \
  --source ../vendor/beken/bk_avdk_smp \
  --jobs 8 [--replace]
```

`sdk list` shows tracked profiles, not only locally installed proprietary
bundles. `cp-aidk` disables the vendor MP_A external-32-kHz override because
the AIDK X2/C16/C17 network is not fitted; the internal calibrated ROSC remains
the low-power source. `ap-sdio4` is an explicit hardware-capability variant.
Verification is expected to fail until an optional bundle is rebuilt or
installed. A variant is not a build input unless a board seed selects it.

Install and verify the separately locked Arm GNU toolchain before rebuilding
an SDK profile:

```bash
tools/bk7258/bk7258.py toolchain install
tools/bk7258/bk7258.py toolchain verify
```

`rebuild` requires the exact clean manifest revision, builds in a temporary
local checkout, extracts the official app link command, removes only the
NuttX-owned inputs declared by profile comments, patches the UART archive,
and transactionally replaces the bundle plus its hash comment. Rebuilds use
the SDK's deterministic archive mode, the manifest commit timestamp and
canonical debug/file paths, including the UART recompilation. Independent
temporary directories must therefore produce the same bundle-tree hash.

The compiler is the content-addressed Arm GNU release locked by
`tools/bk7258/toolchain.json`; there is no PATH or developer-supplied
toolchain fallback. A prepared bundle accepted by `install` is an optional
distribution cache and must already match the tracked profile hash. It is not
a second source or version authority. There is no registry, lock/set,
manifest/provenance pair, version selector, or Make/CMake library-name map.
