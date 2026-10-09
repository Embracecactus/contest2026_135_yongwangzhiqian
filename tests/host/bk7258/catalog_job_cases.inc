/* SPDX-License-Identifier: Apache-2.0 */
/* Public metadata/page APIs and the real worker/store; only mount/FB are peers. */
static void catalog_job_case(struct bkdisplay_service_s *service,
                              const char *mode)
{
  struct bkdisplay_selection_status_s status;
  struct bkdisplay_catalog_page_s page;
  unsigned io = mounts, stored = writes, painted = frames;
  uint32_t id = 0, next = 0;
  assert(bk7258_display_catalog_request("../bad.bkep", 0, &id) == -EINVAL);
  assert(bk7258_display_catalog_request(NULL, 1, &id) == -ESTALE);
  assert(bk7258_display_catalog_page(0, &page) == -EINVAL);
  if (!strcmp(mode, "catalog-job-conflict"))
    {
      assert(bk7258_display_selection_refresh(0, &id) == 0);
      assert(bk7258_display_catalog_request(NULL, id, &next) == -EBUSY);
      assert(bk7258_display_selection_cancel(id) == 0);
    }
  assert(bk7258_display_catalog_request(NULL, id, &selection_id) == 0);
  for (unsigned i = 0; i < 100; i++)
    {
      assert(bk7258_display_selection_status(&status) == 0);
      assert(status.catalog && status.id == selection_id);
      assert(status.state == BKDISPLAY_SELECTION_PENDING);
      memset(&page, 0xa5, sizeof(page));
      assert(bk7258_display_catalog_page(selection_id, &page) == -EAGAIN);
      assert(page.count == 0 && !page.more);
    }
  assert(mounts == io && writes == stored && frames == painted);
  assert(bk7258_display_selection_refresh(selection_id, &next) == -EBUSY);
  assert(bk7258_display_request_expression("happy", &next) == -EBUSY);
  assert(bk7258_display_catalog_request(NULL, selection_id, &next) == -EBUSY);
  if (!strcmp(mode, "catalog-job-queued-cancel") ||
      !strcmp(mode, "catalog-job-gate"))
    {
      if (!strcmp(mode, "catalog-job-gate")) bkdisplay_intent_gate(false);
      else assert(bk7258_display_selection_cancel(selection_id) == 0);
      assert(!bkdisplay_selection_step(service, true));
      assert(bk7258_display_selection_status(&status) == 0);
      assert(status.state == BKDISPLAY_SELECTION_CANCELED && mounts == io);
      assert(bk7258_display_catalog_page(selection_id, &page) == -ECANCELED);
      bkdisplay_intent_gate(true);
      return;
    }
  if (!strcmp(mode, "catalog-job-mount-cancel")) cancel_on_mount = true;
  if (!strcmp(mode, "catalog-job-scan-cancel")) catalog_cancel_on_read = true;
  if (!strcmp(mode, "catalog-job-release")) fail_unmount = true;
  if (!strcmp(mode, "catalog-job-corrupt"))
    {
      char path[512];
      snprintf(path, sizeof(path), "%s/shaniu/display/packs/shaniu-default-v1.bkep", root);
      int fd = open(path, O_WRONLY | O_TRUNC);
      assert(fd >= 0 && close(fd) == 0);
    }
  /* A read-only catalog does not require framebuffer readiness. */
  assert(bkdisplay_selection_step(service, false));
  assert(bk7258_display_selection_status(&status) == 0);
  assert(status.catalog && !status.save_confirmed && !status.render_confirmed &&
         !status.version_known && frames == painted && writes == stored);
  if (!strcmp(mode, "catalog-job-mount-cancel") ||
      !strcmp(mode, "catalog-job-scan-cancel"))
    {
      assert(status.state == BKDISPLAY_SELECTION_CANCELED);
      assert(bk7258_display_catalog_page(selection_id, &page) == -ECANCELED);
      assert(bk7258_display_selection_cancel(selection_id) == 0);
    }
  else if (!strcmp(mode, "catalog-job-release"))
    {
      assert(status.state == BKDISPLAY_SELECTION_UNKNOWN && status.release_error == -EIO);
      assert(bk7258_display_catalog_page(selection_id, &page) == -EIO && !page.count);
      assert(bk7258_display_catalog_request(NULL, selection_id, &next) == -EBUSY);
      assert(bk7258_display_selection_refresh(selection_id, &next) == -EBUSY);
      assert(bk7258_display_request_expression("happy", &next) == -EBUSY);
      unsigned before = mounts;
      assert(bk7258_display_selection_recover(selection_id) == 0);
      bkdisplay_intent_gate(false);
      assert(bkdisplay_selection_recover_step(service));
      assert(bk7258_display_selection_status(&status) == 0);
      assert(status.state == BKDISPLAY_SELECTION_UNKNOWN && status.release_error == 0);
      assert(bk7258_display_catalog_page(selection_id, &page) == -EIO && !page.count);
      assert(mounts == before && writes == stored && frames == painted);
      bkdisplay_intent_gate(true);
    }
  else if (!strcmp(mode, "catalog-job-corrupt"))
    {
      assert(status.state == BKDISPLAY_SELECTION_FAILED && status.error < 0);
      assert(bk7258_display_catalog_page(selection_id, &page) == status.error && !page.count);
    }
  else
    {
      assert(status.state == BKDISPLAY_SELECTION_DONE);
      assert(bk7258_display_catalog_page(selection_id, &page) == 0);
      assert(page.count == 2 && !page.more);
      assert(!strcmp(page.entries[0].filename, "shaniu-default-v1.bkep"));
      assert(!strcmp(page.entries[1].filename, "shaniu-upload-v1.bkep"));
      assert(bk7258_display_selection_cancel(selection_id) == -EALREADY);
      page.count = 0;
      assert(bk7258_display_catalog_page(selection_id, &page) == 0 && page.count == 2);
      assert(bk7258_display_catalog_request("shaniu-default-v1.bkep", selection_id, &next) == 0);
      memset(&page, 0xa5, sizeof(page));
      assert(bk7258_display_catalog_page(selection_id, &page) == -ESTALE && !page.count);
      assert(bkdisplay_selection_step(service, true));
      assert(bk7258_display_catalog_page(next, &page) == 0 && page.count == 1);
      assert(!strcmp(page.entries[0].filename, "shaniu-upload-v1.bkep"));
    }
  assert(!bkdisplay_selection_step(service, true));
  assert(frames == painted && writes == stored);
}
