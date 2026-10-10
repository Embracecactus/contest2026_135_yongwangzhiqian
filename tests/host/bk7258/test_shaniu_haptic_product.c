/* SPDX-License-Identifier: Apache-2.0 */
/* Current GPIO motor service; scheduler and physical motor only are mocked. */
#include "bk7258_sensor_rpc_test.h"
#define CONFIG_BK7258_HAPTIC_SERVICE 1
#define CONFIG_BK7258_HAPTIC_RPC_PRIORITY 80
#define CONFIG_BK7258_HAPTIC_RPC_STACKSIZE 2048
#define BK7258_BOARD_MOTOR_MAX_ON_MS 100u
#define OK 0
#define usleep nxsig_usleep
typedef long sclock_t;
static bool motor_on, motor_ready = true;
static int motor_start_error, motor_stop_error;
static unsigned motor_starts, motor_stops;
static bool bk7258_aidk_motor_ready(void) { return motor_ready; }
static int bk7258_aidk_motor_set(bool on)
{
  assert(in_worker && callback_depth == 0);
  if (on)
    {
      motor_starts++;
      if (motor_start_error) return motor_start_error;
    }
  else
    {
      motor_stops++;
      if (motor_stop_error) return motor_stop_error;
    }
  motor_on = on;
  return 0;
}
static int rpmsg_trysend(struct rpmsg_endpoint *e, const void *data, int size)
{ (void)e; (void)data; return size; }
#include "bk7258_haptic_service.c"
static void drain(void)
{
  in_worker = true;
  if (setjmp(worker_idle) == 0) worker_entry(0, NULL);
  in_worker = false;
}
static void cancel_during_pulse(void)
{ assert(bkhaptic_service_stop_product() == 0); }
int main(int argc, char **argv)
{
  assert(argc == 2);
  assert(bkhaptic_service_initialize() == 0);
  if (!strcmp(argv[1], "limit"))
    {
      assert(bkhaptic_service_pulse(0) == -EINVAL);
      assert(bkhaptic_service_pulse(101) == -EINVAL);
      assert(!motor_starts);
    }
  else
    {
      assert(bkhaptic_service_pulse(40) == 0);
      assert(!motor_starts && !motor_on);
      assert(bkhaptic_service_pulse(40) == -EBUSY);
      if (!strcmp(argv[1], "cancel-pending")) cancel_during_pulse();
      if (!strcmp(argv[1], "cancel-active")) sleep_hook = cancel_during_pulse;
      if (!strcmp(argv[1], "capture-quiet")) motor_start_error = -EBUSY;
      if (!strcmp(argv[1], "stop-error")) motor_stop_error = -EIO;
      drain();
      if (!strcmp(argv[1], "stop-error"))
        {
          assert(g_bkhaptic.local_result == -EIO);
          assert(bkhaptic_service_pulse(40) == -EIO);
          assert(bkhaptic_service_quiesce(true) == -EIO);
        }
      else
        {
          assert(!motor_on);
          if (!strcmp(argv[1], "cancel-active"))
            {
              assert(ticks <= 5);
              assert(g_bkhaptic.local_result == -ECANCELED);
            }
          if (!strcmp(argv[1], "cancel-pending")) assert(motor_starts == 0);
          if (!strcmp(argv[1], "capture-quiet"))
            assert(g_bkhaptic.local_result == -EBUSY);
        }
    }
  puts("CONTRACT_PASS");
  return 0;
}
