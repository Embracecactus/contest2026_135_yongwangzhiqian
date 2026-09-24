/* SPDX-License-Identifier: Apache-2.0 */
static int bkdisplay_focus_locked(struct bkdisplay_service_s *service, unsigned visual)
{
  if (!visual)
    {
      int ret;
      if (service->status.state != BKDISPLAY_SERVICE_READY || !service->frames[0])
        ret = bkdisplay_builtin_locked(service, true);
      else
        {
          ret = bkdisplay_framebuffer_write(BKDISPLAY_FB0, service->frames[0]);
          if (!ret) ret = bkdisplay_framebuffer_write(BKDISPLAY_FB1, service->frames[0]);
        }
      return ret;
    }
  uint16_t *pixels = calloc(BKDISPLAY_CANVAS_PIXELS, sizeof(*pixels));
  if (!pixels) return -ENOMEM;
  for (int y=0;y<BKDISPLAY_CANVAS_HEIGHT;y++)
    for (int x=0;x<BKDISPLAY_CANVAS_WIDTH;x++)
      pixels[y*BKDISPLAY_CANVAS_WIDTH+x] = bkfocus_pixel(visual>>8, visual&255,
        x-BKDISPLAY_CANVAS_WIDTH/2, y-BKDISPLAY_CANVAS_HEIGHT/2);
  int ret = bkdisplay_framebuffer_write(BKDISPLAY_FB0, pixels);
  if (!ret) ret = bkdisplay_framebuffer_write(BKDISPLAY_FB1, pixels);
  free(pixels);
  return ret;
}

static int bkdisplay_focus_present_locked(struct bkdisplay_service_s *service, unsigned visual)
{
  if (visual == service->focus_painted) return 0;
  int ret = bkdisplay_focus_locked(service, visual);
  if (!ret)
    {
      service->focus_painted = visual;
      service->speaking_painted = false;
      service->status.render_sequence++;
    }
  else service->status.last_error = ret;
  return ret;
}
