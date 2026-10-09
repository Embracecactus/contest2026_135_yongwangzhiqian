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

static int product_voice_capture_validation(void)
{
  for (unsigned int session = 0; session < 2; session++)
    {
      struct bkcloud_models_s models;
      const char *backend = voice_asr_get_backend();

      /* Before core readiness the product cannot load cloud settings. Only
       * an unconfigured batch backend is safe here: it cannot preconnect,
       * and even an unexpected endpoint fails prepare before any transport.
       * Do not change the selected backend, saved configuration or callback. */

      if (atomic_load(&g_agent_core_ready) || !voice_channel_is_idle() ||
          voice_asr_stream_supported() || backend == NULL ||
          (strcmp(backend, "mimo") && strcmp(backend, "openai-audio")) ||
          bkagent_cloud_models_get(&models) != -ENOKEY)
        {
          syslog(LOG_NOTICE, "BKVOICECAP session=%u skipped=guard\n", session);
          return 0;
        }

      audio_capture_t *cap = audio_capture_open_local(NULL, 16000, 1, 16);
      if (cap == NULL)
        {
          return errno ? -errno : -EIO;
        }

      int result = audio_capture_start(cap);
      int16_t pcm[320];
      uint64_t first_sample = 0;
      uint64_t deadline = bkvoice_config_now_ms(NULL) + 2000;
      int n = 0;
      while (result == 0)
        {
          n = audio_capture_read_local(cap, pcm, sizeof(pcm), &first_sample);
          memset(pcm, 0, sizeof(pcm));
          if (n == sizeof(pcm))
            {
              result = audio_capture_handoff_at(cap, first_sample + 320);
              break;
            }

          if (n != -EAGAIN)
            {
              result = n < 0 ? n : -EPROTO;
            }
          else if (bkvoice_config_now_ms(NULL) >= deadline)
            {
              result = -ETIMEDOUT;
            }
        }

      if (!result)
        {
          result = voice_channel_start_auto();
        }

      if (result)
        {
          /* No auto task owns the pending handoff when start fails. */

          audio_capture_abort(cap);
          audio_capture_close(cap);
          audio_capture_cleanup(2000);
          return result;
        }

      /* Ownership has transferred. Never access cap from this caller again.
       * The delay bounds capture; existing recorder logs, not the delay,
       * establish whether cancellation actually reached a recording turn. */

      nxsig_usleep(250000);
      int canceled = voice_channel_cancel();
      deadline = bkvoice_config_now_ms(NULL) + 3000;
      while (!voice_channel_is_idle() &&
             bkvoice_config_now_ms(NULL) < deadline)
        {
          nxsig_usleep(20000);
        }

      int idle = voice_channel_is_idle();
      int cleanup = idle ? audio_capture_cleanup(2000) : -ETIMEDOUT;
      syslog(LOG_NOTICE,
             "BKVOICECAP session=%u handoff_bytes=%d cancel=%d idle=%d "
             "cleanup=%d no-cloud\n", session, n, canceled, idle, cleanup);
      if (canceled || !idle || cleanup)
        {
          return canceled ? canceled : cleanup;
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

#ifdef CONFIG_BK7258_AUDIO_PIPELINE_VALIDATION
/* This calls the real manual voice owner and Agent service worker. Only the
 * external services are fixed memory responses. Mic samples are erased by the
 * capture owner; the ASR adapter substitutes deterministic PCM before encode. */
static int product_pipeline_validation(void)
{
  if (atomic_load(&g_agent_core_ready) || !voice_channel_is_idle()) return -EBUSY;
  int result = bkagent_cloud_validation_begin();
  syslog(LOG_NOTICE, "BKPIPE begin=%d peer=memory no-network fixed-ASR-PCM\n", result);
  if (result) return result;
  atomic_store(&g_pipeline_window, true);
  for (unsigned int session = 0; session < 3 && !result; session++)
    {
      bool cancel = session == 1;
      atomic_store(&g_pipeline_media, false);
      atomic_store(&g_pipeline_complete, false);
      atomic_store(&g_pipeline_result, -EINPROGRESS);
      result = bkagent_cloud_validation_reset(cancel);
      if (!result) result = voice_channel_start();
      if (!result)
        {
          nxsig_usleep(250000);
          result = voice_channel_stop();
        }
      uint64_t deadline = bkvoice_config_now_ms(NULL) + 10000;
      bool canceled = false;
      while (!result && !atomic_load(&g_pipeline_complete) &&
             bkvoice_config_now_ms(NULL) < deadline)
        {
          if (cancel && !canceled && atomic_load(&g_pipeline_media))
            {
              result = voice_channel_cancel();
              canceled = true;
            }
          nxsig_usleep(20000);
        }
      if (!result && !atomic_load(&g_pipeline_complete)) result = -ETIMEDOUT;
      if (result) (void)voice_channel_cancel();
      deadline = bkvoice_config_now_ms(NULL) + 3000;
      while ((!voice_channel_is_idle() || voice_asr_is_busy() ||
              voice_tts_is_busy() || llm_request_busy()) &&
             bkvoice_config_now_ms(NULL) < deadline)
        {
          (void)voice_channel_recover();
          nxsig_usleep(20000);
        }
      if (!voice_channel_is_idle() || voice_asr_is_busy() ||
          voice_tts_is_busy() || llm_request_busy())
        {
          result = -EBUSY; /* Do not read stats or free an in-flight owner. */
          break;
        }
      int complete = atomic_load(&g_pipeline_result);
      if (!result && complete != (cancel ? -ECANCELED : 0)) result = -EPROTO;
      struct bkcloud_fixture_report_s peer;
      struct bkagent_cloud_validation_s pcm;
      struct bkcloud_fixture_pcm_expectation_s expected;
      bkcloud_fixture_report(&peer);
      bkagent_cloud_validation_pcm(&pcm);
      bkcloud_fixture_pcm_expectation(&expected);
      bool samples = pcm.bytes[0] == expected.expected_16k_bytes[0] &&
                     pcm.hash[0] == expected.expected_16k_hash[0];
      if (!cancel) samples = samples &&
        pcm.bytes[1] == expected.expected_16k_bytes[1] &&
        pcm.hash[1] == expected.expected_16k_hash[1];
      bool texts = pcm.text_matches[0] && (cancel || pcm.text_matches[1]);
      bool idle = voice_channel_is_idle();
      if (!result && (!idle || !samples || !texts ||
          !atomic_load(&g_pipeline_media) ||
          peer.asr_requests != 1 || peer.plan_requests != 1 ||
          peer.final_requests != 1 || (cancel &&
          (peer.tail_released_after_media || peer.tts_requests != 1 ||
           pcm.bytes[1] != 0)) || (!cancel &&
          (!peer.tail_released_after_media || peer.tts_requests != 2))))
        result = -EPROTO;
      syslog(LOG_NOTICE,
        "BKPIPE session=%u ret=%d complete=%d idle=%d media=%d cancel=%d "
        "asr=%u plan=%u final=%u tts=%u tail_after_media=%d samples=%d texts=%d "
        "pcm0=%zu/%08lx pcm1=%zu/%08lx\n", session, result, complete, idle,
        atomic_load(&g_pipeline_media), canceled, peer.asr_requests,
        peer.plan_requests, peer.final_requests, peer.tts_requests,
        peer.tail_released_after_media, samples, texts, pcm.bytes[0],
        (unsigned long)pcm.hash[0], pcm.bytes[1], (unsigned long)pcm.hash[1]);
      if (!idle)
        {
          int recovered = voice_channel_recover();
          syslog(LOG_NOTICE, "BKPIPE recovery=%d idle=%d\n", recovered,
                 voice_channel_is_idle());
        }
    }
  int restored = bkagent_cloud_validation_end();
  atomic_store(&g_pipeline_window, false);
  syslog(LOG_NOTICE, "BKPIPE restore=%d idle=%d saved-config-untouched\n",
         restored, voice_channel_is_idle());
  return result ? result : restored;
}
#endif
