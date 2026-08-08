/*
 * contest2026_135_yongwangzhiqian/board/bk7258_t5ai/tests/qemu_mbox_proxy/mbox_proxy.c
 *
 * QEMU mps2-an521 (dual Cortex-M33) behavioural proxy for the BK7258 RPTUN
 * MBOX0 notify logic implemented in:
 *   board/bk7258_t5ai/chip/common/bk7258_rptun_mbox.c
 *
 * This is a TEST/BOARD harness.  It does NOT compile or modify the real SDK or
 * firmware; it re-implements the *algorithm* (queue_notify OR-coalescing,
 * -EAGAIN retry, tx_complete re-wake, dispatch generation gate, 1 ms worker
 * poll) and runs it on two real cores so cross-core visibility, interrupts and
 * the one-deep mailbox race can be observed.  The wire frame mirrors the SDK's
 * 16-byte mb_chnl_cmd_t: { cmd=0xb9, type, generation, value }.
 *
 * Topology (mirrors CP/AP):
 *   core 0 = CP (sender): runs the notify state machine + worker
 *   core 1 = AP (receiver): stores incoming frames and dispatches them
 * Transport: a one-deep shared slot + ARM SSE-200 MHU doorbell interrupt.
 */

#include <stdint.h>
#include <stddef.h>

/* ------------------------------------------------------------------ */
/* MMIO / core primitives                                              */
/* ------------------------------------------------------------------ */

#define MHU0        0x40003000u   /* doorbell CP -> AP (AP gets IRQ6) */
#define MHU1        0x40004000u   /* doorbell AP -> CP (CP gets IRQ7) */
#define NVIC_ISER0  0xE000E100u
#define SYST_CSR    0xE000E010u
#define SYST_RVR    0xE000E014u
#define SYST_CVR    0xE000E018u
#define CPUWAIT     0x50021118u   /* secure SYSCTL CPUWAIT */
#define CPUID       0x4001F000u
#define UART0       0x40200000u   /* CMSDK PL011, captured by -nographic */

#define __DMB()  __asm volatile ("dmb sy" ::: "memory")
#define __DSB()  __asm volatile ("dsb sy" ::: "memory")
#define __SEV()  __asm volatile ("sev" ::: "memory")
#define __WFE()  __asm volatile ("wfe" ::: "memory")
#define __WFI()  __asm volatile ("wfi" ::: "memory")

static inline void mmio_wr(uint32_t reg, uint32_t off, uint32_t v)
{
  *(volatile uint32_t *)(reg + off) = v;
}
static inline uint32_t mmio_rd(uint32_t reg, uint32_t off)
{
  return *(volatile uint32_t *)(reg + off);
}

typedef uint32_t irqflags_t;
static inline irqflags_t irq_save(void)
{
  uint32_t f;
  __asm volatile ("mrs %0, primask" : "=r" (f));
  __asm volatile ("cpsid i" ::: "memory");
  return f;
}
static inline void irq_restore(irqflags_t f)
{
  __asm volatile ("msr primask, %0" :: "r" (f) : "memory");
}
static inline void irq_enable(uint32_t irq)
{
  mmio_wr(NVIC_ISER0, 0, 1u << irq);
}

/* UART0 is a CMSDK APB UART (NOT a PL011) on mps2-an521. */
#define UART_DATA  0x00   /* write: transmit register */
#define UART_STATE 0x04   /* bit0 = TXFULL */
#define UART_CTRL  0x08   /* bit0 = TXEN */
#define UART_BAUD  0x10   /* baud divisor (must be non-zero) */

static void uart_init(void)
{
  mmio_wr(UART0, UART_BAUD, 0x10);   /* baud divisor (non-zero) */
  mmio_wr(UART0, UART_CTRL, 0x1);    /* TX enable */
}
static void uart_putc(char c)
{
  /* Bounded poll so a misconfigured UART can never hang the test. */
  for (volatile uint32_t n = 0; n < 100000u; n++)
    {
      if ((mmio_rd(UART0, UART_STATE) & 0x1u) == 0)   /* TX not full */
        {
          break;
        }
    }
  mmio_wr(UART0, UART_DATA, (uint32_t)(unsigned char)c);
}
static void uart_puts(const char *s)
{
  while (*s)
    {
      uart_putc(*s++);
    }
}

/* Semihosting output (BKPT 0xAB) used only for the final exit code. */
static void sh_exit(int code)
{
  register uint32_t r0 __asm ("r0") = 0x18; /* SYS_EXIT */
  register uint32_t r1 __asm ("r1") = (uint32_t)&code;
  __asm volatile ("bkpt 0xab" :: "r" (r0), "r" (r1) : "memory");
}

/* ------------------------------------------------------------------ */
/* Protocol / error constants (mirror bk7258_rptun.h + errno)         */
/* ------------------------------------------------------------------ */

#define MBOX_CMD    0xb9u
#define TYPE_INVALID   0u
#define TYPE_LIFECYCLE 1u
#define TYPE_NOTIFY    2u
#define TYPE_HEARTBEAT 3u
#define TYPE_PROBE     4u
#define TYPE_COUNT     5u

#define GEN 1u   /* the boot generation both cores agree on */

#define BK_OK        0
#define BK_ERR_BUSY  1
#define ENODEV     19
#define EINVAL     22
#define EAGAIN     11
#define EIO        5
#define OK           0

typedef void (*notify_cb_t)(uint32_t generation, uint32_t value);

/* ------------------------------------------------------------------ */
/* Modeled SDK mb_chnl transport: a one-deep MBOX0 slot + MHU doorbell */
/* ------------------------------------------------------------------ */

volatile uint32_t g_send_ready;     /* CP produced, AP consumes */
uint32_t          g_send_frame[4];  /* {cmd,type,gen,value} */
volatile uint32_t g_ack_ready;      /* AP produced, CP consumes */
uint32_t          g_ack_val;

/* Test control: let the scenario force the transport into edge cases. */
volatile uint32_t g_force_busy;     /* simulate a full mailbox -> -EAGAIN */
volatile uint32_t g_suppress_ack;   /* simulate a lost/lost TX-complete */

/* CP-side state machine (mirrors bk7258_rptun_mbox.c globals) */
volatile uint32_t g_worker_wake;
volatile uint32_t g_ticks;
uint32_t          g_notify_pending;
uint32_t          g_notify_generation;
uint32_t          g_notify_value;
notify_cb_t       g_notify_cb;
uint32_t          g_initialized;
uint32_t          g_cp_send_count;   /* successful modeled writes */
uint32_t          g_cp_ack_count;    /* TX-complete (ack) callbacks */
volatile uint32_t g_cp_sweep;        /* sweep (value 0) callbacks */

/* AP-side results */
volatile uint32_t g_ap_alive;
volatile uint32_t g_boot_generation;
volatile uint32_t g_ap_pending;
uint32_t          g_ap_frame[4];
volatile uint32_t g_ap_recv_count;
volatile uint32_t g_ap_notify_count;
volatile uint32_t g_ap_notify_last;
volatile uint32_t g_ap_results[TYPE_COUNT];
volatile uint32_t g_ap_lifecycle;

/* Test reporting */
volatile uint32_t g_checks;
volatile uint32_t g_failures;

/* ------------------------------------------------------------------ */
/* Modeled mb_chnl_write (one-deep)                                    */
/* ------------------------------------------------------------------ */

static int mb_chnl_write(uint32_t chnl, uint32_t frame[4])
{
  (void)chnl;
  if (g_force_busy || g_send_ready)
    {
      return BK_ERR_BUSY;   /* caller maps this to -EAGAIN */
    }

  g_send_frame[0] = frame[0];
  g_send_frame[1] = frame[1];
  g_send_frame[2] = frame[2];
  g_send_frame[3] = frame[3];
  g_send_ready = 1;
  __DMB();
  mmio_wr(MHU0, 0x14, 1u);     /* CPU1INTR_SET -> core 1 IRQ6 */
  g_cp_send_count++;
  return BK_OK;
}

/* ------------------------------------------------------------------ */
/* State machine (ported 1:1 from bk7258_rptun_mbox.c)                 */
/* ------------------------------------------------------------------ */

static int  bk_send_wrapped(uint32_t type, uint32_t generation, uint32_t value);
static void tx_complete(void);

static void queue_notify(uint32_t generation, uint32_t value, int wake)
{
  irqflags_t flags = irq_save();

  if (!g_notify_pending || g_notify_generation != generation)
    {
      g_notify_generation = generation;
      g_notify_value = value;
    }
  else
    {
      g_notify_value |= value;   /* OR-coalesce within a generation */
    }

  g_notify_pending = 1;
  irq_restore(flags);

  if (wake)
    {
      g_worker_wake = 1;
      __SEV();
    }
}

static void retry_notify(void)
{
  irqflags_t flags = irq_save();
  int pending = (int)g_notify_pending;
  uint32_t generation = g_notify_generation;
  uint32_t value = g_notify_value;

  g_notify_pending = 0;
  g_notify_value = 0;
  irq_restore(flags);

  if (!pending)
    {
      return;
    }

  int ret = bk_send_wrapped(TYPE_NOTIFY, generation, value);
  if (ret == -EAGAIN)
    {
      /* In-flight slot will produce TX-complete and wake the worker again;
         preserve the coalesced truth without spinning. */
      queue_notify(generation, value, 0);
    }
}

static void tx_complete(void)
{
  g_worker_wake = 1;
  __SEV();
}

static void dispatch(uint32_t type, uint32_t generation, uint32_t value)
{
  if (generation == 0 || generation != g_boot_generation)
    {
      return;   /* generation gate */
    }

  if (type == TYPE_NOTIFY)
    {
      if (g_notify_cb != NULL)
        {
          g_notify_cb(generation, value);
        }
    }
  else if (type == TYPE_LIFECYCLE)
    {
      g_ap_lifecycle = value;
    }
  /* (HEARTBEAT / PROBE are not exercised by this proxy) */
}

/* The worker: drain incoming (none on CP here) + sweep + retry outgoing.
   is_timeout is set when woken by the 1 ms SysTick poll. */
static void worker(int is_timeout)
{
  if (is_timeout)
    {
      notify_cb_t cb = g_notify_cb;
      if (cb != NULL && g_boot_generation != 0)
        {
          cb(g_boot_generation, 0);   /* sweep notify, value 0 */
        }
    }

  retry_notify();
}

/* ------------------------------------------------------------------ */
/* Public-ish wrappers (mirror the repo entry points)                  */
/* ------------------------------------------------------------------ */

static int bk_send_wrapped(uint32_t type, uint32_t generation, uint32_t value)
{
  uint32_t frame[4];

  if (!g_initialized)
    {
      return -ENODEV;
    }
  if (type == TYPE_INVALID || type >= TYPE_COUNT || generation == 0)
    {
      return -EINVAL;
    }

  frame[0] = MBOX_CMD;
  frame[1] = type;
  frame[2] = generation;
  frame[3] = value;

  int ret = mb_chnl_write(0, frame);
  if (ret == BK_OK)
    {
      return OK;
    }

  return (ret == BK_ERR_BUSY) ? -EAGAIN : -EIO;
}

static int bk_notify(uint32_t generation, uint32_t value)
{
  int ret = bk_send_wrapped(TYPE_NOTIFY, generation, value);
  if (ret == -EAGAIN)
    {
      queue_notify(generation, value, 1);
      return OK;
    }

  return ret;
}

static void cp_notify_cb(uint32_t generation, uint32_t value)
{
  (void)generation;
  (void)value;
  g_cp_sweep++;
}

/* ------------------------------------------------------------------ */
/* Interrupt handlers                                                  */
/* ------------------------------------------------------------------ */

/* Core 1 (AP) receives a CP->AP doorbell. */
void MHU0_IRQHandler(void)
{
  uint32_t s = mmio_rd(MHU0, 0x10);   /* CPU1INTR_STAT */
  mmio_wr(MHU0, 0x18, s);             /* CPU1INTR_CLR */
  __DSB();

  if (g_send_ready)
    {
      g_ap_frame[0] = g_send_frame[0];
      g_ap_frame[1] = g_send_frame[1];
      g_ap_frame[2] = g_send_frame[2];
      g_ap_frame[3] = g_send_frame[3];
      g_send_ready = 0;
      __DMB();

      g_ap_pending = 1;
      __SEV();

      if (!g_suppress_ack)
        {
          g_ack_val = g_ap_frame[1];
          g_ack_ready = 1;
          __DMB();
          mmio_wr(MHU1, 0x10, 1u);   /* CPU0INTR_SET -> core 0 IRQ7 */
        }
    }
}

/* Core 0 (CP) receives an AP->CP doorbell (the TX-complete ACK). */
void MHU1_IRQHandler(void)
{
  uint32_t s = mmio_rd(MHU1, 0x00);   /* CPU0INTR_STAT */
  mmio_wr(MHU1, 0x08, s);             /* CPU0INTR_CLR */
  __DSB();

  if (g_ack_ready)
    {
      g_ack_ready = 0;
      __DMB();
      g_cp_ack_count++;
      tx_complete();
    }
}

void SysTick_Handler(void)
{
  /* On this QEMU board the SysTick has no reference clock, so the 1 ms
     worker poll is driven by the harness pump loop (equivalent to the
     kthread being woken).  Kept as a no-op ISR for completeness. */
  g_ticks++;
}

/* ------------------------------------------------------------------ */
/* AP side (core 1)                                                    */
/* ------------------------------------------------------------------ */

static void ap_dispatch(void)
{
  uint32_t cmd  = g_ap_frame[0];
  uint32_t type = g_ap_frame[1];
  uint32_t gen  = g_ap_frame[2];
  uint32_t val  = g_ap_frame[3];

  if (cmd != MBOX_CMD)
    {
      return;   /* only RPTUN frames */
    }
  if (gen != g_boot_generation)
    {
      return;   /* generation gate */
    }
  if (type == TYPE_INVALID || type >= TYPE_COUNT)
    {
      return;
    }

  /* last-value-wins per type (the mailbox wrapper stores, it does not OR) */
  g_ap_results[type] = val;
  g_ap_recv_count++;

  if (type == TYPE_NOTIFY)
    {
      g_ap_notify_last = val;
      g_ap_notify_count++;
    }
  else if (type == TYPE_LIFECYCLE)
    {
      g_ap_lifecycle = val;
    }

  /* Mirror the repo's worker dispatch for any incoming NOTIFY (this proxy
     only sends NOTIFY/LIFECYCLE from CP, so this records the gen-gated
     delivery the RPTUN layer would act on). */
  dispatch(type, gen, val);
}

void core1_main(void)
{
  uart_init();
  uart_puts("C1 alive\n");
  __asm volatile ("cpsie i" ::: "memory");
  irq_enable(6);   /* MHU0 -> core 1 */

  g_ap_alive = 1;
  __SEV();         /* wake core 0 if it is waiting for us */

  for (;;)
    {
      if (g_ap_pending)
        {
          g_ap_pending = 0;
          ap_dispatch();
        }
      else
        {
          __WFE();
        }
    }
}

/* ------------------------------------------------------------------ */
/* CP side (core 0): test scenario + worker pump                       */
/* ------------------------------------------------------------------ */

static void check(int cond, const char *name)
{
  g_checks++;
  if (cond)
    {
      uart_puts("[PASS] ");
    }
  else
    {
      uart_puts("[FAIL] ");
      g_failures++;
    }
  uart_puts(name);
  uart_puts("\n");
}

/* Pump the worker (driving queued-notify retries) until the AP has
   received `recv_target` frames AND the CP has seen `ack_target` ACKs. */
static void pump_until_recv_ack(uint32_t recv_target, uint32_t ack_target)
{
  for (uint32_t i = 0; i < 4000000u; i++)
    {
      if (g_ap_recv_count >= recv_target && g_cp_ack_count >= ack_target)
        {
          return;
        }
      worker(1);
      __WFE();
    }
}

/* Same, but used when the ACK is intentionally suppressed (no ack expected). */
static void pump_until_recv(uint32_t recv_target)
{
  for (uint32_t i = 0; i < 4000000u; i++)
    {
      if (g_ap_recv_count >= recv_target)
        {
          return;
        }
      worker(1);
      __WFE();
    }
}

static void run_scenario(void)
{
  uint32_t base;
  uint32_t sbase;

  /* A: single notify delivered on the wire. */
  base = g_ap_recv_count;
  bk_notify(GEN, 0x01);
  pump_until_recv_ack(base + 1, g_cp_ack_count + 1);
  check(g_ap_notify_last == 0x01, "A single notify delivered value 0x01");
  check(g_ap_recv_count == base + 1, "A single wire write (one dispatch)");

  /* B: same-generation OR-coalesce -> one wire write of 0x01|0x02 = 0x03. */
  g_force_busy = 1;
  base = g_ap_recv_count;
  sbase = g_cp_send_count;
  bk_notify(GEN, 0x01);
  bk_notify(GEN, 0x02);
  check(g_ap_recv_count == base, "B queued while busy: no wire write yet");
  g_force_busy = 0;
  pump_until_recv_ack(base + 1, g_cp_ack_count + 1);
  check(g_ap_notify_last == 0x03, "B coalesced OR value 0x01|0x02 == 0x03");
  check(g_cp_send_count == sbase + 1, "B exactly one wire write after retry");

  /* C: -EAGAIN recovery (distinct value 0x04). */
  g_force_busy = 1;
  base = g_ap_recv_count;
  sbase = g_cp_send_count;
  bk_notify(GEN, 0x04);
  g_force_busy = 0;
  pump_until_recv_ack(base + 1, g_cp_ack_count + 1);
  check(g_ap_notify_last == 0x04, "C -EAGAIN recovery delivered 0x04");
  check(g_cp_send_count == sbase + 1, "C recovered via a single retry write");

  /* D: stranded notify (TX-complete suppressed) still delivered by the
       1 ms worker poll. */
  g_suppress_ack = 1;
  g_force_busy = 1;
  base = g_ap_recv_count;
  sbase = g_cp_send_count;
  bk_notify(GEN, 0x08);
  g_force_busy = 0;
  pump_until_recv(base + 1);   /* no ACK expected */
  check(g_ap_notify_last == 0x08, "D stranded notify delivered via poll");
  check(g_cp_send_count == sbase + 1, "D parked notify retired by worker");
  g_suppress_ack = 0;

  /* E: incoming last-value-wins. Two distinct wire writes; the AP stores
       the last, NOT the OR (would be 0x33). */
  base = g_ap_recv_count;
  sbase = g_cp_send_count;
  bk_notify(GEN, 0x11);
  pump_until_recv_ack(base + 1, g_cp_ack_count + 1);
  bk_notify(GEN, 0x22);
  pump_until_recv_ack(base + 2, g_cp_ack_count + 1);
  check(g_ap_notify_last == 0x22, "E incoming last-value-wins == 0x22 (not 0x33)");
  check(g_ap_notify_count == base + 2, "E two distinct dispatches (not coalesced)");

  /* F: invalid arguments -> -EINVAL. */
  check(bk_send_wrapped(TYPE_INVALID, GEN, 0x05) == -EINVAL,
        "F send INVALID type -> -EINVAL");
  check(bk_send_wrapped(TYPE_NOTIFY, 0, 0x05) == -EINVAL,
        "F send generation 0 -> -EINVAL");
  check(bk_send_wrapped(99, GEN, 0x05) == -EINVAL,
        "F send out-of-range type -> -EINVAL");
}

static void systick_init(void)
{
  /* Intentionally empty: mps2-an521 provides no SysTick reference clock in
     QEMU, so the 1 ms worker poll is driven by the harness pump loop (see
     pump_until_recv*), which is equivalent to the kthread being woken. */
  (void)0;
}

void core0_main(void)
{
  uart_init();
  uart_puts("C0 start\n");
  __asm volatile ("cpsie i" ::: "memory");

  irq_enable(7);          /* MHU1 -> core 0 */
  systick_init();

  /* Bring up the modeled RPTUN MBOX0 channel. */
  g_boot_generation = GEN;
  g_notify_cb = cp_notify_cb;
  g_initialized = 1;

  /* Release core 1 (clear CPUWAIT bit 1). */
  mmio_wr(CPUWAIT, 0, 0u);

  /* Wait until the AP is alive. */
  while (!g_ap_alive)
    {
      __WFE();
    }

  uart_puts("QEMU BK7258 MBOX0 notify proxy (mps2-an521 SMP2)\n");
  run_scenario();

  uart_puts("-------------------------------------------\n");
  if (g_failures == 0)
    {
      uart_puts("ALL CHECKS PASSED\n");
    }
  else
    {
      uart_puts("SOME CHECKS FAILED\n");
    }
  sh_exit((int)g_failures);
}
