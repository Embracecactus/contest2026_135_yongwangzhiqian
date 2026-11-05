#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Inject GC2145 I2C faults below the production DVP register path.

The fixture compiles, unchanged, the team `bk7258_dvp_sensor_read/write()`
and `bk7258_dvp_error()`, the pinned SDK `bk_dvp_sensor_*_register()` and the
pinned SDK `gc2145_*_register()` callbacks.  Only the SDK I2C helpers
`dvp_camera_i2c_{read,write}_uint8()` are replaced, so a failure injected at
the sensor bus must surface through every real layer above it.

`--sdk-patch` applies a candidate SDK change to temporary copies of the two
SDK files; the pinned checkout is never modified.
"""

import argparse
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
SDK = ROOT.parent / "vendor/beken/bk_avdk_smp"
TEAM = ROOT / "chips/bk7258/ap/bk7258_dvp.c"
SDK_DVP = Path("ap/components/bk_dvp/src/bk_dvp.c")
SDK_GC2145 = Path("ap/components/bk_peripheral/src/dvp/dvp_gc2145.c")


def function(source: str, signature: str) -> str:
    """Return the full definition whose header matches `signature`."""
    match = re.search(signature + r"\s*\{", source)
    if match is None:
        raise RuntimeError(f"function not found: {signature}")
    depth = 0
    for index in range(match.end() - 1, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[match.start() : index + 1]
    raise RuntimeError(f"unclosed function: {signature}")


def gc2145_macros(source: str) -> str:
    """Return the GC2145 address and bus macros the callbacks may use."""
    address = re.search(r"#define GC2145_WRITE_ADDRESS\b.*", source)
    bus = re.search(
        r"#define SENSOR_I2C_READ\(.*?\n\s*\} while \(0\)\n"
        r"\s*#define SENSOR_I2C_WRITE\(.*?\n\s*\} while \(0\)",
        source,
        re.DOTALL,
    )
    if address is None or bus is None:
        raise RuntimeError("GC2145 bus macros not found")
    return address.group(0) + "\n" + bus.group(0) + "\n"


PREFIX = r"""
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "bk_err.h"
/* Same value as ap/include/driver/i2c_types.h, without its driver deps. */
#define BK_ERR_I2C_ACK_TIMEOUT (BK_ERR_I2C_BASE - 4)
#define FAR
typedef void *camera_handle_t;
typedef struct { uint32_t reg; uint32_t val; } dvp_sensor_reg_val_t;
typedef struct
{
  int (*read_register)(uint32_t reg, uint32_t *val);
  int (*write_register)(uint32_t reg, uint32_t val);
} dvp_sensor_config_t;
typedef struct { const dvp_sensor_config_t *sensor; } dvp_driver_handle_t;
struct bk7258_dvp_s { camera_handle_t handle; };

/* Fault seam: the SDK I2C helpers return 0 or an SDK error code. */
static uint8_t g_bus[256];
static int g_fail_next;
static unsigned int g_reads, g_writes;

static int take_fault(void)
{
  int ret = g_fail_next;
  g_fail_next = 0;
  return ret;
}

int dvp_camera_i2c_read_uint8(uint8_t addr, uint8_t reg, uint8_t *value)
{
  int ret = take_fault();
  (void)addr;
  g_reads++;
  if (ret == 0)
    {
      *value = g_bus[reg];
    }

  return ret;
}

int dvp_camera_i2c_write_uint8(uint8_t addr, uint8_t reg, uint8_t value)
{
  int ret = take_fault();
  (void)addr;
  g_writes++;
  if (ret == 0)
    {
      g_bus[reg] = value;
    }

  return ret;
}
"""

SUFFIX = r"""
static const dvp_sensor_config_t g_sensor =
{
  .read_register = gc2145_read_register,
  .write_register = gc2145_write_register,
};

static dvp_driver_handle_t g_handle = { .sensor = &g_sensor };
static struct bk7258_dvp_s g_priv = { .handle = &g_handle };

#define CHECK(id, condition) \
  do { if (!(condition)) { printf("FAIL %s\n", id); failures++; } } while (0)

int main(void)
{
  int failures = 0;
  uint8_t value;
  int ret;

  /* Baseline: normal write and read through every real layer. */
  ret = bk7258_dvp_sensor_write(&g_priv, 0x12, 0x34);
  CHECK("write-ok", ret == 0 && g_bus[0x12] == 0x34);
  value = 0;
  ret = bk7258_dvp_sensor_read(&g_priv, 0x12, &value);
  CHECK("read-ok", ret == 0 && value == 0x34);

  /* A NACKed write must reach the caller and must not change the bus. */
  g_fail_next = BK_ERR_I2C_ACK_TIMEOUT;
  ret = bk7258_dvp_sensor_write(&g_priv, 0x12, 0x56);
  CHECK("write-fault-reported", ret < 0);
  CHECK("write-fault-no-effect", g_bus[0x12] == 0x34);

  /* A failed read must fail and must not publish a value. */
  value = 0xa5;
  g_fail_next = BK_FAIL;
  ret = bk7258_dvp_sensor_read(&g_priv, 0x12, &value);
  CHECK("read-fault-reported", ret < 0);
  CHECK("read-fault-not-published", value == 0xa5);

  /* The SDK-level read must not overwrite its caller's buffer either. */
  {
    dvp_sensor_reg_val_t transfer = { .reg = 0x12, .val = 0x77 };
    g_fail_next = BK_FAIL;
    ret = bk_dvp_sensor_read_register(&g_handle, &transfer);
    CHECK("sdk-read-fault-reported", ret != BK_OK);
    CHECK("sdk-read-fault-not-published", transfer.val == 0x77);
  }

  /* One failure does not poison the next operation. */
  ret = bk7258_dvp_sensor_write(&g_priv, 0x12, 0x9a);
  CHECK("write-recovers", ret == 0 && g_bus[0x12] == 0x9a);
  value = 0;
  ret = bk7258_dvp_sensor_read(&g_priv, 0x12, &value);
  CHECK("read-recovers", ret == 0 && value == 0x9a);

  /* Every operation, including failed ones, reached the bus exactly once. */
  CHECK("bus-attempts", g_reads == 4 && g_writes == 3);

  if (failures != 0)
    {
      printf("FAIL: %d DVP register error-propagation checks\n", failures);
      return 1;
    }

  puts("PASS: GC2145 I2C read/write faults reach the caller; "
       "failed reads publish nothing; operations recover");
  return 0;
}
"""


def build_source(sdk: Path) -> str:
    team = TEAM.read_text()
    dvp = (sdk / SDK_DVP).read_text()
    gc2145 = (sdk / SDK_GC2145).read_text()
    parts = [
        PREFIX,
        gc2145_macros(gc2145),
        function(
            gc2145, r"\nint gc2145_read_register\(uint32_t reg, uint32_t \*data\)"
        ),
        function(gc2145, r"\nint gc2145_write_register\(uint32_t reg, uint32_t data\)"),
        function(dvp, r"\nbk_err_t bk_dvp_sensor_write_register\([^)]*\)"),
        function(dvp, r"\nbk_err_t bk_dvp_sensor_read_register\([^)]*\)"),
        function(team, r"\nstatic int bk7258_dvp_error\(bk_err_t error\)"),
        function(team, r"\nint bk7258_dvp_sensor_read\([^)]*\)"),
        function(team, r"\nint bk7258_dvp_sensor_write\([^)]*\)"),
        SUFFIX,
    ]
    return "\n".join(parts)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", type=Path, default=SDK)
    parser.add_argument(
        "--sdk-patch",
        type=Path,
        help="candidate SDK patch applied to temporary copies only",
    )
    args = parser.parse_args()

    with tempfile.TemporaryDirectory(prefix="dvp-register-errors-") as temporary:
        work = Path(temporary)
        sdk = args.sdk.resolve()
        if args.sdk_patch is not None:
            copy = work / "sdk"
            for relative in (SDK_DVP, SDK_GC2145):
                (copy / relative).parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(sdk / relative, copy / relative)
            subprocess.run(
                ["git", "apply", str(args.sdk_patch.resolve())],
                cwd=copy,
                check=True,
            )
            sdk = copy

        test = work / "dvp_register_errors.c"
        binary = work / "dvp_register_errors"
        test.write_text(build_source(sdk))
        subprocess.run(
            [
                "cc",
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-Wno-unused-function",
                "-I",
                str(args.sdk.resolve() / "ap/include/common"),
                str(test),
                "-o",
                str(binary),
            ],
            check=True,
        )
        return subprocess.run([str(binary)]).returncode


if __name__ == "__main__":
    raise SystemExit(main())
