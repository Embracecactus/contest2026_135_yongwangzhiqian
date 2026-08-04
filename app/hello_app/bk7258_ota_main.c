/****************************************************************************
 * contest2026_135_yongwangzhiqian/app/hello_app/bk7258_ota_main.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * N15-F validation-only OTA command.  This is deliberately not a production
 * updater: artifacts are unsigned and every Flash mutation requires an exact
 * generation-bound operator token.
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <nuttx/clock.h>
#include <nuttx/timers/watchdog.h>

#include <arch/chip/bk7258_amp.h>
#include <arch/chip/bk7258_ota_fault.h>
#include <arch/chip/bk7258_ota_staging.h>
#include <arch/chip/bk7258_ota_trial.h>
#include <arch/chip/bk7258_psram.h>

#define BKOTA_TOKEN_PREFIX "N15-WRITE-"

struct bkota_file_source_s
{
  int fd;
  uint32_t size;
};

struct bkota_memory_source_s
{
  const volatile uint8_t *base;
  uint32_t size;
};

static uint64_t bkota_now_ms(void)
{
  return (uint64_t)TICK2MSEC(clock_systime_ticks());
}

static int bkota_u64(const char *text, uint64_t *value)
{
  char *end;
  unsigned long long parsed;

  errno = 0;
  parsed = strtoull(text, &end, 0);
  if (errno != 0 || text[0] == '\0' || *end != '\0')
    {
      return -EINVAL;
    }

  *value = (uint64_t)parsed;
  return 0;
}

static int bkota_u32(const char *text, uint32_t *value)
{
  uint64_t parsed;
  int ret;

  ret = bkota_u64(text, &parsed);
  if (ret < 0 || parsed > UINT32_MAX)
    {
      return -EINVAL;
    }

  *value = (uint32_t)parsed;
  return 0;
}

static int bkota_authorize(const char *token, uint64_t generation)
{
  char expected[48];
  int length;

  length = snprintf(expected, sizeof(expected), BKOTA_TOKEN_PREFIX "%" PRIu64,
                    generation);
  if (length < 0 || (size_t)length >= sizeof(expected) ||
      strcmp(token, expected) != 0)
    {
      printf("BKOTA DENY expected_token=" BKOTA_TOKEN_PREFIX "%" PRIu64
             "\n", generation);
      return -EACCES;
    }

  return 0;
}

static const char *bkota_fault_name(uint32_t point)
{
  switch (point)
    {
      case BK7258_OTA_FAULT_NONE:
        return "none";
      case BK7258_OTA_FAULT_STAGE_ERASE:
        return "stage-erase";
      case BK7258_OTA_FAULT_STAGE_WRITE:
        return "stage-write";
      case BK7258_OTA_FAULT_STAGE_READ:
        return "stage-read";
      case BK7258_OTA_FAULT_PUBLISH_READ:
        return "publish-read";
      case BK7258_OTA_FAULT_PUBLISH_ERASE:
        return "publish-erase";
      case BK7258_OTA_FAULT_PUBLISH_WRITE:
        return "publish-write";
      case BK7258_OTA_FAULT_TRIAL_READ:
        return "trial-read";
      case BK7258_OTA_FAULT_TRIAL_WRITE:
        return "trial-write";
      default:
        return "invalid";
    }
}

static int bkota_fault_parse(const char *name,
                             enum bk7258_ota_fault_point_e *point)
{
  uint32_t candidate;

  if (name == NULL || point == NULL)
    {
      return -EINVAL;
    }

  for (candidate = BK7258_OTA_FAULT_STAGE_ERASE;
       candidate < BK7258_OTA_FAULT_POINT_COUNT; candidate++)
    {
      if (strcmp(name, bkota_fault_name(candidate)) == 0)
        {
          *point = (enum bk7258_ota_fault_point_e)candidate;
          return 0;
        }
    }

  return -EINVAL;
}

static void bkota_fault_print(const char *label,
                              const struct bk7258_ota_fault_status_s *status)
{
  printf("BKOTA FAULT %s point=%s ordinal=%" PRIu32
         " seen=%" PRIu32 " generation=%" PRIu64
         " configured=%u active=%u triggered=%u\n",
         label, bkota_fault_name(status->point), status->ordinal,
         status->seen, status->generation,
         status->configured ? 1u : 0u, status->active ? 1u : 0u,
         status->triggered ? 1u : 0u);
}

static int bkota_fault_finish_session(void)
{
  struct bk7258_ota_fault_status_s status;
  int ret;

  memset(&status, 0, sizeof(status));
  ret = bk7258_ota_fault_finish(&status);
  if (ret == 0 && status.configured)
    {
      bkota_fault_print("RESULT", &status);
    }

  return ret;
}

static int bkota_disarm(void)
{
  int staging;
  int metadata;

  staging = bk7258_ota_staging_set_write_enabled(false);
  metadata = bk7258_ota_trial_set_write_enabled(false);
  if (staging < 0)
    {
      return staging;
    }

  return metadata;
}

static int bkota_source_read(void *arg, uint32_t offset, uint8_t *buffer,
                             size_t len)
{
  struct bkota_file_source_s *source = arg;
  size_t copied = 0;
  ssize_t count;

  if (source == NULL || buffer == NULL || offset > source->size ||
      len > source->size - offset)
    {
      return -ERANGE;
    }

  if (lseek(source->fd, (off_t)offset, SEEK_SET) != (off_t)offset)
    {
      return errno == 0 ? -EIO : -errno;
    }

  while (copied < len)
    {
      count = read(source->fd, buffer + copied, len - copied);
      if (count < 0)
        {
          if (errno == EINTR)
            {
              continue;
            }

          return -errno;
        }

      if (count == 0)
        {
          return -EIO;
        }

      copied += (size_t)count;
    }

  return 0;
}

static void bkota_memory_barrier(void)
{
  __asm volatile ("dmb sy" ::: "memory");
}

static int bkota_memory_read(void *arg, uint32_t offset, uint8_t *buffer,
                             size_t len)
{
  struct bkota_memory_source_s *source = arg;
  size_t index;

  if (source == NULL || source->base == NULL || buffer == NULL ||
      offset > source->size || len > source->size - offset)
    {
      return -ERANGE;
    }

  bkota_memory_barrier();
  for (index = 0; index < len; index++)
    {
      buffer[index] = source->base[offset + index];
    }

  bkota_memory_barrier();
  return 0;
}

static int bkota_memory_copy(uintptr_t address, uint8_t *buffer, size_t len)
{
  struct bkota_memory_source_s source;

  if (address > UINTPTR_MAX - len)
    {
      return -ERANGE;
    }

  source.base = (const volatile uint8_t *)address;
  source.size = (uint32_t)len;
  return bkota_memory_read(&source, 0, buffer, len);
}

static int bkota_transfer_ready(struct bk7258_psram_info_s *info)
{
  uintptr_t physical_end;
  int ret;

  if (info == NULL)
    {
      return -EINVAL;
    }

  memset(info, 0, sizeof(*info));
  if (!bk7258_psram_ready())
    {
      return -EAGAIN;
    }

  if (!bk7258_psram_mpu_valid())
    {
      return -EACCES;
    }

  ret = bk7258_psram_get_info(info);
  if (ret < 0)
    {
      return ret;
    }

  if (info->ready == 0 || info->init_status < 0)
    {
      return -EAGAIN;
    }

  if (info->capacity < BK7258_PSRAM_16M_SIZE)
    {
      return -ENOSPC;
    }

  physical_end = BK7258_PSRAM_BASE + (uintptr_t)info->capacity;
  if (BK7258_OTA_TRANSFER_CANDIDATE_ADDRESS <
        BK7258_PSRAM_BASE + BK7258_PSRAM_8M_SIZE ||
      BK7258_OTA_TRANSFER_CANDIDATE_ADDRESS +
        BK7258_OTA_TRANSFER_CANDIDATE_SIZE !=
        BK7258_OTA_TRANSFER_DESCRIPTOR_ADDRESS ||
      BK7258_OTA_TRANSFER_DESCRIPTOR_ADDRESS +
        BK7258_OTA_STAGE_DESCRIPTOR_SIZE >
        BK7258_OTA_TRANSFER_RECORD_ADDRESS ||
      BK7258_OTA_TRANSFER_RECORD_ADDRESS +
        BK7258_OTA_TRANSFER_RECORD_SIZE != BK7258_OTA_TRANSFER_END ||
      BK7258_OTA_TRANSFER_END > physical_end)
    {
      return -ERANGE;
    }

  return 0;
}

static int bkota_read_exact(const char *path, uint8_t *buffer, size_t size)
{
  uint8_t extra;
  size_t copied = 0;
  ssize_t count;
  int saved;
  int fd;

  fd = open(path, O_RDONLY);
  if (fd < 0)
    {
      return -errno;
    }

  while (copied < size)
    {
      count = read(fd, buffer + copied, size - copied);
      if (count < 0 && errno == EINTR)
        {
          continue;
        }

      if (count <= 0)
        {
          saved = count < 0 ? errno : EIO;
          close(fd);
          return -saved;
        }

      copied += (size_t)count;
    }

  do
    {
      count = read(fd, &extra, 1);
    }
  while (count < 0 && errno == EINTR);
  saved = count < 0 ? errno : 0;
  close(fd);
  if (saved != 0)
    {
      return -saved;
    }

  return count == 0 ? 0 : -EFBIG;
}

static int bkota_open_source(const char *path,
                             struct bkota_file_source_s *source)
{
  struct stat info;

  source->fd = open(path, O_RDONLY);
  if (source->fd < 0)
    {
      return -errno;
    }

  if (fstat(source->fd, &info) < 0)
    {
      int saved = errno;
      close(source->fd);
      source->fd = -1;
      return -saved;
    }

  if (info.st_size <= 0 || (uint64_t)info.st_size > UINT32_MAX)
    {
      close(source->fd);
      source->fd = -1;
      return -EFBIG;
    }

  source->size = (uint32_t)info.st_size;
  return 0;
}

static int bkota_expected(const char *generation, const char *timestamp,
                          const char *version, const char *base_version,
                          const char *timeout,
                          struct bk7258_ota_expected_s *expected,
                          uint32_t *timeout_ms)
{
  int length;

  if (bkota_u64(generation, &expected->generation) < 0 ||
      expected->generation == 0 ||
      bkota_u32(timestamp, &expected->timestamp) < 0)
    {
      return -EINVAL;
    }

  length = snprintf(expected->version, sizeof(expected->version), "%s",
                    version);
  if (length <= 0 || (size_t)length >= sizeof(expected->version))
    {
      return -EINVAL;
    }

  length = snprintf(expected->base_version,
                    sizeof(expected->base_version), "%s", base_version);
  if (length <= 0 || (size_t)length >= sizeof(expected->base_version) ||
      strcmp(expected->version, expected->base_version) == 0)
    {
      return -EINVAL;
    }

  if (timeout_ms != NULL &&
      (timeout == NULL || bkota_u32(timeout, timeout_ms) < 0 ||
       *timeout_ms == 0))
    {
      return -EINVAL;
    }

  return 0;
}

static void bkota_print_digest(const uint8_t digest[32])
{
  unsigned int index;

  for (index = 0; index < 32; index++)
    {
      printf("%02x", digest[index]);
    }
}

static int bkota_status(void)
{
  struct bk7258_ap_supervisor_status_s supervisor;
  struct bk7258_ota_fault_status_s fault;
  struct bk7258_ota_trial_status_s trial;
  int fault_ret;
  int supervisor_ret;
  int ret;

  memset(&trial, 0, sizeof(trial));
  ret = bk7258_ota_trial_get_status(&trial, 2000);
  printf("BKOTA STATUS ret=%d format=%" PRIu32
         " state=%" PRIu32 " records=%" PRIu32
         " sequence=%" PRIu64 " generation=%" PRIu64
         " bank=%" PRIu32 " base=%" PRIu32 " target=%" PRIu32
         " stable=%" PRIu32 " active=%" PRIu32
         " valid=%u trusted=%u erased=%u degraded=%u secondary=%u"
         " stage_gate=%u metadata_gate=%u\n",
         ret, trial.format, trial.state, trial.valid_records,
         trial.sequence, trial.generation, trial.selected_bank,
         trial.base_slot, trial.target_slot, trial.stable_slot,
         trial.active_slot, trial.metadata_valid ? 1u : 0u,
         trial.metadata_trusted ? 1u : 0u,
         trial.metadata_erased ? 1u : 0u,
         trial.metadata_degraded ? 1u : 0u,
         trial.secondary_mapping_active ? 1u : 0u,
         trial.staging_write_enabled ? 1u : 0u,
         trial.metadata_write_enabled ? 1u : 0u);

  memset(&supervisor, 0, sizeof(supervisor));
  supervisor_ret = bk7258_ap_supervisor_get_status(&supervisor);
  printf("BKOTA HEALTH ret=%d state=%" PRIu32 " reason=%" PRIu32
         " generation=%" PRIu32 " faults=%" PRIu32
         " recoveries=%" PRIu32 " flags=0x%08" PRIx32
         " injection=%" PRIu32 " last_error=%" PRId32 "\n",
         supervisor_ret, supervisor.state, supervisor.reason,
         supervisor.generation, supervisor.fault_count,
         supervisor.recovery_count, supervisor.flags,
         supervisor.injection, supervisor.last_error);

  memset(&fault, 0, sizeof(fault));
  fault_ret = bk7258_ota_fault_get_status(&fault);
  if (fault_ret == 0)
    {
      bkota_fault_print("STATUS", &fault);
    }
  else
    {
      printf("BKOTA FAULT STATUS ret=%d\n", fault_ret);
    }

  return ret;
}

static int bkota_run_validate_or_stage(
  const char *operation, bool stage,
  enum bk7258_ota_slot_e target_slot,
  const uint8_t descriptor[BK7258_OTA_STAGE_DESCRIPTOR_SIZE],
  const struct bk7258_ota_expected_s *expected,
  const struct bk7258_ota_source_s *source, uint32_t timeout_ms)
{
  struct bk7258_ota_stage_result_s result;
  uint64_t started;
  uint64_t elapsed;
  int disarm_ret;
  int fault_ret;
  int ret;

  memset(&result, 0, sizeof(result));
  started = bkota_now_ms();
  if (stage)
    {
      ret = bk7258_ota_fault_begin(expected->generation,
                                   BK7258_OTA_FAULT_STAGE_MASK);
      if (ret == 0)
        {
          ret = bk7258_ota_staging_set_write_enabled(true);
        }

      if (ret == 0)
        {
          ret = bk7258_ota_staging_stage_inactive(
            target_slot, descriptor, expected, source, timeout_ms, &result);
        }

      disarm_ret = bk7258_ota_staging_set_write_enabled(false);
      if (ret == 0 && disarm_ret < 0)
        {
          ret = disarm_ret;
        }

      fault_ret = bkota_fault_finish_session();
      if (ret == 0 && fault_ret < 0)
        {
          ret = fault_ret;
        }
    }
  else
    {
      ret = bk7258_ota_staging_validate_slot(
        target_slot, descriptor, expected, source, &result);
    }

  elapsed = bkota_now_ms() - started;
  printf("BKOTA %s ret=%d phase=%u target=%u generation=%" PRIu64
         " elapsed_ms=%" PRIu64 " sectors=%" PRIu32
         " programmed=%" PRIu32 " readback=%" PRIu32 " sha256=",
         operation, ret, (unsigned int)result.phase,
         (unsigned int)target_slot,
         result.generation, elapsed, result.sectors_erased,
         result.bytes_programmed, result.bytes_readback);
  bkota_print_digest(result.slot_sha256);
  printf("\n");
  return ret;
}

static int bkota_validate_or_stage(int argc, char **argv, bool stage)
{
  struct bk7258_ota_trial_status_s trial;
  struct bk7258_ota_expected_s expected;
  struct bkota_file_source_s file = {.fd = -1};
  struct bk7258_ota_source_s source;
  uint8_t descriptor[BK7258_OTA_STAGE_DESCRIPTOR_SIZE];
  uint32_t timeout_ms = 0;
  enum bk7258_ota_slot_e target_slot;
  int ret;

  if ((!stage && argc != 8) || (stage && argc != 10))
    {
      return -EINVAL;
    }

  memset(&expected, 0, sizeof(expected));
  ret = bkota_expected(argv[4], argv[5], argv[6], argv[7],
                       stage ? argv[8] : NULL, &expected,
                       stage ? &timeout_ms : NULL);
  if (ret < 0)
    {
      return ret;
    }

  if (stage)
    {
      ret = bkota_authorize(argv[9], expected.generation);
      if (ret < 0)
        {
          return ret;
        }
    }

  ret = bkota_read_exact(argv[3], descriptor, sizeof(descriptor));
  if (ret < 0)
    {
      return ret;
    }

  ret = bkota_open_source(argv[2], &file);
  if (ret < 0)
    {
      return ret;
    }

  source.read = bkota_source_read;
  source.arg = &file;
  source.size = file.size;
  memset(&trial, 0, sizeof(trial));
  (void)bk7258_ota_trial_get_status(&trial, 2000);
  if (trial.active_slot > BK7258_OTA_SLOT_B)
    {
      close(file.fd);
      return -EIO;
    }

  target_slot = trial.active_slot == BK7258_OTA_SLOT_A ?
                BK7258_OTA_SLOT_B : BK7258_OTA_SLOT_A;
  ret = bkota_run_validate_or_stage(stage ? "STAGE" : "VALIDATE", stage,
                                    target_slot, descriptor, &expected, &source,
                                    timeout_ms);
  close(file.fd);
  return ret;
}

static int bkota_validate_or_stage_memory(int argc, char **argv, bool stage)
{
  struct bk7258_ota_trial_status_s trial;
  struct bk7258_psram_info_s info;
  struct bk7258_ota_expected_s expected;
  struct bkota_memory_source_s memory;
  struct bk7258_ota_source_s source;
  uint8_t descriptor[BK7258_OTA_STAGE_DESCRIPTOR_SIZE];
  uint32_t timeout_ms = 0;
  enum bk7258_ota_slot_e target_slot;
  int ret;

  if ((!stage && argc != 6) || (stage && argc != 8))
    {
      return -EINVAL;
    }

  memset(&expected, 0, sizeof(expected));
  ret = bkota_expected(argv[2], argv[3], argv[4], argv[5],
                       stage ? argv[6] : NULL, &expected,
                       stage ? &timeout_ms : NULL);
  if (ret < 0)
    {
      return ret;
    }

  if (stage)
    {
      ret = bkota_authorize(argv[7], expected.generation);
      if (ret < 0)
        {
          return ret;
        }
    }

  ret = bkota_transfer_ready(&info);
  if (ret < 0)
    {
      printf("BKOTA TRANSFER DENY ret=%d capacity=0x%08" PRIx32 "\n",
             ret, info.capacity);
      return ret;
    }

  ret = bkota_memory_copy(BK7258_OTA_TRANSFER_DESCRIPTOR_ADDRESS,
                          descriptor, sizeof(descriptor));
  if (ret < 0)
    {
      return ret;
    }

  memory.base = (const volatile uint8_t *)
                BK7258_OTA_TRANSFER_CANDIDATE_ADDRESS;
  memory.size = BK7258_OTA_TRANSFER_CANDIDATE_SIZE;
  source.read = bkota_memory_read;
  source.arg = &memory;
  source.size = memory.size;
  memset(&trial, 0, sizeof(trial));
  (void)bk7258_ota_trial_get_status(&trial, 2000);
  if (trial.active_slot > BK7258_OTA_SLOT_B)
    {
      return -EIO;
    }

  target_slot = trial.active_slot == BK7258_OTA_SLOT_A ?
                BK7258_OTA_SLOT_B : BK7258_OTA_SLOT_A;
  return bkota_run_validate_or_stage(
    stage ? "STAGE-MEM" : "VALIDATE-MEM", stage, target_slot, descriptor,
    &expected, &source, timeout_ms);
}

static int bkota_run_publish(
  const char *operation,
  const uint8_t record[BK7258_OTA_PENDING_RECORD_SIZE],
  uint64_t generation, uint32_t timeout_ms)
{
  struct bk7258_ota_publish_result_s result;
  uint64_t started;
  uint64_t elapsed;
  int disarm_ret;
  int fault_ret;
  int ret;

  memset(&result, 0, sizeof(result));
  started = bkota_now_ms();
  ret = bk7258_ota_fault_begin(generation,
                               BK7258_OTA_FAULT_PUBLISH_MASK);
  if (ret == 0)
    {
      ret = bk7258_ota_trial_set_write_enabled(true);
    }

  if (ret == 0)
    {
      ret = bk7258_ota_publish_pending(record, generation, timeout_ms,
                                       &result);
    }

  disarm_ret = bk7258_ota_trial_set_write_enabled(false);
  if (ret == 0 && disarm_ret < 0)
    {
      ret = disarm_ret;
    }

  fault_ret = bkota_fault_finish_session();
  if (ret == 0 && fault_ret < 0)
    {
      ret = fault_ret;
    }

  elapsed = bkota_now_ms() - started;
  printf("BKOTA %s ret=%d phase=%" PRIu32
         " generation=%" PRIu64 " previous_generation=%" PRIu64
         " previous_state=%" PRIu32 " elapsed_ms=%" PRIu64
         " previous_bank=%" PRIu32 " published_bank=%" PRIu32
         " stable=%" PRIu32 " target=%" PRIu32
         " programmed=%" PRIu32 " verified=%" PRIu32
         " base=%u candidate=%u degraded=%u mutation=%u reclaimed=%u"
         " erase_verified=%u"
         " readback=%u idempotent=%u\n",
         operation, ret, result.phase, result.generation,
         result.previous_generation,
         result.previous_state, elapsed,
         result.previous_bank, result.published_bank, result.stable_slot,
         result.target_slot, result.programmed_chunks,
         result.verified_chunks,
         result.base_verified ? 1u : 0u,
         result.candidate_verified ? 1u : 0u,
         result.metadata_degraded ? 1u : 0u,
         result.mutation_attempted ? 1u : 0u,
         result.sector_reclaimed ? 1u : 0u,
         result.erase_verified ? 1u : 0u,
         result.readback_verified ? 1u : 0u,
         result.idempotent ? 1u : 0u);
  return ret;
}

static int bkota_publish(int argc, char **argv)
{
  uint8_t record[BK7258_OTA_PENDING_RECORD_SIZE];
  uint64_t generation;
  uint32_t timeout_ms;
  int ret;

  if (argc != 6 || bkota_u64(argv[3], &generation) < 0 || generation == 0 ||
      bkota_u32(argv[4], &timeout_ms) < 0 || timeout_ms == 0)
    {
      return -EINVAL;
    }

  ret = bkota_authorize(argv[5], generation);
  if (ret < 0)
    {
      return ret;
    }

  ret = bkota_read_exact(argv[2], record, sizeof(record));
  if (ret < 0)
    {
      return ret;
    }

  return bkota_run_publish("PUBLISH", record, generation, timeout_ms);
}

static int bkota_publish_memory(int argc, char **argv)
{
  struct bk7258_psram_info_s info;
  uint8_t record[BK7258_OTA_PENDING_RECORD_SIZE];
  uint64_t generation;
  uint32_t timeout_ms;
  int ret;

  if (argc != 5 || bkota_u64(argv[2], &generation) < 0 || generation == 0 ||
      bkota_u32(argv[3], &timeout_ms) < 0 || timeout_ms == 0)
    {
      return -EINVAL;
    }

  ret = bkota_authorize(argv[4], generation);
  if (ret < 0)
    {
      return ret;
    }

  ret = bkota_transfer_ready(&info);
  if (ret < 0)
    {
      printf("BKOTA TRANSFER DENY ret=%d capacity=0x%08" PRIx32 "\n",
             ret, info.capacity);
      return ret;
    }

  ret = bkota_memory_copy(BK7258_OTA_TRANSFER_RECORD_ADDRESS,
                          record, sizeof(record));
  if (ret < 0)
    {
      return ret;
    }

  return bkota_run_publish("PUBLISH-MEM", record, generation, timeout_ms);
}

static int bkota_transfer_status(void)
{
  struct bk7258_psram_info_s info;
  struct watchdog_status_s watchdog;
  int watchdog_ret;
  int transfer_ret;
  int fd;

  memset(&info, 0, sizeof(info));
  transfer_ret = bkota_transfer_ready(&info);
  memset(&watchdog, 0, sizeof(watchdog));
  fd = open("/dev/watchdog0", O_RDONLY);
  if (fd < 0)
    {
      watchdog_ret = -errno;
    }
  else
    {
      watchdog_ret = ioctl(fd, WDIOC_GETSTATUS,
                           (unsigned long)(uintptr_t)&watchdog);
      if (watchdog_ret < 0)
        {
          watchdog_ret = errno == 0 ? -EIO : -errno;
        }

      close(fd);
    }

  printf("BKOTA TRANSFER ret=%d capacity=0x%08" PRIx32
         " ready=%" PRIu32 " mpu=%" PRIu32
         " candidate=0x%08" PRIx32 "+0x%08" PRIx32
         " descriptor=0x%08" PRIx32 "+0x%08x"
         " record=0x%08" PRIx32 "+0x%08" PRIx32
         " end=0x%08" PRIx32
         " watchdog_ret=%d watchdog_active=%u\n",
         transfer_ret, info.capacity, info.ready, info.mpu_valid,
         (uint32_t)BK7258_OTA_TRANSFER_CANDIDATE_ADDRESS,
         (uint32_t)BK7258_OTA_TRANSFER_CANDIDATE_SIZE,
         (uint32_t)BK7258_OTA_TRANSFER_DESCRIPTOR_ADDRESS,
         (unsigned int)BK7258_OTA_STAGE_DESCRIPTOR_SIZE,
         (uint32_t)BK7258_OTA_TRANSFER_RECORD_ADDRESS,
         (uint32_t)BK7258_OTA_TRANSFER_RECORD_SIZE,
         (uint32_t)BK7258_OTA_TRANSFER_END, watchdog_ret,
         watchdog_ret == 0 && (watchdog.flags & WDFLAGS_ACTIVE) != 0 ?
           1u : 0u);
  return transfer_ret < 0 ? transfer_ret : watchdog_ret;
}

static int bkota_prepare_transfer(int argc, char **argv)
{
  struct bk7258_psram_info_s info;
  struct watchdog_status_s watchdog;
  uint64_t generation;
  int ret;
  int fd;

  if (argc != 4 || bkota_u64(argv[2], &generation) < 0 || generation == 0)
    {
      return -EINVAL;
    }

  ret = bkota_authorize(argv[3], generation);
  if (ret < 0)
    {
      return ret;
    }

  ret = bkota_transfer_ready(&info);
  if (ret < 0)
    {
      return ret;
    }

  fd = open("/dev/watchdog0", O_RDONLY);
  if (fd < 0)
    {
      return -errno;
    }

  ret = ioctl(fd, WDIOC_STOP, 0ul);
  if (ret < 0)
    {
      ret = errno == 0 ? -EIO : -errno;
      close(fd);
      return ret;
    }

  memset(&watchdog, 0, sizeof(watchdog));
  ret = ioctl(fd, WDIOC_GETSTATUS, (unsigned long)(uintptr_t)&watchdog);
  if (ret < 0)
    {
      ret = errno == 0 ? -EIO : -errno;
    }
  else if ((watchdog.flags & WDFLAGS_ACTIVE) != 0)
    {
      ret = -EIO;
    }

  close(fd);
  if (ret < 0)
    {
      return ret;
    }

  printf("BKOTA TRANSFER READY generation=%" PRIu64
         " watchdog_active=0 action=load-psram-then-stage-publish-reset\n",
         generation);
  return 0;
}

static int bkota_fault_status_command(int argc)
{
  struct bk7258_ota_fault_status_s status;
  int ret;

  if (argc != 2)
    {
      return -EINVAL;
    }

  memset(&status, 0, sizeof(status));
  ret = bk7258_ota_fault_get_status(&status);
  if (ret == 0)
    {
      bkota_fault_print("STATUS", &status);
    }

  return ret;
}

static int bkota_fault_clear_command(int argc)
{
  struct bk7258_ota_fault_status_s status;
  int ret;

  if (argc != 2)
    {
      return -EINVAL;
    }

  memset(&status, 0, sizeof(status));
  ret = bk7258_ota_fault_finish(&status);
  if (ret == 0)
    {
      bkota_fault_print("CLEARED", &status);
    }

  return ret;
}

static int bkota_fault_arm_command(int argc, char **argv)
{
  enum bk7258_ota_fault_point_e point;
  uint64_t generation;
  uint32_t ordinal;
  int ret;

  if (argc != 6 || bkota_fault_parse(argv[2], &point) < 0 ||
      bkota_u32(argv[3], &ordinal) < 0 || ordinal == 0 ||
      ordinal > BK7258_OTA_FAULT_MAX_ORDINAL ||
      bkota_u64(argv[4], &generation) < 0 || generation == 0)
    {
      return -EINVAL;
    }

  ret = bkota_authorize(argv[5], generation);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_ota_fault_arm(point, ordinal, generation);
  printf("BKOTA FAULT ARM ret=%d point=%s ordinal=%" PRIu32
         " generation=%" PRIu64 " one_shot=1\n",
         ret, bkota_fault_name(point), ordinal, generation);
  return ret;
}

static int bkota_corrupt_memory(int argc, char **argv)
{
  struct bk7258_psram_info_s info;
  volatile uint8_t *target;
  uint64_t generation;
  uint32_t offset;
  uint32_t mask;
  uint8_t before;
  uint8_t after;
  int ret;

  if (argc != 6 || bkota_u32(argv[2], &offset) < 0 ||
      offset >= BK7258_OTA_TRANSFER_CANDIDATE_SIZE ||
      bkota_u32(argv[3], &mask) < 0 || mask == 0 || mask > UINT8_MAX ||
      bkota_u64(argv[4], &generation) < 0 || generation == 0)
    {
      return -EINVAL;
    }

  ret = bkota_authorize(argv[5], generation);
  if (ret < 0)
    {
      return ret;
    }

  ret = bkota_transfer_ready(&info);
  if (ret < 0)
    {
      return ret;
    }

  target = (volatile uint8_t *)(uintptr_t)
           (BK7258_OTA_TRANSFER_CANDIDATE_ADDRESS + offset);
  bkota_memory_barrier();
  before = *target;
  after = before ^ (uint8_t)mask;
  *target = after;
  bkota_memory_barrier();
  if (*target != after)
    {
      return -EIO;
    }

  printf("BKOTA CORRUPT-MEM ret=0 generation=%" PRIu64
         " offset=0x%08" PRIx32 " address=0x%08" PRIxPTR
         " before=0x%02x after=0x%02x xor=0x%02" PRIx32 "\n",
         generation, offset, (uintptr_t)target, (unsigned int)before,
         (unsigned int)after, mask);
  return 0;
}

static int bkota_transition(int argc, char **argv, bool confirm)
{
  uint64_t generation;
  uint64_t started;
  uint64_t elapsed;
  uint32_t timeout_ms;
  int disarm_ret;
  int fault_ret;
  int ret;

  if (argc != 5 || bkota_u64(argv[2], &generation) < 0 || generation == 0 ||
      bkota_u32(argv[3], &timeout_ms) < 0 || timeout_ms == 0)
    {
      return -EINVAL;
    }

  ret = bkota_authorize(argv[4], generation);
  if (ret < 0)
    {
      return ret;
    }

  started = bkota_now_ms();
  ret = bk7258_ota_fault_begin(generation, BK7258_OTA_FAULT_TRIAL_MASK);
  if (ret == 0)
    {
      ret = bk7258_ota_trial_set_write_enabled(true);
    }

  if (ret == 0)
    {
      ret = confirm ? bk7258_ota_trial_confirm(generation, timeout_ms) :
                      bk7258_ota_trial_rollback(generation, timeout_ms);
    }

  disarm_ret = bk7258_ota_trial_set_write_enabled(false);
  if (ret == 0 && disarm_ret < 0)
    {
      ret = disarm_ret;
    }

  fault_ret = bkota_fault_finish_session();
  if (ret == 0 && fault_ret < 0)
    {
      ret = fault_ret;
    }

  elapsed = bkota_now_ms() - started;
  printf("BKOTA %s ret=%d generation=%" PRIu64
         " elapsed_ms=%" PRIu64 " timeout_ms=%" PRIu32 "\n",
         confirm ? "CONFIRM" : "ROLLBACK", ret, generation,
         elapsed, timeout_ms);
  return ret;
}

static void bkota_usage(void)
{
  printf("N15 validation only: unsigned artifacts; do not deploy.\n"
         "usage:\n"
         "  bkota status\n"
         "  bkota buffer\n"
         "  bkota prepare-transfer <generation> "
         BKOTA_TOKEN_PREFIX "<generation>\n"
         "  bkota fault-status\n"
         "  bkota fault-clear\n"
         "  bkota fault-arm <point> <ordinal> <generation> "
         BKOTA_TOKEN_PREFIX "<generation>\n"
         "  bkota corrupt-mem <offset> <xor-mask> <generation> "
         BKOTA_TOKEN_PREFIX "<generation>\n"
         "  bkota validate <s_app.bin> <descriptor.bin> <generation> "
         "<timestamp> <version> <base-version>\n"
         "  bkota stage <s_app.bin> <descriptor.bin> <generation> "
         "<timestamp> <version> <base-version> <timeout-ms> "
         BKOTA_TOKEN_PREFIX "<generation>\n"
         "  bkota validate-mem <generation> <timestamp> <version> "
         "<base-version>\n"
         "  bkota stage-mem <generation> <timestamp> <version> "
         "<base-version> <timeout-ms> " BKOTA_TOKEN_PREFIX "<generation>\n"
         "  bkota publish <pending-record.bin> <generation> <timeout-ms> "
         BKOTA_TOKEN_PREFIX "<generation>\n"
         "  bkota publish-mem <generation> <timeout-ms> "
         BKOTA_TOKEN_PREFIX "<generation>\n"
         "  bkota confirm <generation> <timeout-ms> "
         BKOTA_TOKEN_PREFIX "<generation>\n"
         "  bkota rollback <generation> <timeout-ms> "
         BKOTA_TOKEN_PREFIX "<generation>\n");
}

int main(int argc, FAR char *argv[])
{
  int ret;

  ret = bkota_disarm();
  if (ret < 0)
    {
      printf("BKOTA FAIL disarm=%d\n", ret);
      return EXIT_FAILURE;
    }

  if (argc == 2 && strcmp(argv[1], "status") == 0)
    {
      ret = bkota_status();
    }
  else if (argc == 2 && strcmp(argv[1], "buffer") == 0)
    {
      ret = bkota_transfer_status();
    }
  else if (argc >= 2 && strcmp(argv[1], "prepare-transfer") == 0)
    {
      ret = bkota_prepare_transfer(argc, argv);
    }
  else if (argc >= 2 && strcmp(argv[1], "fault-status") == 0)
    {
      ret = bkota_fault_status_command(argc);
    }
  else if (argc >= 2 && strcmp(argv[1], "fault-clear") == 0)
    {
      ret = bkota_fault_clear_command(argc);
    }
  else if (argc >= 2 && strcmp(argv[1], "fault-arm") == 0)
    {
      ret = bkota_fault_arm_command(argc, argv);
    }
  else if (argc >= 2 && strcmp(argv[1], "corrupt-mem") == 0)
    {
      ret = bkota_corrupt_memory(argc, argv);
    }
  else if (argc >= 2 && strcmp(argv[1], "validate") == 0)
    {
      ret = bkota_validate_or_stage(argc, argv, false);
    }
  else if (argc >= 2 && strcmp(argv[1], "stage") == 0)
    {
      ret = bkota_validate_or_stage(argc, argv, true);
    }
  else if (argc >= 2 && strcmp(argv[1], "validate-mem") == 0)
    {
      ret = bkota_validate_or_stage_memory(argc, argv, false);
    }
  else if (argc >= 2 && strcmp(argv[1], "stage-mem") == 0)
    {
      ret = bkota_validate_or_stage_memory(argc, argv, true);
    }
  else if (argc >= 2 && strcmp(argv[1], "publish") == 0)
    {
      ret = bkota_publish(argc, argv);
    }
  else if (argc >= 2 && strcmp(argv[1], "publish-mem") == 0)
    {
      ret = bkota_publish_memory(argc, argv);
    }
  else if (argc >= 2 && strcmp(argv[1], "confirm") == 0)
    {
      ret = bkota_transition(argc, argv, true);
    }
  else if (argc >= 2 && strcmp(argv[1], "rollback") == 0)
    {
      ret = bkota_transition(argc, argv, false);
    }
  else
    {
      bkota_usage();
      return EXIT_FAILURE;
    }

  if (bkota_disarm() < 0)
    {
      printf("BKOTA FAIL final-disarm\n");
      return EXIT_FAILURE;
    }

  return ret < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
