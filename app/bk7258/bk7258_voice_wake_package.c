/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L

#include "bk7258_voice_wake_package.h"
#include "bk7258_provision_store.h"
#include "bk7258_voice_kws_model.h"
#include "bk7258_voice_kws_frontend.h"
#include <nuttx/config.h>
#include <nuttx/mutex.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <mbedtls/sha256.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define WKA_DESC_V1 321u
#define WKA_SIZE_V1 (4u + 2u * WKA_DESC_V1)
#define WKA_DESC (WKA_DESC_V1 + 4u)
#define WKA_SIZE (4u + 2u * WKA_DESC)

static const char g_root[] = BKVOICE_WAKE_PACKAGE_ROOT;
static mutex_t g_model_store_lock = NXMUTEX_INITIALIZER;

/* WKA2 selection and WKM1/WKM2 assets live in the protected CP store.
 * Serialize only this asset transaction; mounting the unrelated SD/FAT
 * preferences volume would make model loading depend on removable storage. */
static int with_model_store(int (*operation)(void *), void *context)
{
  int ret = nxmutex_lock(&g_model_store_lock);
  if (ret < 0) return ret;
  ret = operation(context);
  nxmutex_unlock(&g_model_store_lock);
  return ret;
}

static int bounded(const char *s, size_t cap, size_t *n)
{
  *n = strnlen(s, cap);
  return *n == cap ? -EBADMSG : 0;
}

static uint32_t read_u32(const uint8_t *p)
{
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
         ((uint32_t)p[2] << 8) | p[3];
}

static void write_u32(uint8_t *p, uint32_t n)
{
  p[0] = n >> 24;
  p[1] = n >> 16;
  p[2] = n >> 8;
  p[3] = n;
}

const char *bkvoice_wake_package_frontend_id(uint32_t version)
{
  return version == 1 ? BKVOICE_KWS_FRONTEND_ID :
         version == 2 ? BKVOICE_KWS_FRONTEND_V2_ID : NULL;
}

size_t bkvoice_wake_package_header_size(uint32_t version)
{
  return version == 1 ? BKVOICE_WAKE_PACKAGE_HEADER :
         version == 2 ? BKVOICE_WAKE_PACKAGE_HEADER_V2 : 0;
}

static int utf8(const uint8_t *p, size_t n)
{
  for (size_t i = 0; i < n; )
    {
      uint32_t v;
      uint32_t minimum;
      unsigned more;
      uint8_t c = p[i++];

      if (c < 0x80)
        continue;
      if (c >= 0xc2 && c <= 0xdf)
        { v = c & 31; more = 1; minimum = 0x80; }
      else if (c >= 0xe0 && c <= 0xef)
        { v = c & 15; more = 2; minimum = 0x800; }
      else if (c >= 0xf0 && c <= 0xf4)
        { v = c & 7; more = 3; minimum = 0x10000; }
      else
        return -EBADMSG;
      if (i + more > n)
        return -EBADMSG;
      while (more--)
        {
          c = p[i++];
          if ((c & 0xc0) != 0x80)
            return -EBADMSG;
          v = (v << 6) | (c & 63);
        }
      if (v < minimum || (v >= 0xd800 && v <= 0xdfff) || v > 0x10ffff)
        return -EBADMSG;
    }
  return 0;
}

static int text(const uint8_t *p, size_t n, char *out, size_t cap,
                bool label)
{
  size_t len = 0;

  while (len < n && p[len])
    len++;
  if (!len || len >= n || len >= cap)
    return -EBADMSG;
  for (size_t i = 0; i < len; i++)
    if (label ? !((p[i] >= 'a' && p[i] <= 'z') ||
                  (p[i] >= '0' && p[i] <= '9') || p[i] == '_') : p[i] < 0x20)
      return -EBADMSG;
  for (size_t i = len + 1; i < n; i++)
    if (p[i])
      return -EBADMSG;
  if (!label && utf8(p, len))
    return -EBADMSG;
  memcpy(out, p, len);
  out[len] = 0;
  return 0;
}

static void hex(const uint8_t in[32], char out[65])
{
  static const char h[] = "0123456789abcdef";

  for (unsigned i = 0; i < 32; i++)
    {
      out[2 * i] = h[in[i] >> 4];
      out[2 * i + 1] = h[in[i] & 15];
    }
  out[64] = 0;
}

int bkvoice_wake_package_decode(const void *record, size_t size,
                                struct bkvoice_wake_package_s *s)
{
  const uint8_t *p = record;
  size_t n;
  size_t header;
  uint32_t frontend;

  if (!p || !s || size < BKVOICE_WAKE_PACKAGE_HEADER)
    return -EBADMSG;
  if (!memcmp(p, "WKM1", 4))
    {
      header = BKVOICE_WAKE_PACKAGE_HEADER;
      frontend = 1;
    }
  else if (!memcmp(p, "WKM2", 4) &&
           size >= BKVOICE_WAKE_PACKAGE_HEADER_V2)
    {
      header = BKVOICE_WAKE_PACKAGE_HEADER_V2;
      frontend = read_u32(p + BKVOICE_WAKE_PACKAGE_HEADER);
      if (!bkvoice_wake_package_frontend_id(frontend)) return -EBADMSG;
    }
  else return -EBADMSG;
  n = read_u32(p + 4);
  if (!n || n > BKVOICE_WAKE_PACKAGE_MAX_MODEL ||
      size != header + n)
    return -EBADMSG;
  memset(s, 0, sizeof(*s));
  if (text(p + 40, 32, s->label, sizeof(s->label), true) ||
      text(p + 72, 64, s->phrase, sizeof(s->phrase), false))
    return -EBADMSG;
  memcpy(s->sha256, p + 8, 32);
  s->model = p + header;
  s->model_size = n;
  s->frontend_version = frontend;
  return 0;
}

int bkvoice_wake_package_validate(const struct bkvoice_wake_package_s *s)
{
  uint8_t hash[32];
  float scores[3];
  float *zero;
  void *raw;
  void *arena;
  int ret;
  struct bkvoice_kws_model_s *model = NULL;
  struct bkvoice_kws_model_spec_s spec = {
    .data = s ? s->model : NULL,
    .bytes = s ? s->model_size : 0,
    .frontend = s ? bkvoice_wake_package_frontend_id(s->frontend_version) : NULL,
    .labels = {"silence", "unknown", s ? s->label : NULL}
  };

  if (!s || !s->model || !s->model_size || !spec.frontend ||
      s->model_size > BKVOICE_WAKE_PACKAGE_MAX_MODEL)
    return -EINVAL;
  if (mbedtls_sha256(s->model, s->model_size, hash, 0) ||
      memcmp(hash, s->sha256, 32))
    return -EKEYREJECTED;
  zero = calloc(BKVOICE_KWS_FEATURES, sizeof(*zero));
  raw = malloc(CONFIG_BK7258_VOICE_KWS_ARENA_BYTES + 15u);
  if (!zero || !raw)
    {
      free(zero);
      free(raw);
      return -ENOMEM;
    }
  arena = (void *)(((uintptr_t)raw + 15u) & ~(uintptr_t)15u);
  ret = bkvoice_kws_model_open(&spec, arena,
                               CONFIG_BK7258_VOICE_KWS_ARENA_BYTES, &model);
  if (!ret)
    ret = bkvoice_kws_model_is_streaming(model) ?
          bkvoice_kws_model_step(model, zero, scores) :
          bkvoice_kws_model_infer(model, zero, scores);
  if (!ret && (!isfinite(scores[0]) || !isfinite(scores[1]) ||
               !isfinite(scores[2])))
    ret = -EPROTO;
  if (model)
    bkvoice_kws_model_close(model);
  free(zero);
  free(raw);
  return ret;
}

static int verify(const char *path, const uint8_t sha[32], size_t expected)
{
  struct stat st;
  uint8_t buf[512];
  uint8_t hash[32];
  size_t got = 0;
  ssize_t n;
  int fd;
  int ret = 0;
  int hash_ret;
  mbedtls_sha256_context c;

  fd = open(path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
  if (fd < 0)
    return -errno;
  if (fstat(fd, &st) < 0 || !S_ISREG(st.st_mode) ||
      (size_t)st.st_size != expected)
    {
      ret = -EBADMSG;
      goto done;
    }
  mbedtls_sha256_init(&c);
  hash_ret = mbedtls_sha256_starts(&c, 0);
  if (hash_ret)
    {
      ret = hash_ret;
      goto hash_done;
    }
  while (got < expected)
    {
      n = read(fd, buf, sizeof(buf));
      if (n < 0 && errno == EINTR)
        continue;
      if (n <= 0)
        {
          ret = n < 0 ? -errno : -EIO;
          break;
        }
      got += (size_t)n;
      hash_ret = mbedtls_sha256_update(&c, buf, (size_t)n);
      if (hash_ret)
        {
          ret = hash_ret;
          break;
        }
    }
  if (!ret)
    {
      do
        n = read(fd, buf, 1);
      while (n < 0 && errno == EINTR);
      if (n != 0)
        ret = n < 0 ? -errno : -EBADMSG;
    }
  if (!ret)
    {
      hash_ret = mbedtls_sha256_finish(&c, hash);
      if (hash_ret)
        ret = hash_ret;
    }
  if (!ret && memcmp(hash, sha, 32))
    ret = -EKEYREJECTED;
hash_done:
  mbedtls_sha256_free(&c);
done:
  if (close(fd) < 0 && !ret)
    ret = -errno;
  return ret;
}

struct stage_s
{
  const struct bkvoice_wake_package_s *spec;
  struct bkvoice_wake_package_descriptor_s *desc;
  int ret;
};

static int stage_io(void *arg)
{
  struct stage_s *x = arg;
  const char *final = x->desc->model_path;
  char parent[80];
  char directory[100];
  char pending[120];
  size_t off = 0;
  int fd;
  ssize_t n;

  /* The pinned VFS rejects a NAME_MAX-length intermediate component before
   * it consumes the following slash. Keep each component strictly shorter
   * than the target's 32-character limit, retaining all 256 hash bits. */
  snprintf(parent, sizeof(parent), "%s/%.24s", g_root,
           x->desc->sha256_hex);
  snprintf(directory, sizeof(directory), "%s/%.24s/%.24s", g_root,
           x->desc->sha256_hex, x->desc->sha256_hex + 24);
  snprintf(pending, sizeof(pending), "%s/.pending", directory);
  if (mkdir(g_root, 0700) < 0 && errno != EEXIST)
    return x->ret = -errno;
  if (bkprov_store_check_filesystem(g_root))
    return x->ret = -ENODEV;
  if (mkdir(parent, 0700) < 0 && errno != EEXIST)
    return x->ret = -errno;
  if (mkdir(directory, 0700) < 0 && errno != EEXIST)
    return x->ret = -errno;
  if (verify(final, x->spec->sha256, x->spec->model_size) == 0)
    return 0;
  if (unlink(pending) < 0 && errno != ENOENT)
    return x->ret = -errno;
  fd = open(pending, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
  if (fd < 0)
    return x->ret = -errno;
  while (off < x->spec->model_size)
    {
      n = write(fd, x->spec->model + off, x->spec->model_size - off);
      if (n < 0 && errno == EINTR)
        continue;
      if (n <= 0)
        {
          x->ret = n < 0 ? -errno : -EIO;
          break;
        }
      off += (size_t)n;
    }
  if (!x->ret && fsync(fd) < 0)
    x->ret = -errno;
  if (close(fd) < 0 && !x->ret)
    x->ret = -errno;
  if (!x->ret && verify(pending, x->spec->sha256, x->spec->model_size))
    x->ret = -EKEYREJECTED;
  if (!x->ret && rename(pending, final) < 0)
    x->ret = -errno;
  if (!x->ret)
    x->ret = bkprov_store_sync_directory(directory);
  return x->ret;
}

int bkvoice_wake_package_stage(const struct bkvoice_wake_package_s *s,
                               struct bkvoice_wake_package_descriptor_s *d)
{
  struct stage_s x;
  int ret;

  if (!s || !d || !bkvoice_wake_package_frontend_id(s->frontend_version))
    return -EINVAL;
  memset(d, 0, sizeof(*d));
  hex(s->sha256, d->sha256_hex);
  snprintf(d->model_path, sizeof(d->model_path), "%s/%.24s/%.24s/%s", g_root,
           d->sha256_hex, d->sha256_hex + 24, d->sha256_hex + 48);
  strcpy(d->label, s->label);
  strcpy(d->phrase, s->phrase);
  d->frontend_version = s->frontend_version;
  x = (struct stage_s){s, d, 0};
  ret = with_model_store(stage_io, &x);
  return ret ? ret : x.ret;
}

static int descriptor_valid(const struct bkvoice_wake_package_descriptor_s *d,
                            bool allow_empty)
{
  size_t path_len;
  size_t hash_len;
  size_t label_len;
  size_t phrase_len;
  char expected[160];

  if (d == NULL)
    return -EINVAL;
  if (bounded(d->model_path, sizeof(d->model_path), &path_len) ||
      bounded(d->sha256_hex, sizeof(d->sha256_hex), &hash_len) ||
      bounded(d->label, sizeof(d->label), &label_len) ||
      bounded(d->phrase, sizeof(d->phrase), &phrase_len))
    return -EBADMSG;
  if (path_len == 0)
    return allow_empty && hash_len == 0 && label_len == 0 && phrase_len == 0 &&
           d->frontend_version == 0
             ? 0 : -EBADMSG;
  if (!bkvoice_wake_package_frontend_id(d->frontend_version) ||
      hash_len != 64 || label_len == 0 || phrase_len == 0 ||
      utf8((const uint8_t *)d->phrase, phrase_len))
    return -EBADMSG;
  for (size_t i = 0; i < hash_len; i++)
    if (strchr("0123456789abcdef", d->sha256_hex[i]) == NULL)
      return -EBADMSG;
  for (size_t i = 0; i < label_len; i++)
    if (!((d->label[i] >= 'a' && d->label[i] <= 'z') ||
          (d->label[i] >= '0' && d->label[i] <= '9') || d->label[i] == '_'))
      return -EBADMSG;
  if (!strcmp(d->model_path, "/etc/media/nihao_openvela.tflite"))
    return d->frontend_version == 1 ? 0 : -EBADMSG;
  snprintf(expected, sizeof(expected), "%s/%.24s/%.24s/%s", g_root,
           d->sha256_hex, d->sha256_hex + 24, d->sha256_hex + 48);
  return strcmp(expected, d->model_path) ? -EBADMSG : 0;
}

int bkvoice_wake_package_encode_header(void *record, size_t capacity,
  const struct bkvoice_wake_package_descriptor_s *d, size_t model_size)
{
  uint8_t *p = record;
  if (!p || descriptor_valid(d, false) || !model_size ||
      model_size > BKVOICE_WAKE_PACKAGE_MAX_MODEL) return -EINVAL;
  size_t header = bkvoice_wake_package_header_size(d->frontend_version);
  if (capacity < header) return -ENOSPC;
  memset(p, 0, header);
  memcpy(p, d->frontend_version == 1 ? "WKM1" : "WKM2", 4);
  write_u32(p + 4, model_size);
  for (size_t i = 0; i < 32; i++)
    {
      const char *digits = "0123456789abcdef";
      p[8 + i] = ((strchr(digits, d->sha256_hex[2 * i]) - digits) << 4) |
                 (strchr(digits, d->sha256_hex[2 * i + 1]) - digits);
    }
  memcpy(p + 40, d->label, sizeof(d->label));
  memcpy(p + 72, d->phrase, sizeof(d->phrase));
  if (d->frontend_version != 1)
    write_u32(p + BKVOICE_WAKE_PACKAGE_HEADER, d->frontend_version);
  return (int)header;
}

static void encode_desc(uint8_t *p,
                        const struct bkvoice_wake_package_descriptor_s *d)
{
  memcpy(p, d->model_path, 160);
  memcpy(p + 160, d->sha256_hex, 65);
  memcpy(p + 225, d->label, 32);
  memcpy(p + 257, d->phrase, 64);
  write_u32(p + WKA_DESC_V1, d->frontend_version);
}

static void decode_desc(const uint8_t *p,
                        struct bkvoice_wake_package_descriptor_s *d, bool legacy)
{
  memset(d, 0, sizeof(*d));
  memcpy(d->model_path, p, 160);
  memcpy(d->sha256_hex, p + 160, 65);
  memcpy(d->label, p + 225, 32);
  memcpy(d->phrase, p + 257, 64);
  d->frontend_version = legacy ? (d->model_path[0] ? 1 : 0) :
                        read_u32(p + WKA_DESC_V1);
}

struct load_s
{
  struct bkvoice_wake_package_descriptor_s *a;
  struct bkvoice_wake_package_descriptor_s *p;
  uint64_t *r;
  int ret;
};

static int load_io(void *v)
{
  struct load_s *x = v;
  uint8_t b[WKA_SIZE];
  size_t n;
  struct bkprov_store_s s;

  x->ret = bkprov_store_open(&s, g_root);
  if (x->ret)
    return x->ret;
  x->ret = bkprov_store_load(&s, b, sizeof(b), &n, x->r, NULL);
  if (x->ret)
    return x->ret;
  bool legacy = n == WKA_SIZE_V1 && !memcmp(b, "WKA1", 4);
  if (!legacy && (n != sizeof(b) || memcmp(b, "WKA2", 4)))
    return x->ret = -EBADMSG;
  decode_desc(b + 4, x->a, legacy);
  decode_desc(b + 4 + (legacy ? WKA_DESC_V1 : WKA_DESC), x->p, legacy);
  return x->ret = descriptor_valid(x->a, false) ||
                         descriptor_valid(x->p, true) ? -EBADMSG : 0;
}

int bkvoice_wake_package_load(struct bkvoice_wake_package_descriptor_s *a,
                              struct bkvoice_wake_package_descriptor_s *p,
                              uint64_t *r)
{
  struct load_s x = {a, p, r, 0};
  int e;

  if (!a || !p || !r)
    return -EINVAL;
  e = with_model_store(load_io, &x);
  return e ? e : x.ret;
}

struct commit_s
{
  const struct bkvoice_wake_package_descriptor_s *d;
  const struct bkvoice_wake_package_descriptor_s *o;
  uint64_t rev;
  int ret;
};

static int commit_io(void *v)
{
  struct commit_s *x = v;
  uint8_t b[WKA_SIZE] = {"WKA2"};
  uint8_t t[16] = {'W', 'K', 'A', '2'};
  struct bkprov_store_s s;
  uint64_t next = x->rev + 1;

  for (int i = 11; i >= 4; i--)
    {
      t[i] = (uint8_t)next;
      next >>= 8;
    }
  encode_desc(b + 4, x->d);
  encode_desc(b + 4 + WKA_DESC, x->o);
  x->ret = bkprov_store_open(&s, g_root);
  if (!x->ret)
    x->ret = bkprov_store_commit(&s, x->rev, t, b, sizeof(b));
  return x->ret;
}

int bkvoice_wake_package_commit(
  const struct bkvoice_wake_package_descriptor_s *d,
  const struct bkvoice_wake_package_descriptor_s *o, uint64_t r)
{
  struct commit_s x = {d, o, r, 0};
  int e;

  if (descriptor_valid(d, false) || descriptor_valid(o, true))
    return -EINVAL;
  e = with_model_store(commit_io, &x);
  return e ? e : x.ret;
}
