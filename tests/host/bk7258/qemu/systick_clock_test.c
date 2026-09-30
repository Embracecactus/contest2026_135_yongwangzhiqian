/* SPDX-License-Identifier: GPL-2.0-or-later
 * Downstream AI-assisted local experiment; not for upstream submission.
 * Exercises production SysTick and ptimer logic with QEMU unit-test clock stubs.
 */
#include "qemu/osdep.h"
#include "qemu/main-loop.h"
#include "hw/timer/armv7m_systick.h"
#include "migration/vmstate.h"
#include "qemu/module.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "hw/timer/trace.h"
#include "ptimer-test.h"

/* No QOM registration or host logging is needed by these stack-local devices. */
#undef type_init
#define type_init(fn)
#define SYSTICK(obj) ((SysTickState *)(obj))
#undef qemu_log_mask
#define qemu_log_mask(...) ((void)0)
#undef error_setg
#define error_setg(...) abort()
#define trace_systick_timer_tick(...) ((void)0)
#define trace_systick_read(...) ((void)0)
#define trace_systick_write(...) ((void)0)
#include SYSTICK_SOURCE

static unsigned irqs;
void qemu_set_irq(qemu_irq irq, int level)
{
    irqs += !!level;
}

static Clock source, cpuclk, refclk;
static SysTickState s;
static QEMUTimerList timers;

static void init(bool ref_connected)
{
    memset(&s, 0, sizeof(s));
    memset(&timers, 0, sizeof(timers));
    memset(&cpuclk, 0, sizeof(cpuclk));
    memset(&refclk, 0, sizeof(refclk));
    main_loop_tlg.tl[QEMU_CLOCK_VIRTUAL] = &timers;
    ptimer_test_time_ns = 0;
    qtest_allowed = true;
    irqs = 0;
    cpuclk.source = &source;
    cpuclk.period = CLOCK_PERIOD_FROM_NS(1000);
    refclk.source = ref_connected ? &source : NULL;
    refclk.period = ref_connected ? CLOCK_PERIOD_FROM_NS(31250) : 0;
    s.cpuclk = &cpuclk;
    s.refclk = &refclk;
    systick_realize((DeviceState *)&s, NULL);
    systick_reset((DeviceState *)&s);
}

static void finish(void)
{
    ptimer_transaction_begin(s.ptimer);
    ptimer_stop(s.ptimer);
    ptimer_transaction_commit(s.ptimer);
    ptimer_free(s.ptimer);
    g_assert_null(timers.active_timers.next);
}

static void wr(unsigned offset, uint32_t value)
{
    g_assert_cmpint(systick_write(&s, offset, value, 4,
                                 MEMTXATTRS_UNSPECIFIED), ==, MEMTX_OK);
}

static uint32_t rd(unsigned offset)
{
    uint64_t value;
    g_assert_cmpint(systick_read(&s, offset, &value, 4,
                                MEMTXATTRS_UNSPECIFIED), ==, MEMTX_OK);
    return value;
}

static void step(uint64_t ns)
{
    int64_t target = ptimer_test_time_ns + ns;
    int64_t deadline;
    while ((deadline = qemu_clock_deadline_ns_all(QEMU_CLOCK_VIRTUAL,
                                                 QEMU_TIMER_ATTR_ALL)) >= 0 &&
           deadline <= target) {
        QEMUTimer *timer = timers.active_timers.next;
        ptimer_test_time_ns = deadline;
        g_assert_nonnull(timer);
        timer_del(timer);
        timer->cb(timer->opaque);
    }
    ptimer_test_time_ns = target;
}

static void clock_change(bool cpu, uint64_t period_ns)
{
    Clock *clk = cpu ? &cpuclk : &refclk;
    clk->period = CLOCK_PERIOD_FROM_NS(period_ns);
    if (cpu) {
        systick_cpuclk_update(&s, ClockUpdate);
    } else {
        systick_refclk_update(&s, ClockUpdate);
    }
}

static void start(uint32_t reload, bool cpu)
{
    wr(4, reload);
    wr(8, 0);
    wr(0, 3 | (cpu ? 4 : 0));
}

static void unselected_sources(void)
{
    init(true);
    start(1000, true);
    step(101001);
    g_assert_cmpuint(rd(8), ==, 900);
    clock_change(false, 0);
    step(100000);
    g_assert_cmpuint(rd(8), ==, 800);
    clock_change(false, 31250);
    step(100000);
    g_assert_cmpuint(rd(8), ==, 700);
    wr(0, 3);
    clock_change(true, 0);
    step(31250 * 100 + 1);
    g_assert_cmpuint(rd(8), ==, 600);
    clock_change(true, 2000);
    step(31250 * 100 + 1);
    g_assert_cmpuint(rd(8), ==, 500);
    g_assert_cmpuint(irqs, ==, 0);
    finish();
}

static void gate_resume(bool cpu)
{
    uint64_t period = cpu ? 1000 : 31250;
    init(true);
    start(9, cpu);
    step(period * 4 + 1);
    g_assert_cmpuint(rd(8), ==, 6);
    clock_change(cpu, 0);
    g_assert_true(s.clock_paused);
    g_assert_false(ptimer_is_running(s.ptimer));
    step(1000000000);
    g_assert_cmpuint(rd(8), ==, 6);
    g_assert_cmpuint(rd(0), ==, cpu ? 7 : 3);
    g_assert_cmpuint(irqs, ==, 0);
    clock_change(cpu, period);
    g_assert_false(s.clock_paused);
    g_assert_true(ptimer_is_running(s.ptimer));
    g_assert_cmpuint(rd(8), ==, 6);
    step(period * 6 - 1);
    g_assert_cmpuint(irqs, ==, 0);
    g_assert_cmpuint(rd(8), ==, 1);
    step(1);
    g_assert_cmpuint(irqs, ==, 1);
    g_assert_cmpuint(rd(8), ==, 0);
    g_assert_true(rd(0) & SYSTICK_COUNTFLAG);
    g_assert_false(rd(0) & SYSTICK_COUNTFLAG);
    step(period + 1);
    g_assert_cmpuint(rd(8), ==, 9);
    step(period * 9);
    g_assert_cmpuint(irqs, ==, 2);
    finish();
}

static void gate_resume_cpu(void) { gate_resume(true); }
static void gate_resume_ref(void) { gate_resume(false); }

static void simultaneous_disable_switch(void)
{
    init(true);
    clock_change(false, 0);
    start(100, true);
    step(11001);
    g_assert_cmpuint(rd(8), ==, 90);
    wr(0, 0); /* Previously could divide by zero in ptimer_stop(). */
    g_assert_cmpuint(rd(8), ==, 90);
    g_assert_false(s.clock_paused);
    clock_change(false, 31250);
    step(100000000);
    g_assert_cmpuint(rd(8), ==, 90);
    g_assert_cmpuint(irqs, ==, 0);
    finish();
}

static void enabled_source_switch(void)
{
    init(true);
    clock_change(false, 0);
    start(100, true);
    step(11001);
    wr(0, 3);
    g_assert_true(s.clock_paused);
    g_assert_cmpuint(rd(8), ==, 90);
    step(100000000);
    g_assert_cmpuint(rd(8), ==, 90);
    wr(0, 7);
    g_assert_false(s.clock_paused);
    step(10001);
    g_assert_cmpuint(rd(8), ==, 80);
    clock_change(false, 31250);
    step(10001);
    g_assert_cmpuint(rd(8), ==, 70);
    finish();
}

static void enabled_while_gated(void)
{
    init(true);
    clock_change(false, 0);
    start(9, false);
    g_assert_true(s.clock_paused);
    step(100000000);
    g_assert_cmpuint(rd(8), ==, 0);
    g_assert_cmpuint(irqs, ==, 0);
    clock_change(false, 31250);
    g_assert_cmpuint(rd(8), ==, 0);
    step(31249);
    g_assert_cmpuint(rd(8), ==, 0);
    step(1);
    g_assert_cmpuint(rd(8), ==, 9);
    g_assert_cmpuint(irqs, ==, 0);
    clock_change(false, 0);
    wr(0, 0);
    g_assert_false(s.clock_paused);
    clock_change(false, 31250);
    step(100000000);
    g_assert_cmpuint(rd(8), ==, 9);
    g_assert_cmpuint(irqs, ==, 0);
    finish();
}

static void zero_reload_stop(void)
{
    init(true);
    start(9, false);
    step(31250 * 4 + 1);
    wr(4, 0);
    step(31250 * 6);
    g_assert_cmpuint(irqs, ==, 1);
    g_assert_false(ptimer_is_running(s.ptimer));
    g_assert_true(rd(0) & SYSTICK_ENABLE);
    wr(4, 20);
    clock_change(false, 0);
    g_assert_false(s.clock_paused);
    clock_change(false, 31250);
    step(100000000);
    g_assert_cmpuint(rd(8), ==, 0);
    g_assert_cmpuint(irqs, ==, 1);
    /* Only the existing ENABLE transition restarts this stopped timer. */
    wr(0, 2);
    wr(0, 3);
    step(31250);
    g_assert_cmpuint(rd(8), ==, 20);
    finish();
}

static void zero_reload_deferred_stop(void)
{
    init(true);
    start(9, false);
    wr(4, 0); /* Before the first deferred reload, which stops at that edge. */
    step(31250);
    g_assert_false(ptimer_is_running(s.ptimer));
    g_assert_cmpuint(irqs, ==, 1);
    wr(4, 9);
    clock_change(false, 0);
    g_assert_false(s.clock_paused);
    clock_change(false, 31250);
    step(100000000);
    g_assert_cmpuint(rd(8), ==, 0);
    g_assert_cmpuint(irqs, ==, 1);
    finish();
}

static void cvr_write_while_gated(void)
{
    init(true);
    start(9, false);
    step(31250 * 4 + 1);
    clock_change(false, 0);
    wr(8, 0xabcdef);
    g_assert_cmpuint(rd(8), ==, 0);
    g_assert_true(s.clock_paused);
    clock_change(false, 31250);
    step(31250);
    g_assert_cmpuint(rd(8), ==, 9);
    clock_change(false, 0);
    wr(4, 0);
    wr(8, 0);
    g_assert_false(s.clock_paused);
    wr(4, 9);
    clock_change(false, 31250);
    step(100000000);
    g_assert_cmpuint(rd(8), ==, 0);
    g_assert_cmpuint(irqs, ==, 0);
    finish();
}

static void calibration_reset_access(void)
{
    uint64_t data;
    MemTxAttrs user = { .user = true };
    init(true);
    g_assert_cmphex(rd(12), ==, 319);
    clock_change(false, 0);
    g_assert_cmphex(rd(12), ==, SYSCALIB_SKEW);
    start(9, false);
    g_assert_true(s.clock_paused);
    systick_reset((DeviceState *)&s);
    g_assert_false(s.clock_paused);
    g_assert_cmpuint(rd(0), ==, 0);
    g_assert_cmpuint(rd(4), ==, 0);
    g_assert_cmpuint(rd(8), ==, 0);
    clock_change(false, 31250);
    g_assert_cmphex(rd(12), ==, 319);
    g_assert_cmpint(systick_read(&s, 0, &data, 4, user), ==, MEMTX_ERROR);
    g_assert_cmpint(systick_write(&s, 0, 3, 4, user), ==, MEMTX_ERROR);
    finish();
    init(false);
    g_assert_cmphex(rd(12), ==, SYSCALIB_NOREF);
    wr(0, 1);
    g_assert_cmpuint(rd(0), ==, 5);
    finish();
}

static void switch_between_gated_sources(void)
{
    init(true);
    start(100, true);
    step(11001);
    clock_change(true, 0);
    clock_change(false, 0);
    wr(0, 3);
    g_assert_true(s.clock_paused);
    step(100000000);
    g_assert_cmpuint(rd(8), ==, 90);
    clock_change(true, 1000);
    g_assert_true(s.clock_paused);
    step(100000000);
    g_assert_cmpuint(rd(8), ==, 90);
    clock_change(false, 31250);
    step(31250 * 10 + 1);
    g_assert_cmpuint(rd(8), ==, 80);
    finish();
}

static void empty_timer_stays_stopped(void)
{
    init(true);
    start(0, false);
    g_assert_false(ptimer_is_running(s.ptimer));
    wr(4, 9);
    clock_change(false, 0);
    g_assert_false(s.clock_paused);
    clock_change(false, 31250);
    step(100000000);
    g_assert_cmpuint(rd(8), ==, 0);
    g_assert_cmpuint(irqs, ==, 0);
    finish();
}

static void zero_reload_during_pause(void)
{
    init(true);
    start(9, false);
    step(31250 * 4 + 1);
    clock_change(false, 0);
    wr(4, 0);
    step(100000000);
    g_assert_cmpuint(rd(8), ==, 6);
    clock_change(false, 31250);
    step(31250 * 6);
    g_assert_cmpuint(irqs, ==, 1);
    g_assert_cmpuint(rd(8), ==, 0);
    g_assert_false(ptimer_is_running(s.ptimer));
    wr(4, 9);
    clock_change(false, 0);
    clock_change(false, 31250);
    step(100000000);
    g_assert_cmpuint(irqs, ==, 1);
    g_assert_cmpuint(rd(8), ==, 0);
    finish();
}

static void tickint_and_countflag(void)
{
    init(true);
    wr(4, 9);
    wr(0, 1);
    step(31250 * 10);
    g_assert_cmpuint(irqs, ==, 0);
    g_assert_true(s.control & SYSTICK_COUNTFLAG);
    clock_change(false, 0);
    step(100000000);
    clock_change(false, 31250);
    g_assert_true(rd(0) & SYSTICK_COUNTFLAG);
    g_assert_false(rd(0) & SYSTICK_COUNTFLAG);
    wr(0, 3);
    step(31250 * 10);
    g_assert_cmpuint(irqs, ==, 1);
    g_assert_true(s.control & SYSTICK_COUNTFLAG);
    wr(8, 0);
    g_assert_false(rd(0) & SYSTICK_COUNTFLAG);
    finish();
}

static void migration_pause_metadata(void)
{
    init(true);
    g_assert_false(systick_clock_paused_needed(&s));
    start(9, false);
    clock_change(false, 0);
    g_assert_true(systick_clock_paused_needed(&s));
    g_assert_cmpstr(vmstate_systick.subsections[0]->name, ==,
                    "armv7m_systick/clock-paused");
    g_assert_cmpint(systick_pre_load(&s), ==, 0);
    g_assert_false(s.clock_paused);
    finish();
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/systick/unselected-sources", unselected_sources);
    g_test_add_func("/systick/gate-resume-cpu", gate_resume_cpu);
    g_test_add_func("/systick/gate-resume-ref", gate_resume_ref);
    g_test_add_func("/systick/simultaneous-disable-switch", simultaneous_disable_switch);
    g_test_add_func("/systick/enabled-source-switch", enabled_source_switch);
    g_test_add_func("/systick/enabled-while-gated", enabled_while_gated);
    g_test_add_func("/systick/zero-reload-stop", zero_reload_stop);
    g_test_add_func("/systick/zero-reload-deferred-stop", zero_reload_deferred_stop);
    g_test_add_func("/systick/cvr-write-while-gated", cvr_write_while_gated);
    g_test_add_func("/systick/calibration-reset-access", calibration_reset_access);
    g_test_add_func("/systick/switch-between-gated-sources", switch_between_gated_sources);
    g_test_add_func("/systick/empty-timer-stays-stopped", empty_timer_stays_stopped);
    g_test_add_func("/systick/zero-reload-during-pause", zero_reload_during_pause);
    g_test_add_func("/systick/tickint-and-countflag", tickint_and_countflag);
    g_test_add_func("/systick/migration-pause-metadata", migration_pause_metadata);
    return g_test_run();
}
