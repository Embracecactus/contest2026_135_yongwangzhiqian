/* Bare-metal handoff around the NuttX-pinned upstream MCUboot bootutil. */
#include <stdint.h>
#include <stdbool.h>

#include <bootutil/bootutil.h>
#include <bootutil/image.h>

#include "bk7258_bl2_abi.h"
#include "../boot_wdt.h"
#include "bk7258_debug_route.h"

#define SCB_VTOR 0xe000ed08u
#define SCB_ICIALLU 0xe000ef50u
#define NVIC_ICER0 0xe000e180u
#define NVIC_ICPR0 0xe000e280u
#define SYSTICK_CTRL 0xe000e010u
#if BK7258_BL2_CONSOLE_UART == 0
#  define BK7258_BL2_UART_BASE 0x44820000u
#elif BK7258_BL2_CONSOLE_UART == 1
#  define BK7258_BL2_UART_BASE 0x45830000u
#elif BK7258_BL2_CONSOLE_UART == 2
#  define BK7258_BL2_UART_BASE 0x45840000u
#endif
#if BK7258_BL2_CONSOLE_UART < 3
#  define BK7258_BL2_UART_FIFO (BK7258_BL2_UART_BASE + 0x1cu)
#  define BK7258_BL2_UART_STATUS (BK7258_BL2_UART_BASE + 0x18u)
#endif
#define BK7258_AP_VECTOR_RAM_BASE 0x28050000u
#define BK7258_AP_VECTOR_RAM_END  0x2809f000u
#define BK7258_BL2_RAM_BASE       0x28020000u
#define BK7258_CP_MSPLIM          0x28010000u

extern void boot_prepare_app_handoff(void);
extern void boot_console_prepare_app_handoff(void);

static bool bk7258_bl2_header_valid(const struct image_header *hdr,
                                    uint32_t slot_size);
static const struct image_header *bk7258_bl2_ap_header(uint32_t cp_offset);
static bool bk7258_bl2_ap_vector(uint32_t cp_offset);
static bool bk7258_bl2_pair_generation_valid(uint32_t cp_offset,
                                             const struct image_header *cp_hdr);
static void bk7258_bl2_mark(const char *mark);

#if BK7258_BL2_SWD_ENABLE
static void bk7258_bl2_debug_gate(void)
{
  uint32_t route;
  uint32_t function;
  uint32_t pin;

  /* This is the final bootloader observation boundary: image selection and
   * validation are complete, UART/cache/MPU handoff cleanup has run, and CP
   * has not executed a single instruction.  Re-assert the route selected by
   * the CP defconfig and explicitly enable internal debug authentication.
   * Use the current security-state alias only; the explicit NS alias is not
   * writable when this BL2 is entered non-secure.
   */

  REG32(BK7258_DAUTHCTRL_REG) = 0x0fu;
  route = REG32(BK7258_SYS_CPU_ROUTE_REG);
  route &= ~BK7258_SYS_JTAG_CORE_MASK;
  route |= BK7258_BL2_SWD_TARGET << BK7258_SYS_JTAG_CORE_SHIFT;
  REG32(BK7258_SYS_CPU_ROUTE_REG) = route;

#if BK7258_BL2_SWD_PIN_GROUP == BK7258_SWD_PIN_GROUP_0
  function = REG32(BK7258_SYS_GPIO16_23_FUNC_REG);
  function &= ~BK7258_SWD_GROUP0_FUNC_MASK;
  function |= BK7258_SWD_GROUP0_FUNC_VALUE;
  REG32(BK7258_SYS_GPIO16_23_FUNC_REG) = function;
  pin = REG32(BK7258_GPIO20_CTRL_REG);
  REG32(BK7258_GPIO20_CTRL_REG) =
    (pin & ~BK7258_GPIO_FUNC_CTRL_MASK) | BK7258_GPIO_JTAG_CTRL;
  pin = REG32(BK7258_GPIO21_CTRL_REG);
  REG32(BK7258_GPIO21_CTRL_REG) =
    (pin & ~BK7258_GPIO_FUNC_CTRL_MASK) | BK7258_GPIO_JTAG_CTRL;
#else
  function = REG32(BK7258_SYS_GPIO0_7_FUNC_REG);
  function &= ~BK7258_SWD_GROUP1_FUNC_MASK;
  function |= BK7258_SWD_GROUP1_FUNC_VALUE;
  REG32(BK7258_SYS_GPIO0_7_FUNC_REG) = function;
  pin = REG32(BK7258_GPIO0_CTRL_REG);
  REG32(BK7258_GPIO0_CTRL_REG) =
    (pin & ~BK7258_GPIO_FUNC_CTRL_MASK) | BK7258_GPIO_JTAG_CTRL;
  pin = REG32(BK7258_GPIO1_CTRL_REG);
  REG32(BK7258_GPIO1_CTRL_REG) =
    (pin & ~BK7258_GPIO_FUNC_CTRL_MASK) | BK7258_GPIO_JTAG_CTRL;
#endif
  __asm volatile ("dsb sy; isb" ::: "memory");

#if BK7258_BL2_SWD_BOOT_HOLD
  /* A debugger halt stops loop-time feeding but not the hardware watchdog.
   * Disable both boot watchdogs for the intentional hold and leave them
   * disabled through the short CP branch.  Period zero does not clear the
   * elapsed hardware count; re-enabling after an unbounded hold can reset on
   * the key write itself.  CP disables the inherited boot watchdogs in its
   * reset entry before the board watchdog driver takes ownership. */
  boot_wdt_debug_hold_disable();
  REG32(BK7258_SWD_BOOT_RELEASE_ADDRESS) = 0u;
  __asm volatile ("dsb sy" ::: "memory");

  while ((REG32(BK7258_DHCSR_REG) & BK7258_DHCSR_C_DEBUGEN) == 0)
    {
    }

  /* C_DEBUGEN becomes visible before the probe has completed CoreSight
   * enumeration and sent its first halt.  Require an explicit debugger
   * release instead of racing CP security initialization.
   */

  while (REG32(BK7258_SWD_BOOT_RELEASE_ADDRESS) !=
         BK7258_SWD_BOOT_RELEASE_MAGIC)
    {
    }

  REG32(BK7258_SWD_BOOT_RELEASE_ADDRESS) = 0u;
  __asm volatile ("dsb sy; isb" ::: "memory");
#endif
}
#endif

static bool bk7258_bl2_try_pair(int slot, struct boot_rsp *rsp)
{
  fih_ret result;
  uint32_t expected_cp;

  bk7258_bl2_set_slot_limit(slot);
  boot_state_clear(NULL);
  FIH_CALL(boot_go, result, rsp);
  if (FIH_NOT_EQ(result, FIH_SUCCESS) ||
      rsp->br_flash_dev_id != 0 || rsp->br_hdr == NULL)
    {
      return false;
    }

  if (slot == BK7258_BL2_SLOT_PRIMARY)
    {
      expected_cp = BK7258_ROLE_SLOT_A_CP_XIP_START;
      if (rsp->br_image_off != expected_cp)
        {
          return false;
        }
    }
  else if (slot == BK7258_BL2_SLOT_SECONDARY)
    {
      expected_cp = BK7258_BL2_B_CP_XIP_START;
      if (rsp->br_image_off != expected_cp)
        {
          return false;
        }
    }
  else if (rsp->br_image_off != BK7258_ROLE_SLOT_A_CP_XIP_START &&
           rsp->br_image_off != BK7258_BL2_B_CP_XIP_START)
    {
      return false;
    }

  /* boot_go() verifies each image independently.  The board launch ABI is
   * stricter: CP and AP are one generation and must carry the same MCUboot
   * version and protected security counter before either core is released. */
  if (!bk7258_bl2_pair_generation_valid(rsp->br_image_off, rsp->br_hdr))
    {
      bk7258_bl2_mark("B2GENBAD");
      return false;
    }

  /* MCUboot authenticates the AP image, but it cannot know the BK7258
   * launch-vector ABI.  Treat a signed AP with an unusable vector as a
   * rejected candidate while there are still other slots to try. */
  if (!bk7258_bl2_ap_vector(rsp->br_image_off))
    {
      return false;
    }

  return true;
}

/*
 * This is the board-owned equivalent of the small platform-initialization
 * part of Beken's BL2 main().  BL1 already performs the cold clock/flash
 * preparation; BL2 must still establish its own vector base, discard reset
 * residue, and take over the watchdog before entering boot_go().
 */
static void bk7258_bl2_platform_init(void)
{
  boot_wdt_init();
  REG32(SCB_VTOR) = BK7258_BL2_RAM_BASE;
  REG32(SYSTICK_CTRL) = 0;
  REG32(NVIC_ICER0) = 0xffffffffu;
  REG32(NVIC_ICER0 + 4u) = 0xffffffffu;
  REG32(NVIC_ICPR0) = 0xffffffffu;
  REG32(NVIC_ICPR0 + 4u) = 0xffffffffu;
  REG32(SCB_ICIALLU) = 0;
  __asm volatile ("dsb sy; isb" ::: "memory");
}

static bool bk7258_bl2_header_valid(const struct image_header *hdr,
                                    uint32_t slot_size)
{
  if (hdr->ih_magic != IMAGE_MAGIC ||
      hdr->ih_hdr_size < IMAGE_HEADER_SIZE ||
      hdr->ih_hdr_size > slot_size - 8u ||
      hdr->ih_img_size < 8u ||
      hdr->ih_img_size > slot_size - hdr->ih_hdr_size)
    {
      return false;
    }

  return true;
}

static const struct image_header *bk7258_bl2_ap_header(uint32_t cp_offset)
{
  uint32_t ap_header = cp_offset == BK7258_ROLE_SLOT_A_CP_XIP_START ?
    BK7258_ROLE_SLOT_A_AP_XIP_START : BK7258_BL2_B_AP_XIP_START;

  return (const struct image_header *)(uintptr_t)ap_header;
}

static bool bk7258_bl2_version_equal(const struct image_version *left,
                                     const struct image_version *right)
{
  return left->iv_major == right->iv_major &&
         left->iv_minor == right->iv_minor &&
         left->iv_revision == right->iv_revision &&
         left->iv_build_num == right->iv_build_num;
}

static bool bk7258_bl2_security_counter_equal(int slot,
                                              const struct image_header *cp_hdr,
                                              const struct image_header *ap_hdr)
{
  const struct flash_area *cp_area = NULL;
  const struct flash_area *ap_area = NULL;
  uint32_t cp_counter = 0;
  uint32_t ap_counter = 0;
  int cp_id;
  int ap_id;
  int32_t cp_rc;
  int32_t ap_rc;

  cp_id = flash_area_id_from_multi_image_slot(0, slot);
  ap_id = flash_area_id_from_multi_image_slot(1, slot);
  if (cp_id < 0 || ap_id < 0 ||
      flash_area_open((uint8_t)cp_id, &cp_area) != 0 ||
      flash_area_open((uint8_t)ap_id, &ap_area) != 0)
    {
      return false;
    }

  cp_rc = bootutil_get_img_security_cnt((struct image_header *)cp_hdr,
                                        cp_area, &cp_counter);
  ap_rc = bootutil_get_img_security_cnt((struct image_header *)ap_hdr,
                                        ap_area, &ap_counter);
  flash_area_close(ap_area);
  flash_area_close(cp_area);

  /* A legacy image may have no security-counter TLV.  In this API the
   * specific return value -1 means that the requested TLV was not found;
   * malformed TLVs, invalid protected areas and flash errors have distinct
   * nonzero values and must not be silently treated as legacy images. */
  if (cp_rc == -1 || ap_rc == -1)
    {
      return cp_rc == -1 && ap_rc == -1;
    }

  return cp_rc == 0 && ap_rc == 0 && cp_counter == ap_counter;
}

static bool bk7258_bl2_pair_generation_valid(uint32_t cp_offset,
                                             const struct image_header *cp_hdr)
{
  const struct image_header *ap_hdr;
  int slot;

  ap_hdr = bk7258_bl2_ap_header(cp_offset);
  if (!bk7258_bl2_header_valid(ap_hdr, BK7258_ROLE_SLOT_A_AP_LOGICAL_SIZE) ||
      !bk7258_bl2_version_equal(&cp_hdr->ih_ver, &ap_hdr->ih_ver))
    {
      return false;
    }

  slot = cp_offset == BK7258_ROLE_SLOT_A_CP_XIP_START ? 0 : 1;
  return bk7258_bl2_security_counter_equal(slot, cp_hdr, ap_hdr);
}

static void bk7258_bl2_log(const char *text)
{
#if BK7258_BL2_CONSOLE_UART < 3
  while (*text)
    {
      unsigned int n;

      for (n = 0; n < 100000u; n++)
        {
          if ((REG32(BK7258_BL2_UART_STATUS) & (1u << 20)) != 0)
            {
              break;
            }
        }

      REG32(BK7258_BL2_UART_FIFO) = (uint8_t)*text++;
    }
#else
  (void)text;
#endif
}

void bk7258_bl2_panic(void)
{
  bk7258_bl2_log("B2BAD\r\n");
  boot_wdt_fail_reset();
}

/* Keep the first BL2 bring-up trace deliberately tiny and independent from
 * MCUboot logging.  A reset after BL2RAM can otherwise look identical to a
 * BL1 failure: the watchdog simply brings the core back through BL1.  These
 * markers let one COM11 capture distinguish a fault in boot_go(), the
 * CP/AP-pair check, or the final vector handoff without adding a libc printf
 * dependency to the SRAM image. */
static void bk7258_bl2_mark(const char *mark)
{
  bk7258_bl2_log(mark);
  bk7258_bl2_log("\r\n");
}

static void bk7258_bl2_load_boot_policy(int *preferred, int *fallback)
{
  volatile struct bk7258_bl2_boot_policy_s *handoff =
    (volatile struct bk7258_bl2_boot_policy_s *)(uintptr_t)
    BK7258_BL2_BOOT_POLICY_ADDRESS;
  struct bk7258_bl2_boot_policy_s policy;

  policy.magic = handoff->magic;
  policy.version = handoff->version;
  policy.preferred_slot = handoff->preferred_slot;
  policy.fallback_slot = handoff->fallback_slot;
  policy.source = handoff->source;
  policy.state = handoff->state;
  policy.generation_low = handoff->generation_low;
  policy.generation_high = handoff->generation_high;
  policy.check = handoff->check;

  /* Consume the record once.  BL1 republishes it on every boot, while this
   * clear prevents an unrelated direct BL2 entry from inheriting stale
   * lifecycle authority. */
  handoff->magic = 0;
  __asm volatile ("dsb sy" ::: "memory");

  *preferred = BK7258_BL2_SLOT_PRIMARY;
  *fallback = BK7258_BL2_SLOTS_BOTH;
  if (policy.magic != BK7258_BL2_BOOT_POLICY_MAGIC ||
      policy.version != BK7258_BL2_BOOT_POLICY_VERSION ||
      policy.check != bk7258_bl2_boot_policy_check(&policy) ||
      policy.preferred_slot > BK7258_BL2_SLOT_SECONDARY ||
      (policy.fallback_slot != BK7258_BL2_BOOT_POLICY_SLOT_NONE &&
       (policy.fallback_slot > BK7258_BL2_SLOT_SECONDARY ||
        policy.fallback_slot == policy.preferred_slot)) ||
      policy.source != BK7258_BL2_BOOT_POLICY_SOURCE_FIXED)
    {
      bk7258_bl2_mark("B2POLDEF");
      return;
    }

  *preferred = (int)policy.preferred_slot;
  if (policy.fallback_slot != BK7258_BL2_BOOT_POLICY_SLOT_NONE)
    {
      *fallback = (int)policy.fallback_slot;
    }
}

static bool bk7258_bl2_remap_secondary(void)
{
  REG32(BK7258_BL2_FLASH_REMAP_ENABLE) &= ~1u;
  REG32(BK7258_BL2_FLASH_REMAP_BEGIN) = BK7258_BL2_REMAP_BEGIN;
  REG32(BK7258_BL2_FLASH_REMAP_END) = BK7258_BL2_REMAP_END;
  REG32(BK7258_BL2_FLASH_REMAP_OFFSET) = BK7258_BL2_REMAP_OFFSET;
  __asm volatile ("dsb sy; isb" ::: "memory");

  /* Match the verified SDK/legacy board handoff: an accepted write is not
   * enough because a blocked or misaligned remap would redirect the final
   * vector fetch to an unrelated XIP region. */
  if (REG32(BK7258_BL2_FLASH_REMAP_BEGIN) != BK7258_BL2_REMAP_BEGIN ||
      REG32(BK7258_BL2_FLASH_REMAP_END) != BK7258_BL2_REMAP_END ||
      REG32(BK7258_BL2_FLASH_REMAP_OFFSET) != BK7258_BL2_REMAP_OFFSET)
    {
      return false;
    }

  REG32(BK7258_BL2_FLASH_REMAP_ENABLE) |= 1u;
  REG32(SCB_ICIALLU) = 0;
  __asm volatile ("dsb sy; isb" ::: "memory");
  return (REG32(BK7258_BL2_FLASH_REMAP_ENABLE) & 1u) != 0u;
}

static bool bk7258_bl2_ap_vector(uint32_t cp_offset)
{
  const struct image_header *hdr = bk7258_bl2_ap_header(cp_offset);
  uint32_t ap_header = (uint32_t)(uintptr_t)hdr;
  uint32_t image;
  uint32_t msp;
  uint32_t reset;
  uint32_t reset_addr;

  /* Do not use ih_hdr_size until the header itself has been bounded.  This
   * check is intentionally repeated outside MCUboot's internal state so the
   * paired AP vector cannot turn a malformed header into an arbitrary read. */
  if (!bk7258_bl2_header_valid(hdr, BK7258_ROLE_SLOT_A_AP_LOGICAL_SIZE))
    {
      bk7258_bl2_mark("B2APHDR");
      return false;
    }

  image = ap_header + hdr->ih_hdr_size;
  msp = *(volatile const uint32_t *)(uintptr_t)image;
  reset = *(volatile const uint32_t *)(uintptr_t)(image + 4u);
  reset_addr = reset & ~1u;

  /* The secondary pair is physically stored at B, but the board remap
   * exposes it through the primary A CP/AP XIP window before the reset
   * vector is used.  The packer consequently keeps A-linked vector values
   * in both pairs.  Validate the post-remap execution window here; checking
   * the raw B address would reject every valid secondary image. */
  if ((msp & 3u) != 0 || msp < BK7258_AP_VECTOR_RAM_BASE ||
      msp >= BK7258_AP_VECTOR_RAM_END)
    {
      bk7258_bl2_mark("B2APMSP");
      return false;
    }

  if ((reset & 1u) == 0)
    {
      bk7258_bl2_mark("B2APTHUMB");
      return false;
    }

  if (reset_addr < BK7258_ROLE_SLOT_A_AP_XIP_START +
                   (uint32_t)hdr->ih_hdr_size ||
      reset_addr >= BK7258_ROLE_SLOT_A_AP_XIP_START + hdr->ih_hdr_size +
                    hdr->ih_img_size)
    {
      bk7258_bl2_mark("B2APRST");
      return false;
    }
  return true;
}

/* Reproduce the final register-state contract recovered from official
 * BK7258 A/B bootloader 0x020026c4.  Keep the reset vector in r9 while the
 * remaining general-purpose registers are cleared, then branch without
 * returning through the BL2 stack.  BL2 always runs privileged on MSP, so
 * the official privilege guard around MSR MSP is satisfied by construction.
 */
static void __attribute__((naked, noinline, noreturn))
bk7258_bl2_enter(uint32_t msp __attribute__((unused)),
                 uint32_t reset __attribute__((unused)))
{
  __asm volatile
  (
    "mov r9, r1\n"
    "msr msp, r0\n"
    "dsb sy\n"
    "isb sy\n"
    "movs r0, #0\n"
    "mov r1, r0\n"
    "mov r2, r0\n"
    "mov r3, r0\n"
    "mov r4, r0\n"
    "mov r5, r0\n"
    "mov r6, r0\n"
    "mov r7, r0\n"
    "mov r8, r0\n"
    "mov r10, r0\n"
    "mov r11, r0\n"
    "mov r12, r0\n"
    "dsb sy\n"
    "isb sy\n"
    "bx r9\n"
  );
}

static void __attribute__((noreturn)) bk7258_bl2_jump(uint32_t image)
{
  uint32_t msp = *(volatile const uint32_t *)(uintptr_t)image;
  uint32_t reset = *(volatile const uint32_t *)(uintptr_t)(image + 4u);
  uint32_t reset_addr = reset & ~1u;

  if ((msp & 7u) != 0 || msp < 0x28010000u || msp >= 0x28050000u ||
      (reset & 1u) == 0 || reset_addr < BK7258_ROLE_SLOT_A_CP_XIP_START ||
      reset_addr >= BK7258_ROLE_SLOT_A_AP_XIP_START)
    {
      bk7258_bl2_panic();
    }

  __asm volatile ("cpsid i" ::: "memory");
  REG32(SYSTICK_CTRL) = 0;
  REG32(NVIC_ICER0) = 0xffffffffu;
  REG32(NVIC_ICER0 + 4u) = 0xffffffffu;
  REG32(NVIC_ICPR0) = 0xffffffffu;
  REG32(NVIC_ICPR0 + 4u) = 0xffffffffu;
  REG32(SCB_VTOR) = image;
  __asm volatile ("dsb sy; isb" ::: "memory");
  /* BL2 widened MSPLIM for its own SRAM stack.  The board-owned MCUboot CP
   * image is linked with RAM origin 0x28010000, so restore that image ABI
   * lower bound before CP owns the vector table and MSP.  The public Beken
   * bootloaders use 0x2802f800 for their own direct-XIP application layout;
   * applying that value to this MCUboot CP image would raise an ARMv8-M
   * stack-limit fault because its initial MSP is 0x280146c0. */
  __asm volatile ("msr msplim, %0; dsb sy; isb" :: "r" (BK7258_CP_MSPLIM) : "memory");
  bk7258_bl2_enter(msp, reset);
  __builtin_unreachable();
}

void bk7258_bl2_main(void)
{
  struct boot_rsp rsp;
  uint32_t image;
  bool paired_ap_valid;
  bool pair_ok;
  int preferred_slot;
  int fallback_slot;

  bk7258_bl2_mark("B2INIT");
  bk7258_bl2_platform_init();
  bk7258_bl2_load_boot_policy(&preferred_slot, &fallback_slot);
  bk7258_bl2_mark(preferred_slot == BK7258_BL2_SLOT_PRIMARY ?
                  "B2POLA" : "B2POLB");
  bk7258_bl2_mark("B2GO");
  /* BL1 owns lifecycle journal mutation and supplies an ordered pair policy.
   * BL2 is the sole image acceptance authority: each allowed pair is exposed
   * to unmodified upstream boot_go() in isolation, then the board's CP/AP
   * generation and vector gates run before handoff.  Never scan both slots
   * independently here; that would let MCUboot version ordering bypass a
   * consumed trial or explicit rollback state. */
  pair_ok = bk7258_bl2_try_pair(preferred_slot, &rsp);
  bk7258_bl2_mark("B2GORET");
  if (!pair_ok && fallback_slot != BK7258_BL2_SLOTS_BOTH)
    {
      bk7258_bl2_mark(fallback_slot == BK7258_BL2_SLOT_PRIMARY ?
                      "B2TRYA" : "B2TRYB");
      pair_ok = bk7258_bl2_try_pair(fallback_slot, &rsp);
      bk7258_bl2_mark(fallback_slot == BK7258_BL2_SLOT_PRIMARY ?
                      "B2ARET" : "B2BRET");
    }
  if (!pair_ok)
    {
      bk7258_bl2_panic();
    }

  bk7258_bl2_set_slot_limit(-1);

  bk7258_bl2_mark("B2GOOK");

  bk7258_bl2_mark(rsp.br_image_off == BK7258_ROLE_SLOT_A_CP_XIP_START ?
                  "B2SELA" : "B2SELB");

  if (rsp.br_flash_dev_id != 0 || rsp.br_hdr == NULL ||
      (rsp.br_image_off != BK7258_ROLE_SLOT_A_CP_XIP_START &&
       rsp.br_image_off != BK7258_BL2_B_CP_XIP_START) ||
      !bk7258_bl2_header_valid(rsp.br_hdr,
                               BK7258_ROLE_SLOT_A_CP_LOGICAL_SIZE))
    {
      bk7258_bl2_panic();
    }

  /* boot_go() has already validated the complete selected pair.  This
   * additional check keeps the CP/AP vector contract explicit at the final
   * handoff boundary, after the XIP addresses have been resolved. */
  paired_ap_valid = bk7258_bl2_ap_vector(rsp.br_image_off);
  bk7258_bl2_mark(paired_ap_valid ? "B2APOK" : "B2APBAD");
  if (!paired_ap_valid)
    {
      bk7258_bl2_panic();
    }

  if (rsp.br_image_off != BK7258_ROLE_SLOT_A_CP_XIP_START)
    {
      if (!bk7258_bl2_remap_secondary())
        {
          bk7258_bl2_panic();
        }
      image = BK7258_ROLE_SLOT_A_CP_XIP_START + rsp.br_hdr->ih_hdr_size;
    }
  else
    {
      image = rsp.br_image_off + rsp.br_hdr->ih_hdr_size;
    }

  /* Match the official BL2 do_boot() platform quit contract.  The selected
   * CP/AP image may be reached through a newly enabled remap window, so clean
   * data cache state, clear MPU regions and invalidate instruction cache only
   * after the final logical image address has been established.  The official
   * A/B success path also rearms both watchdogs with period 0xa000 immediately
   * before the console/cache shutdown, leaving a bounded takeover window for
   * the selected application. */
  boot_wdt_feed_period(APP_HANDOFF_WDT_PERIOD);
  bk7258_bl2_mark("B2HANDOFF");
  boot_console_prepare_app_handoff();
  boot_prepare_app_handoff();
#if BK7258_BL2_SWD_ENABLE
  bk7258_bl2_debug_gate();
#endif
  bk7258_bl2_jump(image);
}
