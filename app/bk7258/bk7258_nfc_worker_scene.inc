/* SPDX-License-Identifier: Apache-2.0 */
/* 与原读卡worker、request_lock、I/O退出握手共用所有权。 */
static void bknfc_scene_stop_locked(struct bknfc_server_s *s)
{
  s->scene_admitted = false;
  s->scene_epoch++;
  s->scene.armed = false;
  if (s->scene_intent) (void)bkfocus_intent_cancel(s->scene_intent);
}
void bk7258_nfc_scene_admit(bool admitted)
{
  struct bknfc_server_s *s = &g_bknfc_server;
  irqstate_t flags = spin_lock_irqsave(&s->request_lock);
  admitted = admitted && !s->quiescing &&
    __atomic_load_n(&s->initialized, __ATOMIC_ACQUIRE);
  if (!admitted && s->scene_admitted) bknfc_scene_stop_locked(s);
  else if (admitted && !s->scene_admitted)
    {
      s->scene_epoch++;
      s->scene_admitted = true;
    }
  spin_unlock_irqrestore(&s->request_lock, flags);
}
static void bknfc_scene_work(struct bknfc_server_s *s)
{
  irqstate_t flags = spin_lock_irqsave(&s->request_lock);
  if (!s->scene_admitted || s->quiescing || s->io_active || s->active ||
      bknfc_job_busy(s) || s->release_pending ||
      __atomic_load_n(&s->source.release_error, __ATOMIC_ACQUIRE))
    {
      spin_unlock_irqrestore(&s->request_lock, flags);
      return;
    }
  if (s->scene.sequence == UINT64_MAX)
    {
      bknfc_scene_stop_locked(s);
      s->scene_error = -EOVERFLOW;
      spin_unlock_irqrestore(&s->request_lock, flags);
      return;
    }
  uint32_t epoch = s->scene_epoch;
  s->io_active = true;
  spin_unlock_irqrestore(&s->request_lock, flags);
  int ret = 0;
  if (!s->bindings.ready)
    {
      if (s->scene_load_attempted) ret = -ENODEV;
      else
        {
          s->scene_load_attempted = true;
          ret = bknfc_job_open(s);
        }
    }
  if (!ret && s->bindings.uncertain) ret = -EINPROGRESS;
  /* 存储加载可能较慢；开始RF之前再检查本轮准入是否已经撤销。 */
  flags = spin_lock_irqsave(&s->request_lock);
  if (!s->scene_admitted || s->quiescing || s->scene_epoch != epoch)
    ret = -ECANCELED;
  spin_unlock_irqrestore(&s->request_lock, flags);
  struct mfrc522_observation_s sample = {0};
  if (!ret)
    {
      ret = bknfc_open(&s->source);
      if (!ret)
        {
          int observed = ioctl(s->source.fd, MFRC522IOC_OBSERVE,
                               (unsigned long)&sample);
          ret = observed < 0 ? bknfc_errno() : observed ? -EPROTO : 0;
          int closed = bknfc_close(&s->source);
          if (closed < 0) ret = closed;
        }
    }
  struct bknfc_card_s card = {0};
  unsigned int kind = BKNFC_SCENE_UNKNOWN;
  if (!ret && sample.present == 0) kind = BKNFC_SCENE_ABSENT;
  else if (!ret && sample.present == 1 && sample.uid.size <= sizeof(card.uid))
    {
      card.size = sample.uid.size;
      card.sak = sample.uid.sak;
      memcpy(card.uid, sample.uid.uid_data, card.size);
      if (bknfc_card_valid(&card)) kind = BKNFC_SCENE_PRESENT;
      else ret = -EPROTO;
    }
  else if (!ret) ret = -EPROTO;
  memset(&sample, 0, sizeof(sample));
  flags = spin_lock_irqsave(&s->request_lock);
  bknfc_job_publish_bindings(s);
  s->scene_error = ret;
  /* close失败或撤销后的采样不能发意图；也不重新打开旧任务。 */
  if (s->scene_admitted && !s->quiescing && s->scene_epoch == epoch)
    {
      uint32_t intent = 0;
      int submitted = bknfc_scene_observe(&s->scene, &s->bindings, 1,
                          s->scene.sequence + 1, kind,
                          kind == BKNFC_SCENE_PRESENT ? &card : NULL,
                          ret == 0, &intent);
      if (!ret) s->scene_error = submitted;
      if (intent) s->scene_intent = intent;
    }
  s->io_active = false;
  spin_unlock_irqrestore(&s->request_lock, flags);
  memset(&card, 0, sizeof(card));
}
