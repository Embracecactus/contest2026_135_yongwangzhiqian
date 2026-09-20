#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Replay masked FIFO-empty races through the actual pinned SDK V2 ISR."""

from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
REL = Path("ap/middleware/driver/sdio_host/sdio_host_driver.c")
SDK = ROOT.parent / "vendor/beken/bk_avdk_smp"
PATCH = ROOT / "chips/bk7258/bk_idk/sdk-profiles/v3.1.1.9/ap-sdio-tx-start.patch"

PREFIX = r"""
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#define CONFIG_SDIO_GDMA_EN 1
#define CONFIG_SDIO_V2P0 1
#define SDIO_HOST_LOGV(...) ((void)0)
#define SDIO_HOST_LOGW(...) ((void)0)
#define SEND_OP_COND 1
#define SDIO_GET_WR_STS_MAX_COUNT 10
typedef struct { struct { bool tx_fifo_empty_mask; } sd_cmd_rsp_int_mask; } hw_t;
typedef struct { hw_t *hw; } sdio_host_hal_t;
static hw_t hw;
static uint32_t pending, s_sdio_cmd_index;
static bool s_sdio_host_data_crc_error;
static struct {
  sdio_host_hal_t hal;
  uint32_t int_status;
  int tx_sema, rx_sema, irq_cmd_msg;
  bool dma_rx_en;
} s_sdio_host = { .hal = { .hw = &hw } };
#define sdio_host_hal_get_interrupt_status(h) pending
#define sdio_host_hal_get_cmd_index_interrupt_status(h,s) (((s)>>14)&63)
#define sdio_host_hal_is_cmd_rsp_interrupt_triggered(h,s) ((s)&7)
#define sdio_host_hal_is_cmd_end_interrupt_triggered(h,s) ((s)&3)
#define sdio_host_hal_is_cmd_rsp_crc_ok_interrupt_triggered(h,s) ((s)&(1u<<10))
#define sdio_host_hal_is_cmd_rsp_crc_fail_interrupt_triggered(h,s) ((s)&(1u<<11))
#define sdio_host_hal_is_cmd_rsp_timeout_interrupt_triggered(h,s) ((s)&4)
#define sdio_host_hal_is_data_write_end_int_triggered(h,s) ((s)&(1u<<4))
#define sdio_host_hal_is_fifo_empty_int_triggered(h,s) ((s)&(1u<<9))
#define sdio_host_hal_is_data_recv_end_int_triggered(h,s) ((s)&(1u<<3))
#define sdio_host_hal_is_data_timeout_int_triggered(h,s) ((s)&(1u<<5))
#define sdio_host_hal_is_data_crc_ok_int_triggered(h,s) ((s)&(1u<<12))
#define sdio_host_hal_is_data_crc_fail_int_triggered(h,s) ((s)&(1u<<13))
#define sdio_host_hal_disable_tx_fifo_empty_mask(h) ((h)->hw->sd_cmd_rsp_int_mask.tx_fifo_empty_mask=false)
#define sdio_host_hal_get_wr_status(h) 2
#define sdio_host_hal_clear_cmd_rsp_interrupt_status(h,s) (pending &= ~7u)
#define sdio_host_hal_clear_write_data_interrupt_status(h,s) (pending &= ~((1u<<4)|(1u<<7)))
#define sdio_host_hal_clear_read_data_interrupt_status(h,s) (pending &= ~((1u<<3)|(1u<<5)|(1u<<6)|(1u<<12)|(1u<<13)))
#define sdio_host_hal_clear_read_data_timeout_interrupt_status(h,s) (pending &= ~(1u<<5))
#define bk_sdio_host_reset_sd_state() ((void)0)
#define rtos_push_to_queue(q,s,t) 0
static void rtos_set_semaphore(int *s) { ++*s; }
"""

SUFFIX = r"""
int main(void) {
  /* A block ended while DMA has more data: raw empty must not release TX. */
  pending=(1u<<4)|(1u<<9); sdio_host_isr();
  if(s_sdio_host.tx_sema != 0) return 1;
  /* A CPU read completes with TX FIFO empty but its interrupt masked. */
  pending=(1u<<3)|(1u<<12)|(1u<<9); sdio_host_isr();
  if(s_sdio_host.rx_sema != 1 || (pending & (1u<<3))) return 2;
  /* DMA finish enables FIFO-empty, then drain completes exactly once. */
  hw.sd_cmd_rsp_int_mask.tx_fifo_empty_mask=true;
  pending=(1u<<9); sdio_host_isr();
  if(s_sdio_host.tx_sema != 1 || hw.sd_cmd_rsp_int_mask.tx_fifo_empty_mask) return 3;
  sdio_host_isr();
  if(s_sdio_host.tx_sema != 1) return 4;
  puts("PASS: masked FIFO empty cannot complete TX or divert RX; drain posts once");
  return 0;
}
"""


def isr(source):
    start = source.index("#if (CONFIG_SDIO_V2P0)\nstatic void sdio_host_isr(void)")
    end = source.index("\n#else\nstatic void sdio_host_isr(void)", start)
    return source[start:end] + "\n#endif\n"


with tempfile.TemporaryDirectory(prefix="sdio-dma-isr-") as temporary:
    work = Path(temporary)
    original = (SDK / REL).read_text()
    target = work / REL
    target.parent.mkdir(parents=True)
    target.write_text(original)
    subprocess.run(["patch", "--silent", "-p1", "-i", str(PATCH)], cwd=work, check=True)
    for name, source in (("original", original), ("patched", target.read_text())):
        test = work / (name + ".c")
        test.write_text(PREFIX + isr(source) + SUFFIX)
        binary = work / name
        subprocess.run(
            [
                "cc",
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                str(test),
                "-o",
                str(binary),
            ],
            check=True,
        )
        result = subprocess.run([str(binary)], check=False)
        assert result.returncode == (1 if name == "original" else 0), name
    print("PASS: original SDK reproduces premature TX completion")
