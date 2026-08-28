/****************************************************************************
 * chips/bk7258/ap/bk7258_jpeg_decoder.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 AP JPEG decoder typed helper.  The immutable v3.1.1.9 bundle
 * exports the high-level hardware decoder from libbk_jpeg_decoder.a.
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <nuttx/cache.h>
#include <nuttx/kmalloc.h>
#include <nuttx/mutex.h>

#include <common/bk_err.h>
#include <components/avdk_utils/avdk_error.h>
#include <components/media_types.h>

#include <arch/chip/bk7258_jpeg_decoder.h>

/* The v3.1.1.9 jpeg_dec_driver.c routes JPEGDEC to CPU2 unconditionally.
 * AP NuttX executes on CPU1, so migrate the interrupt route after the SDK
 * decoder has opened.  sys_driver.h and sys_types.h are not exported by the
 * immutable bundle; these declarations/constants are the minimal ABI from
 * those v3.1.1.9 public driver symbols and the verified BK7258 definitions:
 * CPU1=1, CPU2=2, and JPEGDEC interrupt bit 26. */

extern int32_t sys_drv_core_intr_group1_enable(uint32_t core_id,
                                               uint32_t param);
extern int32_t sys_drv_core_intr_group1_disable(uint32_t core_id,
                                                uint32_t param);

#define BK7258_JPEGDEC_CPU1_CORE_ID          1u
#define BK7258_JPEGDEC_CPU2_CORE_ID          2u
#define BK7258_JPEGDEC_INTERRUPT_CTRL_BIT    (1u << 26)

/*
 * The v3.1.1.9 public bk_jpeg_decode_types.h includes frame_buffer.h, which
 * is not exported by the immutable AP bundle.  The complete frame_buffer_t
 * is nevertheless exported by media_types.h, so use that public type here.
 * Keep only the JPEG config/info/handle declarations needed to bridge the
 * leaked public include; the implementations remain in immutable
 * libbk_jpeg_decoder.a.  Image information is parsed locally because the SDK
 * parser is not bounded for untrusted buffers.
 */

typedef struct
{
  bk_err_t (*in_complete)(frame_buffer_t *in_frame);
  frame_buffer_t *(*out_malloc)(uint32_t size);
  bk_err_t (*out_complete)(uint32_t format_type, uint32_t result,
                           frame_buffer_t *out_frame);
} bk7258_sdk_jpeg_decode_callback_t;

typedef struct
{
  bk7258_sdk_jpeg_decode_callback_t decode_cbs;
} bk7258_sdk_jpeg_decode_hw_config_t;

typedef struct bk7258_sdk_jpeg_decode_hw_s
  *bk7258_sdk_jpeg_decode_hw_handle_t;

extern avdk_err_t bk_hardware_jpeg_decode_new(
  bk7258_sdk_jpeg_decode_hw_handle_t *handle,
  bk7258_sdk_jpeg_decode_hw_config_t *config);
extern avdk_err_t bk_jpeg_decode_hw_open(
  bk7258_sdk_jpeg_decode_hw_handle_t handle);
extern avdk_err_t bk_jpeg_decode_hw_close(
  bk7258_sdk_jpeg_decode_hw_handle_t handle);
extern avdk_err_t bk_jpeg_decode_hw_decode(
  bk7258_sdk_jpeg_decode_hw_handle_t handle,
  frame_buffer_t *in_frame,
  frame_buffer_t *out_frame);
extern avdk_err_t bk_jpeg_decode_hw_delete(
  bk7258_sdk_jpeg_decode_hw_handle_t handle);

#define BK7258_JPEG_YUV_PIXEL_BYTES 2u
#define BK7258_JPEG_DMA_READ_GUARD  2048u

#define BK7258_JPEG_MARKER_PREFIX   0xffu
#define BK7258_JPEG_MARKER_SOF0     0xc0u
#define BK7258_JPEG_MARKER_DHT      0xc4u
#define BK7258_JPEG_MARKER_SOI      0xd8u
#define BK7258_JPEG_MARKER_EOI      0xd9u
#define BK7258_JPEG_MARKER_SOS      0xdau
#define BK7258_JPEG_MARKER_DQT      0xdbu
#define BK7258_JPEG_MARKER_DRI      0xddu
#define BK7258_JPEG_MARKER_COM      0xfeu
#define BK7258_JPEG_MARKER_APP0     0xe0u
#define BK7258_JPEG_MARKER_APP15    0xefu

#define BK7258_JPEG_SOF0_PRECISION  8u
#define BK7258_JPEG_SOF0_COMPONENTS 3u
#define BK7258_JPEG_SOF0_LENGTH     17u
#define BK7258_JPEG_SAMPLE_444      0x11u
#define BK7258_JPEG_SAMPLE_422      0x21u
#define BK7258_JPEG_SAMPLE_420      0x22u

#define BK7258_JPEG_DQT_RECORD_SIZE 65u
#define BK7258_JPEG_DQT_TABLE0_BIT  (1u << 0)
#define BK7258_JPEG_DQT_TABLE1_BIT  (1u << 1)
#define BK7258_JPEG_DQT_REQUIRED    (BK7258_JPEG_DQT_TABLE0_BIT | \
                                     BK7258_JPEG_DQT_TABLE1_BIT)

#define BK7258_JPEG_DHT_COUNT_BYTES 16u
#define BK7258_JPEG_DHT_HEADER_SIZE (1u + BK7258_JPEG_DHT_COUNT_BYTES)
#define BK7258_JPEG_DHT_DC0_BIT     (1u << 0)
#define BK7258_JPEG_DHT_DC1_BIT     (1u << 1)
#define BK7258_JPEG_DHT_AC0_BIT     (1u << 2)
#define BK7258_JPEG_DHT_AC1_BIT     (1u << 3)
#define BK7258_JPEG_DHT_REQUIRED    (BK7258_JPEG_DHT_DC0_BIT | \
                                     BK7258_JPEG_DHT_DC1_BIT | \
                                     BK7258_JPEG_DHT_AC0_BIT | \
                                     BK7258_JPEG_DHT_AC1_BIT)
#define BK7258_JPEG_DHT_DC_SYMBOLS  12u
#define BK7258_JPEG_DHT_AC_SYMBOLS  162u
#define BK7258_JPEG_DHT_AC_SIZE_MAX 10u

struct bk7258_jpeg_decoder_s
{
  bk7258_sdk_jpeg_decode_hw_handle_t handle;
  FAR uint8_t *bounce;
  uint32_t bounce_capacity;
  bool initialized;
  bool opened;
  bool cpu1_route_enabled;
  bool operation_active;
  bool faulted;
  bool orphaned;
};

static mutex_t g_bk7258_jpeg_decoder_lock = NXMUTEX_INITIALIZER;
static struct bk7258_jpeg_decoder_s g_bk7258_jpeg_decoder;
static FAR struct bk7258_jpeg_decoder_s *g_bk7258_jpeg_decoder_owner;

static int bk7258_jpeg_decoder_sdk_error(avdk_err_t error)
{
  switch (error)
    {
      case BK_OK:
        return 0;

      case AVDK_ERR_INVAL:
      case BK_ERR_PARAM:
      case BK_ERR_NULL_PARAM:
        return -EINVAL;

      case AVDK_ERR_NOMEM:
      case BK_ERR_NO_MEM:
        return -ENOMEM;

      case AVDK_ERR_BUSY:
      case BK_ERR_BUSY:
      case BK_ERR_IN_PROGRESS:
        return -EBUSY;

      case AVDK_ERR_TIMEOUT:
      case BK_ERR_TIMEOUT:
        return -ETIMEDOUT;

      case AVDK_ERR_NODEV:
      case BK_ERR_NO_DEV:
      case BK_ERR_NOT_INIT:
        return -ENODEV;

      case AVDK_ERR_UNSUPPORTED:
      case BK_ERR_NOT_SUPPORT:
        return -ENOTSUP;

      case AVDK_ERR_NO_RESOURCE:
        return -ENOMEM;

      case AVDK_ERR_SHUTDOWN:
      case BK_ERR_SHUT_DOWN:
        return -ESHUTDOWN;

      case BK_ERR_TRY_AGAIN:
        return -EAGAIN;

      case AVDK_ERR_GENERIC:
      case AVDK_ERR_HWERROR:
      case AVDK_ERR_IO:
      default:
        /* The SDK's fixed hardware timeout is reported as HWERROR by its
         * public synchronous path.  Do not manufacture -ETIMEDOUT here. */
        return -EIO;
    }
}

static int bk7258_jpeg_decoder_check_locked(
  FAR struct bk7258_jpeg_decoder_s *priv)
{
  if (priv == NULL)
    {
      return -EINVAL;
    }

  if (priv != g_bk7258_jpeg_decoder_owner || !priv->initialized ||
      !priv->opened || !priv->cpu1_route_enabled || priv->handle == NULL ||
      priv->orphaned)
    {
      return -ENODEV;
    }

  if (priv->faulted)
    {
      return -EIO;
    }

  return 0;
}

/* A failed initialize may itself encounter a close/delete failure.  Preserve
 * that unique SDK handle as a recovery-only owner instead of silently losing
 * it and allowing a second hardware instance to be constructed.
 */

static FAR struct bk7258_jpeg_decoder_s *
bk7258_jpeg_decoder_retain_orphan_locked(
  bk7258_sdk_jpeg_decode_hw_handle_t handle, bool opened)
{
  g_bk7258_jpeg_decoder.handle = handle;
  g_bk7258_jpeg_decoder.bounce = NULL;
  g_bk7258_jpeg_decoder.bounce_capacity = 0;
  g_bk7258_jpeg_decoder.initialized = true;
  g_bk7258_jpeg_decoder.opened = opened;
  g_bk7258_jpeg_decoder.cpu1_route_enabled = false;
  g_bk7258_jpeg_decoder.operation_active = false;
  g_bk7258_jpeg_decoder.faulted = true;
  g_bk7258_jpeg_decoder.orphaned = true;
  g_bk7258_jpeg_decoder_owner = &g_bk7258_jpeg_decoder;
  return g_bk7258_jpeg_decoder_owner;
}

static int bk7258_jpeg_decoder_route_to_cpu1(void)
{
  int ret;

  /* v3.1.1.9 disable returns the pre-update enable register, not an errno.
   * Match the verified SDK consumers: perform the disable and only test the
   * zero-on-success enable operation below. */

  (void)sys_drv_core_intr_group1_disable(BK7258_JPEGDEC_CPU2_CORE_ID,
                                         BK7258_JPEGDEC_INTERRUPT_CTRL_BIT);

  ret = sys_drv_core_intr_group1_enable(BK7258_JPEGDEC_CPU1_CORE_ID,
                                        BK7258_JPEGDEC_INTERRUPT_CTRL_BIT);
  if (ret != 0)
    {
      (void)sys_drv_core_intr_group1_disable(
        BK7258_JPEGDEC_CPU1_CORE_ID, BK7258_JPEGDEC_INTERRUPT_CTRL_BIT);
      return -EIO;
    }

  return 0;
}

static int bk7258_jpeg_decoder_route_from_cpu1(void)
{
  /* Disable returns the pre-update enable register, not an errno. */

  (void)sys_drv_core_intr_group1_disable(BK7258_JPEGDEC_CPU1_CORE_ID,
                                         BK7258_JPEGDEC_INTERRUPT_CTRL_BIT);
  return 0;
}

/* The SDK passes addresses to a 32-bit JPEG register interface.  Reject a
 * range whose final byte cannot be represented by that interface, even when
 * this wrapper is syntax-checked on a wider host. */

static int bk7258_jpeg_decoder_validate_sdk_address(
  FAR const uint8_t *data, uint32_t span)
{
  uint64_t base;
  uint64_t last;

  if (data == NULL || span == 0)
    {
      return -EINVAL;
    }

  base = (uint64_t)(uintptr_t)data;
  if (base > UINT64_MAX - span)
    {
      return -EOVERFLOW;
    }

  last = base + span - 1;
  if (last > UINT32_MAX)
    {
      return -EOVERFLOW;
    }

  return 0;
}

static int bk7258_jpeg_decoder_validate_input(
  FAR const struct bk7258_jpeg_decoder_frame_s *input)
{
  int ret;

  if (input == NULL || input->data == NULL || input->capacity == 0 ||
      input->length == 0 || input->length > input->capacity)
    {
      return -EINVAL;
    }

  ret = bk7258_jpeg_decoder_validate_sdk_address(input->data,
                                                 input->length);
  if (ret < 0)
    {
      return ret;
    }

  return bk7258_jpeg_decoder_validate_sdk_address(input->data,
                                                   input->capacity);
}

static int bk7258_jpeg_decoder_validate_output(
  FAR const struct bk7258_jpeg_decoder_frame_s *output)
{
  if (output == NULL || output->data == NULL || output->capacity == 0)
    {
      return -EINVAL;
    }

  return bk7258_jpeg_decoder_validate_sdk_address(output->data,
                                                  output->capacity);
}

/* Invalidating a partial cache line can discard unrelated caller data.  The
 * decoder therefore accepts only an output range whose payload occupies
 * whole, caller-owned cache lines.  Keep this check separate from the basic
 * descriptor validation because the required span depends on JPEG metadata.
 */

static int bk7258_jpeg_decoder_output_cache_span(
  FAR const struct bk7258_jpeg_decoder_frame_s *output,
  size_t payload,
  FAR uint32_t *span)
{
  uintptr_t base;
  size_t line;
  size_t rounded;

  if (output == NULL || span == NULL || payload == 0)
    {
      return -EINVAL;
    }

  line = up_get_dcache_linesize();
  if (line == 0)
    {
      /* The maintained AP handoff intentionally disables D-cache and maps
       * its DMA SRAM non-cacheable.  Keep a natural alignment contract in
       * that configuration; the cache maintenance helpers are no-ops.
       */

      line = sizeof(uintptr_t);
    }

  if ((line & (line - 1)) != 0)
    {
      return -EIO;
    }

  base = (uintptr_t)output->data;
  if ((base & (line - 1)) != 0)
    {
      return -EINVAL;
    }

  if (payload > SIZE_MAX - (line - 1))
    {
      return -EOVERFLOW;
    }

  rounded = (payload + line - 1) & ~(line - 1);
  if (rounded > UINT32_MAX || base > UINTPTR_MAX - rounded)
    {
      return -EOVERFLOW;
    }

  if (rounded > output->capacity)
    {
      return -ENOSPC;
    }

  *span = (uint32_t)rounded;
  return 0;
}

static int bk7258_jpeg_decoder_validate_no_overlap(
  FAR const struct bk7258_jpeg_decoder_frame_s *input,
  FAR const struct bk7258_jpeg_decoder_frame_s *output)
{
  uintptr_t input_start = (uintptr_t)input->data;
  uintptr_t output_start = (uintptr_t)output->data;
  uintptr_t input_end;
  uintptr_t output_end;

  if (input_start > UINTPTR_MAX - input->length ||
      output_start > UINTPTR_MAX - output->capacity)
    {
      return -EINVAL;
    }

  input_end = input_start + input->length;
  output_end = output_start + output->capacity;

  if (input_start < output_end && output_start < input_end)
    {
      return -EINVAL;
    }

  return 0;
}

static int bk7258_jpeg_decoder_make_input(
  FAR const struct bk7258_jpeg_decoder_frame_s *input,
  FAR frame_buffer_t *sdk_input)
{
  int ret = bk7258_jpeg_decoder_validate_input(input);

  if (ret < 0)
    {
      return ret;
    }

  *sdk_input = (frame_buffer_t){0};
  sdk_input->frame = input->data;
  sdk_input->size = input->capacity;
  sdk_input->length = input->length;
  sdk_input->fmt = PIXEL_FMT_JPEG;
  return 0;
}

static int bk7258_jpeg_decoder_make_output(
  FAR const struct bk7258_jpeg_decoder_frame_s *output,
  FAR frame_buffer_t *sdk_output)
{
  int ret = bk7258_jpeg_decoder_validate_output(output);

  if (ret < 0)
    {
      return ret;
    }

  *sdk_output = (frame_buffer_t){0};
  sdk_output->frame = output->data;
  sdk_output->size = output->capacity;
  sdk_output->fmt = PIXEL_FMT_YUYV;
  return 0;
}

/* The immutable HAL programs BASE_RD_LEN to compressed length + 2048.  A
 * caller-owned V4L2 buffer may end exactly at bytesused, so never expose it
 * directly to the JPEG bus master.  Grow and reuse a cache-line-exclusive
 * bounce, copy the payload, and zero the required read guard for each frame.
 */

static int bk7258_jpeg_decoder_prepare_bounce(
  FAR struct bk7258_jpeg_decoder_s *priv,
  FAR const struct bk7258_jpeg_decoder_frame_s *input)
{
  FAR uint8_t *replacement;
  FAR uint8_t *old;
  size_t alignment;
  size_t guarded;
  size_t allocated;
  uintptr_t start;
  int ret;

  if (priv == NULL || input == NULL)
    {
      return -EINVAL;
    }

  if (input->length > UINT32_MAX - BK7258_JPEG_DMA_READ_GUARD)
    {
      return -EOVERFLOW;
    }

  alignment = up_get_dcache_linesize();
  if (alignment == 0)
    {
      alignment = sizeof(uintptr_t);
    }

  if (alignment < sizeof(uintptr_t))
    {
      alignment = sizeof(uintptr_t);
    }

  if ((alignment & (alignment - 1)) != 0)
    {
      return -EIO;
    }

  guarded = (size_t)input->length + BK7258_JPEG_DMA_READ_GUARD;
  if (guarded > SIZE_MAX - (alignment - 1))
    {
      return -EOVERFLOW;
    }

  allocated = (guarded + alignment - 1) & ~(alignment - 1);
  if (allocated > UINT32_MAX)
    {
      return -EOVERFLOW;
    }

  if (priv->bounce == NULL || priv->bounce_capacity < allocated)
    {
      replacement = kmm_memalign(alignment, allocated);
      if (replacement == NULL)
        {
          return -ENOMEM;
        }

      ret = bk7258_jpeg_decoder_validate_sdk_address(
        replacement, (uint32_t)allocated);
      if (ret < 0)
        {
          kmm_free(replacement);
          return ret;
        }

      /* Preserve the usable old allocation until its replacement has
       * passed every allocation and DMA-address check.
       */

      old = priv->bounce;
      priv->bounce = replacement;
      priv->bounce_capacity = (uint32_t)allocated;
      if (old != NULL)
        {
          kmm_free(old);
        }
    }

  memcpy(priv->bounce, input->data, input->length);
  memset(priv->bounce + input->length, 0,
         guarded - input->length);

  start = (uintptr_t)priv->bounce;
  up_clean_dcache(start, start + guarded);

  return 0;
}

static bool bk7258_jpeg_decoder_is_sof(uint8_t marker)
{
  return marker >= 0xc0u && marker <= 0xcfu &&
         marker != BK7258_JPEG_MARKER_DHT && marker != 0xc8u &&
         marker != 0xccu;
}

static bool bk7258_jpeg_decoder_is_header_segment(uint8_t marker)
{
  return marker == BK7258_JPEG_MARKER_DHT ||
         marker == BK7258_JPEG_MARKER_DQT ||
         marker == BK7258_JPEG_MARKER_DRI ||
         marker == BK7258_JPEG_MARKER_COM ||
         (marker >= BK7258_JPEG_MARKER_APP0 &&
          marker <= BK7258_JPEG_MARKER_APP15);
}

static uint16_t bk7258_jpeg_decoder_get_be16(FAR const uint8_t *data)
{
  return ((uint16_t)data[0] << 8) | data[1];
}

static bool bk7258_jpeg_decoder_scan_matches(
  FAR const uint8_t component_ids[BK7258_JPEG_SOF0_COMPONENTS],
  FAR const uint8_t *scan)
{
  return scan[0] == BK7258_JPEG_SOF0_COMPONENTS &&
         scan[1] == component_ids[0] && scan[2] == 0x00u &&
         scan[3] == component_ids[1] && scan[4] == 0x11u &&
         scan[5] == component_ids[2] && scan[6] == 0x11u &&
         scan[7] == 0 && scan[8] == 63u && scan[9] == 0;
}

/* Admit only the two 8-bit quantization tables consumed by the pinned
 * v3.1.1.9 hardware path.  A DQT segment may contain one or both records,
 * but each table is defined exactly once across the complete header.
 */

static int bk7258_jpeg_decoder_validate_dqt(FAR const uint8_t *segment,
                                            uint16_t segment_length,
                                            FAR uint8_t *seen)
{
  FAR const uint8_t *record;
  size_t remaining;
  uint8_t table;
  uint8_t bit;
  unsigned int i;

  if (segment == NULL || seen == NULL || segment_length < 2)
    {
      return -EINVAL;
    }

  record = segment + 2;
  remaining = segment_length - 2;
  if (remaining == 0 ||
      (remaining % BK7258_JPEG_DQT_RECORD_SIZE) != 0)
    {
      return -EINVAL;
    }

  while (remaining != 0)
    {
      if ((record[0] >> 4) != 0 || (record[0] & 0x0fu) > 1u)
        {
          return -ENOTSUP;
        }

      table = record[0] & 0x0fu;
      bit = 1u << table;
      if ((*seen & bit) != 0)
        {
          return -ENOTSUP;
        }

      for (i = 1; i < BK7258_JPEG_DQT_RECORD_SIZE; i++)
        {
          if (record[i] == 0)
            {
              return -EINVAL;
            }
        }

      *seen |= bit;
      record += BK7258_JPEG_DQT_RECORD_SIZE;
      remaining -= BK7258_JPEG_DQT_RECORD_SIZE;
    }

  return 0;
}

static int bk7258_jpeg_decoder_validate_huffman_symbols(
  FAR const uint8_t *symbols, uint16_t count, bool ac)
{
  uint32_t seen[8] = {0};
  uint8_t symbol;
  uint8_t run;
  uint8_t size;
  uint32_t bit;
  unsigned int word;
  unsigned int i;

  for (i = 0; i < count; i++)
    {
      symbol = symbols[i];
      word = symbol >> 5;
      bit = 1u << (symbol & 31u);
      if ((seen[word] & bit) != 0)
        {
          return -EINVAL;
        }

      seen[word] |= bit;
      if (!ac)
        {
          if (symbol >= BK7258_JPEG_DHT_DC_SYMBOLS)
            {
              return -ENOTSUP;
            }

          continue;
        }

      run = symbol >> 4;
      size = symbol & 0x0fu;
      if (size > BK7258_JPEG_DHT_AC_SIZE_MAX ||
          (size == 0 && run != 0 && run != 15u))
        {
          return -ENOTSUP;
        }
    }

  return 0;
}

/* Validate canonical baseline Huffman records without constructing their
 * codes.  The available-slots recurrence rejects an over-subscribed prefix
 * tree while intentionally allowing the incomplete trees used by JPEG.
 */

static int bk7258_jpeg_decoder_validate_dht(FAR const uint8_t *segment,
                                            uint16_t segment_length,
                                            FAR uint8_t *seen)
{
  FAR const uint8_t *record;
  uint32_t available;
  uint16_t symbol_count;
  size_t record_size;
  size_t remaining;
  uint8_t table_class;
  uint8_t table_id;
  uint8_t bit;
  unsigned int i;
  int ret;

  if (segment == NULL || seen == NULL || segment_length < 2)
    {
      return -EINVAL;
    }

  record = segment + 2;
  remaining = segment_length - 2;
  if (remaining == 0)
    {
      return -EINVAL;
    }

  while (remaining != 0)
    {
      if (remaining < BK7258_JPEG_DHT_HEADER_SIZE)
        {
          return -EINVAL;
        }

      table_class = record[0] >> 4;
      table_id = record[0] & 0x0fu;
      if (table_class > 1u || table_id > 1u)
        {
          return -ENOTSUP;
        }

      symbol_count = 0;
      available = 1;
      for (i = 0; i < BK7258_JPEG_DHT_COUNT_BYTES; i++)
        {
          available <<= 1;
          if (record[1 + i] > available)
            {
              return -EINVAL;
            }

          available -= record[1 + i];
          symbol_count += record[1 + i];
        }

      /* JPEG canonical Huffman tables must leave at least one code point
       * unused.  A complete tree would assign the all-ones code, which the
       * format reserves for marker padding.
       */

      if (available == 0 || symbol_count == 0 ||
          (!table_class && symbol_count > BK7258_JPEG_DHT_DC_SYMBOLS) ||
          (table_class && symbol_count > BK7258_JPEG_DHT_AC_SYMBOLS))
        {
          return -ENOTSUP;
        }

      record_size = BK7258_JPEG_DHT_HEADER_SIZE + symbol_count;
      if (record_size > remaining)
        {
          return -EINVAL;
        }

      bit = 1u << (table_class * 2u + table_id);
      if ((*seen & bit) != 0)
        {
          return -ENOTSUP;
        }

      ret = bk7258_jpeg_decoder_validate_huffman_symbols(
        record + BK7258_JPEG_DHT_HEADER_SIZE, symbol_count,
        table_class != 0);
      if (ret < 0)
        {
          return ret;
        }

      *seen |= bit;
      record += record_size;
      remaining -= record_size;
    }

  return 0;
}

/* Scan a single baseline entropy-coded segment through EOI.  Marker fill,
 * byte stuffing, and restart markers are the only marker-like byte sequences
 * admitted inside the scan.  Requiring EOI to terminate bytesused prevents a
 * second scan or a concatenated/trailing stream from reaching the SDK.
 */

static int bk7258_jpeg_decoder_scan_entropy(FAR const uint8_t *data,
                                            size_t length,
                                            size_t offset)
{
  uint8_t marker;

  while (offset < length)
    {
      if (data[offset++] != BK7258_JPEG_MARKER_PREFIX)
        {
          continue;
        }

      if (offset >= length)
        {
          return -EINVAL;
        }

      do
        {
          marker = data[offset++];
        }
      while (marker == BK7258_JPEG_MARKER_PREFIX && offset < length);

      if (marker == BK7258_JPEG_MARKER_PREFIX)
        {
          return -EINVAL;
        }

      if (marker == 0 || (marker >= 0xd0u && marker <= 0xd7u))
        {
          continue;
        }

      if (marker == BK7258_JPEG_MARKER_EOI)
        {
          return offset == length ? 0 : -EINVAL;
        }

      return -ENOTSUP;
    }

  return -EINVAL;
}

/* Parse only bounded, baseline three-component JPEG headers.  The SDK's
 * bk_get_jpeg_data_info() walks segment lengths without first proving that
 * the referenced bytes are inside the caller's buffer.  Keep untrusted V4L2
 * buffers out of that public entry point and admit only a SOF0 layout that is
 * also accepted by the immutable hardware decoder.
 */

static int bk7258_jpeg_decoder_parse_info(
  FAR const struct bk7258_jpeg_decoder_frame_s *input,
  FAR struct bk7258_jpeg_decoder_info_s *info)
{
  FAR const uint8_t *data;
  FAR const uint8_t *scan;
  FAR const uint8_t *sof;
  struct bk7258_jpeg_decoder_info_s parsed = {0};
  uint8_t component_ids[BK7258_JPEG_SOF0_COMPONENTS] = {0};
  uint16_t segment_length;
  uint16_t width;
  uint16_t height;
  uint8_t marker;
  uint8_t y_sample;
  uint8_t dqt_seen = 0;
  uint8_t dht_seen = 0;
  bool sof_seen = false;
  size_t offset;
  int ret;

  if (info == NULL)
    {
      return -EINVAL;
    }

  ret = bk7258_jpeg_decoder_validate_input(input);
  if (ret < 0)
    {
      return ret;
    }

  data = input->data;
  if (input->length < 4 || data[0] != BK7258_JPEG_MARKER_PREFIX ||
      data[1] != BK7258_JPEG_MARKER_SOI)
    {
      return -EINVAL;
    }

  offset = 2;
  while (offset < input->length)
    {
      if (data[offset] != BK7258_JPEG_MARKER_PREFIX)
        {
          return -EINVAL;
        }

      do
        {
          offset++;
        }
      while (offset < input->length &&
             data[offset] == BK7258_JPEG_MARKER_PREFIX);

      if (offset >= input->length)
        {
          return -EINVAL;
        }

      marker = data[offset++];
      if (marker == 0 || marker == BK7258_JPEG_MARKER_SOI ||
          marker == BK7258_JPEG_MARKER_EOI ||
          (marker >= 0xd0u && marker <= 0xd7u))
        {
          return -EINVAL;
        }

      if (bk7258_jpeg_decoder_is_sof(marker) &&
          marker != BK7258_JPEG_MARKER_SOF0)
        {
          return -ENOTSUP;
        }

      if (marker != BK7258_JPEG_MARKER_SOF0 &&
          marker != BK7258_JPEG_MARKER_SOS &&
          !bk7258_jpeg_decoder_is_header_segment(marker))
        {
          return -ENOTSUP;
        }

      if (input->length - offset < 2)
        {
          return -EINVAL;
        }

      segment_length = bk7258_jpeg_decoder_get_be16(&data[offset]);
      if (segment_length < 2 ||
          segment_length > input->length - offset)
        {
          return -EINVAL;
        }

      if (marker == BK7258_JPEG_MARKER_SOS)
        {
          if (!sof_seen)
            {
              return -EINVAL;
            }

          if (segment_length != 12u || dqt_seen != BK7258_JPEG_DQT_REQUIRED ||
              dht_seen != BK7258_JPEG_DHT_REQUIRED)
            {
              return -ENOTSUP;
            }

          scan = &data[offset + 2];
          if (!bk7258_jpeg_decoder_scan_matches(component_ids, scan))
            {
              return -ENOTSUP;
            }

          ret = bk7258_jpeg_decoder_scan_entropy(
            data, input->length, offset + segment_length);
          if (ret < 0)
            {
              return ret;
            }

          *info = parsed;
          return 0;
        }

      if (marker == BK7258_JPEG_MARKER_DQT)
        {
          ret = bk7258_jpeg_decoder_validate_dqt(&data[offset],
                                                 segment_length,
                                                 &dqt_seen);
          if (ret < 0)
            {
              return ret;
            }

          offset += segment_length;
          continue;
        }

      if (marker == BK7258_JPEG_MARKER_DHT)
        {
          ret = bk7258_jpeg_decoder_validate_dht(&data[offset],
                                                 segment_length,
                                                 &dht_seen);
          if (ret < 0)
            {
              return ret;
            }

          offset += segment_length;
          continue;
        }

      if (marker == BK7258_JPEG_MARKER_DRI)
        {
          if (segment_length != 4u)
            {
              return -ENOTSUP;
            }

          offset += segment_length;
          continue;
        }

      if (marker != BK7258_JPEG_MARKER_SOF0)
        {
          offset += segment_length;
          continue;
        }

      if (sof_seen)
        {
          return -ENOTSUP;
        }

      if (segment_length != BK7258_JPEG_SOF0_LENGTH)
        {
          return -ENOTSUP;
        }

      sof = &data[offset + 2];
      if (sof[0] != BK7258_JPEG_SOF0_PRECISION ||
          sof[5] != BK7258_JPEG_SOF0_COMPONENTS)
        {
          return -ENOTSUP;
        }

      height = bk7258_jpeg_decoder_get_be16(&sof[1]);
      width = bk7258_jpeg_decoder_get_be16(&sof[3]);
      if (width == 0 || height == 0)
        {
          return -EINVAL;
        }

      /* The hardware path consumes components in Y, Cb, Cr order.  Require
       * unique IDs, one of the three supported Y sampling factors, 1x1
       * chroma sampling, and valid baseline quantization-table selectors.
       */

      if (sof[6] == sof[9] || sof[6] == sof[12] ||
          sof[9] == sof[12] || sof[8] != 0 || sof[11] != 1u ||
          sof[14] != 1u || sof[10] != BK7258_JPEG_SAMPLE_444 ||
          sof[13] != BK7258_JPEG_SAMPLE_444)
        {
          return -ENOTSUP;
        }

      y_sample = sof[7];
      switch (y_sample)
        {
          case BK7258_JPEG_SAMPLE_444:
            parsed.format = BK7258_JPEG_DECODER_YUV444;
            break;

          case BK7258_JPEG_SAMPLE_422:
            parsed.format = BK7258_JPEG_DECODER_YUV422;
            break;

          case BK7258_JPEG_SAMPLE_420:
            parsed.format = BK7258_JPEG_DECODER_YUV420;
            break;

          default:
            return -ENOTSUP;
        }

      component_ids[0] = sof[6];
      component_ids[1] = sof[9];
      component_ids[2] = sof[12];
      parsed.width = width;
      parsed.height = height;
      sof_seen = true;
      offset += segment_length;
    }

  return -EINVAL;
}

static int bk7258_jpeg_decoder_get_info_locked(
  FAR struct bk7258_jpeg_decoder_s *priv,
  FAR struct bk7258_jpeg_decoder_frame_s *input,
  FAR struct bk7258_jpeg_decoder_info_s *info)
{
  (void)priv;
  return bk7258_jpeg_decoder_parse_info(input, info);
}

int bk7258_jpeg_decoder_initialize(FAR struct bk7258_jpeg_decoder_s **out)
{
  bk7258_sdk_jpeg_decode_hw_config_t config = {0};
  bk7258_sdk_jpeg_decode_hw_handle_t handle = NULL;
  avdk_err_t sdkret;
  int cleanup_ret;
  int ret;

  if (out == NULL)
    {
      return -EINVAL;
    }

  *out = NULL;
  ret = nxmutex_lock(&g_bk7258_jpeg_decoder_lock);
  if (ret < 0)
    {
      return ret;
    }

  if (g_bk7258_jpeg_decoder_owner != NULL)
    {
      if (g_bk7258_jpeg_decoder_owner->orphaned)
        {
          /* Return the recovery-only handle so a caller that did not retain
           * it from the original failed initialize can retry uninitialize.
           */

          *out = g_bk7258_jpeg_decoder_owner;
          nxmutex_unlock(&g_bk7258_jpeg_decoder_lock);
          return -EIO;
        }

      nxmutex_unlock(&g_bk7258_jpeg_decoder_lock);
      return -EBUSY;
    }

  sdkret = bk_hardware_jpeg_decode_new(&handle, &config);
  ret = bk7258_jpeg_decoder_sdk_error(sdkret);
  if (ret < 0 || handle == NULL)
    {
      if (ret >= 0)
        {
          ret = -EIO;
        }

      if (handle != NULL)
        {
          sdkret = bk_jpeg_decode_hw_delete(handle);
          cleanup_ret = bk7258_jpeg_decoder_sdk_error(sdkret);
          if (cleanup_ret < 0)
            {
              *out = bk7258_jpeg_decoder_retain_orphan_locked(handle,
                                                              false);
              ret = cleanup_ret;
            }
        }

      nxmutex_unlock(&g_bk7258_jpeg_decoder_lock);
      return ret;
    }

  sdkret = bk_jpeg_decode_hw_open(handle);
  ret = bk7258_jpeg_decoder_sdk_error(sdkret);
  if (ret < 0)
    {
      /* A failed open leaves the SDK controller disabled on its normal
       * path, so delete is the documented failure cleanup. */

      sdkret = bk_jpeg_decode_hw_delete(handle);
      cleanup_ret = bk7258_jpeg_decoder_sdk_error(sdkret);
      if (cleanup_ret < 0)
        {
          *out = bk7258_jpeg_decoder_retain_orphan_locked(handle, false);
          ret = cleanup_ret;
        }

      nxmutex_unlock(&g_bk7258_jpeg_decoder_lock);
      return ret;
    }

  ret = bk7258_jpeg_decoder_route_to_cpu1();
  if (ret < 0)
    {
      /* The SDK open path has already registered its ISR and enabled the
       * CPU2 route.  Always close/delete on migration failure; close repeats
       * the SDK's CPU2 disable and the route helper clears any CPU1 partial
       * state. */

      sdkret = bk_jpeg_decode_hw_close(handle);
      cleanup_ret = bk7258_jpeg_decoder_sdk_error(sdkret);
      if (cleanup_ret < 0)
        {
          *out = bk7258_jpeg_decoder_retain_orphan_locked(handle, true);
          nxmutex_unlock(&g_bk7258_jpeg_decoder_lock);
          return cleanup_ret;
        }

      sdkret = bk_jpeg_decode_hw_delete(handle);
      cleanup_ret = bk7258_jpeg_decoder_sdk_error(sdkret);
      if (cleanup_ret < 0)
        {
          *out = bk7258_jpeg_decoder_retain_orphan_locked(handle, false);
          nxmutex_unlock(&g_bk7258_jpeg_decoder_lock);
          return cleanup_ret;
        }

      nxmutex_unlock(&g_bk7258_jpeg_decoder_lock);
      return -EIO;
    }

  g_bk7258_jpeg_decoder.handle = handle;
  g_bk7258_jpeg_decoder.bounce = NULL;
  g_bk7258_jpeg_decoder.bounce_capacity = 0;
  g_bk7258_jpeg_decoder.initialized = true;
  g_bk7258_jpeg_decoder.opened = true;
  g_bk7258_jpeg_decoder.cpu1_route_enabled = true;
  g_bk7258_jpeg_decoder.operation_active = false;
  g_bk7258_jpeg_decoder.faulted = false;
  g_bk7258_jpeg_decoder.orphaned = false;
  g_bk7258_jpeg_decoder_owner = &g_bk7258_jpeg_decoder;
  *out = g_bk7258_jpeg_decoder_owner;

  nxmutex_unlock(&g_bk7258_jpeg_decoder_lock);
  return 0;
}

int bk7258_jpeg_decoder_uninitialize(FAR struct bk7258_jpeg_decoder_s *priv)
{
  avdk_err_t sdkret;
  int ret;

  if (priv == NULL)
    {
      return -EINVAL;
    }

  ret = nxmutex_lock(&g_bk7258_jpeg_decoder_lock);
  if (ret < 0)
    {
      return ret;
    }

  if (priv != g_bk7258_jpeg_decoder_owner || !priv->initialized)
    {
      nxmutex_unlock(&g_bk7258_jpeg_decoder_lock);
      return -EINVAL;
    }

  if (priv->operation_active)
    {
      nxmutex_unlock(&g_bk7258_jpeg_decoder_lock);
      return -EBUSY;
    }

  if (priv->opened)
    {
      sdkret = bk_jpeg_decode_hw_close(priv->handle);
      ret = bk7258_jpeg_decoder_sdk_error(sdkret);
      if (ret < 0)
        {
          nxmutex_unlock(&g_bk7258_jpeg_decoder_lock);
          return ret;
        }

      priv->opened = false;
    }

  if (priv->cpu1_route_enabled)
    {
      ret = bk7258_jpeg_decoder_route_from_cpu1();
      if (ret < 0)
        {
          /* Keep ownership and the closed handle so a later uninitialize
           * can retry the CPU1 route disable before deleting the controller. */
          nxmutex_unlock(&g_bk7258_jpeg_decoder_lock);
          return ret;
        }

      priv->cpu1_route_enabled = false;
    }

  sdkret = bk_jpeg_decode_hw_delete(priv->handle);
  ret = bk7258_jpeg_decoder_sdk_error(sdkret);
  if (ret < 0)
    {
      /* Keep ownership and the closed handle so a later uninitialize can
       * retry deletion without allowing a second SDK instance. */
      nxmutex_unlock(&g_bk7258_jpeg_decoder_lock);
      return ret;
    }

  if (priv->bounce != NULL)
    {
      kmm_free(priv->bounce);
      priv->bounce = NULL;
    }

  priv->bounce_capacity = 0;
  priv->handle = NULL;
  priv->initialized = false;
  priv->operation_active = false;
  priv->faulted = false;
  priv->orphaned = false;
  g_bk7258_jpeg_decoder_owner = NULL;

  nxmutex_unlock(&g_bk7258_jpeg_decoder_lock);
  return 0;
}

int bk7258_jpeg_decoder_get_info(
  FAR struct bk7258_jpeg_decoder_s *priv,
  FAR struct bk7258_jpeg_decoder_frame_s *input,
  FAR struct bk7258_jpeg_decoder_info_s *info)
{
  int ret;

  if (info == NULL)
    {
      return -EINVAL;
    }

  ret = nxmutex_lock(&g_bk7258_jpeg_decoder_lock);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_jpeg_decoder_check_locked(priv);
  if (ret >= 0)
    {
      ret = bk7258_jpeg_decoder_get_info_locked(priv, input, info);
    }

  nxmutex_unlock(&g_bk7258_jpeg_decoder_lock);
  return ret;
}

int bk7258_jpeg_decoder_decode(
  FAR struct bk7258_jpeg_decoder_s *priv,
  FAR struct bk7258_jpeg_decoder_frame_s *input,
  FAR struct bk7258_jpeg_decoder_frame_s *output)
{
  struct bk7258_jpeg_decoder_frame_s guarded_input;
  frame_buffer_t sdk_input;
  frame_buffer_t sdk_output;
  struct bk7258_jpeg_decoder_info_s info;
  uint64_t output_bytes;
  uint32_t output_cache_span;
  uintptr_t output_start;
  avdk_err_t sdkret;
  int ret;

  if (output != NULL)
    {
      output->length = 0;
    }

  ret = bk7258_jpeg_decoder_validate_input(input);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_jpeg_decoder_validate_output(output);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_jpeg_decoder_validate_no_overlap(input, output);
  if (ret < 0)
    {
      return ret;
    }

  ret = nxmutex_lock(&g_bk7258_jpeg_decoder_lock);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_jpeg_decoder_check_locked(priv);
  if (ret < 0)
    {
      goto out_unlock;
    }

  if (priv->operation_active)
    {
      ret = -EBUSY;
      goto out_unlock;
    }

  ret = bk7258_jpeg_decoder_get_info_locked(priv, input, &info);
  if (ret < 0)
    {
      goto out_unlock;
    }

  output_bytes = (uint64_t)info.width * info.height *
                 BK7258_JPEG_YUV_PIXEL_BYTES;
  if (output_bytes > UINT32_MAX || output_bytes > output->capacity)
    {
      ret = -ENOSPC;
      goto out_unlock;
    }

  ret = bk7258_jpeg_decoder_output_cache_span(
    output, (size_t)output_bytes, &output_cache_span);
  if (ret < 0)
    {
      goto out_unlock;
    }

  ret = bk7258_jpeg_decoder_prepare_bounce(priv, input);
  if (ret < 0)
    {
      goto out_unlock;
    }

  guarded_input = *input;
  guarded_input.data = priv->bounce;
  guarded_input.capacity = priv->bounce_capacity;
  ret = bk7258_jpeg_decoder_make_input(&guarded_input, &sdk_input);
  if (ret < 0)
    {
      goto out_unlock;
    }

  ret = bk7258_jpeg_decoder_make_output(output, &sdk_output);
  if (ret < 0)
    {
      goto out_unlock;
    }

  sdk_input.width = (uint16_t)info.width;
  sdk_input.height = (uint16_t)info.height;
  sdk_output.width = (uint16_t)info.width;
  sdk_output.height = (uint16_t)info.height;

  output_start = (uintptr_t)output->data;
  up_flush_dcache(output_start,
                  output_start + (uintptr_t)output_cache_span);

  priv->operation_active = true;
  sdkret = bk_jpeg_decode_hw_decode(priv->handle, &sdk_input, &sdk_output);
  priv->operation_active = false;

  /* A timeout or malformed stream can leave partially written cache lines.
   * Drop them even when the frame is returned as an error.
   */

  up_invalidate_dcache(output_start,
                       output_start + (uintptr_t)output_cache_span);

  ret = bk7258_jpeg_decoder_sdk_error(sdkret);
  if (ret >= 0 &&
      (sdk_output.width != info.width || sdk_output.height != info.height))
    {
      ret = -EIO;
    }

  if (ret < 0)
    {
      /* Some immediate SDK start failures leave its global state BUSY.  Do
       * not admit another frame through a possibly poisoned controller; the
       * sole media owner must uninitialize and create a fresh generation.
       */

      priv->faulted = true;
      ret = -EIO;
    }
  else
    {
      input->width = sdk_input.width;
      input->height = sdk_input.height;
      output->width = sdk_output.width;
      output->height = sdk_output.height;
      output->length = (uint32_t)output_bytes;
    }

out_unlock:
  nxmutex_unlock(&g_bk7258_jpeg_decoder_lock);
  return ret;
}
