/****************************************************************************
 * contest2026_135_yongwangzhiqian/app/hello_app/bk7258_ota_main.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Minimal operator command for the CP-owned paired OTA mechanism.
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/stat.h>

#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <arch/board/bk7258_image_layout.h>
#include <arch/board/bk7258_ota.h>

struct bkota_files_s
{
  int fd[2];
};

static int bkota_read(void *arg, enum bk7258_ota_image_e image,
                      uint32_t offset, uint8_t *buffer, size_t nbytes)
{
  struct bkota_files_s *files = arg;
  size_t done = 0;

  if (files == NULL || image < BK7258_OTA_IMAGE_CP ||
      image > BK7258_OTA_IMAGE_AP ||
      lseek(files->fd[image], (off_t)offset, SEEK_SET) != (off_t)offset)
    {
      return -EIO;
    }

  while (done < nbytes)
    {
      ssize_t count = read(files->fd[image], buffer + done, nbytes - done);
      if (count <= 0)
        {
          return -EIO;
        }

      done += (size_t)count;
    }

  return 0;
}

static int bkota_status(void)
{
  struct bk7258_ota_geometry_s geometry;
  int ret = bk7258_ota_inactive_geometry(&geometry);

  if (ret < 0)
    {
      fprintf(stderr, "bkota: active slot is invalid: %d\n", ret);
      return EXIT_FAILURE;
    }

  printf("active=%c inactive=%c cp=0x%08" PRIx32 "+0x%08" PRIx32
         " ap=0x%08" PRIx32 "+0x%08" PRIx32 "\n",
         geometry.active_slot == BK7258_BOOT_SLOT_PRIMARY ? 'A' : 'B',
         geometry.inactive_slot == BK7258_BOOT_SLOT_PRIMARY ? 'A' : 'B',
         geometry.cp_raw_offset, geometry.cp_raw_size,
         geometry.ap_raw_offset, geometry.ap_raw_size);
  return EXIT_SUCCESS;
}

static int bkota_stage(const char *cp_path, const char *ap_path)
{
  struct bkota_files_s files = {{-1, -1}};
  struct stat status;
  int ret = EXIT_FAILURE;

  files.fd[BK7258_OTA_IMAGE_CP] = open(cp_path, O_RDONLY);
  files.fd[BK7258_OTA_IMAGE_AP] = open(ap_path, O_RDONLY);
  if (files.fd[BK7258_OTA_IMAGE_CP] < 0 ||
      files.fd[BK7258_OTA_IMAGE_AP] < 0)
    {
      fprintf(stderr, "bkota: cannot open CP/AP image: %d\n", errno);
      goto out;
    }

  if (fstat(files.fd[BK7258_OTA_IMAGE_CP], &status) < 0 ||
      status.st_size != (off_t)BK7258_CP_RAW_PHYSICAL_SIZE)
    {
      fprintf(stderr, "bkota: CP image must be exactly 0x%08" PRIx32
              " bytes\n", (uint32_t)BK7258_CP_RAW_PHYSICAL_SIZE);
      goto out;
    }

  if (fstat(files.fd[BK7258_OTA_IMAGE_AP], &status) < 0 ||
      status.st_size != (off_t)BK7258_AP_RAW_PHYSICAL_SIZE)
    {
      fprintf(stderr, "bkota: AP image must be exactly 0x%08" PRIx32
              " bytes\n", (uint32_t)BK7258_AP_RAW_PHYSICAL_SIZE);
      goto out;
    }

  printf("bkota: staging inactive pair; do not remove power\n");
  ret = bk7258_ota_stage_pair(bkota_read, &files);
  if (ret < 0)
    {
      fprintf(stderr, "bkota: stage failed: %d\n", ret);
      ret = EXIT_FAILURE;
    }
  else
    {
      printf("bkota: pending pair staged; reboot to try it\n");
      ret = EXIT_SUCCESS;
    }

out:
  if (files.fd[BK7258_OTA_IMAGE_AP] >= 0)
    {
      close(files.fd[BK7258_OTA_IMAGE_AP]);
    }
  if (files.fd[BK7258_OTA_IMAGE_CP] >= 0)
    {
      close(files.fd[BK7258_OTA_IMAGE_CP]);
    }
  return ret;
}

int main(int argc, char *argv[])
{
  int ret;

  if (argc == 2 && strcmp(argv[1], "status") == 0)
    {
      return bkota_status();
    }

  if (argc == 2 && strcmp(argv[1], "confirm") == 0)
    {
      ret = bk7258_ota_confirm_pair();
      if (ret < 0)
        {
          fprintf(stderr, "bkota: pair confirm failed: %d\n", ret);
          return EXIT_FAILURE;
        }

      printf("bkota: active CP/AP pair confirmed\n");
      return EXIT_SUCCESS;
    }

  if (argc == 4 && strcmp(argv[1], "stage") == 0)
    {
      return bkota_stage(argv[2], argv[3]);
    }

  fprintf(stderr,
          "usage: bkota status | bkota stage <cp-physical.bin> "
          "<ap-physical.bin> | bkota confirm\n");
  return EXIT_FAILURE;
}
