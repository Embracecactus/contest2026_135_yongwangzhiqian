# BK7258 PWM Bundle Export Defect

## Symptom

`bk7258_pwm.c` includes `<driver/pwm.h>` and calls `bk_pwm_driver_init`,
`bk_pwm_init`, `bk_pwm_set_period_duty`, `bk_pwm_start`, `bk_pwm_stop`,
`bk_pwm_deinit`, `bk_pwm_driver_deinit`. All declarations are present in
`armino_as_lib/versions/v3.1.1.9/ap/include/driver/pwm.h`, but the AP
static library `libdriver.a` exposes **zero** `bk_pwm_*` symbols:

```
$ nm armino_as_lib/versions/v3.1.1.9/ap/libs/libdriver.a | grep bk_pwm
(empty)
```

Linking the AP image with the wrapper therefore fails with
`undefined reference to 'bk_pwm_*'`.

## Root Cause

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

## Fix (SDK / bundle side)

Add `#define CONFIG_PWM 1` to
`armino_as_lib/versions/v3.1.1.9/ap/config/sdkconfig.h` and re-export
`libdriver.a`. The full BK7258 build's
`build/bk7258/app/bk7258_ap/config/sdkconfig.h` is the authoritative
reference for the complete config set.

## Wrapper Side

This wrapper stays a real implementation (calls `bk_pwm_*` per the SDK
header contract); it is **not** degraded to `-ENOSYS` stubs. Once the
bundle is re-exported with `CONFIG_PWM=1`, the wrapper links and runs
unchanged. The wrapper compiles syntactically today (verified with
`arm-none-eabi-gcc -fsyntax-only -mcpu=cortex-m33`); only the link
step is blocked by the bundle defect.
