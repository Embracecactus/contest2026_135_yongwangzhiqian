/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include "bk7258_voice_wake_session.h"

#include "bk7258_voice_kws_model.h"
#include "bk7258_voice_wake_owner.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <mbedtls/platform_util.h>
#include <mbedtls/sha256.h>

#define BKVOICE_WAKE_SHA256_HEX_BYTES 64u

struct bkvoice_wake_session_s
{
  struct bkvoice_wake_window_s window;
  struct bkvoice_wake_listener_s listener;
  struct bkvoice_wake_owner_s owner;
  unsigned char *model_bytes;
  size_t model_size;
  bool window_initialized;
  bool listener_initialized;
  bool owner_initialized;
};

static int hex_nibble(char value)
{
  if (value >= '0' && value <= '9') return value - '0';
  if (value >= 'a' && value <= 'f') return value - 'a' + 10;
  if (value >= 'A' && value <= 'F') return value - 'A' + 10;
  return -1;
}

static int decode_sha256(const char *text, unsigned char digest[32])
{
  size_t index;

  if (text == NULL || strlen(text) != BKVOICE_WAKE_SHA256_HEX_BYTES)
    {
      return -EINVAL;
    }

  for (index = 0; index < 32u; index++)
    {
      int high = hex_nibble(text[index * 2u]);
      int low = hex_nibble(text[index * 2u + 1u]);
      if (high < 0 || low < 0)
        {
          mbedtls_platform_zeroize(digest, 32u);
          return -EINVAL;
        }
      digest[index] = (unsigned char)((high << 4) | low);
    }

  return 0;
}

static bool digest_equal(const unsigned char left[32],
                         const unsigned char right[32])
{
  unsigned char difference = 0;
  size_t index;

  for (index = 0; index < 32u; index++) difference |= left[index] ^ right[index];
  return difference == 0;
}

static int load_authenticated_model(const char *path, const char *sha256_hex,
                                    unsigned char **data, size_t *size)
{
  unsigned char expected[32];
  unsigned char observed[32];
  mbedtls_sha256_context sha;
  struct stat info;
  unsigned char trailing;
  unsigned char *bytes = NULL;
  size_t offset = 0;
  ssize_t nread;
  int fd = -1;
  int ret;

  if (path == NULL || path[0] != '/' || data == NULL || size == NULL)
    {
      return -EINVAL;
    }
  *data = NULL;
  *size = 0;
  ret = decode_sha256(sha256_hex, expected);
  if (ret < 0) return ret;

  fd = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
  if (fd < 0) { ret = -errno; goto out; }
  if (fstat(fd, &info) < 0) { ret = -errno; goto out; }
  if (!S_ISREG(info.st_mode) || info.st_size <= 0 ||
      (uint64_t)info.st_size > BKVOICE_KWS_MODEL_MAX_BYTES)
    { ret = -EFBIG; goto out; }

  bytes = malloc((size_t)info.st_size);
  if (bytes == NULL) { ret = -ENOMEM; goto out; }
  while (offset < (size_t)info.st_size)
    {
      nread = read(fd, bytes + offset, (size_t)info.st_size - offset);
      if (nread < 0 && errno == EINTR) continue;
      if (nread <= 0) { ret = nread < 0 ? -errno : -EBADMSG; goto out; }
      offset += (size_t)nread;
    }
  do nread = read(fd, &trailing, 1u); while (nread < 0 && errno == EINTR);
  if (nread != 0) { ret = nread < 0 ? -errno : -EBADMSG; goto out; }
  if (close(fd) < 0) { fd = -1; ret = -errno; goto out; }
  fd = -1;

  mbedtls_sha256_init(&sha);
  ret = mbedtls_sha256_starts(&sha, 0);
  if (ret == 0) ret = mbedtls_sha256_update(&sha, bytes, offset);
  if (ret == 0) ret = mbedtls_sha256_finish(&sha, observed);
  mbedtls_sha256_free(&sha);
  if (ret != 0) { ret = -EIO; goto out; }
  if (!digest_equal(expected, observed))
    { ret = -EKEYREJECTED; goto out; }

  *data = bytes;
  *size = offset;
  bytes = NULL;
  ret = 0;

out:
  if (fd >= 0 && close(fd) < 0 && ret == 0) ret = -errno;
  if (bytes != NULL)
    {
      mbedtls_platform_zeroize(bytes, offset);
      free(bytes);
    }
  mbedtls_platform_zeroize(expected, sizeof(expected));
  mbedtls_platform_zeroize(observed, sizeof(observed));
  return ret;
}

static void release_allocations(struct bkvoice_wake_session_s *session)
{
  if (session->model_bytes != NULL)
    {
      mbedtls_platform_zeroize(session->model_bytes, session->model_size);
      free(session->model_bytes);
    }

}

static void close_partial(struct bkvoice_wake_session_s *session)
{
  /* open() has not exposed the session and never calls owner_step(), so an
   * initialized listener cannot own a worker, source or MIC on this path. */
  if (session->listener_initialized)
    (void)bkvoice_wake_listener_uninitialize(&session->listener);
  if (session->window_initialized)
    bkvoice_wake_window_uninitialize(&session->window);
  release_allocations(session);
  mbedtls_platform_zeroize(session, sizeof(*session));
  free(session);
}

int bkvoice_wake_session_open(struct bkvoice_wake_session_s **output,
  const struct bkvoice_wake_session_config_s *config,
  struct bkvoice_ptt_s *ptt, struct bkcloud_runtime_s *cloud, sem_t *wake)
{
  static const struct bkvoice_wake_window_policy_s window_policy =
    {.minimum_speech_mean_abs = 200u, .speech_to_noise_q8 = 384u,
     .speech_confirm_frames = 3u, .wake_quiet_frames = 8u,
     .silence_end_frames = 25u,
     .no_speech_frames = 250u, .maximum_turn_frames = 1500u};
  struct bkvoice_wake_session_s *session;
  int ret;

  if (output == NULL || *output != NULL || config == NULL || ptt == NULL ||
      !ptt->initialized || cloud == NULL || wake == NULL)
    return -EINVAL;

  session = calloc(1, sizeof(*session));
  if (session == NULL) return -ENOMEM;
  ret = load_authenticated_model(config->model_path,
          config->model_sha256_hex, &session->model_bytes,
          &session->model_size);
  if (ret < 0) { close_partial(session); return ret; }

  ret = bkvoice_wake_window_initialize(&session->window, &window_policy);
  if (ret < 0) { close_partial(session); return ret; }
  session->window_initialized = true;
  ret = bkvoice_wake_listener_initialize(&session->listener,
          session->model_bytes, session->model_size, &session->window, wake);
  if (ret < 0) { close_partial(session); return ret; }
  session->listener_initialized = true;
  ret = bkvoice_wake_owner_initialize(&session->owner, &session->listener,
                                      &session->window, cloud, wake);
  if (ret < 0) { close_partial(session); return ret; }
  session->owner_initialized = true;
  *output = session;
  return 0;
}

int bkvoice_wake_session_step(struct bkvoice_wake_session_s *session,
                              bool allowed, uint64_t now_ms)
{
  return session == NULL || !session->owner_initialized ? -EINVAL :
         bkvoice_wake_owner_step(&session->owner, allowed, now_ms);
}

int bkvoice_wake_session_suspend(struct bkvoice_wake_session_s *session)
{
  return session == NULL || !session->owner_initialized ? -EINVAL :
         bkvoice_wake_owner_suspend(&session->owner);
}

int bkvoice_wake_session_close(struct bkvoice_wake_session_s **sessionp)
{
  struct bkvoice_wake_session_s *session;
  int ret;

  if (sessionp == NULL || *sessionp == NULL) return 0;
  session = *sessionp;
  if (session->owner_initialized)
    {
      ret = bkvoice_wake_owner_close(&session->owner);
      if (ret < 0) return ret;
      session->owner_initialized = false;
    }
  if (session->listener_initialized)
    {
      ret = bkvoice_wake_listener_uninitialize(&session->listener);
      if (ret < 0) return ret;
      session->listener_initialized = false;
    }
  if (session->window_initialized)
    {
      bkvoice_wake_window_uninitialize(&session->window);
      session->window_initialized = false;
    }
  release_allocations(session);
  mbedtls_platform_zeroize(session, sizeof(*session));
  free(session);
  *sessionp = NULL;
  return 0;
}
