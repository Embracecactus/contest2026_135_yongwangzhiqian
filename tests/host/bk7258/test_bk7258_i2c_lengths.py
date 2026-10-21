#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Exercise the production I2C transfer validator before SDK calls."""
import subprocess
import tempfile
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
SOURCE = ROOT / "chips/bk7258/ap/bk7258_i2c.c"


def function(source: str, name: str) -> str:
    match = re.search(rf"static int {name}\([^;]*?\)\n{{", source, re.DOTALL)
    if match is None:
        raise RuntimeError("production function not found")
    start = match.start()
    brace = match.end() - 1
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}" and not (depth := depth - 1):
            return source[start : index + 1]
    raise RuntimeError("unclosed production function")


transfer = function(SOURCE.read_text(), "bk7258_i2c_transfer")
fixture = f"""
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <inttypes.h>
#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#define FAR
#define OK 0
#define BK7258_I2C_MAX_MSG 8
#define CONFIG_BK7258_I2C_TIMEOUT_MS 50
#define I2C_M_READ 1u
#define I2C_M_TEN 2u
#define I2C_M_NOSTOP 4u
#define I2C_M_NOSTART 8u
typedef int mutex_t;
typedef int i2c_id_t;
typedef int bk_err_t;
typedef struct {{ uint16_t dev_addr; uint32_t mem_addr; int mem_addr_size;
                 uint8_t *data; uint32_t data_size; uint32_t timeout_ms; }} i2c_mem_param_t;
#define I2C_MEM_ADDR_SIZE_8BIT 1
#define I2C_MEM_ADDR_SIZE_16BIT 2
struct i2c_master_s {{ int unused; }};
struct i2c_msg_s {{ uint32_t frequency; uint16_t addr; uint16_t flags;
                    uint8_t *buffer; ssize_t length; }};
struct bk7258_i2c_priv_s {{ struct i2c_master_s dev; mutex_t lock; i2c_id_t id;
                             uint32_t baud; int initialized; }};
static unsigned int sdk_calls, lock_calls, baud_calls, memory_calls;
static int nxmutex_lock(mutex_t *lock) {{ (void)lock; lock_calls++; return 0; }}
static int nxmutex_unlock(mutex_t *lock) {{ (void)lock; return 0; }}
static int bk7258_i2c_set_baud(struct bk7258_i2c_priv_s *priv, uint32_t hz)
{{ (void)priv; (void)hz; baud_calls++; return 0; }}
static int bk7258_i2c_map_err(bk_err_t error) {{ return error; }}
static int bk_i2c_master_read(i2c_id_t id, uint16_t addr, uint8_t *data,
                               uint32_t length, unsigned int timeout)
{{ (void)id; (void)addr; (void)data; (void)length; (void)timeout; sdk_calls++; return 0; }}
static int bk_i2c_master_write(i2c_id_t id, uint16_t addr, uint8_t *data,
                                uint32_t length, unsigned int timeout)
{{ (void)id; (void)addr; (void)data; (void)length; (void)timeout; sdk_calls++; return 0; }}
static int bk_i2c_memory_read(i2c_id_t id, i2c_mem_param_t *param)
{{ (void)id; (void)param; sdk_calls++; memory_calls++; return 0; }}
static int bk_i2c_memory_write(i2c_id_t id, i2c_mem_param_t *param)
{{ (void)id; (void)param; sdk_calls++; memory_calls++; return 0; }}
{transfer}
static void reset(void) {{ sdk_calls = lock_calls = baud_calls = memory_calls = 0; }}
static void rejected(struct bk7258_i2c_priv_s *priv, struct i2c_msg_s *msgs,
                     int count, int expected)
{{ reset(); assert(bk7258_i2c_transfer(&priv->dev, msgs, count) == expected);
   assert(sdk_calls == 0 && lock_calls == 0 && baud_calls == 0); }}
int main(void)
{{ struct bk7258_i2c_priv_s priv = {{ .initialized = 1 }};
   uint8_t one[2] = {{0, 1}}, data[4] = {{0}};
   struct i2c_msg_s msg[3] = {{
     {{ .addr=0x22, .buffer=one, .length=1 }},
     {{ .addr=0x22, .buffer=data, .length=1 }},
     {{ .addr=0x22, .buffer=data, .length=1 }} }};
   for (int index = 0; index < 3; index++) {{
     msg[index].length = -1; rejected(&priv, msg, 3, -EINVAL); msg[index].length = 1;
     msg[index].length = 0; rejected(&priv, msg, 3, -EINVAL); msg[index].length = 1;
     if (sizeof(ssize_t) > 4) {{ msg[index].length = (ssize_t)UINT32_MAX + 1;
       rejected(&priv, msg, 3, -E2BIG); msg[index].length = 1; }}
   }}
   reset(); assert(bk7258_i2c_transfer(&priv.dev, msg, 1) == 0);
   assert(sdk_calls == 1 && memory_calls == 0 && lock_calls == 1);
   msg[0].flags = I2C_M_NOSTOP; msg[0].length = 1;
   msg[1].flags = I2C_M_READ; msg[1].length = 4;
   reset(); assert(bk7258_i2c_transfer(&priv.dev, msg, 2) == 0);
   assert(sdk_calls == 1 && memory_calls == 1 && lock_calls == 1);
   return 0; }}
"""

with tempfile.TemporaryDirectory(prefix="bk7258-i2c-length-") as directory:
    path = Path(directory)
    source = path / "fixture.c"
    binary = path / "fixture"
    source.write_text(fixture)
    subprocess.run(
        [
            "cc",
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            str(source),
            "-o",
            str(binary),
        ],
        check=True,
    )
    subprocess.run([str(binary)], check=True)
