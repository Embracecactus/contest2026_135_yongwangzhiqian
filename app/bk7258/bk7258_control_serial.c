/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include "bk7258_control_serial.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

static uint32_t serial_generation(void *context)
{
  struct bkcontrol_serial_s *serial = context;
  struct pollfd watch;
  int ret;

  if (!serial->opened || !serial->live) return 0;
  memset(&watch, 0, sizeof(watch));
  watch.fd = serial->fd;
  watch.events = POLLIN | POLLOUT;
  ret = poll(&watch, 1, 0);
  if ((ret < 0 && errno != EINTR) ||
      (ret >= 0 && (watch.revents & (POLLERR | POLLHUP | POLLNVAL))))
    {
      serial->live = false;
      return 0;
    }
  return serial->epoch;
}

static ssize_t serial_result(struct bkcontrol_serial_s *serial, ssize_t ret)
{
  if (ret > 0) return ret;
  if (ret < 0 && (errno == EAGAIN || errno == EINTR)) return -EAGAIN;
  int error = ret == 0 ? -ENOTCONN : -errno;
  serial->live = false;
  return error;
}

static ssize_t serial_read(void *context, uint32_t epoch, void *data,
                            size_t size)
{
  struct bkcontrol_serial_s *serial = context;
  if (data == NULL || size == 0) return -EINVAL;
  if (epoch == 0 || serial_generation(serial) != epoch) return -ESTALE;
  if (size > 1024) size = 1024;
  return serial_result(serial, read(serial->fd, data, size));
}

static ssize_t serial_send(void *context, uint32_t epoch, const void *data,
                            size_t size)
{
  struct bkcontrol_serial_s *serial = context;
  if (data == NULL || size == 0) return -EINVAL;
  if (epoch == 0 || serial_generation(serial) != epoch) return -ESTALE;
  if (size > 64) size = 64;
  return serial_result(serial, write(serial->fd, data, size));
}

int bkcontrol_serial_close(struct bkcontrol_serial_s *serial)
{
  int fd;
  if (serial == NULL) return -EINVAL;
  serial->live = false;
  if (serial->close_error) return serial->close_error;
  if (!serial->opened) return 0;
  fd = serial->fd;
  serial->opened = false;
  serial->fd = -1;
  if (close(fd) < 0) serial->close_error = -errno;
  return serial->close_error;
}

static int serial_rollback(struct bkcontrol_serial_s *serial, int error)
{
  int closed = bkcontrol_serial_close(serial);
  return closed < 0 ? closed : error;
}

int bkcontrol_serial_open(struct bkcontrol_serial_s *serial,
                          struct bkprov_tls_transport_s *transport)
{
  struct termios raw;
  int fd;
  int ret;

  if (serial == NULL || transport == NULL) return -EINVAL;
  if (serial->close_error) return serial->close_error;
  if (serial->opened) return -EBUSY;
  if (serial->epoch == UINT32_MAX) return -EOVERFLOW;
  fd = open("/dev/ttyGS0", O_RDWR | O_NONBLOCK | O_NOCTTY | O_CLOEXEC);
  if (fd < 0) return -errno;
  /* Acquire cleanup ownership immediately, before any setup can fail.
   * No callback is published and live remains false until setup completes.
   */

  serial->fd = fd;
  serial->opened = true;
  serial->live = false;
  memset(&raw, 0, sizeof(raw));
  if (tcgetattr(fd, &raw) < 0)
    {
      ret = -errno;
      return serial_rollback(serial, ret);
    }
  /* Preserve the virtual port's hardware settings, disable all byte/signal
   * translations. Never drain/flush or toggle DTR as part of authentication.
   */
  raw.c_iflag = 0;
  raw.c_oflag = 0;
  raw.c_lflag = 0;
  raw.c_cc[VMIN] = 1;
  raw.c_cc[VTIME] = 0;
  if (tcsetattr(fd, TCSANOW, &raw) < 0)
    {
      ret = -errno;
      return serial_rollback(serial, ret);
    }
  serial->fd = fd;
  serial->epoch++;
  serial->opened = true;
  serial->live = true;
  if (serial_generation(serial) == 0)
    {
      return serial_rollback(serial, -ENOTCONN);
    }
  const struct bkprov_tls_transport_s selected =
    { serial, serial_generation, serial_read, serial_send, 64, 0 };
  *transport = selected;
  return 0;
}
