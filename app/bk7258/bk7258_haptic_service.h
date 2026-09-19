/****************************************************************************
 * app/bk7258/bk7258_haptic_service.h
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/
#ifndef __APP_BK7258_BK7258_HAPTIC_SERVICE_H
#define __APP_BK7258_BK7258_HAPTIC_SERVICE_H
#include <stdbool.h>
int bkhaptic_service_initialize(void);
int bkhaptic_service_pulse(unsigned int duration_ms);
/* Serialized product owner only, while MIC is released. Bounded completion. */
int bkhaptic_service_pulse_wait(unsigned int duration_ms);
int bkhaptic_service_stop_product(void);
int bkhaptic_service_quiesce(bool quiesce);
#endif
