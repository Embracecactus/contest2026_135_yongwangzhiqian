/****************************************************************************
 * app/vela_claw/include/claw_json.h
 *
 * Minimal, dependency-free JSON parser/serializer sufficient for the LLM
 * request/response protocol, router_rules.json and the config file. Keeps
 * Vela-Claw self-contained and host-compilable (no cJSON needed for tests).
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef VELA_CLAW_JSON_H
#define VELA_CLAW_JSON_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  CLAW_JSON_NULL,
  CLAW_JSON_BOOL,
  CLAW_JSON_NUM,
  CLAW_JSON_STR,
  CLAW_JSON_OBJ,
  CLAW_JSON_ARR
} claw_json_type_t;

typedef struct claw_json_s claw_json_t;

struct claw_json_s {
  claw_json_type_t type;
  union {
    bool        b;
    double      num;
    char       *str;
    struct {
      claw_json_t **items;   /* array of members/values */
      size_t      count;
    } arr;
    struct {
      char       **keys;
      claw_json_t **vals;
      size_t      count;
    } obj;
  } u;
};

/* Parse a NUL-terminated JSON string. Returns NULL on error. */
claw_json_t *claw_json_parse(const char *text);

/* Free a tree. */
void claw_json_free(claw_json_t *node);

/* Path lookup, e.g. "choices/0/message/content". Returns NULL if missing. */
const claw_json_t *claw_json_get(const claw_json_t *root, const char *path);

/* Typed accessors (NULL-safe). */
const char *claw_json_str(const claw_json_t *node);
double      claw_json_num(const claw_json_t *node);
bool        claw_json_bool(const claw_json_t *node);

/* Build a JSON string for a fixed-shape chat-completions request.
 * tools_json is an OPTIONAL NUL-terminated string containing the inner JSON
 * of the "tools" array (e.g. one or more
 * {"type":"function","function":{...}} objects, no surrounding brackets).
 * Pass NULL (or "") to omit the tools array. */
char *claw_json_build_chat_request(const char *model, const char *system,
                                   const char *user, const char *tools_json,
                                   size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif /* VELA_CLAW_JSON_H */
