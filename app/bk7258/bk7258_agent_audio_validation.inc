/* SPDX-License-Identifier: Apache-2.0 */
/* Development-only caller of the deployed Agent/Media adapter. No microphone,
 * cloud request, volume change, persistent write or replacement player. */
#include "voice/audio_playback.h"
#include "bk7258_engineering_test.h"

static int product_audio_validation_report(struct bkengaudio_report_s *report)
{
  int16_t pcm[256];
  int overall = 0;

  if (report == NULL)
    {
      return -EINVAL;
    }

  memset(report, 0, sizeof(*report));
  /* Fixed 500 Hz triangle, peak 256/32768, no change to saved volume. */
  for (unsigned int i = 0; i < 256; i++)
    {
      unsigned int phase = i % 32;
      pcm[i] = (int16_t)((phase < 16 ? phase : 32 - phase) * 32 - 256);
    }

  for (unsigned int stage = 0; stage < 3; stage++)
    {
      audio_playback_t *player = audio_playback_open(NULL, 16000, 1, 16);
      if (!player)
        {
          overall = errno ? -errno : -EIO;
          break;
        }

      int result = 0;
      unsigned int bytes = 0;
      for (unsigned int block = 0; block < 16; block++)
        {
          int n = audio_playback_write(player, pcm, sizeof(pcm));
          if (n != sizeof(pcm))
            {
              result = n < 0 ? n : -EIO;
              break;
            }
          bytes += n;
        }

      report->accepted_bytes += bytes;

      if (!result && stage == 1)
        {
          audio_playback_stop(player);
          int written = audio_playback_write(player, pcm, sizeof(pcm));
          int drained = audio_playback_drain(player, 2000);
          report->cancel_write = written;
          report->cancel_drain = drained;
          if (written != -ECANCELED || drained != -ECANCELED)
            {
              result = -EPROTO;
            }

          syslog(LOG_NOTICE, "BKAUDIO cancel write=%d drain=%d\n",
                 written, drained);
        }
      else if (!result)
        {
          result = audio_playback_drain(player, 2000);
        }

      int closed = audio_playback_close(player);
      if (stage == 0)
        {
          report->eof_result = result;
          report->eof_close = closed;
        }
      else if (stage == 1)
        {
          report->cancel_close = closed;
        }
      else
        {
          report->next_result = result;
          report->next_close = closed;
        }

      report->stages++;
      syslog(LOG_NOTICE,
             "BKAUDIO stage=%u accepted_bytes=%u result=%d close=%d\n",
             stage, bytes, result, closed);
      if (result || closed)
        {
          overall = result ? result : closed;
          break;
        }
    }

  if (overall == 0)
    {
      overall = audio_playback_cleanup(2000);
    }

  report->result = overall;
  return overall;
}

#if defined(CONFIG_BK7258_AUDIO_PLAYBACK_VALIDATION) || \
    !defined(CONFIG_BK7258_ENGINEERING_TEST)
static int product_audio_validation(void)
{
  struct bkengaudio_report_s report;

  return product_audio_validation_report(&report);
}
#endif
