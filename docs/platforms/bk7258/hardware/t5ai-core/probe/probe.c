/* SPDX-License-Identifier: Apache-2.0 */
/*
 * probe.c - BK7258 (T5AI-Core, tri-core Cortex-M33) minimal bare probe.
 *
 * Linked to flash/XIP logical 0x02010000 (the app Reset_Handler entry the
 * bootloader jumps to after validating the app magic). On reset it reads the
 * current core number, the SCB CPUID and the SCB VTOR, prints them over UART1
 * (115200 8N1), then halts. One image verifies:
 *   - the new linker script (FLASH @ 0x02010000, RAM @ 0x28000000)
 *   - the vector table layout (incl. app magic "BK7236\0\0" at offset 0x100)
 *   - the UART1 early-print path (no libc, no interrupts)
 *   - the bootloader's actual jump landing in our Reset_Handler
 *
 * The addresses and registers below record the historical probe contract.
 *
 * Freestanding: no libc, no headers.
 */

/* ---- Symbols exported by probe.ld ---- */
extern unsigned int __data_load_start__;
extern unsigned int __data_start__;
extern unsigned int __data_end__;
extern unsigned int __bss_start__;
extern unsigned int __bss_end__;

/* ---- Fixed memory-mapped registers / words ---- */
#define CPU_ID_WORD  (*(volatile unsigned int *)0x20000000u) /* SW core-id: 0/1/2 = CPU0/1/2 */
#define SCB_CPUID    (*(volatile unsigned int *)0xE000ED00u)
#define SCB_VTOR     (*(volatile unsigned int *)0xE000ED08u)
#define SCB_CPACR    (*(volatile unsigned int *)0xE000ED88u)

#define UART1_CFG         (*(volatile unsigned int *)0x45830010u)
#define UART1_FIFO_STAT   (*(volatile unsigned int *)0x45830018u)
#define UART1_FIFO_PORT   (*(volatile unsigned int *)0x4583001Cu)
#define UART1_FIFO_READY  (1u << 20)            /* fifo_status.bit20 = fifo_wr_ready */

#define VTOR_VALUE        0x02010000u           /* our vector table, in flash/XIP */

/* ---- Handlers ---- */
void Reset_Handler(void);

static void Default_Handler(void)
{
    for (;;) { }
}

/* ---- Bare UART output (polled, no libc) ---- */
static void uart_putc(unsigned char c)
{
    while ((UART1_FIFO_STAT & UART1_FIFO_READY) == 0) { }
    UART1_FIFO_PORT = (unsigned int)(c & 0xFFu);
}

static void uart_puts(const char *s)
{
    while (*s)
    {
        uart_putc((unsigned char)*s);
        s++;
    }
}

static void print_hex32(unsigned int v)
{
    /* "0123456789ABCDEF" lives in .rodata (flash/XIP), not in .data/.bss. */
    static const char hexdigits[] = "0123456789ABCDEF";
    int shift;
    for (shift = 28; shift >= 0; shift -= 8)
    {
        uart_putc((unsigned char)hexdigits[(v >> (shift + 4)) & 0xFu]);
        uart_putc((unsigned char)hexdigits[(v >> shift) & 0xFu]);
    }
}

/*
 * Vector table, 66 entries, placed at the very start of the image via
 * section .vectors (pinned at 0x02010000 by probe.ld).
 *
 *   [0]      initial MSP
 *   [1]      Reset_Handler (Thumb; toolchain sets bit0 of the stored address)
 *   [2..15]  14 system exception handlers (NMI .. SysTick)
 *   [16..63] 48 IRQ handlers (IRQ0 .. IRQ47)
 *   [64..65] app magic "BK7236\0\0" (little-endian)
 *
 * Entry [64] sits at byte offset 0x100 in the image; the bootloader reads the
 * magic there before jumping to slot [1].
 */
__attribute__((section(".vectors"), used))
const unsigned int vector_table[66] = {
    0x2809FFFCu,                    /* [0]  initial MSP */
    (unsigned int)Reset_Handler,    /* [1]  reset */
    /* [2..15] system exceptions (14) */
    (unsigned int)Default_Handler,  /* [2]  NMI */
    (unsigned int)Default_Handler,  /* [3]  HardFault */
    (unsigned int)Default_Handler,  /* [4]  MemManage */
    (unsigned int)Default_Handler,  /* [5]  BusFault */
    (unsigned int)Default_Handler,  /* [6]  UsageFault */
    (unsigned int)Default_Handler,  /* [7]  SecureFault (ARMv8-M) */
    (unsigned int)Default_Handler,  /* [8]  Reserved */
    (unsigned int)Default_Handler,  /* [9]  Reserved */
    (unsigned int)Default_Handler,  /* [10] Reserved */
    (unsigned int)Default_Handler,  /* [11] SVCall */
    (unsigned int)Default_Handler,  /* [12] DebugMonitor */
    (unsigned int)Default_Handler,  /* [13] Reserved */
    (unsigned int)Default_Handler,  /* [14] PendSV */
    (unsigned int)Default_Handler,  /* [15] SysTick */
    /* [16..63] external IRQs (48) */
    (unsigned int)Default_Handler,  /* [16] IRQ0  */
    (unsigned int)Default_Handler,  /* [17] IRQ1  */
    (unsigned int)Default_Handler,  /* [18] IRQ2  */
    (unsigned int)Default_Handler,  /* [19] IRQ3  */
    (unsigned int)Default_Handler,  /* [20] IRQ4  */
    (unsigned int)Default_Handler,  /* [21] IRQ5  */
    (unsigned int)Default_Handler,  /* [22] IRQ6  */
    (unsigned int)Default_Handler,  /* [23] IRQ7  */
    (unsigned int)Default_Handler,  /* [24] IRQ8  */
    (unsigned int)Default_Handler,  /* [25] IRQ9  */
    (unsigned int)Default_Handler,  /* [26] IRQ10 */
    (unsigned int)Default_Handler,  /* [27] IRQ11 */
    (unsigned int)Default_Handler,  /* [28] IRQ12 */
    (unsigned int)Default_Handler,  /* [29] IRQ13 */
    (unsigned int)Default_Handler,  /* [30] IRQ14 */
    (unsigned int)Default_Handler,  /* [31] IRQ15 */
    (unsigned int)Default_Handler,  /* [32] IRQ16 */
    (unsigned int)Default_Handler,  /* [33] IRQ17 */
    (unsigned int)Default_Handler,  /* [34] IRQ18 */
    (unsigned int)Default_Handler,  /* [35] IRQ19 */
    (unsigned int)Default_Handler,  /* [36] IRQ20 */
    (unsigned int)Default_Handler,  /* [37] IRQ21 */
    (unsigned int)Default_Handler,  /* [38] IRQ22 */
    (unsigned int)Default_Handler,  /* [39] IRQ23 */
    (unsigned int)Default_Handler,  /* [40] IRQ24 */
    (unsigned int)Default_Handler,  /* [41] IRQ25 */
    (unsigned int)Default_Handler,  /* [42] IRQ26 */
    (unsigned int)Default_Handler,  /* [43] IRQ27 */
    (unsigned int)Default_Handler,  /* [44] IRQ28 */
    (unsigned int)Default_Handler,  /* [45] IRQ29 */
    (unsigned int)Default_Handler,  /* [46] IRQ30 */
    (unsigned int)Default_Handler,  /* [47] IRQ31 */
    (unsigned int)Default_Handler,  /* [48] IRQ32 */
    (unsigned int)Default_Handler,  /* [49] IRQ33 */
    (unsigned int)Default_Handler,  /* [50] IRQ34 */
    (unsigned int)Default_Handler,  /* [51] IRQ35 */
    (unsigned int)Default_Handler,  /* [52] IRQ36 */
    (unsigned int)Default_Handler,  /* [53] IRQ37 */
    (unsigned int)Default_Handler,  /* [54] IRQ38 */
    (unsigned int)Default_Handler,  /* [55] IRQ39 */
    (unsigned int)Default_Handler,  /* [56] IRQ40 */
    (unsigned int)Default_Handler,  /* [57] IRQ41 */
    (unsigned int)Default_Handler,  /* [58] IRQ42 */
    (unsigned int)Default_Handler,  /* [59] IRQ43 */
    (unsigned int)Default_Handler,  /* [60] IRQ44 */
    (unsigned int)Default_Handler,  /* [61] IRQ45 */
    (unsigned int)Default_Handler,  /* [62] IRQ46 */
    (unsigned int)Default_Handler,  /* [63] IRQ47 */
    /* [64..65] app magic, little-endian: 'B''K''7''2' | '3''6''\0''\0' */
    0x32374B42u,                    /* [64] "BK72" */
    0x00003633u,                    /* [65] "36\0\0" */
};

/*
 * Reset handler. By the time we get here the hardware has loaded MSP from
 * vector table slot 0; we run with interrupts masked, on the same stack the
 * NuttX BSP will later reuse.
 */
void Reset_Handler(void)
{
    unsigned int core, cpuid, vtor;
    unsigned int *src;
    unsigned int *dst;

    __asm volatile("cpsid i");

    /* Relocate VTOR to our table in flash/XIP. */
    SCB_VTOR = VTOR_VALUE;
    __asm volatile("dsb; isb");

    /* Enable CP10/CP11 full access (FPU). Optional but cheap. */
    SCB_CPACR = SCB_CPACR | ((3u << 20) | (3u << 22));

    /*
     * .data copy + .bss zero. The probe intentionally keeps .data/.bss empty
     * (static-const strings + locals), so both loops are no-ops here, but the
     * scaffolding is kept for direct reuse when porting NuttX.
     */
    src = &__data_load_start__;
    dst = &__data_start__;
    while (dst < &__data_end__)
    {
        *dst = *src;
        dst++;
        src++;
    }
    dst = &__bss_start__;
    while (dst < &__bss_end__)
    {
        *dst = 0u;
        dst++;
    }

    /* Sample the three identifiers this probe exists to report. */
    core  = CPU_ID_WORD;
    cpuid = SCB_CPUID;
    vtor  = SCB_VTOR;

    /*
     * UART1: inherit the configuration left by the custom bootloader
     * (bk7236_min_bl.S). It already set up GPIO0/1 pinmux, the 26 MHz XTAL,
     * the clock gate, WDT keep-alive, and UART1 global_ctrl/config — observed
     * UART1 CFG on board is 0x00003719 (clk_div=0x37). Re-programming CFG
     * here would clash with the bootloader's actual divider; we only poke
     * fifo_port(0x4583001C), exactly what the Zephyr soc_reset_hook does.
     * See docs/12-custom-bootloader.md §6/§10.
     */

    uart_puts("BK7258 PROBE\r\n");
    uart_puts("core=0x");  print_hex32(core);  uart_puts("\r\n");
    uart_puts("cpuid=0x"); print_hex32(cpuid); uart_puts("\r\n");
    uart_puts("vtor=0x");  print_hex32(vtor);  uart_puts("\r\n");
    uart_puts("HALT\r\n");

    for (;;) { }
}
