/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __APP_BK7258_BK7258_NFC_BINDINGS_H
#define __APP_BK7258_BK7258_NFC_BINDINGS_H
#include <stdbool.h>
#include "bk7258_nfc_protocol.h"
#include "bk7258_provision_store.h"
#ifdef CONFIG_BK7258_AP_CORE
#define BKNFC_BINDINGS_ROOT "/cpdata/shaniu/nfc-cards"
#else
#define BKNFC_BINDINGS_ROOT "/data/shaniu/nfc-cards"
#endif
#define BKNFC_BINDING_SLOTS 8u
#define BKNFC_BINDING_RECORD_SIZE 200u
struct bknfc_binding_s
{
  struct bknfc_card_s card;
  uint64_t duration_ms;
};
/* 单一文件工作者持有；初始化必须全零。读缓存无I/O，不授予身份权限。 */
struct bknfc_bindings_s
{
  struct bkprov_store_s store;
  struct bknfc_binding_s entries[BKNFC_BINDING_SLOTS];
  uint64_t revision;
  uint8_t transaction[16];
  bool ready;
  bool uncertain;
};
int bknfc_bindings_decode(struct bknfc_bindings_s *state,
                          const uint8_t *record, size_t size);
int bknfc_bindings_open(struct bknfc_bindings_s *state, const char *root);
/* 仅供已认证owner的worker调用。NULL卡且duration=0删除；不扫描或改owner。 */
int bknfc_bindings_set(struct bknfc_bindings_s *state, uint64_t expected,
                       uint64_t operation, unsigned int slot,
                       const struct bknfc_card_s *card, uint64_t duration);
int bknfc_bindings_lookup(const struct bknfc_bindings_s *state,
                          const struct bknfc_card_s *card, uint64_t *duration);
/* 仅授权重置工作者在NFC退出后调用；仅删除本组件两个文件，不递归。 */
int bknfc_bindings_reset(const char *root);
#endif
