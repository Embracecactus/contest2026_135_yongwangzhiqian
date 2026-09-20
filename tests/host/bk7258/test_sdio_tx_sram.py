#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Host regression for the real BK7258 SDIO GDMA TX SRAM snapshot."""

import pathlib
import re
import subprocess
import sys
import tempfile

DEFAULT_SOURCE = pathlib.Path(__file__).parents[3] / "chips/bk7258/ap/bk7258_sdio.c"


def extract(source: pathlib.Path):
    text = source.read_text()
    starts = [
        m.start() for m in re.finditer(r"static int bk7258_sdio_sendsetup\(", text)
    ]
    if len(starts) < 2:
        raise RuntimeError("sendsetup definition not found")
    start = starts[-1]
    brace = text.index("{", start)
    depth = 0
    for pos in range(brace, len(text)):
        if text[pos] == "{":
            depth += 1
        elif text[pos] == "}":
            depth -= 1
            if depth == 0:
                end = pos + 1
                break
    else:
        raise RuntimeError("sendsetup definition is incomplete")
    function = text[start:end]
    declaration = re.search(
        r"#ifdef CONFIG_SDIO_GDMA_EN\s*static uint32_t g_sdio_tx_sram\[8192 / sizeof\(uint32_t\)\];\s*#endif",
        text,
    )
    if not declaration:
        raise RuntimeError("real g_sdio_tx_sram declaration not found")
    return declaration.group(0) + "\n", function


def main():
    source = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_SOURCE
    decl, function = extract(source)
    harness = r"""#include <errno.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#define FAR
#define OK 0
#define BK_OK 0
#define BK_FAIL (-1)
#define CONFIG_SDIO_V2P0 1
#define CONFIG_SDIO_GDMA_EN 1
#define CONFIG_SDIO_BLOCKSETUP 1
typedef int bk_err_t;
typedef int dma_id_t;
typedef unsigned irqstate_t;
struct sdio_dev_s { int unused; };
typedef struct { uint32_t data_timeout, data_len, data_block_size, data_dir; } sdio_host_data_config_t;
#define SDIO_HOST_DATA_DIR_WR 1
struct bk7258_sdio_priv_s {
  struct sdio_dev_s dev;
  bool initialized;
  FAR uint8_t *xfer_buf;
  size_t xfer_nbytes, blocklen, nblocks;
  bool xfer_is_read, xfer_pending, single_write, single_read;
  uint32_t data_timeout;
  dma_id_t dma_tx_channel;
};
static int finish_stop_seen, finish_disable_seen;
static int config_result, write_result, dma_enabled;
static const uint8_t *write_ptr;
static uint32_t write_len;
static uint8_t write_copy[8192];
static int stop_calls, disable_calls;
static int bk7258_sdio_finish_single_transfer(struct bk7258_sdio_priv_s *p, int ret)
{
  (void)p; finish_stop_seen = stop_calls; finish_disable_seen = disable_calls; return ret;
}
static int bk7258_sdio_map_err(int e) { return e == BK_OK ? OK : -EIO; }
static bk_err_t bk_sdio_host_config_data(const sdio_host_data_config_t *c)
{ (void)c; return config_result; }
static bk_err_t bk_dma_enable_finish_interrupt(dma_id_t c) { (void)c; return BK_OK; }
static int bk_dma_get_enable_status(dma_id_t c) { (void)c; return dma_enabled; }
static bk_err_t bk_dma_stop(dma_id_t c) { (void)c; stop_calls++; return BK_OK; }
static bk_err_t bk_dma_disable_finish_interrupt(dma_id_t c) { (void)c; disable_calls++; return BK_OK; }
static irqstate_t enter_critical_section(void) { return 0; }
static void leave_critical_section(irqstate_t f) { (void)f; }
static void bk_sdio_host_reset_sd_state(void) {}
static bk_err_t bk_sdio_host_write_fifo(const uint8_t *p, uint32_t n)
{ write_ptr = p; write_len = n; if (n <= sizeof(write_copy)) memcpy(write_copy, p, n); return write_result; }
"""
    main = r"""
static void reset_case(struct bk7258_sdio_priv_s *p, size_t n)
{
  memset(p, 0, sizeof(*p)); p->initialized = true; p->blocklen = 512; p->nblocks = n / 512;
  config_result = BK_OK; write_result = BK_OK; dma_enabled = 0;
  stop_calls = 0; disable_calls = 0; finish_stop_seen = 0; finish_disable_seen = 0; write_ptr = NULL; write_len = 0;
}
static void expect(int ok, const char *what) { if (!ok) { fprintf(stderr, "FAIL: %s\n", what); exit(1); } }
int main(void)
{
  struct bk7258_sdio_priv_s p; static uint8_t src[8192] __attribute__((aligned(4)));
  for (size_t i = 0; i < sizeof(src); i++) src[i] = (uint8_t)(i * 37u + 11u);
  for (size_t n = 512; n <= 8192; n += 768) {
    if (n != 512 && n != 8192) continue;
    reset_case(&p, n); expect(bk7258_sdio_sendsetup(&p.dev, src, n) == 0, "accepted size");
    expect(write_ptr != src && write_len == n, "SRAM pointer and length");
    expect(memcmp(write_copy, src, n) == 0, "SRAM bytes");
  }
  reset_case(&p, 512); expect(bk7258_sdio_sendsetup(&p.dev, NULL, 512) < 0, "NULL rejected"); expect(write_ptr == NULL, "NULL no SDK write");
  reset_case(&p, 0); expect(bk7258_sdio_sendsetup(&p.dev, src, 0) < 0, "zero rejected"); expect(write_ptr == NULL, "zero no SDK write");
  reset_case(&p, 17); expect(bk7258_sdio_sendsetup(&p.dev, src, 8704) < 0, "oversize rejected"); expect(write_ptr == NULL, "oversize no SDK write");
  reset_case(&p, 2); expect(bk7258_sdio_sendsetup(&p.dev, src, 512) < 0, "block contract rejected"); expect(write_ptr == NULL, "contract no SDK write");
  reset_case(&p, 512); write_result = -EIO; expect(bk7258_sdio_sendsetup(&p.dev, src, 512) < 0, "SDK failure propagated"); expect(stop_calls == 1 && disable_calls == 1 && finish_stop_seen == 1 && finish_disable_seen == 1, "SDK failure DMA cleanup before return");
  reset_case(&p, 512); dma_enabled = 1; expect(bk7258_sdio_sendsetup(&p.dev, src, 512) < 0, "armed DMA rejected"); expect(stop_calls == 1 && disable_calls == 1 && finish_stop_seen == 1 && finish_disable_seen == 1, "armed DMA cleanup before return");
  puts("PASS"); return 0;
}
"""
    with tempfile.TemporaryDirectory() as td:
        c = pathlib.Path(td) / "test.c"
        exe = pathlib.Path(td) / "test"
        c.write_text(harness + decl + function + main)
        subprocess.run(
            ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", str(c), "-o", str(exe)],
            check=True,
        )
        subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    main()
