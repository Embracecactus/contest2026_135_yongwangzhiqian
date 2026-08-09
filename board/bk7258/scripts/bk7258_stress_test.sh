#!/usr/bin/env bash
# =============================================================================
# BK7258 全功能连续压力测试驱动脚本 (packaged SOP driver)
#
# 目标：在不插入冷复位的前提下，对板子当前所有【非 RPMsg】功能做连续压力测试，
#       记录长时间运行下的资源泄漏 / OOM / 退化 / 状态漂移。
#
# 默认不运行 RPMsg（bkrpmsgtest），因为其完整矩阵耗时和日志量较大。如需包含，
#   设置 STRESS_RPMSG=1。历史 ENOMEM 已定位为测试 wrapper 每轮退出的 CPU1
#   pthread 栈/TCB 未回收；常驻 worker 修复后应越过原 run 53 阈值且 heap 稳定。
#
# 用法：
#   ./bk7258_stress_test.sh                 # 默认：排除 RPMsg
#   STRESS_RPMSG=1 ./bk7258_stress_test.sh  # 含 RPMsg heap/阈值回归
#
# 依赖：
#   - powershell.exe (Windows interop) 可用
#   - COM11 枚举存在（firmware console, 460800 8N1）
#   - 同目录的 capture_windows_serial.ps1
#
# 产物：
#   $REPO/logs/stress-<时间戳>/<phase>.raw   每个阶段的原始串口
#   $REPO/logs/stress-<时间戳>/stress-master-summary.txt  汇总
# =============================================================================
set -u

REPO=/home/lijian/project/open-vela
SCRIPTS="$REPO/contest2026_135_yongwangzhiqian/board/bk7258/scripts"
PS1=$(wslpath -w "$SCRIPTS/capture_windows_serial.ps1")
LOGDIR="$REPO/logs/stress-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$LOGDIR"
SUMMARY="$LOGDIR/stress-master-summary.txt"

# --- 真失败标记：只有这些算失败；状态行里的 fail=0/0、fail0=0->0、
#     dup/lost/fail=0/0/0、fault 等零值计数器一律不算失败（历史假阳性来源）。
#
# FAIL/FAILED 必须保持大小写敏感，否则小写零值 fail 计数器会被误报；
# panic/assert/HardFault 等崩溃文本则需要大小写不敏感，否则会漏掉 NuttX
# 常见的 PANIC/ASSERT/HardFault 输出。两组规则不可用一个 grep -i 合并。
FAIL_RE_CASE='error[[:space:]]*=[[:space:]]*-[1-9][0-9]*|(^|[^A-Za-z])FAILED([^A-Za-z]|$)|(^|[^A-Za-z])FAIL([^A-Za-z]|$)'
FAIL_RE_FOLD='exception|panic|assert|abort|hard[[:space:]]*fault|data[[:space:]]+abort'

ts()   { date '+%Y-%m-%d %H:%M:%S'; }
# cap <out.raw> <duration_sec> [command]
cap() {
  local out diagnostic rc
  out=$(wslpath -w "$1")
  if [ -n "${3:-}" ]; then
    diagnostic=$(powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$PS1" \
      -Port COM11 -Baud 460800 -DurationSec "$2" -OutputFile "$out" \
      -Command "$3" 2>&1)
    rc=$?
  else
    diagnostic=$(powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$PS1" \
      -Port COM11 -Baud 460800 -DurationSec "$2" -OutputFile "$out" 2>&1)
    rc=$?
  fi

  if [ "$rc" -ne 0 ]; then
    log "CAPTURE ERROR rc=$rc file=$1 command=${3:-<none>}"
    printf '%s\n' "$diagnostic" | tee -a "$SUMMARY" >&2
    exit 1
  fi

  if [ ! -s "$1" ]; then
    log "CAPTURE ERROR empty-or-missing file=$1 command=${3:-<none>}"
    exit 1
  fi
}
log()  { echo "[$(ts)] $*" | tee -a "$SUMMARY"; }
# genuine_fail <file>  -> 打印真失败行数（非假阳性）
# 每一行至多计一次：先匹配大小写敏感的状态/FAIL 标记，再对崩溃文本做
# 大小写折叠匹配。这样既不会把 fail=0 计数器误报为 FAIL，也不会漏掉
# 行首 FAIL、PANIC、ASSERT、HardFault 等形式。
genuine_fail() {
  tr -d '\r' < "$1" |
    awk -v case_re="$FAIL_RE_CASE" -v fold_re="$FAIL_RE_FOLD" '
      $0 ~ case_re          { count++; next }
      tolower($0) ~ fold_re { count++ }
      END                   { print count + 0 }
    '
}

status_healthy() {
  grep -q 'AP state=READY' "$1" &&
    grep -q 'RPTUN state=CONNECTED' "$1" &&
    grep -q 'flags=00003fff' "$1" &&
    [ "$(genuine_fail "$1")" -eq 0 ]
}

echo "STRESS START logdir=$LOGDIR (RPMsg $([ -n "${STRESS_RPMSG:-}" ] && echo INCLUDED || echo EXCLUDED))" | tee -a "$SUMMARY"
overall_fail=0

# ---- Phase 0: 基线状态 ----
log "PHASE 0 baseline apctl status"
p0_status="$LOGDIR/p0-status.raw"
cap "$p0_status" 8 "apctl status"
if ! status_healthy "$p0_status"; then
  log "PHASE 0 baseline is not READY/CONNECTED; normalizing with apctl start"
  cap "$LOGDIR/p0-start.raw" 15 "apctl start"
  sleep 3
  p0_status="$LOGDIR/p0-status-after-start.raw"
  cap "$p0_status" 8 "apctl status"
fi
tr -d '\r' < "$p0_status" | grep -iE "state=|READY|CONNECTED|PASSED|ERROR" | head -20 | tee -a "$SUMMARY"
if ! status_healthy "$p0_status"; then
  log "PHASE 0 result: unhealthy baseline -> FAIL"
  exit 1
fi
log "PHASE 0 result: READY/CONNECTED flags=0x3fff genuine_fails=0 -> PASS"

# ---- Phase A: AP 生命周期（覆盖 SMP/CPU2/affinity/sem-wake/sem-loop/BLCY/
#        BP2P/BDUL/BMIG/BTIM 及 IPI 启动自检）----
log "PHASE A apctl cycle 20 (full AP start/stop generations)"
cap "$LOGDIR/pA-cycle20.raw" 220 "apctl cycle 20"
gens=$(tr -d '\r' < "$LOGDIR/pA-cycle20.raw" | grep -c "AP state=READY")
subs=$(tr -d '\r' < "$LOGDIR/pA-cycle20.raw" | grep -cE "state=PASSED")
fails=$(genuine_fail "$LOGDIR/pA-cycle20.raw")
if [ "$fails" -eq 0 ] && [ "$gens" -ge 20 ] && [ "$subs" -ge 100 ]; then
  phase_a_verdict=PASS
else
  phase_a_verdict=CHECK
  overall_fail=1
fi
log "PHASE A result: generations_READY=$gens subsystems_PASSED=$subs genuine_fails=$fails -> $phase_a_verdict"
cap "$LOGDIR/pA-start.raw" 15 "apctl start"   # 确保收尾回到 READY 供后续阶段

# ---- Phase B: 独立 apctl ipitest（仅做信息记录，不作为 IPI 通过/失败门槛）----
# 重要（2026-08-01 实测验证）：
#   - AP scheduler-online 模式：返回 "ipitest is disabled while AP scheduler-online mode"
#     （设计如此，ENOTSUP），不执行压测循环；
#   - AP 已 stop 后：打印 IPI 状态但 requested/completed/runs=0，压测循环同样不执行。
# => 本固件中独立 apctl ipitest 不是可用的 IPI 压力向量；IPI 仅由 Phase A/F 的
#    cycle 启动自检真正覆盖。此处运行一次仅用于记录该行为。
log "PHASE B apctl ipitest 1000 3000 (INFO: standalone ipitest is no-op in this fw; IPI covered by cycle self-tests)"
cap "$LOGDIR/pB-ipi.raw" 30 "apctl ipitest 1000 3000"
tr -d '\r' < "$LOGDIR/pB-ipi.raw" | grep -iE "disabled while AP scheduler|requested=|IPI 0->1|runs=" | head | tee -a "$SUMMARY"

# ---- Phase C: Mailbox (SDK MBOX0) 压力 ----
log "PHASE C apctl mbox 1000 1000 x5"
for i in 1 2 3 4 5; do
  cap "$LOGDIR/pC-mbox-$i.raw" 35 "apctl mbox 1000 1000"
  ok=$(tr -d '\r' < "$LOGDIR/pC-mbox-$i.raw" | grep -c "MBOX probe passed")
  fails=$(genuine_fail "$LOGDIR/pC-mbox-$i.raw")
  if [ "$ok" -ge 1 ] && [ "$fails" -eq 0 ]; then
    phase_c_verdict=PASS
  else
    phase_c_verdict=FAIL
    overall_fail=1
  fi
  log "PHASE C mbox#$i: probe_passed_lines=$ok genuine_fails=$fails -> $phase_c_verdict"
done

# ---- Phase D: GPIO / IRQ ----
# 三个工程测试程序 bkgpioc0 / bkgpioirq / bkirqtest 由 app/hello_app 提供，分别被
#   CONFIG_BK7258_GPIO_FOUNDATION_TEST / GPIO_IRQ_TEST / SDK_IRQ_TIMER_TEST 门控。
# 经核对：所有已提交 defconfig（含本镜像所用）都未开启这三项，故 NSH 中 command not found。
# 更关键：bkgpioc0 / bkgpioirq 是人工交互测试（源码注释 "requires USERKEY to remain on P29"，
#   需手动按 P29 USERKEY 并目视 P9 LED），不适合无人值守压测 —— 按决策跳过 GPIO/IRQ。
# 当前镜像另有 NuttX 通用 gpio 命令（CONFIG_EXAMPLES_GPIO=y），它是低层 GPIO 工具，
# 不是本工程的 IRQ 压测，不能替代上述三项。脚本仅探测可用性作信息记录。
log "PHASE D gpio/irq apps (detect availability; gated by CONFIG_BK7258_*)"
for cmd in bkgpioc0 bkgpioirq bkirqtest; do
  cap "$LOGDIR/pD-$cmd.raw" 20 "$cmd"
  if tr -d '\r' < "$LOGDIR/pD-$cmd.raw" | grep -qiE "command not found"; then
    log "PHASE D $cmd: UNAVAILABLE (CONFIG_BK7258_* not enabled in current defconfig)"
  else
    fails=$(genuine_fail "$LOGDIR/pD-$cmd.raw")
    if [ "$fails" -eq 0 ]; then
      phase_d_verdict=PASS
    else
      phase_d_verdict=FAIL
      overall_fail=1
    fi
    log "PHASE D $cmd: genuine_fails=$fails -> $phase_d_verdict"
  fi
done
# 仅作信息记录：NuttX 通用 gpio 命令（低层工具，非压测）
cap "$LOGDIR/pD-gpio-generic.raw" 12 "gpio"
if tr -d '\r' < "$LOGDIR/pD-gpio-generic.raw" | grep -qiE "command not found"; then
  log "PHASE D gpio (generic): absent"
else
  log "PHASE D gpio (generic): present (low-level tool, NOT a stress/IRQ test)"
fi

# ---- Phase E: 持续状态轮询（观察慢泄漏/退化，记录 heartbeat 递增与真 error）----
log "PHASE E sustained apctl status poll x10"
for i in $(seq 1 10); do
  cap "$LOGDIR/pE-status-$i.raw" 8 "apctl status"
  err=$(genuine_fail "$LOGDIR/pE-status-$i.raw")
  hb=$(tr -d '\r' < "$LOGDIR/pE-status-$i.raw" | grep -oE "heartbeat=[0-9]+" | head -1)
  if [ "$err" -eq 0 ] && [ -n "$hb" ]; then
    phase_e_verdict=OK
  else
    phase_e_verdict=DEGRADED
    overall_fail=1
  fi
  log "PHASE E poll#$i: $hb genuine_err=$err -> $phase_e_verdict"
  sleep 4
done

# ---- Phase F: 收尾生命周期稳定性 ----
log "PHASE F apctl cycle 10 (post-sustained stability)"
cap "$LOGDIR/pF-cycle10.raw" 130 "apctl cycle 10"
gens=$(tr -d '\r' < "$LOGDIR/pF-cycle10.raw" | grep -c "AP state=READY")
subs=$(tr -d '\r' < "$LOGDIR/pF-cycle10.raw" | grep -cE "state=PASSED")
fails=$(genuine_fail "$LOGDIR/pF-cycle10.raw")
if [ "$fails" -eq 0 ] && [ "$gens" -ge 10 ] && [ "$subs" -ge 50 ]; then
  phase_f_verdict=PASS
else
  phase_f_verdict=CHECK
  overall_fail=1
fi
log "PHASE F result: generations_READY=$gens subsystems_PASSED=$subs genuine_fails=$fails -> $phase_f_verdict"

# apctl cycle 的既定语义是在最后一轮验证 READY 后再次 stop。为保证本 SOP
# 可连续重复执行，结束前显式恢复 AP/RPTUN，并用独立 status 做最终健康门禁。
log "PHASE F restore AP/RPTUN to READY/CONNECTED"
cap "$LOGDIR/pF-start.raw" 15 "apctl start"
sleep 3
cap "$LOGDIR/pF-status.raw" 8 "apctl status"
if ! status_healthy "$LOGDIR/pF-status.raw"; then
  log "PHASE F restore result: unhealthy final state -> FAIL"
  exit 1
fi
log "PHASE F restore result: READY/CONNECTED flags=0x3fff genuine_fails=0 -> PASS"

# ---- Phase R（可选）: RPMsg 常驻 worker heap/阈值回归 ----
if [ -n "${STRESS_RPMSG:-}" ]; then
  log "PHASE R bkrpmsgtest all 100 60000 x20 (persistent-worker threshold/heap regression)"
  rpmsg_heap_baseline=
  for i in $(seq 1 20); do
    cap "$LOGDIR/pR-rpmsg-$i.raw" 75 "bkrpmsgtest all 100 60000"
    if tr -d '\r' < "$LOGDIR/pR-rpmsg-$i.raw" | grep -qiE "status=-12|ENOMEM|BRPT FAIL"; then
      log "PHASE R rpmsg#$i: FAIL (inspect BRPT HEAP/SPAWN and apctl status; do not assume vring exhaustion)"
      overall_fail=1
      break
    fi

    suite_pass=$(tr -d '\r' < "$LOGDIR/pR-rpmsg-$i.raw" | grep -c 'BRPT SUITE PASS')
    first_used=$(tr -d '\r' < "$LOGDIR/pR-rpmsg-$i.raw" | sed -n 's/.*BRPT HEAP .*start_used=\([0-9][0-9]*\).*/\1/p' | head -1)
    last_used=$(tr -d '\r' < "$LOGDIR/pR-rpmsg-$i.raw" | sed -n 's/.*report_used=\([0-9][0-9]*\).*/\1/p' | tail -1)
    if [ -z "$first_used" ] || [ -z "$last_used" ] ||
       [ "$suite_pass" -lt 1 ] || [ "$first_used" != "$last_used" ] ||
       { [ -n "$rpmsg_heap_baseline" ] && [ "$last_used" != "$rpmsg_heap_baseline" ]; }; then
      log "PHASE R rpmsg#$i: FAIL heap drift/missing evidence first=$first_used last=$last_used baseline=$rpmsg_heap_baseline suite_pass=$suite_pass"
      overall_fail=1
      break
    fi

    if [ -z "$rpmsg_heap_baseline" ]; then
      rpmsg_heap_baseline=$first_used
    fi

    log "PHASE R rpmsg#$i: PASS suite=$suite_pass heap_used=$last_used (stable)"
  done
fi

if [ "$overall_fail" -ne 0 ]; then
  log "STRESS DONE verdict=FAIL logdir=$LOGDIR"
  exit 1
fi

log "STRESS DONE verdict=PASS logdir=$LOGDIR"
