#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Exercise actual NuttX audio lifecycle functions with host boundary mocks.

This checks upper ownership and callback lock ordering, not the BK7258 hardware
stop sequence. The lower half, allocators and locks are host seams.
"""

import argparse
from pathlib import Path
import re
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[3]


def extract(source, name):
    match = re.search(r"static int " + name + r"\([^)]*\)\s*\{", source)
    if not match:
        raise AssertionError(f"missing {name}")
    depth = 0
    for index in range(match.end() - 1, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[match.start() : index + 1]
    raise AssertionError(f"unterminated {name}")


PREFIX = r"""
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#define FAR
#define OK 0
#define DEBUGASSERT assert
#define auderr(...) ((void)0)
typedef int irqstate_t;
typedef int mutex_t;
typedef int spinlock_t;
struct inode { void *i_private; };
struct file { struct inode *f_inode; void *f_priv; };
struct audio_info_s { int type; };
struct audio_status_s { unsigned head, tail; };
struct ap_buffer_s { bool returned; };
struct audio_buf_desc_s { union { struct ap_buffer_s *buffer; } u; };
struct audio_lowerhalf_s;
struct audio_ops_s {
  int (*shutdown)(struct audio_lowerhalf_s *);
  int (*freebuffer)(struct audio_lowerhalf_s *, struct audio_buf_desc_s *);
};
struct audio_lowerhalf_s { struct audio_ops_s *ops; };
struct audio_openpriv_s { struct audio_openpriv_s *flink; };
struct audio_upperhalf_s {
  struct audio_info_s info;
  mutex_t lock;
  spinlock_t spinlock;
  unsigned periods;
  struct ap_buffer_s **apbs;
  struct audio_lowerhalf_s *dev;
  struct audio_openpriv_s *head;
  struct audio_status_s *status;
};
static int shutdowns, returned, freed_buffers, fail_shutdown;
static struct audio_upperhalf_s *g_upper;
static int nxmutex_lock(mutex_t *lock)
{
  assert(!*lock);
  *lock = 1;
  return 0;
}
static void nxmutex_unlock(mutex_t *lock)
{
  assert(*lock);
  *lock = 0;
}
static irqstate_t spin_lock_irqsave(spinlock_t *lock)
{
  /* Fail deterministically instead of hanging on a recursive acquisition. */
  assert(!*lock);
  *lock = 1;
  return 0;
}
static void spin_unlock_irqrestore(spinlock_t *lock, irqstate_t flags)
{
  (void)flags;
  assert(*lock);
  *lock = 0;
}
static void *kmm_zalloc(size_t size) { return calloc(1, size); }
static void *kumm_zalloc(size_t size) { return calloc(1, size); }
static void kmm_free(void *ptr) { free(ptr); }
static void kumm_free(void *ptr)
{
  assert(ptr != g_upper->status);
  free(ptr);
}
static void apb_free(struct ap_buffer_s *apb)
{
  assert(apb->returned);
  freed_buffers++;
  free(apb);
}
static void lower_dequeue(struct ap_buffer_s *apb)
{
  irqstate_t flags = spin_lock_irqsave(&g_upper->spinlock);
  assert(g_upper->status != NULL && g_upper->head == NULL);
  assert(!apb->returned);
  apb->returned = true;
  g_upper->status->tail++;
  returned++;
  spin_unlock_irqrestore(&g_upper->spinlock, flags);
}
static int lower_shutdown(struct audio_lowerhalf_s *lower)
{
  (void)lower;
  shutdowns++;
  assert(g_upper->lock && !g_upper->spinlock && g_upper->status);
  if (fail_shutdown) return -EBUSY;
  for (unsigned i = 0; i < g_upper->periods; i++)
    if (!g_upper->apbs[i]->returned) lower_dequeue(g_upper->apbs[i]);
  return 0;
}
static int audio_shutdown(struct audio_upperhalf_s *);
"""

SUFFIX = r"""
static void queue_buffers(struct audio_upperhalf_s *upper)
{
  assert(!upper->apbs && !upper->periods);
  upper->periods = 2;
  upper->apbs = calloc(2, sizeof(*upper->apbs));
  for (unsigned i = 0; i < 2; i++)
    upper->apbs[i] = calloc(1, sizeof(**upper->apbs));
  upper->status->head = 2;
  upper->info.type = 9;
}
int main(void)
{
  struct audio_ops_s ops = { .shutdown = lower_shutdown };
  struct audio_lowerhalf_s lower = { .ops = &ops };
  struct audio_upperhalf_s upper = { .dev = &lower };
  struct inode inode = { .i_private = &upper };
  struct file a = { .f_inode = &inode }, b = { .f_inode = &inode };
  struct audio_status_s *old_status;
  struct ap_buffer_s **old_apbs;
  int calls;
  g_upper = &upper;

  /* Queued without START: synchronous callbacks may acquire the spinlock. */
  assert(audio_open(&a) == 0);
  queue_buffers(&upper);
  assert(audio_close(&a) == 0);
  assert(shutdowns == 1 && returned == 2 && freed_buffers == 2);
  assert(!a.f_priv && !upper.status && !upper.apbs && !upper.periods);
  assert(!upper.info.type && !upper.lock && !upper.spinlock);

  /* Failed close drops the fd but must retain callback state and buffers. */
  assert(audio_open(&a) == 0);
  queue_buffers(&upper);
  old_status = upper.status;
  old_apbs = upper.apbs;
  fail_shutdown = 1;
  assert(audio_close(&a) == -EBUSY);
  assert(!a.f_priv && !upper.head);
  assert(upper.status == old_status && upper.apbs == old_apbs);
  assert(upper.periods == 2 && freed_buffers == 2);
  lower_dequeue(upper.apbs[0]);
  assert(upper.status->tail == 1);
  assert(audio_open(&b) == -EBUSY);
  assert(!b.f_priv && !upper.head && upper.status == old_status);
  assert(upper.apbs == old_apbs && upper.periods == 2);
  fail_shutdown = 0;
  assert(audio_open(&b) == 0);
  assert(upper.status && !upper.status->head && !upper.status->tail);
  assert(!upper.apbs && !upper.periods && freed_buffers == 4);
  assert(audio_close(&b) == 0 && !upper.status);

  /* Multiple opens: only final close can shut down the lower half. */
  assert(audio_open(&a) == 0 && audio_open(&b) == 0);
  old_status = upper.status;
  calls = shutdowns;
  assert(audio_close(&a) == 0);
  assert(shutdowns == calls && upper.status == old_status && upper.head);
  assert(audio_close(&b) == 0);
  assert(shutdowns == calls + 1 && !upper.status && !upper.head);
  puts("PASS: actual audio lifecycle; callback lock, failed close, reopen, "
       "shared buffer reclamation, multiple opens");
  return 0;
}
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--nuttx", type=Path, default=ROOT.parent / "nuttx")
    args = parser.parse_args()
    source = (args.nuttx / "audio/audio.c").read_text()
    functions = "\n".join(
        extract(source, name)
        for name in ("audio_open", "audio_close", "audio_freebuffer", "audio_shutdown")
    )
    with tempfile.TemporaryDirectory(prefix="audio-close-lifecycle-") as temporary:
        work = Path(temporary)
        test = work / "audio.c"
        test.write_text(PREFIX + functions + SUFFIX)
        binary = work / "audio"
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
        subprocess.run([str(binary)], check=True, timeout=10)


if __name__ == "__main__":
    main()
