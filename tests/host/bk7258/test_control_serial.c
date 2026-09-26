/* SPDX-License-Identifier: Apache-2.0 */
#define _XOPEN_SOURCE 600
#include "bk7258_control_serial.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

/* Only redirect the device node to a real POSIX pseudo-terminal. All terminal,
 * poll, read and write behavior comes from the host kernel, not a success mock.
 */
static const char *slave;
static int master;
static int opened_fd = -1;
static unsigned opens;
static int open_error;
int __real_open(const char *, int, ...);
int __wrap_open(const char *path, int flags, ...)
{
  assert(strcmp(path, "/dev/ttyGS0") == 0);
  assert((flags & O_NONBLOCK) && (flags & O_ACCMODE) == O_RDWR);
  opens++;
  if (open_error) { errno = open_error; return -1; }
  opened_fd = __real_open(slave, flags);
  return opened_fd;
}

static void peer(void)
{
  master = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
  assert(master >= 0 && grantpt(master) == 0 && unlockpt(master) == 0);
  slave = ptsname(master);
  assert(slave != NULL);
}

int main(int argc, char **argv)
{
  struct bkcontrol_serial_s serial = {0};
  struct bkprov_tls_transport_s transport;
  unsigned char sent[256], received[256];
  size_t count = 0;
  assert(argc == 2);
  peer();
  if (!strcmp(argv[1], "failed-open"))
    {
      open_error = ENODEV;
      assert(bkcontrol_serial_open(&serial, &transport) == -ENODEV);
      assert(!serial.opened && serial.epoch == 0);
      assert(bkcontrol_serial_close(&serial) == 0);
      assert(opens == 1);
    }
  else
    {
      assert(bkcontrol_serial_open(&serial, &transport) == 0);
      uint32_t epoch = transport.generation(transport.context);
      assert(epoch != 0 && transport.max_send == 64);
      assert(bkcontrol_serial_open(&serial, &transport) == -EBUSY);
      assert(opens == 1);
      for (size_t i = 0; i < sizeof(sent); i++) sent[i] = (unsigned char)i;
      if (!strcmp(argv[1], "binary"))
        {
          assert(write(master, sent, sizeof(sent)) == sizeof(sent));
          for (int i = 0; i < 10000 && count < sizeof(sent); i++)
            {
              ssize_t n = transport.read(transport.context, epoch,
                                         received + count, sizeof(received) - count);
              assert(n > 0 || n == -EAGAIN);
              if (n > 0) count += n;
            }
          assert(count == sizeof(sent) && !memcmp(sent, received, count));
          assert(read(master, received, sizeof(received)) == -1 && errno == EAGAIN);
          count = 0;
          while (count < sizeof(sent))
            {
              ssize_t n = transport.send(transport.context, epoch,
                                         sent + count, sizeof(sent) - count);
              assert(n > 0 && n <= 64); count += n;
            }
          count = 0;
          for (int i = 0; i < 10000 && count < sizeof(sent); i++)
            {
              ssize_t n = read(master, received + count, sizeof(received) - count);
              assert(n > 0 || (n < 0 && errno == EAGAIN));
              if (n > 0) count += n;
            }
          assert(count == sizeof(sent) && !memcmp(sent, received, count));
        }
      else if (!strcmp(argv[1], "backpressure"))
        {
          ssize_t n = 0;
          for (int i = 0; i < 10000 && n != -EAGAIN; i++)
            {
              n = transport.send(transport.context, epoch, sent, sizeof(sent));
              assert(n > 0 || n == -EAGAIN);
            }
          assert(n == -EAGAIN);
          assert(transport.generation(transport.context) == epoch);
          assert(bkcontrol_serial_close(&serial) == 0);
          assert(!serial.opened);
        }
      else if (!strcmp(argv[1], "disconnect"))
        {
          assert(close(master) == 0); master = -1;
          assert(transport.generation(transport.context) == 0);
          assert(transport.read(transport.context, epoch, received, 1) == -ESTALE);
          assert(transport.send(transport.context, epoch, sent, 1) == -ESTALE);
          assert(bkcontrol_serial_close(&serial) == 0);
          assert(fcntl(opened_fd, F_GETFD) < 0 && errno == EBADF);
          peer();
          assert(bkcontrol_serial_open(&serial, &transport) == 0);
          assert(transport.generation(transport.context) != epoch);
          assert(transport.send(transport.context, epoch, sent, 1) == -ESTALE);
        }
      else if (!strcmp(argv[1], "invalid"))
        {
          assert(transport.read(transport.context, epoch, NULL, 1) == -EINVAL);
          assert(transport.send(transport.context, epoch, NULL, 1) == -EINVAL);
          assert(transport.send(transport.context, epoch, sent, 0) == -EINVAL);
          assert(bkcontrol_serial_close(&serial) == 0);
          serial.epoch = UINT32_MAX;
          assert(bkcontrol_serial_open(&serial, &transport) == -EOVERFLOW);
          assert(opens == 1);
        }
      else assert(!"unknown case");
      assert(bkcontrol_serial_close(&serial) == 0);
      assert(bkcontrol_serial_close(&serial) == 0);
    }
  if (master >= 0) assert(close(master) == 0);
  puts("CONTRACT_PASS");
  return 0;
}
