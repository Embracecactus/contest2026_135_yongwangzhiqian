/* SPDX-License-Identifier: Apache-2.0 */
#include "bk7258_cloud_runtime.h"
#include "bk7258_cloud_client.h"
#include "bk7258_voice_tls.h"
#include "bk7258_preferences.h"
#ifdef CONFIG_BK7258_VISION_SERVICE
#include "bk7258_vision_service.h"
#endif
#include <stdio.h>
#include <errno.h>
#include <netdb.h>
#include <stdlib.h>
#ifdef __NuttX__
#include <malloc.h>
#endif
#include <string.h>
#include <syslog.h>
#include <mbedtls/platform_util.h>
#if defined(CONFIG_BK7258_PROVISION_GATT) && defined(CONFIG_BK7258_PREFERENCES)
#define BKCLOUD_MEMORY_RUNTIME 1
#include "bk7258_cloud_memory.h"
#include <mbedtls/entropy.h>
#endif

enum memory_action_e { MEMORY_SAVE, MEMORY_READ_POLICY, MEMORY_DISABLE, MEMORY_ENABLE, MEMORY_DELETE };

struct bkcloud_runtime_s
{
  struct bkcloud_config_s config;
  struct bkvoice_config_s *trust;
  struct bkvoice_ptt_s *ptt;
  struct bkvoice_tls_s tls;
  struct bkcloud_history_s history;
  pthread_t worker;
  sem_t *wake;
  uint8_t *pcm;
  size_t pcm_size;
  char input[BKCLOUD_TEXT_MAX+1];
  char reply[BKCLOUD_TEXT_MAX+1];
  uint32_t epoch;
  int result;
  bool done;
  bool joinable;
  bool probe;
  bool ready;
  bool armed;
  bool pressed;
  bool automatic_capture;
  bool cancelled;
  bool cancel_requested;
  bool pending_history;
  bool memory_job;
  bool memory_bound;
  bool memory_restored;
  bool memory_enabled;
  bool memory_known;
  bool memory_uncertain;
  int memory_error;
  enum memory_action_e memory_action;
  uint8_t memory_owner[32];
  enum bk7258_persona_e persona;
};
static const char *persona_style(enum bk7258_persona_e persona)
{
  switch(persona)
    {
      case BK7258_PERSONA_PLAYFUL: return "语气活泼，适当开轻松的玩笑。";
      case BK7258_PERSONA_QUIET: return "语气安静克制，通常只回复一两句话。";
      case BK7258_PERSONA_SERIOUS: return "语气认真，直接回应问题，给出清楚的建议。";
      case BK7258_PERSONA_TSUNDERE_LITE: return "可以轻微俏皮地嘴硬，但保持尊重，不贬低或操控对方。";
      default: return "语气温柔，耐心倾听，不夸大亲密关系。";
    }
}
static void persona_prompt(struct bkcloud_runtime_s *r,char *prompt,size_t size)
{
#ifdef CONFIG_BK7258_PREFERENCES
  struct bk7258_preferences_s preferences;
  int ret=bk7258_preferences_get(&preferences);
  if(!ret && r->persona!=preferences.persona)
    {
      bkcloud_history_clear(&r->history);
      r->persona=preferences.persona;
    }
  else if(ret) syslog(LOG_WARNING,"BKVOICE CLOUD persona_load=%d cached=1\n",ret);
#endif
  snprintf(prompt,size,
      "你是傻妞，一个虚构的 AI 伴侣。用简短中文自然回答，不冒充真人。不能执行设备操作。%s",
      persona_style(r->persona));
}
#ifdef BKCLOUD_MEMORY_RUNTIME
static bool g_memory_policy_uncertain;
struct memory_io_s
{
  struct bkcloud_memory_policy_s *policy;
  uint8_t *bytes;
  size_t size;
  bool save;
  bool published;
  mbedtls_entropy_context *entropy;
};
static int memory_io(void *context)
{
  struct memory_io_s *io = context;
  if (io->save)
    {
      int ret = bkcloud_memory_save("/mnt/sdnand/shaniu-memory", io->policy,
          io->bytes, io->size, mbedtls_entropy_func, io->entropy);
      io->published = ret == 0 || ret == -EINPROGRESS;
      return ret;
    }
  return bkcloud_memory_restore("/mnt/sdnand/shaniu-memory", io->policy,
      io->bytes, BKCLOUD_HISTORY_BYTES, &io->size);
}
/* Only the joined cloud worker performs I/O. The owner loop never waits for
 * RPMsgFS/SD while handling buttons, configuration or cancellation.
 */
static int memory_process(struct bkcloud_runtime_s *r, bool save)
{
  struct bkcloud_memory_policy_s policy;
  struct memory_io_s io = {0};
  mbedtls_entropy_context entropy;
  if (!r->memory_bound) return 0;
  if (__atomic_load_n(&g_memory_policy_uncertain, __ATOMIC_ACQUIRE)) return -EINPROGRESS;
  int ret = bkcloud_memory_policy_load("/cpdata/shaniu/memory-policy",
                                       r->memory_owner, &policy);
  if (ret < 0) return ret;
  r->memory_enabled = policy.enabled;
  r->memory_known = true;
  if (!policy.enabled) goto done;
  io.bytes = calloc(1, BKCLOUD_HISTORY_BYTES);
  if (!io.bytes) { ret = -ENOMEM; goto done; }
  io.policy = &policy; io.save = save;
  mbedtls_entropy_init(&entropy); io.entropy = &entropy;
  if (save)
    ret = bkcloud_history_encode(&r->history, r->persona, io.bytes,
                                  BKCLOUD_HISTORY_BYTES, &io.size);
  if (!ret) ret = bk7258_preferences_with_storage(memory_io, &io);
  if (save && io.published && ret < 0) ret = -EINPROGRESS;
  /* No destination publication until unmount/release succeeded too. */
  if (!ret && !save)
    ret = bkcloud_history_decode(&r->history, r->persona, io.bytes, io.size);
  mbedtls_entropy_free(&entropy);
  mbedtls_platform_zeroize(io.bytes, BKCLOUD_HISTORY_BYTES); free(io.bytes);
  if (!save && ret == -ENOENT) ret = 0;
done:
  mbedtls_platform_zeroize(&policy, sizeof(policy));
  return ret;
}
#endif
static int capture_start(void *context, const struct bkvoice_turn_token_s *token)
{
  struct bkcloud_runtime_s *r=context; (void)token;
  if(!r->pcm) return -ENOMEM;
  r->pcm_size=0;return 0;
}
static int capture_audio(void *context, const struct bkvoice_turn_token_s *token,
                          const uint8_t *pcm, size_t size)
{
  struct bkcloud_runtime_s *r=context;(void)token;
  if(size>BKCLOUD_PCM_MAX-r->pcm_size) return -EFBIG;
  memcpy(r->pcm+r->pcm_size,pcm,size);r->pcm_size+=size;
  /* The ASR limit includes pre-roll. End on the last accepted frame instead
   * of failing the following frame before the live-only VAD duration ends.
   * The sink never stops Media or joins its own capture worker.
   */
  return r->pcm_size == BKCLOUD_PCM_MAX ? 1 : 0;
}
static int capture_end(void *context, const struct bkvoice_turn_token_s *token)
{ (void)context;(void)token;return 0; }
static int capture_cancel(void *context, const struct bkvoice_turn_token_s *token,int why)
{ (void)context;(void)token;(void)why;return 0; }
static const struct bkvoice_capture_sink_ops_s capture_ops =
{capture_start,capture_audio,capture_end,capture_cancel};
static void wipe_audio(struct bkcloud_runtime_s *r)
{
  if(r->pcm) { mbedtls_platform_zeroize(r->pcm,BKCLOUD_PCM_MAX);free(r->pcm); }
  r->pcm=NULL;r->pcm_size=0;
}
static void wipe_text(struct bkcloud_runtime_s *r)
{
  mbedtls_platform_zeroize(r->input,sizeof(r->input));
  mbedtls_platform_zeroize(r->reply,sizeof(r->reply));
  r->pending_history=false;
}
static bool cancelled(struct bkcloud_runtime_s *r)
{
  return __atomic_load_n(&r->cancelled,__ATOMIC_ACQUIRE);
}
static void bkcloud_probe_failure(bool probe, const char *stage, int ret)
{
  if (probe && ret < 0)
    syslog(LOG_WARNING, "BKVOICE CLOUD probe stage=%s ret=%d\n", stage, ret);
}
#ifdef CONFIG_BK7258_VISION_SERVICE
static bool camera_consent(const char *input)
{
  static const char first[] = "拍照看看";
  static const char second[] = "看看眼前";
  const char *begin = input;
  const char *end = input + strlen(input);

  while (begin < end && *begin == ' ') begin++;
  for (;;)
    {
      if (end > begin && end[-1] == ' ')
        {
          end--;
        }
      else if (end > begin && strchr(".!?,;:", end[-1]) != NULL)
        {
          end--;
        }
      else if (end - begin >= 3 &&
               (!memcmp(end - 3, "。", 3) || !memcmp(end - 3, "！", 3) ||
                !memcmp(end - 3, "？", 3) || !memcmp(end - 3, "，", 3) ||
                !memcmp(end - 3, "；", 3) || !memcmp(end - 3, "：", 3)))
        {
          end -= 3;
        }
      else
        {
          break;
        }
    }

  /* Product phrases are explicit camera-consent triggers, never substrings. */
  return ((size_t)(end - begin) == sizeof(first) - 1 &&
          !memcmp(begin, first, sizeof(first) - 1)) ||
         ((size_t)(end - begin) == sizeof(second) - 1 &&
          !memcmp(begin, second, sizeof(second) - 1));
}

struct bkcloud_camera_turn_s
{
  struct bkcloud_runtime_s *runtime;
  uint64_t deadline;
  uint8_t *jpeg;
};

static int capture_current_view(void *context, const uint8_t **jpeg,
                                size_t *size)
{
  struct bkcloud_camera_turn_s *camera = context;
  if (cancelled(camera->runtime)) return -ECANCELED;
  if (bkvoice_config_now_ms(NULL) >= camera->deadline) return -ETIMEDOUT;
  if (camera->jpeg != NULL) return -EALREADY;
  camera->jpeg = calloc(1, BKCLOUD_JPEG_MAX);
  if (camera->jpeg == NULL) return -ENOMEM;
  int ret = bk7258_vision_capture_jpeg(camera->jpeg, BKCLOUD_JPEG_MAX, size);
  if (cancelled(camera->runtime)) return -ECANCELED;
  if (bkvoice_config_now_ms(NULL) >= camera->deadline) return -ETIMEDOUT;
  if (ret == 0) *jpeg = camera->jpeg;
  return ret;
}

static void release_current_view(struct bkcloud_camera_turn_s *camera)
{
  if (camera->jpeg != NULL)
    {
      mbedtls_platform_zeroize(camera->jpeg, BKCLOUD_JPEG_MAX);
      free(camera->jpeg);
      camera->jpeg = NULL;
    }
}
#endif
static int resolve_endpoint(struct bkcloud_runtime_s *r)
{
  struct addrinfo hints={0};
  struct addrinfo *addresses=NULL;
  hints.ai_family=AF_INET;
  hints.ai_socktype=SOCK_STREAM;
  int error=getaddrinfo(r->config.host,NULL,&hints,&addresses);
  int ret=error == EAI_AGAIN ? -EAGAIN : -EHOSTUNREACH;
  if(!error)
    {
      for(struct addrinfo *p=addresses;p;p=p->ai_next)
        {
          if(p->ai_family!=AF_INET || !p->ai_addr ||
             p->ai_addrlen<sizeof(struct sockaddr_in)) continue;
          struct in_addr address=((struct sockaddr_in *)p->ai_addr)->sin_addr;
          uint32_t value=ntohl(address.s_addr);
          if(!value || value>=0xe0000000u) continue;
          /* The worker exclusively owns TLS before its first connection.
           * Keep certificate hostname and persisted trust unchanged.
           */
          r->tls.config.peer_address=address;
          ret=0;
          break;
        }
    }
  if(addresses) freeaddrinfo(addresses);
  return cancelled(r) ? -ECANCELED : ret;
}
static void *work(void *context)
{
  struct bkcloud_runtime_s *r=context;
  struct bkcloud_client_s *client=calloc(1,sizeof(*client));
  struct bkcloud_tts_s *decoder=NULL;
  struct bkcloud_playback_s *play=NULL;
#ifdef CONFIG_BK7258_VISION_SERVICE
  struct bkcloud_camera_turn_s camera_turn = {.runtime = r};
  const struct bkcloud_camera_s camera = {capture_current_view, &camera_turn};
#endif
  const struct bkcloud_camera_s *offered_camera = NULL;
  int ret=-ENOMEM;
  const char *stage="client_alloc";
  if(!client)
    {
#ifdef __NuttX__
      struct mallinfo memory = mallinfo();
      syslog(LOG_WARNING,
             "BKVOICE CLOUD allocation=%lu free=%lu maxfree=%lu\n",
             (unsigned long)sizeof(*client),
             (unsigned long)memory.fordblks,
             (unsigned long)memory.mxordblk);
#endif
      goto out;
    }
  uint64_t deadline=bkvoice_config_now_ms(NULL)+90000;
  if(cancelled(r)) {stage="cancel";ret=-ECANCELED;goto out;}
  stage="resolve";
  ret=resolve_endpoint(r);
  if(ret) goto out;
  if(r->probe)
    {
      stage="chat";
      ret=bkcloud_chat(client,&r->config,bkvoice_tls_ops(),&r->tls,deadline,
                       "Reply with OK.",&r->history,"Connection check.",
                       NULL,r->reply,sizeof(r->reply));
      goto out;
    }
  stage="asr";
  ret=bkcloud_recognize(client,&r->config,bkvoice_tls_ops(),&r->tls,deadline,
                        r->pcm,r->pcm_size,r->input,sizeof(r->input));
  /* ASR has returned and the capture worker has already joined. Release its
   * raw audio before allocating camera/TTS resources for the same turn. */
  wipe_audio(r);
  if(ret) goto out;
  if(cancelled(r)) {ret=-ECANCELED;goto out;}
  char prompt[512];
  persona_prompt(r,prompt,sizeof(prompt));
#ifdef BKCLOUD_MEMORY_RUNTIME
  if (r->memory_bound && !r->memory_restored && !r->memory_uncertain)
    {
      int memory_ret = r->memory_known && !r->memory_enabled ? 0 : memory_process(r, false);
      r->memory_restored = true;
      if (memory_ret) syslog(LOG_WARNING, "BKVOICE CLOUD memory_restore=%d\n", memory_ret);
    }
#endif
#ifdef CONFIG_BK7258_VISION_SERVICE
  if (camera_consent(r->input))
    {
      camera_turn.deadline = deadline;
      offered_camera = &camera;
    }
#endif
  stage="agent";
  ret=bkcloud_chat(client,&r->config,bkvoice_tls_ops(),&r->tls,deadline,
      prompt,&r->history,r->input,offered_camera,r->reply,sizeof(r->reply));
#ifdef CONFIG_BK7258_VISION_SERVICE
  release_current_view(&camera_turn);
#endif
  if(ret) goto out;
  if(cancelled(r)) {ret=-ECANCELED;goto out;}
  decoder=calloc(1,sizeof(*decoder));play=calloc(1,sizeof(*play));
  if(!decoder || !play) {ret=-ENOMEM;goto out;}
  stage="tts";
  ret=bkcloud_synthesize_turn(client,decoder,play,&r->ptt->turn,&r->config,
                               bkvoice_tls_ops(),&r->tls,deadline,
                               bkvoice_config_now_ms,NULL,r->reply);
out:
#ifdef CONFIG_BK7258_VISION_SERVICE
  release_current_view(&camera_turn);
#endif
  bkcloud_probe_failure(r->probe, stage, ret);
  if (!r->probe && ret < 0)
    syslog(LOG_WARNING, "BKVOICE CLOUD turn stage=%s ret=%d\n", stage, ret);
  if(client) {mbedtls_platform_zeroize(client,sizeof(*client));free(client);}
  if(decoder) {bkcloud_tts_clear(decoder);free(decoder);}
  if(play) {mbedtls_platform_zeroize(play,sizeof(*play));free(play);}
  r->result=ret;
  __atomic_store_n(&r->done,true,__ATOMIC_RELEASE);
  if(r->wake) sem_post(r->wake);
  return NULL;
}
static int launch(struct bkcloud_runtime_s *r,bool probe)
{
  if(r->joinable) return -EBUSY;
  struct bkvoice_tls_config_s config={
    .peer_address=r->trust->peer_address,.server_ca=&r->trust->ca,
    .trusted_time=bkvoice_config_trusted_time,.now_ms=bkvoice_config_now_ms,
    .clock_context=r->trust,.server_auth_only=true};
  int ret=bkvoice_tls_initialize(&r->tls,&config);
  if(ret) {bkcloud_probe_failure(probe,"tls_initialize",ret);return ret;}
  pthread_attr_t attr;
  ret=pthread_attr_init(&attr);
  if(ret) {bkcloud_probe_failure(probe,"pthread_attr_init",-ret);bkvoice_tls_uninitialize(&r->tls);return -ret;}
  const char *stage="pthread_stack";
  ret=pthread_attr_setstacksize(&attr,16384);
  r->memory_job=false;
  r->probe=probe;__atomic_store_n(&r->cancelled,false,__ATOMIC_RELEASE);r->result=0;
  __atomic_store_n(&r->done,false,__ATOMIC_RELEASE);
  if(!ret) {stage="pthread_create";ret=pthread_create(&r->worker,&attr,work,r);}
  pthread_attr_destroy(&attr);
  if(ret) {bkcloud_probe_failure(probe,stage,-ret);bkvoice_tls_uninitialize(&r->tls);return -ret;}
  r->joinable=true;return 0;
}
#ifdef BKCLOUD_MEMORY_RUNTIME
static void *memory_work(void *context)
{
  struct bkcloud_runtime_s *r = context;
  int ret;
  if (r->memory_action == MEMORY_SAVE) ret = memory_process(r, true);
  else
    {
      struct bkcloud_memory_policy_s policy;
      ret = __atomic_load_n(&g_memory_policy_uncertain, __ATOMIC_ACQUIRE) ? -EINPROGRESS : 0;
      if (!ret && r->memory_action != MEMORY_READ_POLICY)
        {
          mbedtls_entropy_context entropy;
          mbedtls_entropy_init(&entropy);
          ret = bkcloud_memory_policy_set("/cpdata/shaniu/memory-policy", r->memory_owner,
              r->memory_action == MEMORY_ENABLE,
              r->memory_action == MEMORY_DELETE, mbedtls_entropy_func, &entropy);
          mbedtls_entropy_free(&entropy);
        }
      /* Uncertain publication must not become success by same-boot readback. */
      if (!ret) ret = bkcloud_memory_policy_load("/cpdata/shaniu/memory-policy",
                                                 r->memory_owner, &policy);
      if (!ret)
        {
          r->memory_known = true; r->memory_enabled = policy.enabled;
          if (r->memory_action == MEMORY_ENABLE) r->memory_restored = r->history.count != 0;
          if (r->memory_action == MEMORY_DELETE)
            { bkcloud_history_clear(&r->history); r->memory_restored = true; }
        }
      else
        {
          r->memory_known = false; r->memory_enabled = false;
          if (ret == -EINPROGRESS)
            {
              r->memory_uncertain = true;
              __atomic_store_n(&g_memory_policy_uncertain, true, __ATOMIC_RELEASE);
            }
        }
      mbedtls_platform_zeroize(&policy, sizeof(policy));
    }
  r->result = ret; r->memory_error = ret;
  __atomic_store_n(&r->done, true, __ATOMIC_RELEASE);
  if (r->wake) sem_post(r->wake);
  return NULL;
}
static int memory_launch(struct bkcloud_runtime_s *r, enum memory_action_e action)
{
  pthread_attr_t attr;
  int ret = pthread_attr_init(&attr);
  if (ret) return -ret;
  ret = pthread_attr_setstacksize(&attr, 16384);
  r->memory_job = true; r->memory_action = action;
  __atomic_store_n(&r->done, false, __ATOMIC_RELEASE);
  if (!ret) ret = pthread_create(&r->worker, &attr, memory_work, r);
  pthread_attr_destroy(&attr);
  if (ret) { r->memory_job = false; return -ret; }
  r->joinable = true;
  return 0;
}
#endif
static int reap(struct bkcloud_runtime_s *r)
{
  if(!r->joinable) return 0;
  if(!__atomic_load_n(&r->done,__ATOMIC_ACQUIRE)) return -EAGAIN;
  int ret=pthread_join(r->worker,NULL);
  if(ret) return -ret;
  r->joinable=false;
  ret=r->memory_job ? 0 : bkvoice_tls_uninitialize(&r->tls);
  return ret;
}
int bkcloud_runtime_create(struct bkcloud_runtime_s **output,
                            const void *record,size_t size,
                            struct bkvoice_config_s *trust,
                            struct bkvoice_ptt_s *ptt,sem_t *wake)
{
  if(!output || *output || !trust || !trust->initialized || !ptt) return -EINVAL;
  struct bkcloud_runtime_s *r=calloc(1,sizeof(*r));
  if(!r) {syslog(LOG_WARNING,"BKVOICE CLOUD create stage=alloc ret=%d\n",-ENOMEM);return -ENOMEM;}
  int ret=bkcloud_config_decode(&r->config,record,size);
  if(!ret && (strcmp(r->config.host,trust->host) || r->config.port!=trust->port)) ret=-EINVAL;
  if(ret) {mbedtls_platform_zeroize(r,sizeof(*r));free(r);return ret;}
  r->trust=trust;r->ptt=ptt;r->wake=wake;*output=r;return 0;
}
int bkcloud_runtime_connect(struct bkcloud_runtime_s *r)
{ return r ? launch(r,true) : -EINVAL; }
int bkcloud_runtime_ready(struct bkcloud_runtime_s *r)
{
  if(!r) return -EINVAL;
  if(r->ready) return 1;
  int ret=reap(r);
  if(ret==-EAGAIN) return 0;
  if(ret) return ret;
  if(r->result) return r->result;
  if(!r->probe || !r->done) return -ENOTCONN;
  if(r->ptt->turn.last_session_id>=UINT32_MAX-1) return -EOVERFLOW;
  ret=bkvoice_ptt_session_open(r->ptt,r->ptt->turn.last_session_id+1,&capture_ops,r);
  if(ret) return ret;
  r->ready=true;wipe_text(r);return 1;
}
int bkcloud_runtime_clear(struct bkcloud_runtime_s **runtime)
{
  if(!runtime || !*runtime) return 0;
  struct bkcloud_runtime_s *r=*runtime;
  if(r->joinable)
    {
      __atomic_store_n(&r->cancelled,true,__ATOMIC_RELEASE);
      if (!r->memory_job) bkvoice_tls_ops()->interrupt(&r->tls);
      int ret=reap(r);if(ret) return ret;
    }
  int ret=bkvoice_ptt_session_close(r->ptt,-ECANCELED);
  /* A failed probe never opens PTT capture.  Its already-closed result is
   * safe only after confirming that no PTT worker remains to be joined.
   */
  if(ret==-EALREADY)
    {
      if(r->ptt->worker_joinable) return -EAGAIN;
      ret=0;
    }
  if(ret) return ret;
  wipe_audio(r);mbedtls_platform_zeroize(r,sizeof(*r));free(r);*runtime=NULL;
  return 0;
}
bool bkcloud_runtime_busy(const struct bkcloud_runtime_s *r)
{ return r && (r->joinable || r->pressed || r->automatic_capture ||
               r->pending_history || r->cancel_requested); }
int bkcloud_runtime_memory_owner(struct bkcloud_runtime_s *r, const uint8_t owner[32])
{
  if (!r || !owner) return -EINVAL;
  /* Idempotent refresh must not touch a worker's borrowed identity/history. */
  if (r->memory_bound && !memcmp(r->memory_owner, owner, 32)) return 0;
  if (!r->ready || bkcloud_runtime_busy(r) || r->ptt->worker_joinable ||
      r->ptt->turn.state != BKVOICE_TURN_IDLE) return -EBUSY;
  uint8_t bits = 0;
  for (size_t i = 0; i < 32; i++) bits |= owner[i];
  if (!bits) return -EINVAL;
  bkcloud_history_clear(&r->history);
  memcpy(r->memory_owner, owner, 32);
  r->memory_bound = true; r->memory_restored = false; r->memory_enabled = false;
  r->memory_known = false; r->memory_error = 0; r->memory_uncertain = false;
#ifdef BKCLOUD_MEMORY_RUNTIME
  int ret = memory_launch(r, MEMORY_READ_POLICY);
  if (ret) { r->memory_bound = false; return ret; }
#endif
  return 0;
}
int bkcloud_runtime_memory_set(struct bkcloud_runtime_s *r, bool enabled, bool erase)
{
#ifndef BKCLOUD_MEMORY_RUNTIME
  (void)r; (void)enabled; (void)erase; return -ENOTSUP;
#else
  if (!r || !r->ready || !r->memory_bound) return -ENOTCONN;
  if (bkcloud_runtime_busy(r) || r->ptt->worker_joinable ||
      r->ptt->turn.state != BKVOICE_TURN_IDLE) return -EBUSY;
  if (r->memory_uncertain || __atomic_load_n(&g_memory_policy_uncertain, __ATOMIC_ACQUIRE)) return -EINPROGRESS;
  return memory_launch(r, erase ? MEMORY_DELETE : enabled ? MEMORY_ENABLE : MEMORY_DISABLE);
#endif
}
int bkcloud_runtime_clear_history(struct bkcloud_runtime_s *r)
{
  if(!r || !r->ready) return -ENOTCONN;
  if(bkcloud_runtime_busy(r) || r->ptt->worker_joinable ||
     r->ptt->turn.state != BKVOICE_TURN_IDLE) return -EBUSY;
  bkcloud_history_clear(&r->history);
  r->memory_restored = true;
  wipe_text(r);
  return 0;
}
int bkcloud_runtime_auto_begin(
  struct bkcloud_runtime_s *r,
  bkvoice_capture_frame_filter_t frame_filter,
  void *live_context)
{
  struct bkvoice_turn_token_s token;
  int ret;

  if (!r || !r->ready)
    {
      return -ENOTCONN;
    }

  if (frame_filter == NULL)
    {
      return -EINVAL;
    }

  if (r->automatic_capture || r->joinable || r->memory_job ||
      r->cancel_requested || r->pending_history || r->pressed || r->pcm ||
      r->ptt->worker_joinable ||
      r->ptt->turn.state != BKVOICE_TURN_IDLE)
    {
      return -EBUSY;
    }

  r->automatic_capture = true;
  r->armed = false;
  r->pcm = malloc(BKCLOUD_PCM_MAX);
  if (r->pcm == NULL)
    {
      r->automatic_capture = false;
      return -ENOMEM;
    }

  ret = bkvoice_ptt_down_stream(
    r->ptt, bkvoice_config_now_ms(NULL), NULL, NULL,
    0, frame_filter, live_context, &token);
  if (ret < 0 && r->ptt->worker_joinable)
    {
      /* The capture worker may still hold the sink PCM.  Preserve it until
       * step() has completed the normal cancel/stop/join sequence, but do
       * not let this failed automatic turn reach launch().
       */

      r->cancel_requested = true;
    }
  else if (ret < 0)
    {
      r->automatic_capture = false;
      wipe_audio(r);
    }

  return ret;
}
int bkcloud_runtime_auto_end(struct bkcloud_runtime_s *r)
{
  int ret;

  if (!r || !r->ready)
    {
      return -ENOTCONN;
    }

  if (!r->automatic_capture)
    {
      return -EPERM;
    }

  if (r->cancel_requested)
    {
      return -ECANCELED;
    }

  ret = bkvoice_ptt_up(r->ptt, bkvoice_config_now_ms(NULL));
  if (ret < 0)
    {
      if (r->ptt->worker_joinable)
        {
          return ret;
        }

      (void)bkvoice_ptt_cancel(r->ptt, ret);
      r->automatic_capture = false;
      r->armed = false;
      wipe_audio(r);
      return ret;
    }

  r->automatic_capture = false;
  if (r->pcm_size != 0)
    {
      ret = launch(r, false);
    }
  else
    {
      ret = -ENODATA;
    }

  if (ret < 0)
    {
      (void)bkvoice_ptt_cancel(r->ptt, ret);
      r->armed = false;
      wipe_audio(r);
    }

  return ret;
}
int bkcloud_runtime_cancel(struct bkcloud_runtime_s *r)
{
  if(!r || !r->ready) return -ENOTCONN;
  r->cancel_requested=true;
  r->armed=false;
  if(r->joinable)
    {
      __atomic_store_n(&r->cancelled,true,__ATOMIC_RELEASE);
      if (!r->memory_job) bkvoice_tls_ops()->interrupt(&r->tls);
    }
  return 0;
}
int bkcloud_runtime_cancel_drain(struct bkcloud_runtime_s *r)
{
  int ret;

  if (!r || !r->ready) return -ENOTCONN;
  if (!r->cancel_requested)
    {
      ret = bkcloud_runtime_cancel(r);
      if (ret < 0) return ret;
    }

  /* The cancellation branch ignores input epochs while work is borrowed.
   * Use a neutral disconnected input so completion cannot re-arm capture. */
  bkcloud_runtime_step(r, false, false, r->epoch);
  return bkcloud_runtime_busy(r) ? -EAGAIN : 0;
}
void bkcloud_runtime_status(const struct bkcloud_runtime_s *r,
                            struct bkcloud_runtime_status_s *status)
{
  memset(status,0,sizeof(*status));
  status->turn_state = UINT32_MAX;
  if(!r) return;
  status->ready=r->ready;
  status->armed=r->armed;
  status->pressed=r->pressed;
  status->busy=bkcloud_runtime_busy(r);
  status->worker_active=r->joinable;
  if (r->automatic_capture)
    {
      enum bkvoice_capture_state_e state =
        __atomic_load_n(&r->ptt->capture.state, __ATOMIC_ACQUIRE);
      status->capture_finished = state == BKVOICE_CAPTURE_STOPPED ||
                                 state == BKVOICE_CAPTURE_FAULTED;
    }
  status->turn_state=r->joinable ? UINT32_MAX : (uint32_t)r->ptt->turn.state;
  if(!r->joinable) status->last_error=r->ptt->turn.last_error;
#ifdef BKCLOUD_MEMORY_RUNTIME
  status->memory_supported = true;
  status->memory_pending = r->joinable && r->memory_job;
  if (!r->joinable)
    {
      status->memory_known = r->memory_known;
      status->memory_enabled = r->memory_known && r->memory_enabled;
      status->memory_failed = r->memory_error != 0;
    }
#endif
}
void bkcloud_runtime_step(struct bkcloud_runtime_s *r,bool link,bool level,uint32_t epoch)
{
  if(!r || !r->ready) return;
  if (r->joinable && r->memory_job)
    {
      if (reap(r)) return;
      syslog(LOG_NOTICE, "BKVOICE CLOUD memory_action=%u result=%d\n", r->memory_action, r->result);
      r->memory_job = false;
      r->armed = link && !level;
      return;
    }
  bool changed=r->epoch!=epoch;
  r->epoch=epoch;
  if(r->cancel_requested)
    {
      if(r->joinable)
        {
          __atomic_store_n(&r->cancelled,true,__ATOMIC_RELEASE);
          bkvoice_tls_ops()->interrupt(&r->tls);
          if(reap(r)) return;
        }
      /* The cloud worker can already have closed the turn after TLS was
       * interrupted. PTT then reports EALREADY (or has no capture session).
       * Completion depends on released resources, not a second cancel's
       * return code; otherwise cancel_requested keeps wake disabled forever.
       */
      if (!bkvoice_ptt_quiescent(r->ptt))
        (void)bkvoice_ptt_cancel(r->ptt, -ECANCELED);
      if (!bkvoice_ptt_quiescent(r->ptt)) return;
      wipe_audio(r);wipe_text(r);r->pressed=false;r->automatic_capture=false;
      r->cancel_requested=false;
      /* A held key must be released before a new capture can start. */
      r->armed=link && !level;
      return;
    }
  if(r->joinable)
    {
      if(cancelled(r) || !link || changed || (level && r->armed))
        {__atomic_store_n(&r->cancelled,true,__ATOMIC_RELEASE);bkvoice_tls_ops()->interrupt(&r->tls);r->armed=false;}
      if(!level) r->armed=true;
      int ret=reap(r);
      if(ret) return;
      wipe_audio(r);
      if(r->result || r->cancelled)
        {
          bkvoice_ptt_cancel(r->ptt,r->result ? r->result : -ECANCELED);
          syslog(LOG_WARNING,"BKVOICE CLOUD turn_fail=%d\n",r->result ? r->result : -ECANCELED);
          wipe_text(r);
        }
      else r->pending_history=true;
    }
  if (r->automatic_capture)
    {
      if (!link || changed)
        {
          if (!bkvoice_ptt_quiescent(r->ptt))
            (void)bkvoice_ptt_cancel(r->ptt, -ENOTCONN);
          r->armed = false;
          if (!bkvoice_ptt_quiescent(r->ptt))
            {
              return;
            }

          wipe_audio(r);
          wipe_text(r);
          r->automatic_capture = false;
          return;
        }

      int ret = bkvoice_ptt_timeout(r->ptt, bkvoice_config_now_ms(NULL));
      if (ret != -EAGAIN && !r->ptt->worker_joinable)
        {
          wipe_audio(r);
          r->automatic_capture = false;
          r->armed = false;
        }

      return;
    }
  if(!link || changed)
    {
      if (!bkvoice_ptt_quiescent(r->ptt))
        (void)bkvoice_ptt_cancel(r->ptt, -ENOTCONN);
      r->armed=false;
      if (!bkvoice_ptt_quiescent(r->ptt)) return;
      wipe_audio(r);wipe_text(r);r->pressed=false;return;
    }
  if(r->pending_history)
    {
      if(r->ptt->turn.state != BKVOICE_TURN_IDLE &&
         bkvoice_config_now_ms(NULL) >= r->ptt->turn.deadline_ms)
        {
          (void)bkvoice_ptt_timeout(r->ptt,bkvoice_config_now_ms(NULL));
          if(r->ptt->turn.state == BKVOICE_TURN_IDLE)
            wipe_text(r);
          r->armed=false;
          return;
        }
      int ret=bkvoice_turn_poll(&r->ptt->turn);
      if(ret || r->ptt->turn.state==BKVOICE_TURN_IDLE)
        {
          if(!ret && !r->ptt->turn.last_error)
            {
              ret=bkcloud_history_commit(&r->history,r->input,r->reply);
#ifdef BKCLOUD_MEMORY_RUNTIME
              if (!ret && r->memory_enabled)
                {
                  int memory_ret = memory_launch(r, MEMORY_SAVE);
                  if (memory_ret) syslog(LOG_WARNING, "BKVOICE CLOUD memory_launch=%d\n", memory_ret);
                }
#endif
            }
          syslog(LOG_NOTICE,"BKVOICE CLOUD playback_complete=%d\n",ret==0 && !r->ptt->turn.last_error);
          wipe_text(r);
        }
      if(level && r->armed) {bkvoice_ptt_cancel(r->ptt,-ECANCELED);wipe_text(r);r->armed=false;}
      if(!level) r->armed=true;
      return;
    }
  if(!level)
    {
      if(r->pressed)
        {
          int ret=bkvoice_ptt_up(r->ptt,bkvoice_config_now_ms(NULL));
          if(r->ptt->worker_joinable) return;
          r->pressed=false;
          if(!ret && r->pcm_size) ret=launch(r,false);
          else if(!ret) ret=-ENODATA;
          if(ret) {bkvoice_ptt_cancel(r->ptt,ret);wipe_audio(r);}
        }
      r->armed=true;
    }
  else if(r->armed && !r->pressed)
    {
      r->armed=false;
      if(r->ptt->turn.state!=BKVOICE_TURN_IDLE || r->ptt->worker_joinable) return;
      r->pcm=malloc(BKCLOUD_PCM_MAX);
      struct bkvoice_turn_token_s token;
      int ret=r->pcm ? bkvoice_ptt_down(r->ptt,bkvoice_config_now_ms(NULL),&token) : -ENOMEM;
      if(!ret || r->ptt->worker_joinable) r->pressed=true;else wipe_audio(r);
    }
  if(r->pressed)
    {
      int ret=bkvoice_ptt_timeout(r->ptt,bkvoice_config_now_ms(NULL));
      if(ret != -EAGAIN && !r->ptt->worker_joinable)
        {wipe_audio(r);r->pressed=false;r->armed=false;}
    }
}
