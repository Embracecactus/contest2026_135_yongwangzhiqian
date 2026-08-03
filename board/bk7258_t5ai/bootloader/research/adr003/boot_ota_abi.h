/*
 * boot_ota_abi.h - BK7258 N15 paired-OTA persistent metadata ABI.
 *
 * This is repository-owned ABI.  It does not modify or depend on the
 * official NuttX/apps/SDK source.  Every persistent object is little-endian
 * and occupies one or more complete v3.1.1.9 32-byte SDK write chunks.
 *
 * N15-R2 safety gate: defining this ABI does not enable a Flash write path.
 */

#ifndef __BOOTLOADER_BOOT_OTA_ABI_H
#define __BOOTLOADER_BOOT_OTA_ABI_H

#include <stddef.h>
#include <stdint.h>

/* Fixed raw-physical Flash layout. */

#define BK7258_OTA_FLASH_ID              0x00c86517u
#define BK7258_OTA_FLASH_SIZE            0x00800000u
#define BK7258_OTA_ERASE_SIZE            0x00001000u
#define BK7258_OTA_WRITE_CHUNK_SIZE      0x00000020u

#define BK7258_OTA_CP_ACTIVE_START       0x00011000u
#define BK7258_OTA_CP_STAGING_START      0x00440000u
#define BK7258_OTA_CP_SLOT_SIZE          0x000ef000u
#define BK7258_OTA_AP_ACTIVE_START       0x00220000u
#define BK7258_OTA_AP_STAGING_START      0x0052f000u
#define BK7258_OTA_AP_SLOT_SIZE          0x00220000u

#define BK7258_OTA_FORWARD_LOG0_START    0x0074f000u
#define BK7258_OTA_FORWARD_LOG1_START    0x00762000u
#define BK7258_OTA_REVERSE_LOG0_START    0x00775000u
#define BK7258_OTA_REVERSE_LOG1_START    0x00788000u
#define BK7258_OTA_JOURNAL_COPY_SIZE     0x00013000u
#define BK7258_OTA_SCRATCH_START         0x0079b000u
#define BK7258_OTA_RESERVED_START        0x0079c000u
#define BK7258_OTA_OFFICIAL_TAIL_START   0x007fa000u

#define BK7258_OTA_PAIR_SECTORS          0x0000030fu
#define BK7258_OTA_PHASES_PER_SECTOR     3u
#define BK7258_OTA_PHASE_MARKERS         0x0000092du

/* Persistent journal layout inside each 0x13000-byte copy. */

#define BK7258_OTA_HEADER_SIZE           0x00000100u
#define BK7258_OTA_CONTROL_SIZE          0x00000100u
#define BK7258_OTA_PHASE_OFFSET          0x00000200u
#define BK7258_OTA_MARKER_SIZE           BK7258_OTA_WRITE_CHUNK_SIZE

#define BK7258_OTA_CONTROL_ARM_OFFSET       0x100u
#define BK7258_OTA_CONTROL_COMPLETE_OFFSET  0x120u
#define BK7258_OTA_CONTROL_TRIAL_OFFSET     0x140u
#define BK7258_OTA_CONTROL_CONFIRM_OFFSET   0x160u
#define BK7258_OTA_CONTROL_ROLLBACK_OFFSET  0x180u
#define BK7258_OTA_CONTROL_RETIRED_OFFSET   0x1a0u

/* Little-endian ASCII: "BKJ1" and "BKM1". */

#define BK7258_OTA_HEADER_MAGIC          0x314a4b42u
#define BK7258_OTA_MARKER_MAGIC          0x314d4b42u
#define BK7258_OTA_FORMAT_VERSION        1u

#define BK7258_OTA_FLAG_PAIRED           (1u << 0)
#define BK7258_OTA_FLAG_ONE_TRIAL        (1u << 1)
#define BK7258_OTA_FLAG_INTEGRITY_ONLY   (1u << 2)
#define BK7258_OTA_REQUIRED_FLAGS        \
    (BK7258_OTA_FLAG_PAIRED | BK7258_OTA_FLAG_ONE_TRIAL | \
     BK7258_OTA_FLAG_INTEGRITY_ONLY)

/* Slot bytes are the exact raw-physical, CRC-expanded bytes to swap. */

#define BK7258_OTA_IMAGE_ENCODING_CRC_PHYSICAL 1u

enum bk7258_ota_marker_kind_e {
    BK7258_OTA_MARKER_ARM = 1,
    BK7258_OTA_MARKER_DIRECTION_COMPLETE = 2,
    BK7258_OTA_MARKER_TRIAL_STARTED = 3,
    BK7258_OTA_MARKER_CONFIRMED = 4,
    BK7258_OTA_MARKER_ROLLBACK_REQUESTED = 5,
    BK7258_OTA_MARKER_RETIRED = 6,
    BK7258_OTA_MARKER_SCRATCH_READY = 0x100,
    BK7258_OTA_MARKER_ACTIVE_REPLACED = 0x101,
    BK7258_OTA_MARKER_STAGING_REPLACED = 0x102,
};

/*
 * All four journal copies contain an identical immutable header.  Direction
 * and mirror index are inferred from the fixed physical address, so a header
 * cannot be transplanted between transactions by merely changing those
 * fields.  The CRC32 covers bytes [0x00, 0xfc); reserved bytes must be 0xff.
 *
 * The three SHA-256 values provide corruption detection only.  There is no
 * signature, publisher authentication, or anti-rollback security claim.
 */

struct bk7258_ota_journal_header_v1 {
    uint32_t magic;                   /* 0x00 */
    uint16_t format_version;          /* 0x04 */
    uint16_t header_size;             /* 0x06 */
    uint32_t journal_copy_size;       /* 0x08 */
    uint32_t flags;                   /* 0x0c */
    uint64_t sequence;                /* 0x10 */
    uint64_t generation;              /* 0x18 */
    uint32_t flash_id;                /* 0x20 */
    uint32_t erase_size;              /* 0x24 */
    uint32_t write_chunk_size;        /* 0x28 */
    uint32_t phase_offset;            /* 0x2c */
    uint32_t phase_marker_count;      /* 0x30 */
    uint32_t pair_sector_count;       /* 0x34 */
    uint32_t phases_per_sector;       /* 0x38 */
    uint32_t scratch_start;           /* 0x3c */
    uint32_t cp_active_start;         /* 0x40 */
    uint32_t cp_staging_start;        /* 0x44 */
    uint32_t cp_slot_size;            /* 0x48 */
    uint32_t cp_image_size;           /* 0x4c */
    uint32_t ap_active_start;         /* 0x50 */
    uint32_t ap_staging_start;        /* 0x54 */
    uint32_t ap_slot_size;            /* 0x58 */
    uint32_t ap_image_size;           /* 0x5c */
    uint32_t flash_status_before;     /* 0x60, low 16 bits are meaningful */
    uint32_t image_encoding;          /* 0x64 */
    uint8_t pair_digest[32];          /* 0x68 */
    uint8_t cp_slot_digest[32];       /* 0x88, includes erased slot padding */
    uint8_t ap_slot_digest[32];       /* 0xa8, includes erased slot padding */
    uint8_t reserved[52];             /* 0xc8, must remain 0xff in v1 */
    uint32_t header_crc32;            /* 0xfc */
};

/*
 * One marker occupies exactly one SDK write chunk.  CRC32 covers bytes
 * [0x00, 0x1c).  A marker is committed only when its entire expected byte
 * string validates; the erased representation is 32 bytes of 0xff.
 */

struct bk7258_ota_journal_marker_v1 {
    uint32_t magic;                   /* 0x00 */
    uint16_t format_version;          /* 0x04 */
    uint16_t kind;                    /* 0x06 */
    uint64_t sequence;                /* 0x08 */
    uint64_t generation;              /* 0x10 */
    uint32_t ordinal;                 /* 0x18 */
    uint32_t marker_crc32;            /* 0x1c */
};

_Static_assert(sizeof(struct bk7258_ota_journal_header_v1) ==
               BK7258_OTA_HEADER_SIZE,
               "N15 journal header ABI drift");
_Static_assert(offsetof(struct bk7258_ota_journal_header_v1, header_crc32) ==
               0xfcu,
               "N15 journal header CRC offset drift");
_Static_assert(sizeof(struct bk7258_ota_journal_marker_v1) ==
               BK7258_OTA_MARKER_SIZE,
               "N15 journal marker ABI drift");
_Static_assert(offsetof(struct bk7258_ota_journal_marker_v1, marker_crc32) ==
               0x1cu,
               "N15 journal marker CRC offset drift");

#endif /* __BOOTLOADER_BOOT_OTA_ABI_H */
