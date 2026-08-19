/****************************************************************************
 * app/vela_claw/src/util/claw_json.c
 *
 * Minimal recursive-descent JSON parser + a fixed-shape chat-request builder.
 * Intentionally small: covers objects/arrays/strings/numbers/bool/null and
 * path lookups of the form "a/0/b".
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#include "claw_common.h"
#include "claw_json.h"

typedef struct {
  const char *p;
} parser_t;

static void skip_ws(parser_t *ps) {
  while (*ps->p && isspace((unsigned char)*ps->p)) ps->p++;
}

static claw_json_t *parse_value(parser_t *ps);

static claw_json_t *new_node(claw_json_type_t t) {
  claw_json_t *n = calloc(1, sizeof(*n));
  if (n) n->type = t;
  return n;
}

static char *parse_string(parser_t *ps) {
  /* assumes leading '"' consumed by caller */
  size_t cap = 16, len = 0;
  char *buf = malloc(cap);
  if (!buf) return NULL;
  while (*ps->p && *ps->p != '"') {
    char c = *ps->p++;
    if (c == '\\') {
      c = *ps->p++;
      switch (c) {
        case 'n': c = '\n'; break;
        case 't': c = '\t'; break;
        case 'r': c = '\r'; break;
        case 'b': c = '\b'; break;
        case 'f': c = '\f'; break;
        case '/': c = '/'; break;
        case '\\': c = '\\'; break;
        case '"': c = '"'; break;
        case 'u': {
          /* skip 4 hex digits (best-effort, no utf decode) */
          ps->p += 4;
          c = '?';
          break;
        }
        default: break;
      }
    }
    if (len + 1 >= cap) {
      cap *= 2;
      char *nb = realloc(buf, cap);
      if (!nb) { free(buf); return NULL; }
      buf = nb;
    }
    buf[len++] = c;
  }
  if (*ps->p == '"') ps->p++;
  buf[len] = '\0';
  return buf;
}

static claw_json_t *parse_string_node(parser_t *ps) {
  claw_json_t *n = new_node(CLAW_JSON_STR);
  if (!n) return NULL;
  n->u.str = parse_string(ps);
  return n;
}

static claw_json_t *parse_array(parser_t *ps) {
  claw_json_t *n = new_node(CLAW_JSON_ARR);
  if (!n) return NULL;
  skip_ws(ps);
  if (*ps->p == ']') { ps->p++; return n; }
  while (1) {
    claw_json_t *v = parse_value(ps);
    if (!v) break;
    n->u.arr.items = realloc(n->u.arr.items,
                              (n->u.arr.count + 1) * sizeof(claw_json_t *));
    n->u.arr.items[n->u.arr.count++] = v;
    skip_ws(ps);
    if (*ps->p == ',') { ps->p++; continue; }
    if (*ps->p == ']') { ps->p++; break; }
    break;
  }
  return n;
}

static claw_json_t *parse_object(parser_t *ps) {
  claw_json_t *n = new_node(CLAW_JSON_OBJ);
  if (!n) return NULL;
  skip_ws(ps);
  if (*ps->p == '}') { ps->p++; return n; }
  while (1) {
    skip_ws(ps);
    if (*ps->p != '"') break;
    ps->p++;
    char *key = parse_string(ps);
    skip_ws(ps);
    if (*ps->p != ':') { free(key); break; }
    ps->p++;
    claw_json_t *v = parse_value(ps);
    if (!v) { free(key); break; }
    n->u.obj.keys = realloc(n->u.obj.keys,
                            (n->u.obj.count + 1) * sizeof(char *));
    n->u.obj.vals = realloc(n->u.obj.vals,
                            (n->u.obj.count + 1) * sizeof(claw_json_t *));
    n->u.obj.keys[n->u.obj.count] = key;
    n->u.obj.vals[n->u.obj.count] = v;
    n->u.obj.count++;
    skip_ws(ps);
    if (*ps->p == ',') { ps->p++; continue; }
    if (*ps->p == '}') { ps->p++; break; }
    break;
  }
  return n;
}

static claw_json_t *parse_literal(parser_t *ps, const char *lit, claw_json_type_t t) {
  size_t len = strlen(lit);
  if (strncmp(ps->p, lit, len) != 0) return NULL;
  ps->p += len;
  claw_json_t *n = new_node(t);
  if (t == CLAW_JSON_BOOL) n->u.b = (lit[0] == 't');
  return n;
}

static claw_json_t *parse_number(parser_t *ps) {
  char *end;
  double v = strtod(ps->p, &end);
  if (end == ps->p) return NULL;
  claw_json_t *n = new_node(CLAW_JSON_NUM);
  n->u.num = v;
  ps->p = end;
  return n;
}

static claw_json_t *parse_value(parser_t *ps) {
  skip_ws(ps);
  char c = *ps->p;
  if (c == '"') { ps->p++; return parse_string_node(ps); }
  if (c == '{') { ps->p++; return parse_object(ps); }
  if (c == '[') { ps->p++; return parse_array(ps); }
  if (c == 't') return parse_literal(ps, "true", CLAW_JSON_BOOL);
  if (c == 'f') return parse_literal(ps, "false", CLAW_JSON_BOOL);
  if (c == 'n') return parse_literal(ps, "null", CLAW_JSON_NULL);
  if (isdigit((unsigned char)c) || c == '-' || c == '+') return parse_number(ps);
  return NULL;
}

claw_json_t *claw_json_parse(const char *text) {
  if (!text) return NULL;
  parser_t ps = { text };
  claw_json_t *root = parse_value(&ps);
  return root;
}

void claw_json_free(claw_json_t *node) {
  if (!node) return;
  size_t i;
  switch (node->type) {
    case CLAW_JSON_STR: free(node->u.str); break;
    case CLAW_JSON_ARR:
      for (i = 0; i < node->u.arr.count; i++) claw_json_free(node->u.arr.items[i]);
      free(node->u.arr.items);
      break;
    case CLAW_JSON_OBJ:
      for (i = 0; i < node->u.obj.count; i++) {
        free(node->u.obj.keys[i]);
        claw_json_free(node->u.obj.vals[i]);
      }
      free(node->u.obj.keys);
      free(node->u.obj.vals);
      break;
    default: break;
  }
  free(node);
}

static const claw_json_t *obj_get(const claw_json_t *o, const char *key) {
  size_t i;
  for (i = 0; i < o->u.obj.count; i++)
    if (strcmp(o->u.obj.keys[i], key) == 0)
      return o->u.obj.vals[i];
  return NULL;
}

static const claw_json_t *arr_get(const claw_json_t *a, size_t idx) {
  if (idx < a->u.arr.count) return a->u.arr.items[idx];
  return NULL;
}

const claw_json_t *claw_json_get(const claw_json_t *root, const char *path) {
  if (!root || !path) return NULL;
  const claw_json_t *cur = root;
  const char *seg = path;
  while (1) {
    const char *slash = strchr(seg, '/');
    size_t seglen = slash ? (size_t)(slash - seg) : strlen(seg);
    if (cur->type == CLAW_JSON_OBJ) {
      char kbuf[128];
      if (seglen >= sizeof(kbuf)) return NULL;
      memcpy(kbuf, seg, seglen);
      kbuf[seglen] = '\0';
      cur = obj_get(cur, kbuf);
    } else if (cur->type == CLAW_JSON_ARR) {
      char kbuf[32];
      if (seglen >= sizeof(kbuf)) return NULL;
      memcpy(kbuf, seg, seglen);
      kbuf[seglen] = '\0';
      cur = arr_get(cur, (size_t)atoi(kbuf));
    } else {
      return NULL;
    }
    if (!cur || !slash) break;
    seg = slash + 1;
  }
  return cur;
}

const char *claw_json_str(const claw_json_t *node) {
  return (node && node->type == CLAW_JSON_STR) ? node->u.str : NULL;
}
double claw_json_num(const claw_json_t *node) {
  if (!node) return 0;
  if (node->type == CLAW_JSON_NUM) return node->u.num;
  if (node->type == CLAW_JSON_BOOL) return node->u.b ? 1 : 0;
  return 0;
}
bool claw_json_bool(const claw_json_t *node) {
  return (node && node->type == CLAW_JSON_BOOL) ? node->u.b : false;
}

/* ---- request builder (fixed chat-completions shape) ---- */

/* Escape a C string into a JSON string, appending into dst. Returns the new
 * write position. Does NOT mutate the source. */
static char *json_escape(char *p, const char *s)
{
  for (; s && *s; s++) {
    switch (*s) {
      case '"':  *p++ = '\\'; *p++ = '"';  break;
      case '\\': *p++ = '\\'; *p++ = '\\'; break;
      case '\n': *p++ = '\\'; *p++ = 'n';  break;
      case '\t': *p++ = '\\'; *p++ = 't';  break;
      case '\r': *p++ = '\\'; *p++ = 'r';  break;
      default:   *p++ = *s;                 break;
    }
  }
  return p;
}

char *claw_json_build_chat_request(const char *model, const char *system,
                                   const char *user, const char *tools_json,
                                   size_t *out_len)
{
  size_t cap = 512
             + (model ? strlen(model) : 0)
             + (system ? strlen(system) : 0)
             + (user ? strlen(user) : 0)
             + (tools_json ? strlen(tools_json) : 0)
             + 128;
  char *buf = malloc(cap);
  if (!buf) return NULL;

  char *p = buf;
  p += sprintf(p, "{\"model\":\"%s\",\"messages\":[", model ? model : "");

  if (system && *system) {
    p += sprintf(p, "{\"role\":\"system\",\"content\":\"");
    p = json_escape(p, system);
    p += sprintf(p, "\"},");
  }

  p += sprintf(p, "{\"role\":\"user\",\"content\":\"");
  p = json_escape(p, user);
  p += sprintf(p, "\"}");

  if (tools_json && *tools_json) {
    p += sprintf(p, "],\"tools\":[%s]", tools_json);
  } else {
    *p++ = ']';
  }

  p += sprintf(p, ",\"temperature\":0.7}");
  *p = '\0';

  if (out_len) *out_len = (size_t)(p - buf);
  return buf;
}
