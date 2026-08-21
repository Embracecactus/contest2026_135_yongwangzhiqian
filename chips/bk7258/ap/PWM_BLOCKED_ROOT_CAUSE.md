# BK7258 PWM Bundle Export Resolution

Status: resolved; the historical r2 overlay is retired and its settings are
part of the canonical AP SDK profile.

> **Profile status:** The old per-role drivercheck profile name formerly used
> near the end of this note is **SUPERSEDED and NON-RUNNABLE**; its deleted
> `configs/` directory must not be restored. Use the canonical build entry:
>
> ```sh
> python3 tools/bk7258/bk7258.py build \
>   --cp-config <cp-profile> --ap-config <ap-profile> --check
> ```

## Original symptom

The original base bundle contained `<driver/pwm.h>`, and the wrapper called
`bk_pwm_driver_init`,
`bk_pwm_init`, `bk_pwm_set_period_duty`, `bk_pwm_start`, `bk_pwm_stop`,
`bk_pwm_deinit`, `bk_pwm_driver_deinit`. All declarations are present in
`armino_as_lib/versions/v3.1.1.9/ap/include/driver/pwm.h`, but the AP
static library `libdriver.a` exposed **zero** `bk_pwm_*` symbols:

```
$ nm armino_as_lib/versions/v3.1.1.9/ap/libs/libdriver.a | grep bk_pwm
(empty)
```

Linking the AP image with the wrapper therefore failed with
`undefined reference to 'bk_pwm_*'`.

## Root cause

The bundle-export config
`armino_as_lib/versions/v3.1.1.9/ap/config/sdkconfig.h` carries the
PWM sub-option:

```c
#define CONFIG_PWM_V1PX 1
#define CONFIG_PWM_USE_DEFAULT_GPIO_MAP 1
```

but is **missing the parent switch**:

```c
#define CONFIG_PWM 1
```

The SDK build script `ap/middleware/driver/CMakeLists.txt` gates PWM
compilation in two levels:

```cmake
if (CONFIG_PWM)
  if (CONFIG_PWM_V1PX)
    list(APPEND srcs pwm/v1px/pwm_driver.c)
  endif()
endif()
```

With `CONFIG_PWM` absent, `pwm_driver.c` is not compiled into
`libdriver.a`, so no `bk_pwm_*` symbols are exported.

## Resolution

The repository now owns the canonical
`bk_idk/sdk-profiles/v3.1.1.9/ap.config`.  `bk7258.py sdk rebuild` copies the
official `projects/app` project to a temporary directory, applies that tracked
profile, and builds with an external temporary build directory.
The official v3.1.1.9 SDK source tree is therefore never edited.

The regenerated AP bundle exports the real `bk_pwm_*` implementation from
`libdriver.a`; its complete file hashes and profile hash are recorded in
`bk_idk/manifests/v3.1.1.9/ap.sha256` and `ap.provenance`.

## Wrapper Side

This wrapper remains a real NuttX PWM lower half and is **not** degraded to
`-ENOSYS` stubs.  The SDK driver's direct `delay_ms()` dependency is routed
to the existing NuttX-aware OS adapter.  The historical per-role drivercheck
image referenced by the original investigation is **SUPERSEDED and
NON-RUNNABLE** after the profile retirement.  The `driver_coverage` validation
suite on `t5_board_bringup` is the canonical replacement; its resolved role
view must compile and link with `CONFIG_BK7258_PWM=y`, and ELF inspection must
confirm both `bk7258_pwm_initialize` and `bk_pwm_driver_init` are present.
