/****************************************************************************
 * app/bk7258/bk7258_voice_pack.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Portable parser for the BK7258 local authorized-voice pack contract.
 ****************************************************************************/

#ifndef __APP_BK7258_BK7258_VOICE_PACK_H
#define __APP_BK7258_BK7258_VOICE_PACK_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#define BKVOICE_PACK_VERSION        1u
#define BKVOICE_SAMPLE_RATE         16000u
#define BKVOICE_CHANNELS            1u
#define BKVOICE_BITS_PER_SAMPLE     16u
#define BKVOICE_MAX_DURATION_MS     30000u
#define BKVOICE_MAX_DATA_BYTES      \
  (BKVOICE_SAMPLE_RATE * (BKVOICE_BITS_PER_SAMPLE / 8u) * \
   BKVOICE_MAX_DURATION_MS / 1000u)
#define BKVOICE_MAX_CLIPS           32u
#define BKVOICE_ID_SIZE             48u
#define BKVOICE_PATH_SIZE           256u

struct bkvoice_pack_info_s
{
  unsigned int version;
  unsigned int clip_count;
  unsigned int error_line;
  char speaker_id[BKVOICE_ID_SIZE];
  char clip_path[BKVOICE_PATH_SIZE];
};

struct bkvoice_wav_info_s
{
  off_t data_offset;
  uint32_t data_bytes;
  uint32_t sample_rate;
  uint16_t channels;
  uint16_t bits_per_sample;
};

/* Load and validate a strict bkvoice-pack-v1 manifest.  selected_clip may be
 * NULL for metadata-only verification.  A selected clip is always resolved
 * relative to the manifest directory; absolute paths and traversal are
 * rejected.
 */

int bkvoice_pack_load(const char *manifest_path, const char *selected_clip,
                      struct bkvoice_pack_info_s *info);

/* Validate a regular RIFF/WAVE PCM file and leave fd positioned at data. */

int bkvoice_wav_parse(int fd, struct bkvoice_wav_info_s *info);

#endif /* __APP_BK7258_BK7258_VOICE_PACK_H */
