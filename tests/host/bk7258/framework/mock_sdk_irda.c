/*
 * mock_sdk_irda.c - SDK mock for the IrDA test suite.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "mock_sdk_irda.h"

#include <stddef.h>
#include <string.h>

#include "nuttx/fs/fs.h"

#define MOCK_IRDA_MAX_CALLS 8

static struct mock_irda_gpio_call_s g_gpio_calls[MOCK_IRDA_MAX_CALLS];
static struct mock_irda_int_call_s g_int_calls[MOCK_IRDA_MAX_CALLS];
static int g_gpio_call_count;
static int g_int_call_count;
static int g_gpio_result;
static int g_int_result;
static void (*g_last_isr)(void);
static int g_fs_register_result;
static int g_fs_register_calls;
static char g_fs_register_path[32];

void mock_irda_sdk_reset(void)
{
  g_gpio_call_count = 0;
  g_int_call_count = 0;
  g_gpio_result = 0;
  g_int_result = 0;
  g_last_isr = NULL;
  g_fs_register_result = 0;
  g_fs_register_calls = 0;
  g_fs_register_path[0] = '\0';
}

void mock_irda_set_gpio_result(int result)
{
  g_gpio_result = result;
}

void mock_irda_set_int_result(int result)
{
  g_int_result = result;
}

int mock_irda_gpio_calls(void)
{
  return g_gpio_call_count;
}

int mock_irda_int_calls(void)
{
  return g_int_call_count;
}

const struct mock_irda_gpio_call_s *mock_irda_gpio_call(int index)
{
  return (index >= 0 && index < g_gpio_call_count) ?
         &g_gpio_calls[index] : NULL;
}

const struct mock_irda_int_call_s *mock_irda_int_call(int index)
{
  return (index >= 0 && index < g_int_call_count) ?
         &g_int_calls[index] : NULL;
}

void (*mock_irda_isr(void))(void)
{
  return g_last_isr;
}

int gpio_dev_map(gpio_id_t gpio_id, gpio_dev_t gpio_dev)
{
  if (g_gpio_call_count < MOCK_IRDA_MAX_CALLS)
    {
      g_gpio_calls[g_gpio_call_count].id = gpio_id;
      g_gpio_calls[g_gpio_call_count].dev = gpio_dev;
      g_gpio_call_count++;
    }

  return g_gpio_result;
}

int bk_int_isr_register(icu_int_src_t dev, void (*isr)(void), void *arg)
{
  (void)arg;

  if (g_int_call_count < MOCK_IRDA_MAX_CALLS)
    {
      g_int_calls[g_int_call_count].src = dev;
      g_int_calls[g_int_call_count].isr_registered = (isr != NULL);
      g_int_call_count++;
    }

  if (isr != NULL)
    {
      g_last_isr = isr;
    }

  return g_int_result;
}

int register_driver(FAR const char *path,
                    FAR const struct file_operations *fops, mode_t mode,
                    FAR void *priv)
{
  (void)fops;
  (void)mode;
  (void)priv;

  g_fs_register_calls++;
  if (path != NULL)
    {
      (void)strncpy(g_fs_register_path, path, sizeof(g_fs_register_path) - 1);
      g_fs_register_path[sizeof(g_fs_register_path) - 1] = '\0';
    }

  return g_fs_register_result;
}

void mock_irda_set_fs_register_result(int result)
{
  g_fs_register_result = result;
}

int mock_irda_fs_register_calls(void)
{
  return g_fs_register_calls;
}

const char *mock_irda_fs_register_path(void)
{
  return g_fs_register_path;
}
