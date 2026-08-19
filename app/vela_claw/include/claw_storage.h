/****************************************************************************
 * app/vela_claw/include/claw_storage.h
 *
 * File-backed key/value store (single JSON document in a store directory).
 * Replaces ESP-IDF NVS; the app_config schema on top is unchanged.
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef VELA_CLAW_STORAGE_H
#define VELA_CLAW_STORAGE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Open a KV store backed by <dir>/vela_claw.kv (created if absent). */
claw_err_t claw_kv_open(const char *dir);
claw_err_t claw_kv_close(void);

/* Set/commit. value may be NULL to delete. */
claw_err_t claw_kv_set(const char *key, const char *value);
claw_err_t claw_kv_commit(void);

/* Returns a heap-allocated string (caller frees) or NULL if absent. */
char *claw_kv_get(const char *key);

#ifdef __cplusplus
}
#endif

#endif /* VELA_CLAW_STORAGE_H */
