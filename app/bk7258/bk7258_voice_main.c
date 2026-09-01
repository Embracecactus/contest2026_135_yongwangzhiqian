/****************************************************************************
 * app/bk7258/bk7258_voice_main.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * CP-visible operator command for the AP-owned BKVoice service.
 ****************************************************************************/

#include <nuttx/config.h>

#include "bk7258_voice_protocol.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BKVOICE_STRESS_MAX 100ul

static void bkvoice_usage(void)
{
  fprintf(stderr,
          "usage: bkvoice status\n"
          "       bkvoice verify <voicepack.ini>\n"
          "       bkvoice play <voicepack.ini> <clip-id>\n"
          "       bkvoice stress <voicepack.ini> <clip-id> <count>\n");
}

static int bkvoice_copy_arg(char *target, size_t size, const char *source)
{
  size_t length = strlen(source);

  if (length == 0 || length >= size)
    {
      return -ENAMETOOLONG;
    }

  memcpy(target, source, length + 1u);
  return OK;
}

static int bkvoice_request(uint16_t command, const char *manifest,
                           const char *clip_id,
                           struct bkvoice_rpc_response_s *response)
{
  struct bkvoice_rpc_request_s request;
  int ret;

  memset(&request, 0, sizeof(request));
  memset(response, 0, sizeof(*response));
  request.command = command;

  if (manifest != NULL)
    {
      ret = bkvoice_copy_arg(request.manifest, sizeof(request.manifest),
                             manifest);
      if (ret < 0)
        {
          return ret;
        }
    }

  if (clip_id != NULL)
    {
      ret = bkvoice_copy_arg(request.clip_id, sizeof(request.clip_id),
                             clip_id);
      if (ret < 0)
        {
          return ret;
        }
    }

  ret = bkvoice_rpc_exchange(&request, response,
                             BKVOICE_RPC_REPLY_WAIT_MS);
  if (ret < 0)
    {
      fprintf(stderr, "BKVOICE RPC FAIL ret=%d\n", ret);
      return ret;
    }

  return response->status;
}

static int bkvoice_status(void)
{
  struct bkvoice_rpc_response_s response;
  int ret = bkvoice_request(BKVOICE_RPC_STATUS, NULL, NULL, &response);

  if (ret < 0)
    {
      fprintf(stderr, "BKVOICE STATUS FAIL ret=%d\n", ret);
      return ret;
    }

  printf("BKVOICE STATUS service=%s pack=bkvoice-pack-v1 "
         "intent_engine=not-installed\n",
         (response.flags & BKVOICE_STATUS_SERVICE_READY) != 0 ?
         "ready" : "not-ready");
  printf("BKVOICE AUDIO owner=ap dev=/dev/audio/pcm0p rate=16000 "
         "channels=1 bits=16 mode=record-then-play\n");
  printf("BKVOICE STORAGE owner=ap block=/dev/mmcsd0 ready=%u "
         "mount=board-policy path=ap-namespace\n",
         (response.flags & BKVOICE_STATUS_BLOCK_PRESENT) != 0 ? 1u : 0u);
  printf("BKVOICE POLICY authorization=explicit-consent "
         "disclosure=synthetic-voice local_only=%u\n",
         (response.flags & BKVOICE_STATUS_LOCAL_ONLY) != 0 ? 1u : 0u);
  return OK;
}

static int bkvoice_verify(const char *manifest,
                          struct bkvoice_rpc_response_s *response)
{
  int ret = bkvoice_request(BKVOICE_RPC_VERIFY, manifest, NULL, response);

  if (ret < 0)
    {
      fprintf(stderr, "BKVOICE VERIFY FAIL ret=%d line=%lu\n", ret,
              (unsigned long)response->error_line);
      return ret;
    }

  printf("BKVOICE VERIFY PASS version=%lu speaker_id=%s clips=%lu "
         "authorization=explicit-consent disclosure=synthetic-voice\n",
         (unsigned long)response->pack_version, response->speaker_id,
         (unsigned long)response->clip_count);
  return OK;
}

static int bkvoice_play(const char *manifest, const char *clip_id)
{
  struct bkvoice_rpc_response_s response;
  int ret;

  /* Verification is a separate round trip so the CP-visible disclosure is
   * emitted before AP starts producing synthetic audio.
   */

  ret = bkvoice_verify(manifest, &response);
  if (ret < 0)
    {
      return ret;
    }

  printf("BKVOICE SYNTHETIC speaker_id=%s clip=%s\n",
         response.speaker_id, clip_id);
  ret = bkvoice_request(BKVOICE_RPC_PLAY, manifest, clip_id, &response);
  if (ret < 0)
    {
      fprintf(stderr, "BKVOICE PLAY FAIL ret=%d line=%lu clip=%s\n", ret,
              (unsigned long)response.error_line, clip_id);
      return ret;
    }

  printf("BKVOICE PLAY PASS clip=%s bytes=%lu duration_ms=%lu\n",
         clip_id, (unsigned long)response.data_bytes,
         (unsigned long)response.duration_ms);
  return OK;
}

static int bkvoice_parse_count(const char *text, unsigned long *count)
{
  char *end;
  unsigned long value;

  errno = 0;
  value = strtoul(text, &end, 10);
  if (errno != 0 || end == text || *end != '\0' || value == 0 ||
      value > BKVOICE_STRESS_MAX)
    {
      return -EINVAL;
    }

  *count = value;
  return OK;
}

static int bkvoice_stress(const char *manifest, const char *clip_id,
                          const char *count_text)
{
  struct bkvoice_rpc_response_s response;
  unsigned long count;
  unsigned long iteration;
  int ret;

  ret = bkvoice_parse_count(count_text, &count);
  if (ret < 0)
    {
      fprintf(stderr, "BKVOICE STRESS FAIL ret=%d count=%s max=%lu\n",
              ret, count_text, BKVOICE_STRESS_MAX);
      return ret;
    }

  ret = bkvoice_verify(manifest, &response);
  if (ret < 0)
    {
      return ret;
    }

  printf("BKVOICE SYNTHETIC speaker_id=%s clip=%s mode=stress count=%lu\n",
         response.speaker_id, clip_id, count);
  for (iteration = 1; iteration <= count; iteration++)
    {
      ret = bkvoice_request(BKVOICE_RPC_PLAY, manifest, clip_id, &response);
      if (ret < 0)
        {
          fprintf(stderr,
                  "BKVOICE STRESS FAIL iteration=%lu/%lu ret=%d line=%lu\n",
                  iteration, count, ret,
                  (unsigned long)response.error_line);
          return ret;
        }

      if (iteration == count || iteration % 10ul == 0)
        {
          printf("BKVOICE STRESS PROGRESS completed=%lu/%lu\n",
                 iteration, count);
        }
    }

  printf("BKVOICE STRESS PASS count=%lu clip=%s\n", count, clip_id);
  return OK;
}

int main(int argc, char **argv)
{
  struct bkvoice_rpc_response_s response;
  int ret;

  if (argc == 2 && strcmp(argv[1], "status") == 0)
    {
      ret = bkvoice_status();
    }
  else if (argc == 3 && strcmp(argv[1], "verify") == 0)
    {
      ret = bkvoice_verify(argv[2], &response);
    }
  else if (argc == 4 && strcmp(argv[1], "play") == 0)
    {
      ret = bkvoice_play(argv[2], argv[3]);
    }
  else if (argc == 5 && strcmp(argv[1], "stress") == 0)
    {
      ret = bkvoice_stress(argv[2], argv[3], argv[4]);
    }
  else
    {
      bkvoice_usage();
      return EXIT_FAILURE;
    }

  return ret == OK ? EXIT_SUCCESS : EXIT_FAILURE;
}
