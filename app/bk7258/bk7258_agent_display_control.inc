/* SPDX-License-Identifier: Apache-2.0 */
/* The callback/context are used only during this tool invocation, never kept
 * by the asynchronous renderer. A cancellation crossing enqueue is checked
 * again and can withdraw only this exact, still-pending request.
 */
static int product_expression_request(const char *expression, uint32_t *id,
                                     int (*check)(void *), void *context)
{
  int ret = check ? check(context) : 0;
  if (ret) return ret;
  ret = bk7258_display_request_expression(expression, id);
  if (ret) return ret;
  ret = check ? check(context) : 0;
  if (ret) (void)bk7258_display_cancel_expression(*id);
  return ret;
}

static int product_cancel(void)
{
  struct bkdisplay_expression_request_s pending = {0};
  (void)bk7258_display_expression_status(&pending);
  int ret = voice_channel_cancel();
  int display_ret = bk7258_display_cancel_expression(pending.id);
  /* Voice cancellation is an accepted request, not proof of renderer exit.
   * Do not report an idle voice error when a queued expression was canceled.
   */
  return ret == -ENOENT && display_ret == 0 ? 0 : ret;
}
