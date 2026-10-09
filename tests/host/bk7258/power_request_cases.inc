/* SPDX-License-Identifier: Apache-2.0 */
/* Public power request and exact production worker apply/pixels; held render
 * lock is an external peer. No timing threshold or fake successful state. */
static void power_request_case(struct bkdisplay_service_s *service,
                                const char *mode)
{
  unsigned io = mounts, painted = frames, stored = writes;
  uint32_t id = 0;
  struct bkdisplay_selection_status_s status;
  struct bkdisplay_catalog_page_s page;
  assert(bk7258_display_power(4) == -EINVAL);
  if (!strcmp(mode, "power-request-catalog-running"))
    {
      assert(bk7258_display_catalog_request(NULL, 0, &id) == 0);
      power_on_read = true;
      assert(bkdisplay_selection_step(service, true));
      assert(bk7258_display_selection_status(&status) == 0);
      assert(status.state == BKDISPLAY_SELECTION_CANCELED);
      assert(bk7258_display_catalog_page(id, &page) == -ECANCELED);
      assert(mounts == io + 1 && mounts == unmounts);
      io = mounts;
    }
  else
    {
      if (!strcmp(mode, "power-request-clear") ||
          !strcmp(mode, "power-request-coalesce"))
        assert(bk7258_display_catalog_request(NULL, 0, &id) == 0);
      assert(bk7258_display_power(2) == 0);
    }
  assert(power_lock_calls == 0 && frames == painted &&
         mounts == io && writes == stored);
  /* A stale worker readiness observation cannot reopen the gate. */
  bkdisplay_intent_gate(true);
  assert(bk7258_display_request_expression("happy", &selection_id) == -EBUSY);
  assert(bk7258_display_selection_refresh(id, &selection_id) == -EBUSY);
  assert(bkdisplay_volume_open(service) == -EBUSY && mounts == io);
  if (!strcmp(mode, "power-request-clear"))
    {
      assert(bk7258_display_power(0) == 0);
      bkdisplay_intent_gate(true);
      assert(bk7258_display_request_expression("happy", &selection_id) == -EBUSY);
      assert(bkdisplay_volume_open(service) == -EBUSY && mounts == io);
      bkdisplay_power_apply_locked(service);
      bkdisplay_intent_gate(true);
      assert(bk7258_display_selection_status(&status) == 0);
      assert(status.state == BKDISPLAY_SELECTION_CANCELED && status.id == id);
      assert(!bkdisplay_selection_step(service, true));
      assert(bk7258_display_catalog_request(NULL, id, &selection_id) == 0);
      assert(bk7258_display_selection_cancel(selection_id) == 0);
      assert(frames == painted && mounts == io && writes == stored);
      return;
    }
  unsigned phase = 2;
  if (!strcmp(mode, "power-request-coalesce"))
    {
      assert(bk7258_display_power(1) == 0);
      assert(bk7258_display_power(3) == 0);
      assert(bk7258_display_power(7) == -EINVAL);
      phase = 3;
    }
  bkdisplay_power_apply_locked(service);
  expect_power_phase = phase;
  assert(bkdisplay_builtin_locked(service, false) == 0);
  expect_power_phase = 0;
  assert(frames == painted + 2 && writes == stored && mounts == io);
  service->overlay_dirty = false; /* External successful-present scheduler. */
  assert(bk7258_display_power(phase) == 0);
  bkdisplay_power_apply_locked(service);
  assert(!service->overlay_dirty && frames == painted + 2);
  assert(power_lock_calls == 0);
}
