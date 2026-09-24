/* SPDX-License-Identifier: Apache-2.0 */
/* Protected by the existing display service mutex. Animation frames do not
 * acquire a new identity; explicit render attempts do, including failures. */
static uint64_t g_bkdisplay_expression_identity;
static int bkdisplay_identity_advance(void)
{
  if (g_bkdisplay_expression_identity == UINT64_MAX) return -EOVERFLOW;
  g_bkdisplay_expression_identity++;
  return 0;
}
static int bkdisplay_render_locked(struct bkdisplay_service_s *service,
                                   const char *expression)
{
  int ret = bkdisplay_identity_advance();
  return ret < 0 ? ret : bkdisplay_render_pixels_locked(service, expression);
}
