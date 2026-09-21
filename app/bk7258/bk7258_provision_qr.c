/* SPDX-License-Identifier: Apache-2.0
 *
 * Compact claim code and its byte-mode QR version 1..6 encoder.
 *
 * The encoder is a direct implementation of ISO/IEC 18004 byte mode with error
 * correction level L: GF(256) Reed-Solomon arithmetic over 0x11d, the eight
 * data masks with the standard penalty rules, and BCH(15,5) format
 * information. Only the versions this product needs are implemented, and the
 * tables below are restricted to the level-L block structure of versions 1..6.
 */
#include "bk7258_provision_qr.h"

#include <errno.h>
#include <stdbool.h>
#include <string.h>

/****************************************************************************
 * Claim code payload
 ****************************************************************************/

static const char g_b64url[] =
  "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

static size_t b64url_encode(char *output, const uint8_t *data, size_t size)
{
  size_t in = 0;
  size_t out = 0;

  while (in + 3u <= size)
    {
      uint32_t value = ((uint32_t)data[in] << 16) |
                       ((uint32_t)data[in + 1u] << 8) | data[in + 2u];
      output[out++] = g_b64url[(value >> 18) & 0x3fu];
      output[out++] = g_b64url[(value >> 12) & 0x3fu];
      output[out++] = g_b64url[(value >> 6) & 0x3fu];
      output[out++] = g_b64url[value & 0x3fu];
      in += 3u;
    }

  if (size - in == 2u)
    {
      uint32_t value = ((uint32_t)data[in] << 16) |
                       ((uint32_t)data[in + 1u] << 8);
      output[out++] = g_b64url[(value >> 18) & 0x3fu];
      output[out++] = g_b64url[(value >> 12) & 0x3fu];
      output[out++] = g_b64url[(value >> 6) & 0x3fu];
    }
  else if (size - in == 1u)
    {
      uint32_t value = (uint32_t)data[in] << 16;
      output[out++] = g_b64url[(value >> 18) & 0x3fu];
      output[out++] = g_b64url[(value >> 12) & 0x3fu];
    }

  return out;
}

static int b64url_value(char character)
{
  if (character >= 'A' && character <= 'Z') return character - 'A';
  if (character >= 'a' && character <= 'z') return character - 'a' + 26;
  if (character >= '0' && character <= '9') return character - '0' + 52;
  if (character == '-') return 62;
  if (character == '_') return 63;
  return -1;
}

static int b64url_decode(const char *text, size_t size, uint8_t *output,
                         size_t expected)
{
  size_t in = 0;
  size_t out = 0;

  if (size == 0 || size % 4u == 1u) return -EBADMSG;

  while (in + 4u <= size)
    {
      int a = b64url_value(text[in]);
      int b = b64url_value(text[in + 1u]);
      int c = b64url_value(text[in + 2u]);
      int d = b64url_value(text[in + 3u]);
      uint32_t value;

      if (a < 0 || b < 0 || c < 0 || d < 0) return -EBADMSG;

      value = ((uint32_t)a << 18) | ((uint32_t)b << 12) |
              ((uint32_t)c << 6) | (uint32_t)d;
      if (out + 3u > expected) return -EBADMSG;

      output[out++] = (uint8_t)(value >> 16);
      output[out++] = (uint8_t)(value >> 8);
      output[out++] = (uint8_t)value;
      in += 4u;
    }

  if (size - in == 2u)
    {
      int a = b64url_value(text[in]);
      int b = b64url_value(text[in + 1u]);

      if (a < 0 || b < 0 || out + 1u > expected) return -EBADMSG;
      output[out++] = (uint8_t)((a << 2) | (b >> 4));
    }
  else if (size - in == 3u)
    {
      int a = b64url_value(text[in]);
      int b = b64url_value(text[in + 1u]);
      int c = b64url_value(text[in + 2u]);

      if (a < 0 || b < 0 || c < 0 || out + 2u > expected) return -EBADMSG;
      output[out++] = (uint8_t)((a << 2) | (b >> 4));
      output[out++] = (uint8_t)((b << 4) | (c >> 2));
    }
  else if (size - in != 0u)
    {
      return -EBADMSG;
    }

  return out == expected ? 0 : -EBADMSG;
}

int bkprov_qr_payload(char *output, size_t capacity, size_t *size,
                      const uint8_t locator[8], const uint8_t fingerprint[32],
                      const uint8_t secret[32])
{
  size_t used = 0;

  if (output == NULL || size == NULL || locator == NULL ||
      fingerprint == NULL || secret == NULL)
    {
      return -EINVAL;
    }

  if (capacity < BKPROV_QR_PAYLOAD_MAX)
    {
      return -ENOSPC;
    }

  output[used++] = 'S';
  output[used++] = 'N';
  output[used++] = '1';
  output[used++] = ':';
  used += b64url_encode(output + used, locator, 8u);
  output[used++] = ':';
  used += b64url_encode(output + used, fingerprint, 32u);
  output[used++] = ':';
  used += b64url_encode(output + used, secret, 32u);
  output[used] = '\0';
  *size = used;
  return 0;
}

int bkprov_qr_payload_decode(const char *text, size_t size, uint8_t locator[8],
                             uint8_t fingerprint[32], uint8_t secret[32])
{
  const char *first;
  const char *second;
  const char *third;

  if (text == NULL || size < 8u || locator == NULL || fingerprint == NULL ||
      secret == NULL)
    {
      return -EINVAL;
    }

  if (size < 4u || memcmp(text, "SN1:", 4u) != 0)
    {
      return -EBADMSG;
    }

  first = text + 4;
  second = memchr(first, ':', size - 4u);
  if (second == NULL || second == first)
    {
      return -EBADMSG;
    }

  third = memchr(second + 1, ':', size - (size_t)(second + 1 - text));
  if (third == NULL || third == second + 1)
    {
      return -EBADMSG;
    }

  if ((size_t)(third + 1 - text) >= size)
    {
      return -EBADMSG;
    }

  if (b64url_decode(first, (size_t)(second - first), locator, 8u) < 0 ||
      b64url_decode(second + 1, (size_t)(third - second - 1), fingerprint,
                    32u) < 0 ||
      b64url_decode(third + 1, size - (size_t)(third + 1 - text), secret,
                    32u) < 0)
    {
      return -EBADMSG;
    }

  return 0;
}

/****************************************************************************
 * QR encoder
 ****************************************************************************/

struct qr_version_s
{
  unsigned int version;
  unsigned int total_codewords;
  unsigned int ec_per_block;
  unsigned int blocks;
  unsigned int data_per_block;
};

/* Level-L block structure for versions 1..6. */
static const struct qr_version_s g_qr_versions[] =
{
  {1, 26, 7, 1, 19},
  {2, 44, 10, 1, 34},
  {3, 70, 15, 1, 55},
  {4, 100, 20, 1, 80},
  {5, 134, 26, 1, 108},
  {6, 172, 18, 2, 68},
};

#define QR_VERSION_COUNT (sizeof(g_qr_versions) / sizeof(g_qr_versions[0]))
#define QR_MASK_COUNT     8u
#define QR_LEVEL_L        1u
#define QR_MAX_BLOCKS     2u
#define QR_MAX_TOTAL      172u
#define QR_MAX_EC         26u
#define QR_MAX_DATA       108u

/* Indexed by version; version 1 has no alignment pattern. */
static const uint8_t g_qr_alignment[7][2] =
{
  {0, 0}, {0, 0}, {6, 18}, {6, 22}, {6, 26}, {6, 30}, {6, 34},
};

static uint8_t gf_multiply(uint8_t a, uint8_t b)
{
  uint16_t product = 0;
  uint16_t left = a;
  unsigned int i;

  for (i = 0; i < 8u; i++)
    {
      if (b & (1u << i))
        {
          product ^= left;
        }

      left <<= 1;
      if (left & 0x100u)
        {
          left ^= 0x11du;
        }
    }

  return (uint8_t)product;
}

static uint8_t gf_power(uint8_t value, unsigned int exponent)
{
  uint8_t result = 1;
  unsigned int i;

  for (i = 0; i < exponent; i++)
    {
      result = gf_multiply(result, value);
    }

  return result;
}

/* g(x) = (x - a^0) ... (x - a^(degree-1)), highest order first. */
static void qr_generator(unsigned int degree, uint8_t *polynomial)
{
  unsigned int i;
  unsigned int j;

  memset(polynomial, 0, degree + 1u);
  polynomial[0] = 1;
  for (i = 0; i < degree; i++)
    {
      uint8_t root = gf_power(2, i);

      for (j = i + 1u; j > 0; j--)
        {
          polynomial[j] = (uint8_t)(polynomial[j - 1u] ^
                                    gf_multiply(polynomial[j], root));
        }

      polynomial[0] = gf_multiply(polynomial[0], root);
    }
}

static void qr_remainder(const uint8_t *data, unsigned int size,
                         const uint8_t *generator, unsigned int degree,
                         uint8_t *remainder)
{
  unsigned int i;
  unsigned int j;

  memset(remainder, 0, QR_MAX_EC);
  for (i = 0; i < size; i++)
    {
      uint8_t factor = (uint8_t)(data[i] ^ remainder[0]);

      memmove(remainder, remainder + 1, degree - 1u);
      remainder[degree - 1u] = 0;
      for (j = 0; j < degree; j++)
        {
          /* generator[] is little-endian: index k owns x^k. */
      remainder[j] ^= gf_multiply(generator[degree - 1u - j], factor);
        }
    }
}

static void bits_append(uint8_t *buffer, size_t *bits, unsigned int value,
                        unsigned int width)
{
  unsigned int i;

  for (i = 0; i < width; i++)
    {
      unsigned int bit = (value >> (width - 1u - i)) & 1u;

      if (bit)
        {
          buffer[*bits >> 3] |= (uint8_t)(0x80u >> (*bits & 7u));
        }

      (*bits)++;
    }
}

static void qr_reserve(uint8_t *reserved, unsigned int size, int row, int col)
{
  if (row >= 0 && col >= 0 && (unsigned int)row < size &&
      (unsigned int)col < size)
    {
      reserved[(unsigned int)row * size + (unsigned int)col] = 1u;
    }
}

static void qr_finder(uint8_t *modules, uint8_t *reserved, unsigned int size,
                      int row, int col)
{
  int r;
  int c;

  /* Reserve the separator ring as well as the 7x7 pattern itself. */
  for (r = -1; r <= 7; r++)
    {
      for (c = -1; c <= 7; c++)
        {
          qr_reserve(reserved, size, row + r, col + c);
        }
    }

  for (r = 0; r < 7; r++)
    {
      for (c = 0; c < 7; c++)
        {
          bool dark = r == 0 || r == 6 || c == 0 || c == 6 ||
                      (r >= 2 && r <= 4 && c >= 2 && c <= 4);

          modules[(unsigned int)(row + r) * size + (unsigned int)(col + c)] =
            dark ? 1u : 0u;
        }
    }
}

static void qr_alignment(uint8_t *modules, uint8_t *reserved,
                         unsigned int size, unsigned int version)
{
  unsigned int i;
  unsigned int j;

  if (version == 1u)
    {
      return;
    }

  for (i = 0; i < 2u; i++)
    {
      for (j = 0; j < 2u; j++)
        {
          int row = g_qr_alignment[version][i];
          int col = g_qr_alignment[version][j];
          int r;
          int c;

          /* The three finder corners already own their own pattern. */
          if ((row == 6 && col == 6) ||
              (row == 6 && col == (int)size - 7) ||
              (row == (int)size - 7 && col == 6))
            {
              continue;
            }

          for (r = -2; r <= 2; r++)
            {
              for (c = -2; c <= 2; c++)
                {
                  bool dark = r == -2 || r == 2 || c == -2 || c == 2 ||
                              (r == 0 && c == 0);

                  qr_reserve(reserved, size, row + r, col + c);
                  modules[(unsigned int)(row + r) * size +
                          (unsigned int)(col + c)] = dark ? 1u : 0u;
                }
            }
        }
    }
}

static void qr_timing(uint8_t *modules, uint8_t *reserved, unsigned int size)
{
  unsigned int i;

  for (i = 8u; i + 8u < size; i++)
    {
      uint8_t value = (i % 2u) == 0u ? 1u : 0u;

      if (!reserved[6u * size + i])
        {
          modules[6u * size + i] = value;
          reserved[6u * size + i] = 1u;
        }

      if (!reserved[i * size + 6u])
        {
          modules[i * size + 6u] = value;
          reserved[i * size + 6u] = 1u;
        }
    }
}

static void qr_format_area(uint8_t *reserved, unsigned int size)
{
  unsigned int i;

  for (i = 0; i <= 8u; i++)
    {
      if (i != 6u)
        {
          qr_reserve(reserved, size, 8, (int)i);
          qr_reserve(reserved, size, (int)i, 8);
        }
    }

  for (i = 0; i < 8u; i++)
    {
      qr_reserve(reserved, size, 8, (int)(size - 1u - i));
      qr_reserve(reserved, size, (int)(size - 1u - i), 8);
    }
}

static unsigned int qr_format_bits(unsigned int mask)
{
  unsigned int value = (QR_LEVEL_L << 3) | (mask & 7u);
  unsigned int remainder = value << 10;
  unsigned int i;

  for (i = 0; i < 5u; i++)
    {
      if (remainder & (1u << (14u - i)))
        {
          remainder ^= 0x537u << (4u - i);
        }
    }

  return ((value << 10) | (remainder & 0x3ffu)) ^ 0x5412u;
}

static void qr_write_format(uint8_t *modules, unsigned int size,
                            unsigned int mask)
{
  unsigned int bits = qr_format_bits(mask);
  unsigned int i;

  /* First copy: low bits ascend column 8, the high bits descend row 8. */
  for (i = 0; i < 6u; i++)
    {
      modules[i * size + 8u] = (bits >> i) & 1u;
    }

  modules[7u * size + 8u] = (bits >> 6) & 1u;
  modules[8u * size + 8u] = (bits >> 7) & 1u;
  modules[8u * size + 7u] = (bits >> 8) & 1u;
  for (i = 9u; i < 15u; i++)
    {
      modules[8u * size + (14u - i)] = (bits >> i) & 1u;
    }

  /* Second copy: low bits along row 8, the remaining bits up column 8. */
  for (i = 0; i < 8u; i++)
    {
      modules[8u * size + (size - 1u - i)] = (bits >> i) & 1u;
    }

  for (i = 8u; i < 15u; i++)
    {
      modules[(size - 15u + i) * size + 8u] = (bits >> i) & 1u;
    }

  modules[(size - 8u) * size + 8u] = 1u; /* Fixed dark module. */
}

static void qr_place_data(uint8_t *modules, const uint8_t *reserved,
                          unsigned int size, const uint8_t *codewords,
                          size_t total_bits)
{
  size_t bit = 0;
  int right;
  bool upward = true;

  /* Two-module-wide columns, right to left, skipping the vertical timing
   * pattern column. Signed arithmetic keeps the final (1,0) pair reachable.
   */
  for (right = (int)size - 1; right >= 1; right -= 2)
    {
      unsigned int row;
      unsigned int k;

      if (right == 6)
        {
          right = 5;
        }

      for (row = 0; row < size; row++)
        {
          unsigned int target_row = upward ? size - 1u - row : row;

          for (k = 0; k < 2u; k++)
            {
              unsigned int target_col = (unsigned int)right - k;

              if (reserved[target_row * size + target_col])
                {
                  continue;
                }

              if (bit < total_bits)
                {
                  modules[target_row * size + target_col] =
                    (codewords[bit >> 3] >> (7u - (bit & 7u))) & 1u;
                }
              else
                {
                  modules[target_row * size + target_col] = 0u;
                }

              bit++;
            }
        }

      upward = !upward;
    }
}

static uint8_t qr_mask_bit(unsigned int mask, unsigned int row,
                           unsigned int col)
{
  switch (mask)
    {
      case 0: return ((row + col) % 2u) == 0u;
      case 1: return (row % 2u) == 0u;
      case 2: return (col % 3u) == 0u;
      case 3: return ((row + col) % 3u) == 0u;
      case 4: return (((row / 2u) + (col / 3u)) % 2u) == 0u;
      case 5: return ((((row * col) % 2u) + ((row * col) % 3u)) == 0u);
      case 6: return (((((row * col) % 2u) + ((row * col) % 3u)) % 2u) == 0u);
      default: return (((((row + col) % 2u) + ((row * col) % 3u)) % 2u) == 0u);
    }
}

static int qr_penalty(const uint8_t *modules, unsigned int size)
{
  static const uint8_t pattern_a[11] =
    {1, 0, 1, 1, 1, 0, 1, 0, 0, 0, 0};
  static const uint8_t pattern_b[11] =
    {0, 0, 0, 0, 1, 0, 1, 1, 1, 0, 1};
  int score = 0;
  unsigned int i;
  unsigned int j;

  for (i = 0; i < size; i++)
    {
      unsigned int run = 1u;

      for (j = 1u; j < size; j++)
        {
          if (modules[i * size + j] == modules[i * size + j - 1u])
            {
              run++;
            }
          else
            {
              if (run >= 5u) score += 3 + (int)(run - 5u);
              run = 1u;
            }
        }

      if (run >= 5u) score += 3 + (int)(run - 5u);

      run = 1u;
      for (j = 1u; j < size; j++)
        {
          if (modules[j * size + i] == modules[(j - 1u) * size + i])
            {
              run++;
            }
          else
            {
              if (run >= 5u) score += 3 + (int)(run - 5u);
              run = 1u;
            }
        }

      if (run >= 5u) score += 3 + (int)(run - 5u);
    }

  for (i = 0; i + 1u < size; i++)
    {
      for (j = 0; j + 1u < size; j++)
        {
          uint8_t value = modules[i * size + j];

          if (value == modules[i * size + j + 1u] &&
              value == modules[(i + 1u) * size + j] &&
              value == modules[(i + 1u) * size + j + 1u])
            {
              score += 3;
            }
        }
    }

  for (i = 0; i < size; i++)
    {
      for (j = 0; j + 11u <= size; j++)
        {
          unsigned int k;
          bool forward = true;
          bool reverse = true;

          for (k = 0; k < 11u; k++)
            {
              uint8_t value = modules[i * size + j + k];
              if (value != pattern_a[k]) forward = false;
              if (value != pattern_b[k]) reverse = false;
            }

          if (forward || reverse) score += 40;
        }

      for (j = 0; j + 11u <= size; j++)
        {
          unsigned int k;
          bool forward = true;
          bool reverse = true;

          for (k = 0; k < 11u; k++)
            {
              uint8_t value = modules[(j + k) * size + i];
              if (value != pattern_a[k]) forward = false;
              if (value != pattern_b[k]) reverse = false;
            }

          if (forward || reverse) score += 40;
        }
    }

  {
    unsigned int dark = 0;
    unsigned int total = size * size;
    int percent;
    int deviation;

    for (i = 0; i < total; i++)
      {
        dark += modules[i] ? 1u : 0u;
      }

    percent = (int)((dark * 100u + total / 2u) / total);
    deviation = percent - 50;
    if (deviation < 0) deviation = -deviation;
    score += (deviation / 5) * 10;
  }

  return score;
}

int bkprov_qr_encode(const uint8_t *payload, size_t size, uint8_t *modules,
                     size_t capacity, unsigned int *version,
                     unsigned int *dimension)
{
  uint8_t bits[QR_MAX_TOTAL * 8u];
  uint8_t blocks[QR_MAX_BLOCKS][QR_MAX_DATA + QR_MAX_EC];
  uint8_t ec[QR_MAX_BLOCKS][QR_MAX_EC];
  uint8_t generator[QR_MAX_EC + 1u];
  uint8_t codewords[QR_MAX_TOTAL];
  uint8_t reserved[BKPROV_QR_MAX_DIMENSION * BKPROV_QR_MAX_DIMENSION];
  uint8_t masked[BKPROV_QR_MAX_DIMENSION * BKPROV_QR_MAX_DIMENSION];
  const struct qr_version_s *spec = NULL;
  unsigned int data_codewords;
  unsigned int i;
  unsigned int data_per_block;
  unsigned int qr_size;
  size_t bit_count = 0;
  size_t total_bits;
  unsigned int write = 0;
  int best_score = -1;

  if (payload == NULL || size == 0 || modules == NULL || version == NULL ||
      dimension == NULL || size > 0xffu)
    {
      return -EINVAL;
    }

  if (capacity < BKPROV_QR_MAX_DIMENSION * BKPROV_QR_MAX_DIMENSION)
    {
      return -ENOSPC;
    }

  for (i = 0; i < QR_VERSION_COUNT; i++)
    {
      unsigned int candidate = g_qr_versions[i].blocks *
                               g_qr_versions[i].data_per_block;

      if (candidate >= size + 2u)
        {
          spec = &g_qr_versions[i];
          break;
        }
    }

  if (spec == NULL)
    {
      return -ENOSPC;
    }

  data_per_block = spec->data_per_block;
  data_codewords = spec->blocks * data_per_block;
  qr_size = 4u * spec->version + 17u;

  memset(bits, 0, sizeof(bits));
  bits_append(bits, &bit_count, 0x4u, 4u);               /* Byte mode. */
  bits_append(bits, &bit_count, (unsigned int)size, 8u);  /* Count, v1..v9. */
  for (i = 0; i < size; i++)
    {
      bits_append(bits, &bit_count, payload[i], 8u);
    }

  if (bit_count + 4u <= data_codewords * 8u)
    {
      bit_count += 4u;
    }

  if ((bit_count & 7u) != 0u)
    {
      bit_count += 8u - (bit_count & 7u);
    }

  memset(codewords, 0, sizeof(codewords));
  memcpy(codewords, bits, bit_count >> 3);
  for (i = (unsigned int)(bit_count >> 3); i < data_codewords; i += 2u)
    {
      codewords[i] = 0xecu;
      if (i + 1u < data_codewords)
        {
          codewords[i + 1u] = 0x11u;
        }
    }

  /* Split into blocks, append Reed-Solomon parity, then interleave. */
  memset(blocks, 0, sizeof(blocks));
  memset(ec, 0, sizeof(ec));
  qr_generator(spec->ec_per_block, generator);
  for (i = 0; i < spec->blocks; i++)
    {
      memcpy(blocks[i], codewords + i * data_per_block, data_per_block);
      qr_remainder(blocks[i], data_per_block, generator, spec->ec_per_block,
                   ec[i]);
    }

  for (i = 0; i < data_per_block; i++)
    {
      unsigned int block;

      for (block = 0; block < spec->blocks; block++)
        {
          codewords[write++] = blocks[block][i];
        }
    }

  for (i = 0; i < spec->ec_per_block; i++)
    {
      unsigned int block;

      for (block = 0; block < spec->blocks; block++)
        {
          codewords[write++] = ec[block][i];
        }
    }

  if (write != spec->total_codewords)
    {
      return -EIO;
    }

  total_bits = (size_t)write * 8u;

  /* Function patterns are shared by every mask; data placement is repeated. */
  memset(modules, 0, qr_size * qr_size);
  memset(reserved, 0, sizeof(reserved));
  qr_finder(modules, reserved, qr_size, 0, 0);
  qr_finder(modules, reserved, qr_size, 0, (int)qr_size - 7);
  qr_finder(modules, reserved, qr_size, (int)qr_size - 7, 0);
  qr_timing(modules, reserved, qr_size);
  qr_alignment(modules, reserved, qr_size, spec->version);
  qr_format_area(reserved, qr_size);

  for (i = 0; i < QR_MASK_COUNT; i++)
    {
      unsigned int row;
      unsigned int col;
      int score;

      memcpy(masked, modules, qr_size * qr_size);
      qr_place_data(masked, reserved, qr_size, codewords, total_bits);

      for (row = 0; row < qr_size; row++)
        {
          for (col = 0; col < qr_size; col++)
            {
              if (!reserved[row * qr_size + col] &&
                  qr_mask_bit(i, row, col))
                {
                  masked[row * qr_size + col] ^= 1u;
                }
            }
        }

      qr_write_format(masked, qr_size, i);
      score = qr_penalty(masked, qr_size);
      if (best_score < 0 || score < best_score)
        {
          best_score = score;
          memcpy(modules, masked, qr_size * qr_size);
        }
    }

  *version = spec->version;
  *dimension = qr_size;
  return 0;
}

int bkprov_qr_render_rgb565(const uint8_t *modules, unsigned int dimension,
                            uint16_t *pixels, unsigned int width,
                            unsigned int height, uint16_t dark,
                            uint16_t light)
{
  unsigned int quiet = 4u;
  unsigned int scale;
  unsigned int total;
  unsigned int offset_x;
  unsigned int offset_y;
  unsigned int x;
  unsigned int y;

  if (modules == NULL || pixels == NULL || dimension == 0u ||
      dimension > BKPROV_QR_MAX_DIMENSION || width == 0u || height == 0u)
    {
      return -EINVAL;
    }

  total = dimension + 2u * quiet;
  if (width < total || height < total)
    {
      return -ENOSPC;
    }

  scale = width < height ? width / total : height / total;
  if (scale == 0u)
    {
      return -ENOSPC;
    }

  offset_x = (width - total * scale) / 2u + quiet * scale;
  offset_y = (height - total * scale) / 2u + quiet * scale;

  for (y = 0; y < height; y++)
    {
      for (x = 0; x < width; x++)
        {
          pixels[y * width + x] = light;
        }
    }

  for (y = 0; y < dimension; y++)
    {
      for (x = 0; x < dimension; x++)
        {
          unsigned int row;
          unsigned int col;

          if (!modules[y * dimension + x])
            {
              continue;
            }

          for (row = 0; row < scale; row++)
            {
              uint16_t *line = pixels + (offset_y + y * scale + row) * width;

              for (col = 0; col < scale; col++)
                {
                  line[offset_x + x * scale + col] = dark;
                }
            }
        }
    }

  return 0;
}
