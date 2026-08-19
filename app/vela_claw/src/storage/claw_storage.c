/****************************************************************************
 * app/vela_claw/src/storage/claw_storage.c
 *
 * File-backed KV store: a single JSON document <dir>/vela_claw.kv. Replaces
 * ESP-IDF NVS without changing the config schema layered on top.
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "claw_common.h"
#include "claw_storage.h"
#include "claw_json.h"

#define KV_FILENAME "vela_claw.kv"

typedef struct {
  claw_json_t *root;     /* object node */
  char         path[512];
} kv_store_t;

static kv_store_t g_kv;

static claw_json_t *ensure_root(void) {
  if (!g_kv.root) {
    g_kv.root = claw_json_parse("{}");
    if (!g_kv.root) return NULL;
  }
  return g_kv.root;
}

claw_err_t claw_kv_open(const char *dir) {
  snprintf(g_kv.path, sizeof(g_kv.path), "%s/%s", dir, KV_FILENAME);
  FILE *f = fopen(g_kv.path, "rb");
  if (f) {
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc(sz + 1);
    if (buf) {
      size_t rd = fread(buf, 1, sz, f);
      buf[rd] = '\0';
      g_kv.root = claw_json_parse(buf);
      free(buf);
    }
    fclose(f);
  }
  if (!g_kv.root) g_kv.root = claw_json_parse("{}");
  if (!g_kv.root) return CLAW_ENOMEM;
  return CLAW_OK;
}

claw_err_t claw_kv_close(void) {
  claw_json_free(g_kv.root);
  g_kv.root = NULL;
  return CLAW_OK;
}

claw_err_t claw_kv_set(const char *key, const char *value) {
  claw_json_t *root = ensure_root();
  if (!root) return CLAW_ENOMEM;
  /* delete existing */
  size_t i;
  for (i = 0; i < root->u.obj.count; i++) {
    if (strcmp(root->u.obj.keys[i], key) == 0) {
      claw_json_free(root->u.obj.vals[i]);
      free(root->u.obj.keys[i]);
      memmove(&root->u.obj.keys[i], &root->u.obj.keys[i + 1],
              (root->u.obj.count - i - 1) * sizeof(char *));
      memmove(&root->u.obj.vals[i], &root->u.obj.vals[i + 1],
              (root->u.obj.count - i - 1) * sizeof(claw_json_t *));
      root->u.obj.count--;
      break;
    }
  }
  if (!value) return CLAW_OK; /* deleted */
  root->u.obj.keys = realloc(root->u.obj.keys,
                             (root->u.obj.count + 1) * sizeof(char *));
  root->u.obj.vals = realloc(root->u.obj.vals,
                             (root->u.obj.count + 1) * sizeof(claw_json_t *));
  claw_json_t *v = malloc(sizeof(claw_json_t));
  v->type = CLAW_JSON_STR;
  v->u.str = strdup(value);
  root->u.obj.keys[root->u.obj.count] = strdup(key);
  root->u.obj.vals[root->u.obj.count] = v;
  root->u.obj.count++;
  return CLAW_OK;
}

claw_err_t claw_kv_commit(void) {
  if (!g_kv.root) return CLAW_OK;
  FILE *f = fopen(g_kv.path, "wb");
  if (!f) return CLAW_EIO;
  /* simple pretty-ish dump */
  fprintf(f, "{\n");
  size_t i;
  for (i = 0; i < g_kv.root->u.obj.count; i++) {
    const char *v = claw_json_str(g_kv.root->u.obj.vals[i]);
    fprintf(f, "  \"%s\": \"%s\"%s\n", g_kv.root->u.obj.keys[i],
            v ? v : "",
            (i + 1 < g_kv.root->u.obj.count) ? "," : "");
  }
  fprintf(f, "}\n");
  fclose(f);
  return CLAW_OK;
}

char *claw_kv_get(const char *key) {
  if (!g_kv.root) return NULL;
  size_t i;
  for (i = 0; i < g_kv.root->u.obj.count; i++)
    if (strcmp(g_kv.root->u.obj.keys[i], key) == 0) {
      const char *v = claw_json_str(g_kv.root->u.obj.vals[i]);
      return v ? strdup(v) : NULL;
    }
  return NULL;
}
