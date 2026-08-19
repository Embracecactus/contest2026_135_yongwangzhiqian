/****************************************************************************
 * app/vela_claw/src/memory/claw_memory.c
 *
 * File-backed memory: per-session history and long-term key/value. Mirrors
 * esp-claw's structured memory (session + long-term).
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>

#include "claw_common.h"
#include "claw_memory.h"

static char g_data_dir[257];

claw_err_t claw_memory_init(const char *data_dir) {
  strncpy(g_data_dir, data_dir ? data_dir : "/data/vela_claw",
          sizeof(g_data_dir) - 1);
  snprintf(g_data_dir, sizeof(g_data_dir), "%s", data_dir ? data_dir : "/data/vela_claw");
#ifdef __NuttX__
  mkdir(g_data_dir, 0777);
#else
  mkdir(g_data_dir, 0777);
#endif
  return CLAW_OK;
}

void claw_memory_deinit(void) {}

static void session_path(const char *session, char *out, size_t n) {
  snprintf(out, n, "%s/session_%s.log", g_data_dir, session);
}

claw_err_t claw_memory_append_session(const char *session,
                                      const char *role, const char *text)
{
  char p[320];
  session_path(session, p, sizeof(p));
  FILE *f = fopen(p, "ab");
  if (!f) return CLAW_EIO;
  fprintf(f, "%s: %s\n", role, text);
  fclose(f);
  return CLAW_OK;
}

claw_err_t claw_memory_get_session_history(const char *session,
                                           char *buf, size_t buflen)
{
  char p[320];
  session_path(session, p, sizeof(p));
  FILE *f = fopen(p, "rb");
  if (!f) { buf[0] = '\0'; return CLAW_OK; }
  size_t rd = fread(buf, 1, buflen - 1, f);
  buf[rd] = '\0';
  fclose(f);
  return CLAW_OK;
}

claw_err_t claw_memory_set_long_term(const char *key, const char *value) {
  char p[320];
  snprintf(p, sizeof(p), "%s/lt_%s.txt", g_data_dir, key);
  FILE *f = fopen(p, "wb");
  if (!f) return CLAW_EIO;
  fprintf(f, "%s", value ? value : "");
  fclose(f);
  return CLAW_OK;
}

claw_err_t claw_memory_get_long_term(const char *key,
                                     char *buf, size_t buflen)
{
  char p[320];
  snprintf(p, sizeof(p), "%s/lt_%s.txt", g_data_dir, key);
  FILE *f = fopen(p, "rb");
  if (!f) { buf[0] = '\0'; return CLAW_OK; }
  size_t rd = fread(buf, 1, buflen - 1, f);
  buf[rd] = '\0';
  fclose(f);
  return CLAW_OK;
}
