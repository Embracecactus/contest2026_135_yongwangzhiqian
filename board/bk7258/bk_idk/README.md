# BK7258 SDK profiles

The team manifest is the only source/version authority. Each maintained
profile is a tracked SDK config with one accepted bundle-tree hash comment:

```text
sdk-profiles/<manifest-version>/
  cp.config
  ap.config
  ap-sdio4.config
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

`rebuild` requires the exact clean manifest revision, builds in a temporary
local checkout, extracts the official app link command, removes only the
NuttX-owned inputs declared by profile comments, patches the UART archive,
and transactionally replaces the bundle plus its hash comment. The compiler
is the ARM prebuilt pinned by the team manifest; there is no PATH or
developer-supplied toolchain fallback. There is no
registry, lock/set, manifest/provenance pair, version selector, or Make/CMake
library-name map.
