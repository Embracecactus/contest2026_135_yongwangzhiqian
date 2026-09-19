/****************************************************************************
 * app/bk7258/bk7258_agent_keys.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * 只接收物理按键并积累产品动作；不调用语音、Media、存储或电源接口。
 ****************************************************************************/

#include <nuttx/config.h>
#include <errno.h>
#include <string.h>
#include <syslog.h>
#include <nuttx/mutex.h>
#include <nuttx/rpmsg/rpmsg.h>
#include <nuttx/spinlock.h>

#include "bk7258_product_keys.h"
#include "bk7258_voice_button.h"
#include "bk7258_voice_config.h"

static struct
{
  struct rpmsg_endpoint endpoint;
  mutex_t endpoint_lock;
  spinlock_t lock;
  struct bkvoice_product_keys_s policy;
  void (*notify)(void);
  uint64_t last_sample;
  uint32_t epoch;
  uint32_t sequence;
  int volume_steps;
  bool power_requested;
  bool connected;
} g_keys =
{
  .endpoint_lock = NXMUTEX_INITIALIZER,
  .lock = SP_UNLOCKED
};

static int keys_receive(struct rpmsg_endpoint *endpoint, void *data,
                        size_t size, uint32_t source, void *priv)
{
  struct bkvoice_button_event_s event;
  uint64_t now = bkvoice_config_now_ms(NULL);
  uint32_t action;
  bool power;
  bool was_armed;
  irqstate_t flags;

  (void)endpoint; (void)source; (void)priv;
  if (size != sizeof(event)) return -EMSGSIZE;
  memcpy(&event, data, sizeof(event));
  if (event.magic != BKVOICE_BUTTON_MAGIC ||
      event.version != BKVOICE_BUTTON_VERSION || !event.sequence ||
      (event.pressed & ~BKVOICE_PRODUCT_KEY_VALID) ||
      event.reserved[0] || event.reserved[1]) return -EPROTO;

  flags = spin_lock_irqsave(&g_keys.lock);
  if (!g_keys.connected || event.sequence <= g_keys.sequence)
    {
      spin_unlock_irqrestore(&g_keys.lock, flags);
      return -ESTALE;
    }
  /* 丢样、断链或时钟倒退不能拼出一次连续长按；先重新看到全松开。 */
  if ((g_keys.sequence && event.sequence != g_keys.sequence + 1u) ||
      now < g_keys.last_sample ||
      now - g_keys.last_sample > BKVOICE_BUTTON_LEASE_MS)
    bkvoice_product_keys_reset(&g_keys.policy, g_keys.epoch);
  g_keys.sequence = event.sequence;
  g_keys.last_sample = now;
  was_armed = g_keys.policy.armed;
  action = bkvoice_product_keys_step(&g_keys.policy, g_keys.epoch,
                                      event.pressed, now, &power);
  /* 组合按键不产生音量动作；积累的是按下边沿，不是每次心跳。 */
  if (event.pressed == BKVOICE_PRODUCT_KEY_VOLUME_DOWN &&
      (action & BKVOICE_PRODUCT_KEY_VOLUME_DOWN) && g_keys.volume_steps > -15)
    g_keys.volume_steps--;
  if (event.pressed == BKVOICE_PRODUCT_KEY_VOLUME_UP &&
      (action & BKVOICE_PRODUCT_KEY_VOLUME_UP) && g_keys.volume_steps < 15)
    g_keys.volume_steps++;
  g_keys.power_requested |= power;
  bool ready = !was_armed && g_keys.policy.armed;
  spin_unlock_irqrestore(&g_keys.lock, flags);
  if (ready) syslog(LOG_INFO, "BKKEYS input ready=1 mask=0\n");
  if (g_keys.notify) g_keys.notify();
  return 0;
}

static void keys_disconnect(void)
{
  irqstate_t flags;
  if (nxmutex_lock(&g_keys.endpoint_lock) < 0) return;
  flags = spin_lock_irqsave(&g_keys.lock);
  g_keys.connected = false;
  g_keys.sequence = 0;
  g_keys.last_sample = 0;
  g_keys.volume_steps = 0;
  g_keys.power_requested = false;
  bkvoice_product_keys_reset(&g_keys.policy, ++g_keys.epoch);
  spin_unlock_irqrestore(&g_keys.lock, flags);
  if (g_keys.endpoint.rdev) rpmsg_destroy_ept(&g_keys.endpoint);
  memset(&g_keys.endpoint, 0, sizeof(g_keys.endpoint));
  nxmutex_unlock(&g_keys.endpoint_lock);
}

static void keys_unbind(struct rpmsg_endpoint *endpoint)
{
  (void)endpoint;
  keys_disconnect();
}

static bool keys_match(struct rpmsg_device *device, void *priv,
                       const char *name, uint32_t destination)
{
  const char *cpu = rpmsg_get_cpuname(device);
  (void)priv; (void)destination;
  return cpu && !strcmp(cpu, "cp") && !strcmp(name, BKVOICE_BUTTON_ENDPOINT);
}

static void keys_bind(struct rpmsg_device *device, void *priv,
                      const char *name, uint32_t destination)
{
  irqstate_t flags;
  int ret = -EALREADY;
  (void)priv;
  if (nxmutex_lock(&g_keys.endpoint_lock) < 0) return;
  if (!g_keys.endpoint.rdev)
    {
      flags = spin_lock_irqsave(&g_keys.lock);
      g_keys.connected = true;
      bkvoice_product_keys_reset(&g_keys.policy, ++g_keys.epoch);
      spin_unlock_irqrestore(&g_keys.lock, flags);
      ret = rpmsg_create_ept(&g_keys.endpoint, device, name, RPMSG_ADDR_ANY,
                              destination, keys_receive, keys_unbind);
      if (ret < 0)
        {
          flags = spin_lock_irqsave(&g_keys.lock);
          g_keys.connected = false;
          spin_unlock_irqrestore(&g_keys.lock, flags);
        }
    }
  nxmutex_unlock(&g_keys.endpoint_lock);
  syslog(ret ? LOG_WARNING : LOG_INFO, "BKKEYS CP link result=%d\n", ret);
}

static void keys_device_destroy(struct rpmsg_device *device, void *priv)
{
  const char *cpu = rpmsg_get_cpuname(device);
  (void)priv;
  if (cpu && !strcmp(cpu, "cp")) keys_disconnect();
}

int bkvoice_keys_listen(void (*notify)(void))
{
  g_keys.notify = notify;
  return rpmsg_register_callback(NULL, NULL, keys_device_destroy,
                                  keys_match, keys_bind);
}

void bkvoice_keys_take(int *volume_steps, bool *power_requested)
{
  uint64_t now = bkvoice_config_now_ms(NULL);
  irqstate_t flags = spin_lock_irqsave(&g_keys.lock);
  if (!g_keys.connected || now < g_keys.last_sample ||
      now - g_keys.last_sample > BKVOICE_BUTTON_LEASE_MS)
    {
      g_keys.volume_steps = 0;
      g_keys.power_requested = false;
      bkvoice_product_keys_reset(&g_keys.policy, g_keys.epoch);
    }
  *volume_steps = g_keys.volume_steps;
  *power_requested = g_keys.power_requested;
  g_keys.volume_steps = 0;
  g_keys.power_requested = false;
  spin_unlock_irqrestore(&g_keys.lock, flags);
}
