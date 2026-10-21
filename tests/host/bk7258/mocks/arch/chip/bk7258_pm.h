/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __MOCK_ARCH_CHIP_BK7258_PM_H
#define __MOCK_ARCH_CHIP_BK7258_PM_H

enum bk7258_pm_clock_e
{
  BK7258_PM_CLOCK_AUDIO = 8,
};

enum bk7258_pm_freq_client_e
{
  BK7258_PM_FREQ_CLIENT_AUDIO = 4,
};

enum bk7258_pm_cpu_freq_e
{
  BK7258_PM_OPP_480M = 6,
  BK7258_PM_OPP_DEFAULT = 7,
};

int bk7258_pm_initialize(void);
int bk7258_pm_clock_get(enum bk7258_pm_clock_e clock);
int bk7258_pm_clock_put(enum bk7258_pm_clock_e clock);
int bk7258_pm_frequency_vote(enum bk7258_pm_freq_client_e client,
                            enum bk7258_pm_cpu_freq_e frequency);

#endif
