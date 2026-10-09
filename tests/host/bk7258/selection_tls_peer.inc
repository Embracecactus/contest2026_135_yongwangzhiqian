/* SPDX-License-Identifier: Apache-2.0 */
/* Host-only ATT delivery and worker scheduling. TLS, SDC1, selection,
 * persistence and rendering are production implementations. The test binds
 * a synthetic authority directly: it does not prove phone owner lifecycle.
 */
#include "bk7258_control_pair.h"
#include <time.h>

static uint8_t tls_input[4096];
static size_t tls_received;

uint32_t bkprov_gatt_generation(void) { return 1; }
ssize_t bkprov_gatt_read(uint32_t generation, void *data, size_t size)
{
  assert(generation == 1);
  if (!tls_received) return -EAGAIN;
  if (size > tls_received) size = tls_received;
  if (size > 20) size = 20;
  memcpy(data, tls_input, size);
  tls_received -= size;
  memmove(tls_input, tls_input + size, tls_received);
  return size;
}
ssize_t bkprov_gatt_send(uint32_t generation, const void *data, size_t size)
{
  const uint8_t *bytes = data;
  assert(generation == 1 && size <= 20);
  printf("DATA ");
  for (size_t i = 0; i < size; i++) printf("%02x", bytes[i]);
  putchar('\n');
  return size;
}
int bkprov_storage_receipt(const uint8_t transaction[16])
{
  (void)transaction;
  assert(false); /* Provisioning is not a default-selection operation. */
  return -ENOTSUP;
}
static uint64_t tls_now(void *context)
{
  struct timespec now;
  (void)context;
  assert(clock_gettime(CLOCK_MONOTONIC, &now) == 0);
  return (uint64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}
static void selection_tls_peer(struct bkdisplay_service_s *service)
{
  struct bkcontrol_pair_s pair = {0};
  mbedtls_x509_crt cert;
  mbedtls_pk_context key;
  mbedtls_entropy_context entropy;
  mbedtls_ctr_drbg_context random;
  const uint8_t owner[32] = {42}, epoch[16] = {7};
  char line[513];
  unsigned initial_writes = writes;
  bool connected = true;
  mbedtls_x509_crt_init(&cert);
  mbedtls_pk_init(&key);
  mbedtls_entropy_init(&entropy);
  mbedtls_ctr_drbg_init(&random);
  assert(mbedtls_ctr_drbg_seed(&random, mbedtls_entropy_func,
                              &entropy, NULL, 0) == 0);
  assert(mbedtls_x509_crt_parse_file(&cert, getenv("SHANIU_TEST_CERT")) == 0);
  assert(mbedtls_pk_parse_keyfile(&key, getenv("SHANIU_TEST_KEY"), NULL,
                                 mbedtls_ctr_drbg_random, &random) == 0);
  assert(bkselection_control_bind(&selection_control, epoch) == 0);
  assert(bkcontrol_pair_start(&pair, 1, &cert, &key, owner, tls_now,
                              NULL, execute, NULL) == 0);
  assert(bkcontrol_session_set_config_handler(&pair.session, config) == 0);
  puts("READY");
  fflush(stdout);
  while (fgets(line, sizeof(line), stdin))
    {
      if (!strcmp(line, "disconnect\n"))
        {
          assert(connected);
          bkcontrol_pair_close(&pair);
          connected = false;
          tls_received = 0;
        }
      else if (!strcmp(line, "connect\n"))
        {
          assert(!connected);
          memset(&pair, 0, sizeof(pair));
          assert(bkcontrol_pair_start(&pair, 1, &cert, &key, owner, tls_now,
                                     NULL, execute, NULL) == 0);
          assert(bkcontrol_session_set_config_handler(&pair.session, config) == 0);
          connected = true;
        }
      else if (!strncmp(line, "wire ", 5))
        {
          size_t n = strcspn(line + 5, "\n");
          assert(connected);
          assert(n && n % 2 == 0 && n / 2 <= 20);
          assert(tls_received + n / 2 <= sizeof(tls_input));
          for (size_t i = 0; i < n / 2; i++)
            {
              unsigned value;
              assert(sscanf(line + 5 + i * 2, "%2x", &value) == 1);
              tls_input[tls_received++] = value;
            }
        }
      else if (!strncmp(line, "time ", 5))
        {
          unsigned long long value;
          assert(sscanf(line + 5, "%llu", &value) == 1);
          clock_ms = value;
        }
      else if (!strcmp(line, "trial-step\n"))
        {
          struct bkdisplay_trial_status_s current;
          assert(bk7258_display_trial_status(&current) == 0);
          expect_green = current.state == BKDISPLAY_TRIAL_PENDING;
          if (!bkdisplay_intent_step(service, true))
            (void)bkdisplay_trial_step(service, true);
        }
      else if (!strcmp(line, "step\n"))
        {
          struct bkdisplay_selection_status_s current;
          assert(bk7258_display_selection_status(&current) == 0);
          expect_green = !strcmp(current.version.filename,
                                  "shaniu-upload-v1.bkep");
          (void)bkdisplay_selection_recover_step(service);
          (void)bkdisplay_selection_step(service, true);
        }
      else if (!strcmp(line, "fail-unmount\n")) fail_unmount = true;
      else assert(!strcmp(line, "poll\n") || !strcmp(line, "stats\n"));
      if (connected) assert(bkcontrol_pair_step(&pair) >= 0);
      if (!strcmp(line, "stats\n"))
        {
          struct bkdisplay_selection_version_s version;
          assert(bkdisplay_store_selection_version(root, &version) == 0);
          printf("STATS %u %u %u %u %llu %s\n", writes - initial_writes,
                 frames, mounts, unmounts,
                 (unsigned long long)version.revision, version.filename);
        }
      puts("READY");
      fflush(stdout);
    }
  bkcontrol_pair_close(&pair);
  mbedtls_pk_free(&key);
  mbedtls_x509_crt_free(&cert);
  mbedtls_ctr_drbg_free(&random);
  mbedtls_entropy_free(&entropy);
}
