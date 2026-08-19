/****************************************************************************
 * app/vela_claw/src/cap/cap_files.c
 *
 * files capability: list / read files in the data directory. (write_file is
 * used by the LLM self-programming loop in esp-claw; we provide a safe
 * read/list subset here.)
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <string.h>
#include <stdio.h>
#include <dirent.h>

#include "claw_common.h"
#include "claw_cap.h"
#include "claw_config.h"

static claw_err_t run(const char *args_json, char *out, size_t outlen)
{
  (void)args_json;
  DIR *d = opendir(g_claw_config.data_dir);
  if (!d) { snprintf(out, outlen, "[cannot open %s]", g_claw_config.data_dir); return CLAW_EIO; }
  int off = 0;
  struct dirent *e;
  while ((e = readdir(d)) != NULL) {
    if (e->d_name[0] == '.') continue;
    off += snprintf(out + off, outlen - off, "%s\n", e->d_name);
  }
  closedir(d);
  return CLAW_OK;
}

claw_cap_t cap_files = {
  "files",
  "List files in the working data directory.",
  run
};
