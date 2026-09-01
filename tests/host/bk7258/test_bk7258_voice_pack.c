/****************************************************************************
 * tests/host/bk7258/test_bk7258_voice_pack.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "bk7258_voice_pack.h"
#include "bk7258_voice_protocol.h"

static void write_all(int fd, const void *buffer, size_t size)
{
  const uint8_t *cursor = buffer;
  size_t done = 0;

  while (done < size)
    {
      ssize_t written = write(fd, cursor + done, size - done);
      assert(written > 0);
      done += (size_t)written;
    }
}

static void write_text(const char *path, const char *text)
{
  int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);

  assert(fd >= 0);
  write_all(fd, text, strlen(text));
  assert(close(fd) == 0);
}

static void put_le16(uint8_t *data, uint16_t value)
{
  data[0] = value & 0xffu;
  data[1] = value >> 8;
}

static void put_le32(uint8_t *data, uint32_t value)
{
  data[0] = value & 0xffu;
  data[1] = (value >> 8) & 0xffu;
  data[2] = (value >> 16) & 0xffu;
  data[3] = value >> 24;
}

static void write_wav(const char *path, uint32_t rate)
{
  uint8_t wav[48];
  int fd;

  memset(wav, 0, sizeof(wav));
  memcpy(wav, "RIFF", 4);
  put_le32(wav + 4, sizeof(wav) - 8);
  memcpy(wav + 8, "WAVEfmt ", 8);
  put_le32(wav + 16, 16);
  put_le16(wav + 20, 1);
  put_le16(wav + 22, 1);
  put_le32(wav + 24, rate);
  put_le32(wav + 28, rate * 2);
  put_le16(wav + 32, 2);
  put_le16(wav + 34, 16);
  memcpy(wav + 36, "data", 4);
  put_le32(wav + 40, 4);

  fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
  assert(fd >= 0);
  write_all(fd, wav, sizeof(wav));
  assert(close(fd) == 0);
}

int main(void)
{
  static const char valid_manifest[] =
    "format=bkvoice-pack-v1\n"
    "speaker_id=private_voice_01\n"
    "authorization=explicit-consent\n"
    "disclosure=synthetic-voice\n"
    "sample_rate=16000\n"
    "channels=1\n"
    "encoding=pcm-s16le\n"
    "clip.greeting=greeting.wav\n";
  char directory[] = "/tmp/bkvoice-test-XXXXXX";
  char manifest[BKVOICE_PATH_SIZE];
  char wavpath[BKVOICE_PATH_SIZE];
  struct bkvoice_pack_info_s pack;
  struct bkvoice_wav_info_s wav;
  int fd;

  assert(BKVOICE_STATUS_SERVICE_READY == (1u << 0));
  assert(BKVOICE_STATUS_BLOCK_PRESENT == (1u << 1));
  assert(BKVOICE_STATUS_LOCAL_ONLY == (1u << 2));
  assert(sizeof(struct bkvoice_rpc_request_s) == 320);
  assert(sizeof(struct bkvoice_rpc_response_s) == 92);

  assert(mkdtemp(directory) != NULL);
  assert(snprintf(manifest, sizeof(manifest), "%s/voicepack.ini",
                  directory) > 0);
  assert(snprintf(wavpath, sizeof(wavpath), "%s/greeting.wav",
                  directory) > 0);

  write_text(manifest, valid_manifest);
  write_wav(wavpath, BKVOICE_SAMPLE_RATE);
  assert(bkvoice_pack_load(manifest, "greeting", &pack) == 0);
  assert(pack.version == BKVOICE_PACK_VERSION);
  assert(pack.clip_count == 1);
  assert(strcmp(pack.speaker_id, "private_voice_01") == 0);
  assert(strcmp(pack.clip_path, wavpath) == 0);

  fd = open(pack.clip_path, O_RDONLY);
  assert(fd >= 0);
  assert(bkvoice_wav_parse(fd, &wav) == 0);
  assert(wav.data_offset == 44);
  assert(wav.data_bytes == 4);
  assert(wav.sample_rate == BKVOICE_SAMPLE_RATE);
  assert(close(fd) == 0);

  write_text(manifest,
             "format=bkvoice-pack-v1\n"
             "speaker_id=private_voice_01\n"
             "authorization=missing-consent\n"
             "disclosure=synthetic-voice\n"
             "sample_rate=16000\nchannels=1\nencoding=pcm-s16le\n"
             "clip.greeting=greeting.wav\n");
  assert(bkvoice_pack_load(manifest, NULL, &pack) == -EACCES);

  write_text(manifest,
             "format=bkvoice-pack-v1\n"
             "speaker_id=private_voice_01\n"
             "authorization=explicit-consent\n"
             "disclosure=synthetic-voice\n"
             "sample_rate=16000\nchannels=1\nencoding=pcm-s16le\n"
             "clip.greeting=../escape.wav\n");
  assert(bkvoice_pack_load(manifest, "greeting", &pack) == -EINVAL);

  write_text(manifest, valid_manifest);
  write_wav(wavpath, 8000);
  fd = open(wavpath, O_RDONLY);
  assert(fd >= 0);
  assert(bkvoice_wav_parse(fd, &wav) == -ENOTSUP);
  assert(close(fd) == 0);

  assert(unlink(manifest) == 0);
  assert(unlink(wavpath) == 0);
  assert(rmdir(directory) == 0);
  printf("BKVOICE_PACK_HOST_PASS\n");
  return 0;
}
