/* SPDX-License-Identifier: Apache-2.0 */
/* Development-only caller of the deployed Agent/Media adapters. Capture is
 * separately selected and never saved or sent to a cloud service. */
#include "voice/audio_playback.h"
#include "bk7258_engineering_test.h"

#ifdef CONFIG_BK7258_AUDIO_CAPTURE_VALIDATION
static int product_capture_validation(void)
{
  int16_t pcm[320];

  for (unsigned int session = 0; session < 2; session++)
    {
      syslog(LOG_NOTICE, "BKCAPTURE session=%u phase=open-enter\n", session);
      audio_capture_t *cap = audio_capture_open_local(NULL, 16000, 1, 16);
      if (cap == NULL)
        {
          return errno ? -errno : -EIO;
        }

      int result = audio_capture_start(cap);
      int bytes = 0;
      unsigned int frames = 0;
      uint64_t next_sample = 0;
      uint64_t deadline = bkvoice_config_now_ms(NULL) + 2000;
      /* Ten 20 ms frames distinguish a startup transient from sustained
       * clipping. Keep only aggregate statistics and clear every frame. */

      while (result == 0 && frames < 10)
        {
          uint64_t first_sample;
          int n = audio_capture_read_local(cap, pcm, sizeof(pcm),
                                           &first_sample);
          if (n > 0)
            {
              int64_t sum = 0;
              unsigned int peak = 0;
              unsigned int clipped = 0;
              unsigned int samples = (unsigned int)n / sizeof(pcm[0]);

              if (n % sizeof(pcm[0]) != 0 ||
                  (frames > 0 && first_sample != next_sample))
                {
                  result = -EPROTO;
                }

              for (unsigned int i = 0; i < samples; i++)
                {
                  int value = pcm[i];
                  unsigned int magnitude = value < 0 ? -value : value;
                  sum += value;
                  if (magnitude > peak)
                    {
                      peak = magnitude;
                    }

                  clipped += magnitude >= 32767;
                }

              memset(pcm, 0, sizeof(pcm));
              syslog(LOG_NOTICE,
                     "BKCAPTURE session=%u frame=%u sample=%llu bytes=%d "
                     "peak=%u dc=%ld clipped_permyriad=%u result=%d\n",
                     session, frames, (unsigned long long)first_sample, n,
                     peak, samples ? (long)(sum / samples) : 0,
                     samples ? clipped * 10000 / samples : 0, result);
              next_sample = first_sample + samples;
              frames++;
              bytes += n;
            }
          else if (n != -EAGAIN)
            {
              result = n < 0 ? n : -EIO;
            }
          else if (bkvoice_config_now_ms(NULL) >= deadline)
            {
              result = -ETIMEDOUT;
            }

          memset(pcm, 0, sizeof(pcm));
        }

      /* The startup owner is exclusive until core readiness is published. */

      syslog(LOG_NOTICE,
             "BKCAPTURE session=%u phase=abort-enter bytes=%d result=%d\n",
             session, bytes, result);
      int stopped = audio_capture_abort(cap);
      syslog(LOG_NOTICE, "BKCAPTURE session=%u phase=abort-return result=%d\n",
             session, stopped);
      int closed = audio_capture_close(cap);
      syslog(LOG_NOTICE, "BKCAPTURE session=%u phase=close-return result=%d\n",
             session, closed);
      int cleanup = audio_capture_cleanup(2000);
      syslog(LOG_NOTICE,
             "BKCAPTURE session=%u bytes=%d result=%d abort=%d "
             "close=%d cleanup=%d no-cloud\n",
             session, bytes, result, stopped, closed, cleanup);
      if (result || stopped || closed || cleanup)
        {
          return result ? result : stopped ? stopped : closed ? closed :
                 cleanup;
        }
    }

  return 0;
}
#endif

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
      syslog(LOG_NOTICE, "BKAUDIO stage=%u phase=open-enter\n", stage);
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
          syslog(LOG_NOTICE, "BKAUDIO stage=%u phase=cancel-enter\n", stage);
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
          syslog(LOG_NOTICE, "BKAUDIO stage=%u phase=drain-enter\n", stage);
          result = audio_playback_drain(player, 2000);
          syslog(LOG_NOTICE, "BKAUDIO stage=%u phase=drain-return result=%d\n",
                 stage, result);
        }

      syslog(LOG_NOTICE, "BKAUDIO stage=%u phase=close-enter\n", stage);
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
