/* SPDX-License-Identifier: Apache-2.0 */
/* 与原NFC worker共享锁和I/O所有权，不增加线程。 */
static bool bknfc_job_busy(struct bknfc_server_s *s)
{
  return s->job_status.phase >= BKNFC_JOB_PENDING &&
         s->job_status.phase <= BKNFC_JOB_COMMITTING;
}
static void bknfc_job_stop_locked(struct bknfc_server_s *s)
{
  if (s->job_status.phase == BKNFC_JOB_PENDING)
    {
      s->job_status.phase = BKNFC_JOB_CANCELED;
      s->job_status.error = -ECANCELED;
    }
  else if (s->job_status.phase == BKNFC_JOB_RUNNING) s->job_cancel = true;
}
static bool bknfc_job_same(const struct bknfc_job_request_s *a,
                           const struct bknfc_job_request_s *b)
{
  return a->operation == b->operation && a->revision == b->revision &&
         a->duration_ms == b->duration_ms && a->action == b->action &&
         a->slot == b->slot;
}
int bk7258_nfc_job_submit(const struct bknfc_job_request_s *request)
{
  struct bknfc_server_s *s = &g_bknfc_server;
  int ret = 0;
  if (!request || !request->operation || request->action < BKNFC_JOB_LOAD ||
      request->action > BKNFC_JOB_REMOVE || request->slot >= BKNFC_BINDING_SLOTS ||
      (request->action == BKNFC_JOB_LOAD &&
       (request->slot || request->revision || request->duration_ms)) ||
      (request->action == BKNFC_JOB_ENROLL ? !request->duration_ms : request->duration_ms))
    return -EINVAL;
  irqstate_t flags = spin_lock_irqsave(&s->request_lock);
  if (s->quiescing) ret = -ESHUTDOWN;
  else if (!__atomic_load_n(&s->initialized, __ATOMIC_ACQUIRE)) ret = -ENODEV;
  else if (request->operation == s->job.operation)
    ret = bknfc_job_same(request, &s->job) ? 0 : -EEXIST;
  else if (request->operation <= s->job_status.operation_floor) ret = -ESTALE;
  else if (bknfc_job_busy(s) || s->active || s->io_active || s->release_pending)
    ret = -EBUSY;
  else
    {
      s->job = *request;
      s->job_status.operation_floor = request->operation;
      s->job_cancel = false;
      s->job_status.operation = request->operation;
      s->job_status.phase = BKNFC_JOB_PENDING;
      s->job_status.error = 0;
      ret = nxsem_post(&s->request_sem);
      if (ret < 0)
        {
          s->job_status.phase = BKNFC_JOB_FAILED;
          s->job_status.error = ret;
        }
    }
  spin_unlock_irqrestore(&s->request_lock, flags);
  return ret;
}
int bk7258_nfc_job_cancel(uint64_t operation)
{
  struct bknfc_server_s *s = &g_bknfc_server;
  irqstate_t flags = spin_lock_irqsave(&s->request_lock);
  int ret = 0;
  if (!operation || operation != s->job.operation) ret = -ESTALE;
  else if (s->job_status.phase == BKNFC_JOB_PENDING ||
           s->job_status.phase == BKNFC_JOB_RUNNING) bknfc_job_stop_locked(s);
  else if (s->job_status.phase != BKNFC_JOB_CANCELED) ret = -EALREADY;
  spin_unlock_irqrestore(&s->request_lock, flags);
  return ret;
}
void bk7258_nfc_job_status(struct bknfc_job_status_s *status)
{
  if (!status) return;
  struct bknfc_server_s *s = &g_bknfc_server;
  irqstate_t flags = spin_lock_irqsave(&s->request_lock);
  *status = s->job_status;
  spin_unlock_irqrestore(&s->request_lock, flags);
}
static int bknfc_job_open(struct bknfc_server_s *s)
{
  if (s->bindings.uncertain) return -EINPROGRESS;
  if (s->bindings.ready) return 0;
  int ret = bknfc_bindings_open(&s->bindings, BKNFC_BINDINGS_ROOT);
  if (ret != -ENOENT) return ret;
  char parent[160];
  struct stat info;
  size_t size = strlen(BKNFC_BINDINGS_ROOT);
  if (size >= sizeof(parent)) return -EINVAL;
  memcpy(parent, BKNFC_BINDINGS_ROOT, size + 1);
  char *slash = strrchr(parent, '/');
  if (!slash || slash == parent) return -EINVAL;
  *slash = 0;
  if (lstat(parent, &info) < 0) return -errno;
  if (!S_ISDIR(info.st_mode)) return -ENOTDIR;
  ret = bkprov_store_check_filesystem(parent);
  if (ret < 0) return ret;
  if (mkdir(BKNFC_BINDINGS_ROOT, 0700) < 0 && errno != EEXIST) return -errno;
  ret = bkprov_store_sync_directory(parent);
  if (ret < 0) return ret;
  return bknfc_bindings_open(&s->bindings, BKNFC_BINDINGS_ROOT);
}
static bool bknfc_job_work(struct bknfc_server_s *s)
{
  irqstate_t flags = spin_lock_irqsave(&s->request_lock);
  if (s->quiescing || s->job_status.phase != BKNFC_JOB_PENDING)
    {
      spin_unlock_irqrestore(&s->request_lock, flags);
      return false;
    }
  struct bknfc_job_request_s job = s->job;
  s->job_status.phase = BKNFC_JOB_RUNNING;
  s->io_active = true;
  spin_unlock_irqrestore(&s->request_lock, flags);
  int ret = bknfc_job_open(s);
  struct bknfc_card_s card = {0};
  if (!ret && job.action != BKNFC_JOB_LOAD && job.revision != s->bindings.revision)
    ret = -ESTALE;
  if (!ret && job.action == BKNFC_JOB_ENROLL)
    {
      struct bknfc_rpc_request_s request = {
        .magic = BKNFC_RPC_MAGIC, .version = BKNFC_CARD_VERSION,
        .command = BKNFC_RPC_CARD, .session = 1, .sequence = 1
      };
      struct bknfc_rpc_response_s response;
      ret = bknfc_rpc_handle_request(&request, &response, &g_bknfc_ops, &s->source);
      if (!ret) ret = response.operation_status;
      if (!ret) card = response.card;
    }
  flags = spin_lock_irqsave(&s->request_lock);
  bool canceled = s->job_cancel || s->quiescing;
  if (canceled) ret = -ECANCELED;
  if (!ret && job.action != BKNFC_JOB_LOAD) s->job_status.phase = BKNFC_JOB_COMMITTING;
  spin_unlock_irqrestore(&s->request_lock, flags);
  if (!ret && job.action != BKNFC_JOB_LOAD)
    ret = bknfc_bindings_set(&s->bindings, job.revision, job.operation, job.slot,
                            job.action == BKNFC_JOB_ENROLL ? &card : NULL,
                            job.duration_ms);
  memset(&card, 0, sizeof(card));
  flags = spin_lock_irqsave(&s->request_lock);
  if (s->job_status.phase != BKNFC_JOB_COMMITTING &&
      (s->job_cancel || s->quiescing))
    {
      canceled = true;
      ret = -ECANCELED;
    }
  s->job_status.error = ret;
  s->job_status.phase = canceled ? BKNFC_JOB_CANCELED :
    ret == -EINPROGRESS ? BKNFC_JOB_UNKNOWN : ret ? BKNFC_JOB_FAILED : BKNFC_JOB_SUCCEEDED;
  s->job_status.revision = s->bindings.revision;
  uint64_t durable_operation = 0;
  for (unsigned int i = 8; i < 16; i++)
    durable_operation = (durable_operation << 8) | s->bindings.transaction[i];
  if (durable_operation > s->job_status.operation_floor)
    s->job_status.operation_floor = durable_operation;
  for (unsigned int i = 0; i < BKNFC_BINDING_SLOTS; i++)
    s->job_status.durations[i] = s->bindings.uncertain ? 0 : s->bindings.entries[i].duration_ms;
  s->io_active = false;
  spin_unlock_irqrestore(&s->request_lock, flags);
  return true;
}
int bk7258_nfc_bindings_reset(void)
{
  struct bknfc_server_s *s = &g_bknfc_server;
  irqstate_t flags = spin_lock_irqsave(&s->request_lock);
  if (!s->quiescing || s->io_active || s->release_pending || bknfc_job_busy(s) ||
      __atomic_load_n(&s->source.release_error, __ATOMIC_ACQUIRE))
    {
      spin_unlock_irqrestore(&s->request_lock, flags);
      return -EBUSY;
    }
  s->io_active = true;
  spin_unlock_irqrestore(&s->request_lock, flags);
  int ret = bknfc_bindings_reset(BKNFC_BINDINGS_ROOT);
  flags = spin_lock_irqsave(&s->request_lock);
  memset(&s->bindings, 0, sizeof(s->bindings));
  s->bindings.uncertain = ret != 0;
  s->job_status.revision = 0;
  memset(s->job_status.durations, 0, sizeof(s->job_status.durations));
  s->job_status.error = ret ? ret : -ECANCELED;
  s->job_status.phase = ret ? BKNFC_JOB_UNKNOWN : BKNFC_JOB_CANCELED;
  s->io_active = false;
  spin_unlock_irqrestore(&s->request_lock, flags);
  return ret;
}
