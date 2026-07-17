/*
 * boot_main.c - BK7258 Tier-1 bootloader C main.
 *
 *   uint32_t c_main(void);
 *
 * Responsibilities (Tier-1 features I / A, with J handled by start.S):
 *   I  UART1 logging of boot progress.
 *   A  FAL partition table parse -> locate "app", derive its logical address.
 *      App header validation (MSP / Reset Thumb / "BK7236\0\0" magic).
 * On success c_main prints "jump to:0x02010000\r\n", "JMP\r\n" and returns
 * the app logical vector address (e.g. 0x02010000). On failure it prints
 * "BAD\r\n" plus a short reason and loops forever (never returns).
 *
 * The app logical address equals FLASH_BASE + partition.offset, so the same
 * scan works if the table is later moved to a real flash partition.
 *
 * Freestanding: no libc, no mutable globals -> no .bss zeroing needed by
 * c_main (start.S calls c_main directly without a C runtime startup).
 */

#include <stdint.h>
#include <stddef.h>

/* ------------------------------------------------------------------ */
/* MMIO: BK7258 UART1 (matches the verified init in start.S).         */
/* ------------------------------------------------------------------ */
#define UART1_FIFO_ADDR   0x4583001Cu
#define UART1_STATUS_ADDR 0x45830018u
#define UART1_TX_READY    (1u << 20)   /* bit20: TX-FIFO-not-full (assumed) */

#define REG32(addr)       (*(volatile uint32_t *)(addr))
#define UART1_FIFO        REG32(UART1_FIFO_ADDR)
#define UART1_STATUS      REG32(UART1_STATUS_ADDR)

/* ------------------------------------------------------------------ */
/* FAL partition table (mirrors struct fal_partition from the BK SDK,  */
/* fal_def.h: magic_word/name[24]/flash_name[24]/offset/len/reserved, */
/* 64 bytes per entry). XIP-readable const in .rodata.                 */
/* ------------------------------------------------------------------ */
#define FAL_DEV_NAME_MAX  24
#define FAL_PART_MAGIC    0x45503130u   /* 'E','P','1','0' (fal_partition.c) */

#define FLASH_BASE        0x02000000u

struct fal_partition {
    uint32_t magic_word;
    char     name[FAL_DEV_NAME_MAX];
    char     flash_name[FAL_DEV_NAME_MAX];
    long     offset;        /* logical offset on flash device */
    size_t   len;
    uint32_t reserved;
};

__attribute__((used))
const struct fal_partition fal_partition_table[] = {
    { FAL_PART_MAGIC, "bootloader", "beken_onchip_crc", 0x00000L, 0x10000L, 0u },
    { FAL_PART_MAGIC, "app",        "beken_onchip_crc", 0x10000L, 0x10000L, 0u },
};
#define FAL_PART_COUNT  (sizeof(fal_partition_table) / sizeof(fal_partition_table[0]))

/* ------------------------------------------------------------------ */
/* UART output (self-implemented; correct nibble order, no v>>32 UB).  */
/* ------------------------------------------------------------------ */
static void uart_putc(char c)
{
    /*
     * Poll bit20 (TX-FIFO-not-full). The poll is bounded: if the bit polarity
     * is ever inverted on silicon, we degrade gracefully to the same
     * write-through behavior the verified minimal bootloader used, rather
     * than hanging the boot.
     */
    for (int i = 0; i < 100000; i++) {
        if (UART1_STATUS & UART1_TX_READY) {
            break;
        }
    }
    UART1_FIFO = (uint32_t)(uint8_t)c;
}

static void uart_puts(const char *s)
{
    while (*s) {
        uart_putc(*s++);
    }
}

static void print_hex32(uint32_t v)
{
    static const char hex[] = "0123456789ABCDEF";
    int s;
    /* for(s=28; s>=0; s-=4): avoids the v>>32 undefined behaviour that the
       earlier probe fell into (top nibble got shifted into oblivion). */
    for (s = 28; s >= 0; s -= 4) {
        uart_putc(hex[(v >> s) & 0xFu]);
    }
}

static void log_u32(const char *label, uint32_t value)
{
    uart_puts(label);
    uart_puts("0x");
    print_hex32(value);
    uart_puts("\r\n");
}

/* ------------------------------------------------------------------ */
/* Partition scan + helpers.                                          */
/* ------------------------------------------------------------------ */
static int name_eq(const char *a, const char *b)
{
    int i;
    for (i = 0; i < FAL_DEV_NAME_MAX; i++) {
        if (a[i] != b[i]) {
            return 0;
        }
        if (a[i] == '\0') {
            return 1;
        }
    }
    return 1;   /* equal across the full FAL_DEV_NAME_MAX width */
}

static const struct fal_partition *fal_find(const char *name)
{
    size_t i;
    for (i = 0; i < FAL_PART_COUNT; i++) {
        if (fal_partition_table[i].magic_word != FAL_PART_MAGIC) {
            continue;
        }
        if (name_eq(fal_partition_table[i].name, name)) {
            return &fal_partition_table[i];
        }
    }
    return (const struct fal_partition *)0;
}

/* ------------------------------------------------------------------ */
/* App header validation (ported from bk7236_min_bl.S to C).          */
/* ------------------------------------------------------------------ */
static int validate_app(uint32_t app_vec)
{
    volatile uint32_t *vec   = (volatile uint32_t *)app_vec;
    volatile uint32_t *magic = (volatile uint32_t *)(app_vec + 0x100u);
    uint32_t msp  = vec[0];
    uint32_t rst  = vec[1];

    /* MSP must land in SRAM [0x28000000 .. 0x280A0000]. */
    if (msp < 0x28000000u || msp > 0x280A0000u) {
        uart_puts("BAD\r\nmsp OOR\r\n");
        return 0;
    }
    /* Reset_Handler must have the Thumb bit set. */
    if ((rst & 1u) == 0u) {
        uart_puts("BAD\r\nreset no-thumb\r\n");
        return 0;
    }
    /* app magic @ app_vec + 0x100 == "BK7236\0\0"
       bytes  42 4B 37 32 -> word 0x32374B42
              33 36 00 00 -> word 0x00003633 */
    if (magic[0] != 0x32374B42u) {
        uart_puts("BAD\r\nmagic0\r\n");
        return 0;
    }
    if (magic[1] != 0x00003633u) {
        uart_puts("BAD\r\nmagic1\r\n");
        return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* C entry called from start.S. Returns app logical vector address.   */
/* ------------------------------------------------------------------ */
__attribute__((used))
uint32_t c_main(void)
{
    const struct fal_partition *app;
    uint32_t app_vec;

    uart_puts("u_bootloader enter\r\n");

    /* A: FAL partition parse -> find "app". */
    app = fal_find("app");
    if (app == (const struct fal_partition *)0) {
        uart_puts("BAD\r\nno app part\r\n");
        for (;;) { }
    }
    app_vec = FLASH_BASE + (uint32_t)app->offset;
    log_u32("partition app @ ", app_vec);

    /* Validate app header. */
    if (!validate_app(app_vec)) {
        for (;;) { }
    }

    log_u32("jump to:", app_vec);
    uart_puts("JMP\r\n");
    return app_vec;
}
