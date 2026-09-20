#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Execute the actual AP haptic service against deterministic RPMsg/VFS mocks.

Unittest discovery consumes this test. The mocked semaphore yields only when
all queued work is drained; no target build, hardware or real FF device is used.
"""
from __future__ import annotations

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

REPOSITORY = Path(__file__).resolve().parents[3]

MOCK = r"""
#pragma once
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <syslog.h>
#include <unistd.h>
#define FAR
#define CODE
#define CONFIG_BK7258_HAPTIC_SERVICE 1
#define CONFIG_PRIORITY_INHERITANCE 1
#define CONFIG_BK7258_HAPTIC_DEVPATH "/dev/input_ff0"
#define CONFIG_BK7258_HAPTIC_RPC_PRIORITY 100
#define CONFIG_BK7258_HAPTIC_RPC_STACKSIZE 2048
#define NXMUTEX_INITIALIZER 0
#define SP_UNLOCKED 0
#define SEM_PRIO_NONE 0
#define RPMSG_ADDR_ANY UINT32_MAX
#define BITS_TO_LONGS(n) (((n)+8*sizeof(unsigned long)-1)/(8*sizeof(unsigned long)))
#define test_bit(n,a) (((a)[(n)/(8*sizeof(unsigned long))] >> ((n)%(8*sizeof(unsigned long)))) & 1ul)
#define _FFIOC(n) (100+(n))
typedef int mutex_t;
typedef int spinlock_t;
typedef unsigned int irqstate_t;
typedef int sem_t;
struct rpmsg_device { const char *cpu; };
struct rpmsg_endpoint { void *priv; struct rpmsg_device *rdev; bool ready; };
static jmp_buf worker_idle;
static bool in_worker;
static int in_callback;
static int (*worker_entry)(int, char **);
static int nxmutex_lock(mutex_t *m) { assert(!*m); *m=1; return 0; }
static int nxmutex_unlock(mutex_t *m) { assert(*m); *m=0; return 0; }
static irqstate_t spin_lock_irqsave(spinlock_t *s) { assert(!*s); *s=1; return 0; }
static void spin_unlock_irqrestore(spinlock_t *s, irqstate_t flags) {
  (void)flags; assert(*s); *s=0;
}
static int nxsem_init(sem_t *s, int shared, unsigned int count) {
  (void)shared; *s=count; return 0;
}
static int nxsem_destroy(sem_t *s) { *s=0; return 0; }
static int nxsem_post(sem_t *s) { (*s)++; return 0; }
static int nxsem_set_protocol(sem_t *s, int protocol) {
  (void)s; (void)protocol; return 0;
}
static int nxsem_wait_uninterruptible(sem_t *s) {
  assert(in_worker);
  if (!*s) longjmp(worker_idle, 1);
  (*s)--; return 0;
}
static int task_create(const char *name, int priority, int stack,
                       int (*entry)(int,char **), char **args) {
  (void)name; (void)priority; (void)stack; (void)args;
  worker_entry=entry; return 42;
}
static const char *rpmsg_get_cpuname(struct rpmsg_device *r) { return r->cpu; }
static bool is_rpmsg_ept_ready(struct rpmsg_endpoint *e) { return e->ready; }
static void rpmsg_destroy_ept(struct rpmsg_endpoint *e) { e->ready=false; }
static int rpmsg_create_ept(struct rpmsg_endpoint *e, struct rpmsg_device *r,
  const char *name, uint32_t addr, uint32_t dest,
  int (*cb)(struct rpmsg_endpoint *,void *,size_t,uint32_t,void *),
  void (*unbind)(struct rpmsg_endpoint *)) {
  (void)name; (void)addr; (void)dest; (void)cb; (void)unbind;
  e->rdev=r; e->ready=true; return 0;
}
static int rpmsg_register_callback(void *priv,
  void (*created)(struct rpmsg_device *,void *),
  void (*destroyed)(struct rpmsg_device *,void *),
  bool (*match)(struct rpmsg_device *,void *,const char *,uint32_t),
  void (*bind)(struct rpmsg_device *,void *,const char *,uint32_t)) {
  (void)priv; (void)created; (void)destroyed; (void)match; (void)bind; return 0;
}
static void rpmsg_unregister_callback(void *priv,
  void (*created)(struct rpmsg_device *,void *),
  void (*destroyed)(struct rpmsg_device *,void *),
  bool (*match)(struct rpmsg_device *,void *,const char *,uint32_t),
  void (*bind)(struct rpmsg_device *,void *,const char *,uint32_t)) {
  (void)priv; (void)created; (void)destroyed; (void)match; (void)bind;
}
static int rpmsg_trysend(struct rpmsg_endpoint *, const void *, int);
static int mock_open(const char *, int, ...);
static int mock_close(int);
static ssize_t mock_write(int, const void *, size_t);
static int mock_ioctl(int, unsigned long, ...);
static void mock_syslog(int priority, const char *format, ...) {
  (void)priority; (void)format;
}
#define open mock_open
#define close mock_close
#define write mock_write
#define ioctl mock_ioctl
#define syslog mock_syslog
"""

HARNESS = r"""
#include "bk7258_haptic_service.c"

static struct rpmsg_device remote = { .cpu="cp" };
static struct bkhaptic_rpc_response_s replies[32];
static struct bkhaptic_rpc_request_s repeat_request;
static int reply_count, opens, closes, uploads, plays, stops, erases, queries;
static int upload_error, play_error, stop_error, erase_error;
static bool no_rumble, no_effects, repeat_active, disconnect_upload;

static void vfs_context(void) { assert(in_worker && !in_callback); }
static int mock_open(const char *path, int flags, ...) {
  vfs_context(); assert(!strcmp(path, CONFIG_BK7258_HAPTIC_DEVPATH));
  assert(flags == O_RDWR); opens++; return 7;
}
static int mock_close(int fd) { vfs_context(); assert(fd==7); closes++; return 0; }
static int deliver(struct bkhaptic_rpc_request_s *request) {
  int ret;
  in_callback++;
  ret=bkhaptic_server_cb(&g_bkhaptic.endpoint, request, sizeof(*request),
                         1, &g_bkhaptic);
  in_callback--; return ret;
}
static void disconnect(void) {
  in_callback++;
  bkhaptic_device_destroy(&remote, &g_bkhaptic);
  in_callback--;
}
static ssize_t mock_write(int fd, const void *data, size_t length) {
  const struct ff_event_s *event=data;
  vfs_context(); assert(fd==7 && length==sizeof(*event) && event->code==0);
  if (event->value) {
    assert(event->value==1); plays++;
    if (repeat_active) {
      repeat_active=false;
      assert(deliver(&repeat_request)==0);
      assert(deliver(&repeat_request)==0);
    }
    if (play_error) { errno=play_error; return -1; }
  } else {
    stops++;
    if (stop_error) { errno=stop_error; return -1; }
  }
  return length;
}
static int mock_ioctl(int fd, unsigned long cmd, ...) {
  unsigned long arg;
  va_list ap;
  vfs_context(); assert(fd==7);
  va_start(ap,cmd); arg=va_arg(ap,unsigned long); va_end(ap);
  if (cmd==EVIOCGBIT) {
    queries++;
    if (!no_rumble) ((unsigned long *)(uintptr_t)arg)[0] |= 1ul << FF_RUMBLE;
  } else if (cmd==EVIOCGEFFECTS) {
    queries++; *(int *)(uintptr_t)arg=no_effects ? 0 : 1;
  } else if (cmd==EVIOCSFF) {
    struct ff_effect *effect=(void *)(uintptr_t)arg;
    uploads++;
    assert(effect->id==-1 || effect->id==0);
    assert(effect->type==FF_RUMBLE && effect->replay.length==60);
    assert(!effect->replay.delay && !effect->direction &&
           !effect->trigger.button && !effect->trigger.interval);
    assert(effect->u.rumble.strong_magnitude || effect->u.rumble.weak_magnitude);
    if (upload_error) { errno=upload_error; return -1; }
    effect->id=0;
    if (disconnect_upload) { disconnect_upload=false; disconnect(); }
  } else if (cmd==EVIOCRMFF) {
    assert(arg==0); erases++;
    if (erase_error) { errno=erase_error; return -1; }
  } else assert(!"unexpected ioctl");
  return 0;
}
static int rpmsg_trysend(struct rpmsg_endpoint *endpoint,
                          const void *data, int length) {
  assert(in_worker && !in_callback && endpoint->ready);
  assert(length==sizeof(replies[0]) && reply_count<32);
  memcpy(&replies[reply_count++],data,length); return 0;
}
static void pump(void) {
  assert(worker_entry && !in_worker);
  in_worker=true;
  if (!setjmp(worker_idle)) worker_entry(0,NULL);
  in_worker=false;
}
static struct bkhaptic_rpc_request_s request(uint16_t command, uint32_t seq) {
  struct bkhaptic_rpc_request_s r={
    .magic=BKHAPTIC_RPC_MAGIC, .version=BKHAPTIC_RPC_VERSION,
    .command=command, .session=11, .sequence=seq,
    .duration_ms=command==BKHAPTIC_RPC_PULSE ? 60 : 0
  };
  return r;
}
static void exchange(struct bkhaptic_rpc_request_s *r, int expected) {
  int before=reply_count;
  assert(deliver(r)==0); pump();
  assert(reply_count==before+1);
  assert(replies[before].session==r->session &&
         replies[before].sequence==r->sequence &&
         replies[before].command==(r->command|BKHAPTIC_RPC_RESPONSE));
  assert(replies[before].status==expected);
  if (expected) assert(!replies[before].accepted_ms);
}
int main(int argc, char **argv) {
  struct bkhaptic_rpc_request_s pulse=request(BKHAPTIC_RPC_PULSE,1);
  struct bkhaptic_rpc_request_s status=request(BKHAPTIC_RPC_STATUS,2);
  struct bkhaptic_rpc_request_s stop=request(BKHAPTIC_RPC_STOP,3);
  assert(argc==2);
  assert(bkhaptic_service_initialize()==0);
  bkhaptic_ns_bind(&remote,&g_bkhaptic,BKHAPTIC_RPC_ENDPOINT,1);
  if (!strcmp(argv[1],"status")) {
    exchange(&status,0);
    assert(opens==1 && queries==2 && !uploads && !plays && !stops && !erases);
    assert(replies[0].ready==1 && !replies[0].accepted_ms);
  } else if (!strcmp(argv[1],"dedup")) {
    repeat_request=pulse; repeat_active=true;
    assert(deliver(&pulse)==0); assert(deliver(&pulse)==0); pump();
    assert(reply_count==1 && uploads==1 && plays==1);
    exchange(&pulse,0);
    assert(uploads==1 && plays==1 && replies[1].accepted_ms==60);
    assert(!memcmp(&replies[0],&replies[1],sizeof(replies[0])));
    pulse.duration_ms=59; exchange(&pulse,-EPROTO);
    exchange(&status,0);
    pulse.duration_ms=60; exchange(&pulse,-EPROTO);
    assert(uploads==1 && plays==1);
  } else if (!strcmp(argv[1],"disconnect_pending")) {
    exchange(&pulse,0); pulse.sequence=2;
    assert(deliver(&pulse)==0); disconnect();
    assert(stops==0 && closes==0); /* callback did not touch VFS */
    pump();
    assert(plays==1 && uploads==1 && stops==1 && erases==1 && closes==1);
    assert(reply_count==1 && g_bkhaptic.fd==-1);
    bkhaptic_ns_bind(&remote,&g_bkhaptic,BKHAPTIC_RPC_ENDPOINT,2);
    status.session=22; exchange(&status,0);
    assert(plays==1 && uploads==1 && opens==2);
  } else if (!strcmp(argv[1],"disconnect_upload")) {
    disconnect_upload=true;
    assert(deliver(&pulse)==0); pump();
    assert(uploads==1 && plays==0 && stops==1 && closes==1 && !reply_count);
  } else if (!strcmp(argv[1],"upload_failure")) {
    upload_error=EBUSY; exchange(&pulse,-EBUSY);
    assert(uploads==1 && plays==0);
    exchange(&pulse,-EBUSY); assert(uploads==1);
  } else if (!strcmp(argv[1],"play_failure")) {
    play_error=EPERM; exchange(&pulse,-EPERM);
    assert(plays==1 && stops==1 && erases==1);
  } else if (!strcmp(argv[1],"stop_failure")) {
    exchange(&pulse,0); stop_error=EIO; exchange(&stop,-EIO);
    assert(stops==1 && erases==1 && closes==1 && g_bkhaptic.fd==-1);
  } else if (!strcmp(argv[1],"erase_failure")) {
    exchange(&pulse,0); erase_error=EINTR; exchange(&stop,-EINTR);
    assert(stops==1 && erases==1 && closes==1 && g_bkhaptic.fd==-1);
  } else if (!strcmp(argv[1],"no_rumble")) {
    no_rumble=true; exchange(&status,-ENOTSUP);
    assert(!uploads && !plays && closes==1);
  } else if (!strcmp(argv[1],"no_effects")) {
    no_effects=true; exchange(&status,-ENOTSUP);
    assert(!uploads && !plays && closes==1);
  } else assert(!"unknown scenario");
  return 0;
}
"""


class HapticServiceTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.temp = tempfile.TemporaryDirectory(prefix="haptic-service-host-")
        cls.addClassCleanup(cls.temp.cleanup)
        tree = Path(cls.temp.name)
        (tree / "mock.h").write_text(MOCK)
        for name in (
            "config.h",
            "irq.h",
            "mutex.h",
            "rpmsg/rpmsg.h",
            "semaphore.h",
            "spinlock.h",
            "bits.h",
            "fs/ioctl.h",
        ):
            header = tree / "nuttx" / name
            header.parent.mkdir(parents=True, exist_ok=True)
            header.write_text('#include "mock.h"\n')
        header = tree / "nuttx/input/ff.h"
        header.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(REPOSITORY.parent / "nuttx/include/nuttx/input/ff.h", header)
        harness = tree / "harness.c"
        harness.write_text(HARNESS)
        cls.binary = tree / "haptic_service"
        result = subprocess.run(
            [
                "cc",
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-fsanitize=undefined",
                "-fno-sanitize-recover=all",
                "-I",
                str(tree),
                "-I",
                str(REPOSITORY / "app/bk7258"),
                str(harness),
                "-o",
                str(cls.binary),
            ],
            capture_output=True,
            text=True,
        )
        if result.returncode:
            raise AssertionError(result.stderr)

    def test_real_service_behavior(self) -> None:
        for scenario in (
            "status",
            "dedup",
            "disconnect_pending",
            "disconnect_upload",
            "upload_failure",
            "play_failure",
            "stop_failure",
            "erase_failure",
            "no_rumble",
            "no_effects",
        ):
            with self.subTest(scenario=scenario):
                result = subprocess.run(
                    [str(self.binary), scenario],
                    capture_output=True,
                    text=True,
                    timeout=10,
                )
                self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
