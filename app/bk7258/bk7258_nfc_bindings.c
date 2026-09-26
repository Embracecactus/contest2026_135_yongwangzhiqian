/* SPDX-License-Identifier: Apache-2.0 */
#include "bk7258_nfc_bindings.h"
#include "bk7258_nfc_core.h"
#include <errno.h>
#include <string.h>

static uint64_t get64(const uint8_t *p)
{
  uint64_t value = 0;
  for (unsigned int i = 0; i < 8; i++) value = (value << 8) | p[i];
  return value;
}
static void put64(uint8_t *p, uint64_t value)
{
  for (int i = 7; i >= 0; i--) { p[i] = value; value >>= 8; }
}
static bool same_card(const struct bknfc_card_s *a, const struct bknfc_card_s *b)
{
  return a->size == b->size && !memcmp(a->uid, b->uid, sizeof(a->uid));
}
static bool zero(const uint8_t *p, size_t size)
{
  for (size_t i = 0; i < size; i++) if (p[i]) return false;
  return true;
}
static void encode(const struct bknfc_binding_s *entries, uint8_t *record)
{
  memset(record, 0, BKNFC_BINDING_RECORD_SIZE);
  memcpy(record, "NCB1", 4);
  for (unsigned int i = 0; i < BKNFC_BINDING_SLOTS; i++)
    {
      uint8_t *p = record + 8 + i * 24;
      memcpy(p, &entries[i].card, 12);
      put64(p + 12, entries[i].duration_ms);
    }
}
int bknfc_bindings_decode(struct bknfc_bindings_s *state,
                          const uint8_t *record, size_t size)
{
  struct bknfc_binding_s entries[BKNFC_BINDING_SLOTS] = {0};
  if (!state || !record || size != BKNFC_BINDING_RECORD_SIZE ||
      memcmp(record, "NCB1", 4) || !zero(record + 4, 4)) return -EPROTO;
  for (unsigned int i = 0; i < BKNFC_BINDING_SLOTS; i++)
    {
      const uint8_t *p = record + 8 + i * 24;
      if (!zero(p + 20, 4)) return -EPROTO;
      memcpy(&entries[i].card, p, 12);
      entries[i].duration_ms = get64(p + 12);
      if (!entries[i].duration_ms)
        {
          if (!zero(p, 12)) return -EPROTO;
          continue;
        }
      if (!bknfc_card_valid(&entries[i].card)) return -EPROTO;
      for (unsigned int j = 0; j < i; j++)
        if (entries[j].duration_ms && same_card(&entries[i].card, &entries[j].card))
          return -EPROTO;
    }
  memcpy(state->entries, entries, sizeof(entries));
  return 0;
}
int bknfc_bindings_open(struct bknfc_bindings_s *state, const char *root)
{
  uint8_t record[BKNFC_BINDING_RECORD_SIZE];
  size_t size;
  int ret;
  if (!state) return -EINVAL;
  if (state->uncertain) return -EINPROGRESS;
  if (state->ready) return -EALREADY;
  ret = bkprov_store_open(&state->store, root);
  if (ret) return ret;
  ret = bkprov_store_load(&state->store, record, sizeof(record), &size,
                          &state->revision, state->transaction);
  if (ret == -ENOENT)
    {
      memset(state->entries, 0, sizeof(state->entries));
      state->revision = 0;
      memset(state->transaction, 0, sizeof(state->transaction));
      ret = 0;
    }
  else if (!ret)
    {
      const uint8_t *t = state->transaction;
      if (memcmp(t, "NCB1", 4) || (t[4] != 1 && t[4] != 2) ||
          t[5] >= BKNFC_BINDING_SLOTS || t[6] || t[7] || !get64(t + 8))
        ret = -EPROTO;
      else ret = bknfc_bindings_decode(state, record, size);
    }
  memset(record, 0, sizeof(record));
  if (!ret) state->ready = true;
  return ret;
}
int bknfc_bindings_set(struct bknfc_bindings_s *state, uint64_t expected,
                       uint64_t operation, unsigned int slot,
                       const struct bknfc_card_s *card, uint64_t duration)
{
  struct bknfc_binding_s candidate[BKNFC_BINDING_SLOTS];
  struct bknfc_card_s empty = {0};
  uint8_t record[BKNFC_BINDING_RECORD_SIZE];
  uint8_t transaction[16] = {'N', 'C', 'B', '1'};
  int ret;
  if (!state || !operation || slot >= BKNFC_BINDING_SLOTS ||
      (card && (!duration || !bknfc_card_valid(card))) || (!card && duration))
    return -EINVAL;
  if (state->uncertain) return -EINPROGRESS;
  if (!state->ready) return -ENODEV;
  transaction[4] = card ? 1 : 2;
  transaction[5] = slot;
  put64(transaction + 8, operation);
  if (get64(state->transaction + 8) == operation)
    {
      if (memcmp(transaction, state->transaction, 16) ||
          memcmp(&state->entries[slot].card, card ? card : &empty, 12) ||
          state->entries[slot].duration_ms != duration) return -EEXIST;
      return expected != UINT64_MAX && state->revision == expected + 1 ? 0 : -ESTALE;
    }
  if (expected != state->revision) return -ESTALE;
  if (expected == UINT64_MAX) return -EOVERFLOW;
  for (unsigned int i = 0; card && i < BKNFC_BINDING_SLOTS; i++)
    if (i != slot && state->entries[i].duration_ms &&
        same_card(card, &state->entries[i].card)) return -EEXIST;
  memcpy(candidate, state->entries, sizeof(candidate));
  candidate[slot].card = card ? *card : empty;
  candidate[slot].duration_ms = duration;
  encode(candidate, record);
  ret = bkprov_store_commit(&state->store, expected, transaction, record, sizeof(record));
  if (ret == -EINPROGRESS) state->uncertain = true;
  if (!ret)
    {
      memcpy(state->entries, candidate, sizeof(candidate));
      memcpy(state->transaction, transaction, sizeof(transaction));
      state->revision = expected + 1;
    }
  memset(record, 0, sizeof(record));
  memset(candidate, 0, sizeof(candidate));
  return ret;
}
int bknfc_bindings_lookup(const struct bknfc_bindings_s *state,
                          const struct bknfc_card_s *card, uint64_t *duration)
{
  if (duration) *duration = 0;
  if (!state || !duration || !bknfc_card_valid(card)) return -EINVAL;
  if (state->uncertain) return -EINPROGRESS;
  if (!state->ready) return -ENODEV;
  for (unsigned int i = 0; i < BKNFC_BINDING_SLOTS; i++)
    if (state->entries[i].duration_ms && same_card(card, &state->entries[i].card))
      {
        *duration = state->entries[i].duration_ms;
        return 0;
      }
  return -ENOENT;
}
