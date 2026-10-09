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
#include "bk7258_provision_storage.h"

/* A bounded, coherent device-internal snapshot, never a filesystem read.
 * The product owner and source context outlive close. Binding identifies the
 * primary configuration and the grant revision. The consumer wipes private
 * snapshot buffers on every path.
 */

struct bkpc_source_s
{
  void *context;
  int (*snapshot)(void *context, uint64_t *binding,
                  struct bkprov_pc_snapshot_s *view);
};

/* One serialized product owner steps the lease and reads the snapshot.
 * The file worker's mutable grants object is never borrowed. Unavailable or
 * changed snapshots close the session; closing cannot cancel an accepted job
 * or prove transport/DMA shutdown.
 */

struct bkpc_control_s
{
  struct bkpc_source_s source;
  uint64_t binding;
  struct bkcontrol_pair_s *pair;
  bkcontrol_execute_t execute;
  bkcontrol_config_t config;
  void (*closed)(void *context);
  void *closed_context;
  void *context;
  uint64_t revision;
  uint32_t capabilities;
  uint8_t client[16];
  bool open;
};

/* Zero-init lease and pair. Uses only the independent persisted PC key.
 * All valid grants may read STATUS/INFO and parser CAP1. SCENES permits
 * FOCUS/EXPRESSION_TRIAL read/begin/apply. RESOURCES permits EYE_PACK read,
 * RESOURCE_JOB installation and DEFAULT_SELECTION jobs. Legacy HTTPS import
 * is not the USB file installer.
 * All other config/basic mutations are denied, no OTA handler or SPV1 entry
 * is installed. TASKS permits PTE1/PTS1 only. DIAGNOSTICS remains unbound in
 * production; an engineering build may bind only config kind 19.
 */

int bkpc_control_start(struct bkpc_control_s *state,
                       struct bkcontrol_pair_s *pair,
                       const struct bkpc_source_s *source,
                       uint32_t generation, mbedtls_x509_crt *certificate,
                       mbedtls_pk_context *key, uint64_t (*now_ms)(void *),
                       void *clock_context, bkcontrol_execute_t execute,
                       bkcontrol_config_t config, void *context,
                       const struct bkprov_tls_transport_s *transport);
int bkpc_control_step(struct bkpc_control_s *state);
int bkpc_control_set_close_handler(struct bkpc_control_s *state,
                                   void (*closed)(void *context),
                                   void *context);
void bkpc_control_close(struct bkpc_control_s *state);

#endif
