/****************************************************************************
 * boards/bk7258/t5_board/src/bk7258_t5_board_rgb_lcd_backlight.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * T5-Board RGB LCD backlight validation through NuttX /dev/pwm0.
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_T5_BOARD_RGB_LCD_PWM_VALIDATION

#include <sys/ioctl.h>

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include <nuttx/kthread.h>
#include <nuttx/timers/pwm.h>

#include <arch/board/board.h>

#define T5_PWM_BACKLIGHT_DEVPATH        "/dev/pwm0"
#define T5_PWM_BACKLIGHT_DELAY_US       1000000u
#define T5_PWM_BACKLIGHT_STEP_US        2000000u
#define T5_PWM_BACKLIGHT_FINAL_US       15000000u
#define T5_PWM_BACKLIGHT_STACK_SIZE     3072
#define T5_PWM_BACKLIGHT_ROUNDS         1u
#define T5_PWM_DUTY_SCALE               65536u

#if CONFIG_BK7258_PWM_CHAN != BK7258_BOARD_LCD_BACKLIGHT_PWM_CHAN
#  error "T5 RGB LCD backlight validation requires PWM3 on GPIO9"
#endif

static int t5_pwm_backlight_set(int fd, uint8_t duty_percent)
{
  struct pwm_info_s info;
  uint32_t duty;

  memset(&info, 0, sizeof(info));
  info.frequency = BK7258_BOARD_LCD_BACKLIGHT_PWM_FREQUENCY;
  duty = ((uint32_t)duty_percent * T5_PWM_DUTY_SCALE) / 100u;
  info.duty = (ub16_t)(duty >= T5_PWM_DUTY_SCALE ?
                       T5_PWM_DUTY_SCALE - 1u : duty);

  if (ioctl(fd, PWMIOC_SETCHARACTERISTICS,
            (unsigned long)(uintptr_t)&info) < 0)
    {
      return -errno;
    }

  return OK;
}

static int t5_pwm_backlight_validation_thread(int argc, FAR char *argv[])
{
  static const uint8_t g_duties[] = {100u, 0u, 10u, 50u, 90u};
  FAR const char *stage = "open";
  unsigned int round;
  unsigned int index;
  int fd;
  int ret;

  (void)argc;
  (void)argv;

  usleep(T5_PWM_BACKLIGHT_DELAY_US);
  fd = open(T5_PWM_BACKLIGHT_DEVPATH, O_RDONLY);
  if (fd < 0)
    {
      ret = -errno;
      goto failed;
    }

  stage = "configure";
  ret = t5_pwm_backlight_set(fd, g_duties[0]);
  if (ret < 0)
    {
      goto failed_with_fd;
    }

  stage = "start";
  if (ioctl(fd, PWMIOC_START, 0) < 0)
    {
      ret = -errno;
      goto failed_with_fd;
    }

  for (round = 0; round < T5_PWM_BACKLIGHT_ROUNDS; round++)
    {
      for (index = 0; index < sizeof(g_duties); index++)
        {
          stage = "update";
          ret = t5_pwm_backlight_set(fd, g_duties[index]);
          if (ret < 0)
            {
              goto failed_with_stop;
            }

          syslog(LOG_INFO, "BKPWM duty=%u%% channel=%u gpio=%u\n",
                 g_duties[index],
                 BK7258_BOARD_LCD_BACKLIGHT_PWM_CHAN,
                 BK7258_BOARD_LCD_BACKLIGHT_GPIO);
          usleep(T5_PWM_BACKLIGHT_STEP_US);
        }
    }

  stage = "final";
  ret = t5_pwm_backlight_set(fd, 100u);
  if (ret < 0)
    {
      goto failed_with_stop;
    }

  syslog(LOG_INFO, "BKPWM final=100%% hold=15s channel=%u gpio=%u\n",
         BK7258_BOARD_LCD_BACKLIGHT_PWM_CHAN,
         BK7258_BOARD_LCD_BACKLIGHT_GPIO);
  usleep(T5_PWM_BACKLIGHT_FINAL_US);
  stage = "stop";
  if (ioctl(fd, PWMIOC_STOP, 0) < 0)
    {
      ret = -errno;
      goto failed_with_fd;
    }

  stage = "close";
  if (close(fd) < 0)
    {
      ret = -errno;
      goto failed;
    }

  syslog(LOG_INFO,
         "BKPWM PASS channel=%u gpio=%u frequency=%u duties=100/0/10/50/90%%\n",
         BK7258_BOARD_LCD_BACKLIGHT_PWM_CHAN,
         BK7258_BOARD_LCD_BACKLIGHT_GPIO,
         BK7258_BOARD_LCD_BACKLIGHT_PWM_FREQUENCY);
  return OK;

failed_with_stop:
  (void)ioctl(fd, PWMIOC_STOP, 0);
failed_with_fd:
  close(fd);
failed:
  syslog(LOG_ERR, "BKPWM FAIL stage=%s ret=%d\n", stage, ret);
  return ret;
}

int bk7258_t5_board_rgb_lcd_backlight_validation_initialize(void)
{
  int pid;

  pid = kthread_create("bkpwm-backlight", SCHED_PRIORITY_DEFAULT,
                       T5_PWM_BACKLIGHT_STACK_SIZE,
                       t5_pwm_backlight_validation_thread, NULL);
  return pid < 0 ? pid : OK;
}

#endif /* CONFIG_BK7258_T5_BOARD_RGB_LCD_PWM_VALIDATION */
