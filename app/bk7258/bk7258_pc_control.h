/****************************************************************************
 * app/bk7258/bk7258_pc_control.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/
#ifndef __APP_BK7258_PC_CONTROL_H
#define __APP_BK7258_PC_CONTROL_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include "bk7258_control_pair.h"
#include "bk7258_pc_grants.h"

/* One serialized owner holds grants, this lease, pair and transport. All
 * borrowed objects outlive close; grant mutations may occur between steps,
 * never concurrently with one. No filesystem I/O occurs in step or dispatch.
 * Every grant revision change invalidates the session, including exact
 * same-key regrants. Close releases only local TLS/staging; it cannot
 * cancel a previously accepted service job or prove transport/DMA shutdown.
 */

struct bkpc_control_s
{
  const struct bkpc_grants_s *grants;
  struct bkcontrol_pair_s *pair;
  bkcontrol_execute_t execute;
  bkcontrol_config_t config;
  void *context;
  uint64_t revision;
  uint32_t capabilities;
  uint8_t client[16];
  bool open;
};

/* Zero-init lease and pair. Uses only the independent persisted PC key.
 * All valid grants may read STATUS/INFO and parser CAP1. SCENES permits
 * FOCUS/EXPRESSION_TRIAL read/begin/apply; RESOURCES currently permits only
 * EYE_PACK read. The legacy HTTPS import is not the USB file installer.
 * All other config/basic mutations are denied, no OTA handler or SPV1 entry
 * is installed. TASKS/DIAGNOSTICS need explicit command contracts.
 */

int bkpc_control_start(struct bkpc_control_s *state,
                       struct bkcontrol_pair_s *pair,
                       const struct bkpc_grants_s *grants,
                       uint32_t generation, mbedtls_x509_crt *certificate,
                       mbedtls_pk_context *key, uint64_t (*now_ms)(void *),
                       void *clock_context, bkcontrol_execute_t execute,
                       bkcontrol_config_t config, void *context,
                       const struct bkprov_tls_transport_s *transport);
int bkpc_control_step(struct bkpc_control_s *state);
void bkpc_control_close(struct bkpc_control_s *state);

#endif
