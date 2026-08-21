/****************************************************************************
 * board/bk7258/chip/include/bk7258_board_binding.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Typed boundary between the reusable BK7258 AP lower halves and the
 * selected physical-board implementation.  Chip code must not include
 * arch/board/board.h or consume BK7258_BOARD_* macros for these devices.
 * The selected board contributes immutable descriptors and callbacks from
 * its AP bring-up source instead.
 ****************************************************************************/

#ifndef __ARCH_BK7258_BOARD_BINDING_H
#define __ARCH_BK7258_BOARD_BINDING_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/compiler.h>

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/****************************************************************************
 * Public Definitions
 ****************************************************************************/

#define BK7258_BINDING_VERSION         1u
#define BK7258_BINDING_GPIO_COUNT      56u

#define BK7258_MIC_BINDING_MIC1       (1u << 0)
#define BK7258_MIC_BINDING_MIC2       (1u << 1)

/* The config and binding records are intentionally versioned and sized.
 * This keeps a stale board object from being interpreted as a valid binding
 * when a board and chip archive are mixed during an incremental build.
 */

struct bk7258_mic_config_s
{
  uint16_t version;
  uint16_t size;
  uint8_t  channels;
  uint8_t  reserved[3];
  uint32_t flags;
  FAR const char *variant_name;
};

typedef int (*bk7258_mic_initialize_t)(void);

struct bk7258_mic_binding_s
{
  uint16_t version;
  uint16_t size;
  FAR const struct bk7258_mic_config_s *config;
  bk7258_mic_initialize_t initialize;
};

struct bk7258_sdio_config_s
{
  uint16_t version;
  uint16_t size;
  bool card_detect_available;
  uint8_t reserved[3];
  uint32_t media_poll_ms;
};

typedef int (*bk7258_sdio_initialize_t)(bool widebus);
typedef bool (*bk7258_sdio_card_present_t)(void);

struct bk7258_sdio_binding_s
{
  uint16_t version;
  uint16_t size;
  FAR const struct bk7258_sdio_config_s *config;
  bk7258_sdio_initialize_t initialize;
  bk7258_sdio_card_present_t card_present;
};

struct bk7258_aud_config_s
{
  uint16_t version;
  uint16_t size;
  FAR const char *variant_name;
  uint32_t speaker_control_gpio;
  uint32_t speaker_on_delay_ms;
  uint32_t speaker_off_delay_ms;
  bool speaker_active_high;
};

typedef int (*bk7258_aud_initialize_t)(
  FAR const struct bk7258_aud_config_s *config);
typedef int (*bk7258_aud_set_t)(FAR const struct bk7258_aud_config_s *config,
                                bool enable);
typedef bool (*bk7258_aud_is_enabled_t)(
  FAR const struct bk7258_aud_config_s *config);

struct bk7258_aud_binding_s
{
  uint16_t version;
  uint16_t size;
  FAR const struct bk7258_aud_config_s *config;
  bk7258_aud_initialize_t initialize;
  bk7258_aud_set_t set;
  bk7258_aud_is_enabled_t is_enabled;
};

typedef int (*bk7258_board_initialize_t)(void);

/* One aggregate avoids a family of weak board symbols and makes an absent
 * physical binding explicit.  A schematic-only board may return a record
 * with every member NULL; chip users then fail closed with -ENODEV.
 */

struct bk7258_board_binding_s
{
  uint16_t version;
  uint16_t size;
  FAR const struct bk7258_mic_binding_s *mic;
  FAR const struct bk7258_sdio_binding_s *sdio;
  FAR const struct bk7258_aud_binding_s *audio;
  bk7258_board_initialize_t early_initialize;
  bk7258_board_initialize_t devices_initialize;
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

FAR const struct bk7258_board_binding_s *bk7258_board_get_binding(void);

/* Defined by the selected physical audio source whenever BK7258_AUD is
 * enabled.  The selected board's aggregate references this immutable record.
 */

extern const struct bk7258_aud_binding_s g_bk7258_board_audio_binding;

#ifdef __cplusplus
}
#endif

#endif /* __ARCH_BK7258_BOARD_BINDING_H */
