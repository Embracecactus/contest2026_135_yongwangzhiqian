/* SPDX-License-Identifier: Apache-2.0 */
#define _XOPEN_SOURCE 700
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int master = -1;
static char *slave;
int __real_open(const char *, int, ...);
int __wrap_open(const char *path, int flags, ...)
{
  if (!strcmp(path, "/dev/ttyGS0"))
    {
      if (master < 0 || slave == NULL) { errno = ENODEV; return -1; }
      return __real_open(slave, flags);
    }
  if (flags & O_CREAT)
    {
      va_list ap;
      va_start(ap, flags);
      int mode = va_arg(ap, int);
      va_end(ap);
      return __real_open(path, flags, mode);
    }
  return __real_open(path, flags);
}
void test_serial_peer_close(void)
{
  if (master >= 0) assert(close(master) == 0);
  master = -1;
  slave = NULL;
}
void test_serial_peer_open(void)
{
  test_serial_peer_close();
  master = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
  assert(master >= 0 && grantpt(master) == 0 && unlockpt(master) == 0);
  slave = ptsname(master);
  assert(slave != NULL);
}
int test_serial_peer_send(const void *data, size_t size)
{
  ssize_t n = write(master, data, size);
  return n < 0 ? -errno : (int)n;
}
int test_serial_peer_recv(void *data, size_t size)
{
  ssize_t n = read(master, data, size);
  return n < 0 ? -errno : (int)n;
}
