#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Exercise actual driver polling with an injected clock and missing IRQs."""
import re
import subprocess
import tempfile
import unittest
from pathlib import Path
from test_mfrc522_read_errors import REPOSITORY, extract_function

source = (REPOSITORY / 'nuttx/drivers/contactless/mfrc522.c').read_text()
header = (REPOSITORY / 'nuttx/drivers/contactless/mfrc522.h').read_text()
bodies = '\n'.join(extract_function(source, 'int ' + name + '(')
                   for name in ('mfrc522_calc_crc', 'mfrc522_comm_picc'))
constants = set(re.findall(r'\bMFRC522_[A-Z_]+\b', bodies))
defines = '\n'.join(line for line in re.sub(r'# +define', '#define', header).splitlines()
                    if line.startswith('#define ') and line.split()[1] in constants)
prefix = r'''
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <errno.h>
#include <assert.h>
#define FAR
#define OK 0
struct mfrc522_dev_s { uint32_t frame_timeout_ms; };
static int nxsig_usleep(int us) { (void)us;return 0; }
static long long now_ns, initial_ns;
static int ticks;
static void clock_systime_timespec(struct timespec *t) {
 assert(ticks++ < 31000);
 t->tv_sec=now_ns/1000000000; t->tv_nsec=now_ns%1000000000;
 now_ns+=10000000;
}
static void mfrc522_writeu8(struct mfrc522_dev_s *d,int r,int v)
{ (void)d;(void)r;(void)v; }
static uint8_t mfrc522_readu8(struct mfrc522_dev_s *d,int r)
{ (void)d;(void)r;return 0; }
static void mfrc522_writeblk(struct mfrc522_dev_s *d,int r,uint8_t *b,int n)
{ (void)d;(void)r;(void)b;(void)n; }
static void mfrc522_readblk(struct mfrc522_dev_s *d,int r,uint8_t *b,int n,int a)
{ (void)d;(void)r;(void)b;(void)n;(void)a; }
'''
main = r'''
int main(void) {
 struct mfrc522_dev_s d={0}; uint8_t b[2]={0};
 long long starts[]={1000000000LL,1850000000LL,1990000000LL};
 for(unsigned i=0;i<3;i++) for(int radio=0;radio<2;radio++) {
  now_ns=initial_ns=starts[i]; ticks=0;
  int ret=radio ? mfrc522_comm_picc(&d,MFRC522_TRANSCV_CMD,0x30,b,1,0,0,0,0,false)
                : mfrc522_calc_crc(&d,b,1,b);
  assert(ret==-ETIMEDOUT);
  assert(now_ns-initial_ns==210000000);
 }
 uint32_t waits[]={1,200,5000,39000,300000};
 for(unsigned i=0;i<sizeof(waits)/sizeof(waits[0]);i++) {
  d.frame_timeout_ms=waits[i];now_ns=initial_ns=1990000000LL;ticks=0;
  assert(mfrc522_comm_picc(&d,MFRC522_TRANSCV_CMD,0x30,b,1,0,0,0,0,false)==-ETIMEDOUT);
  long long elapsed=now_ns-initial_ns-10000000;
  long long deadline=((long long)waits[i]+20)*1000000;
  assert(elapsed>=deadline&&elapsed<deadline+10000000);
 }
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='mfrc522-deadlines-') as directory:
 root=Path(directory); c=root/'test.c'; binary=root/'test'
 c.write_text(prefix+defines+'\n'+bodies+main)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=undefined',str(c),'-o',str(binary)],check=True)
 subprocess.run([str(binary)],check=True)
print('PASS: default CRC/radio deadlines and 1..300000 ms custom waits, including rollover')


UART_SOURCE = (REPOSITORY / "boards/bk7258/aidk_ai_toy/src/"
               "bk7258_aidk_mfrc522.c").read_text()
UART_BODIES = "\n".join(extract_function(UART_SOURCE, "static int " + name + "(")
                         for name in ("aidk_nfc_uart_wait_readable",
                                      "aidk_nfc_uart_read_byte"))
UART_PREFIX = r'''
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#define FAR
#define OK 0
#define POLLIN  0x01
#define POLLERR 0x02
#define POLLHUP 0x04
#define POLLNVAL 0x08
#define LOG_WARNING 0
#define AIDK_NFC_UART_TIMEOUT_LOOPS 20
#define AIDK_NFC_UART_RX_TIMEOUT_MS 200
#define MSEC2TICK(ms) (((ms) + 9) / 10)
#define TICK2MSEC(ticks) ((ticks) * 10)
typedef unsigned long clock_t;
typedef struct { int unused; } sem_t;
struct file { int unused; };
struct aidk_nfc_uart_s { struct file uart; };
struct pollfd {
  int fd;
  unsigned int events;
  unsigned int revents;
  void *arg;
  void (*cb)(struct pollfd *);
  void *priv;
};
enum scenario_e {
  DATA, REGISTER_RACE, EVENT_WAKE, TIMEOUT, EINTR_SPURIOUS,
  POLL_SETUP_FAIL, ERROR, HANGUP
};
static enum scenario_e scenario;
static bool data_ready;
static bool registered;
static int setup_calls;
static int teardown_calls;
static int wait_calls;
static int sem_destroy_calls;
static unsigned long now_ticks;
static struct pollfd *active_pfd;
static clock_t clock_systime_ticks(void) { return now_ticks; }
static int nxsem_init(sem_t *sem, int pshared, unsigned int value)
{ (void)sem; (void)pshared; (void)value; return 0; }
static int nxsem_destroy(sem_t *sem)
{ (void)sem; assert(!registered); sem_destroy_calls++; return 0; }
static int nxsem_tickwait_uninterruptible(sem_t *sem, unsigned long ticks)
{
  (void)sem; wait_calls++;
  assert(registered);
  if (scenario == EVENT_WAKE) {
    data_ready = true; active_pfd->revents = POLLIN; return 0;
  }
  if (scenario == EINTR_SPURIOUS) {
    now_ticks++; return 0;
  }

  assert(scenario == TIMEOUT);
  now_ticks += ticks;
  return -ETIMEDOUT;
}
static void poll_default_cb(struct pollfd *pfd) { (void)pfd; }
static int file_poll(struct file *file, struct pollfd *pfd, bool setup)
{
  (void)file;
  if (!setup) {
    assert(registered && pfd == active_pfd);
    registered = false; active_pfd = NULL; teardown_calls++; return 0;
  }
  assert(!registered);
  if (scenario == POLL_SETUP_FAIL) {
    setup_calls++;
    return -EBUSY;
  }

  registered = true; active_pfd = pfd; setup_calls++;
  if (scenario == REGISTER_RACE) {
    data_ready = true; pfd->revents = POLLIN;
  } else if (scenario == ERROR || scenario == HANGUP) {
    pfd->revents = scenario == ERROR ? POLLERR : POLLHUP;
  }
  return 0;
}
static ssize_t file_read(struct file *file, char *value, size_t count)
{
  (void)file; assert(count == 1);
  if (scenario == EINTR_SPURIOUS) return -EINTR;
  if (!data_ready) return -EAGAIN;
  data_ready = false; *value = 0x5a; return 1;
}
static void aidk_nfc_uart_snapshot(const char *stage) { (void)stage; }
static void test_syslog(int priority, const char *format, ...)
{ (void)priority; (void)format; }
#define syslog(...) test_syslog(__VA_ARGS__)
'''
UART_MAIN = r'''
static void reset(enum scenario_e next)
{
  scenario = next; data_ready = next == DATA; registered = false;
  setup_calls = teardown_calls = wait_calls = sem_destroy_calls = 0;
  now_ticks = 0; active_pfd = NULL;
}
int main(void)
{
  struct aidk_nfc_uart_s uart = {0}; uint8_t value = 0;
  reset(DATA);
  assert(aidk_nfc_uart_read_byte(&uart, &value) == 0 && value == 0x5a);
  assert(setup_calls == 0 && wait_calls == 0);

  reset(REGISTER_RACE);
  assert(aidk_nfc_uart_read_byte(&uart, &value) == 0 && value == 0x5a);
  assert(setup_calls == 1 && teardown_calls == 1 && wait_calls == 0);
  assert(sem_destroy_calls == 1 && !registered && active_pfd == NULL);

  reset(EVENT_WAKE);
  assert(aidk_nfc_uart_read_byte(&uart, &value) == 0 && value == 0x5a);
  assert(setup_calls == 1 && teardown_calls == 1 && wait_calls == 1);
  assert(sem_destroy_calls == 1 && !registered && active_pfd == NULL);

  reset(TIMEOUT);
  assert(aidk_nfc_uart_read_byte(&uart, &value) == -ETIMEDOUT);
  assert(now_ticks == 20 && setup_calls == 1 && teardown_calls == 1);
  assert(sem_destroy_calls == 1 && !registered && active_pfd == NULL);

  reset(EINTR_SPURIOUS);
  assert(aidk_nfc_uart_read_byte(&uart, &value) == -ETIMEDOUT);
  assert(now_ticks == 20 && setup_calls == 20 && teardown_calls == 20);
  assert(wait_calls == 20 && sem_destroy_calls == 20 && !registered);

  reset(POLL_SETUP_FAIL);
  assert(aidk_nfc_uart_read_byte(&uart, &value) == -EBUSY);
  assert(setup_calls == 1 && teardown_calls == 0 && wait_calls == 0);
  assert(sem_destroy_calls == 1 && !registered && active_pfd == NULL);

  reset(ERROR);
  assert(aidk_nfc_uart_read_byte(&uart, &value) == -EIO);
  assert(setup_calls == 1 && teardown_calls == 1 && wait_calls == 0);
  assert(sem_destroy_calls == 1 && !registered && active_pfd == NULL);

  reset(HANGUP);
  assert(aidk_nfc_uart_read_byte(&uart, &value) == -EPIPE);
  assert(setup_calls == 1 && teardown_calls == 1 && wait_calls == 0);
  assert(sem_destroy_calls == 1 && !registered && active_pfd == NULL);
  return 0;
}
'''


class Mfrc522UartWaitTest(unittest.TestCase):
    def test_aidk_uart_rx_uses_event_wait_with_bounded_cleanup(self) -> None:
        read_body = extract_function(UART_SOURCE, "static int aidk_nfc_uart_read_byte(")
        self.assertNotIn("nxsig_usleep(1000)", read_body)
        with tempfile.TemporaryDirectory(prefix="mfrc522-uart-") as temporary:
            root = Path(temporary)
            source = root / "mfrc522_uart_harness.c"
            binary = root / "mfrc522_uart_harness"
            source.write_text(UART_PREFIX + "\n" + UART_BODIES + "\n" + UART_MAIN)
            subprocess.run(
                ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                 "-fsanitize=undefined", str(source), "-o", str(binary)],
                check=True,
            )
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
