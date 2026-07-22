# BK7258 T5-AI SDK Bundle (bk_idk)

## Overview

`armino_as_lib/` is the Beken BK7258 SDK prebuilt static library bundle. It is
generated/copied locally from an authorized Beken or Tuya SDK distribution and
is **intentionally not versioned** in this repository because it contains
BEKEN proprietary/restricted prebuilts that are not authorized for
redistribution here.

## Expected layout

The expected root for the SDK bundle is:

```text
bk_idk/armino_as_lib/cp/
  include/   -- 341 SDK header files
  config/    -- 2 SDK configuration headers
  libs/      -- 81 static library archives (.a) + 4 .obj object files
```

## Linked libraries

`scripts/Make.defs` links **31 of the 81** local `.a` libraries after
`BK_EXCLUDE_LIBS` filtering. The 50 excluded libraries include CMSIS startup,
coredump/unity test tools, networking (lwip, wpa_supplicant, wifi), crypto
(psa_mbedtls, hmac), and duplicate Bluetooth controller/host variants. None
of the excluded or included libraries are redistributed in this repository.

The 4 `.obj` files (`port.c.obj`, `portasm.c.obj`, `rtos_init.c.obj`,
`startup_bk7236.c.obj`) are also not linked or redistributed.

## Setup script

Use `scripts/setup_bk7258_sdk.sh` to validate or install the SDK bundle:

```bash
# Validate the default installed bundle against the tracked checksum manifest
scripts/setup_bk7258_sdk.sh --check

# Validate a specific cp directory
scripts/setup_bk7258_sdk.sh --check /path/to/authorized/cp

# Install from an authorized Beken/Tuya SDK cp bundle
# (refuses if destination already exists; does not overwrite)
scripts/setup_bk7258_sdk.sh --install /path/to/authorized/armino_as_lib/cp
```

The script performs **no network download**. The source must be an authorized
Beken/Tuya SDK `armino_as_lib/cp` bundle obtained separately.

## Checksum manifest

`scripts/bk7258_sdk_manifest.sha256` contains SHA-256 checksums for all 374
tracked files (341 headers + 2 config files + 31 linked libraries). This pins
the exact binary content of the local bundle for reproducibility.

## Future self-contained distribution

A future self-contained distribution of the SDK bundle (committed directly or
via submodule) requires **explicit Beken redistribution approval** and is
preferably housed in a separate vendor-owned repository/manifest project, as
demonstrated by the BK7236N `vendor_beken` precedent (dedicated vendor repo,
plain Git blobs, explicit `LICENSE-NOTES.md`).
