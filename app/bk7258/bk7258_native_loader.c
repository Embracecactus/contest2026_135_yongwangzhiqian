/****************************************************************************
 * apps/bk7258/bk7258_native_loader.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include "bk7258_native_loader.h"

#include <nuttx/config.h>

#include <errno.h>

#ifdef CONFIG_LIBC_ELF

#include <stdbool.h>
#include <elf.h>
#include <fcntl.h>
#include <nuttx/lib/elf.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BKNATIVE_EXPORT_NAME "shaniu_native_entry"
#define BKNATIVE_MAX_SECTIONS 256u

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct bknative_module_s
{
  struct module_s module;
  struct mod_loadinfo_s loadinfo;
};

#ifdef CONFIG_TESTS_BK7258_NATIVE_APPS
static volatile uint32_t g_bknative_loader_trace[4];

static void bknative_loader_trace(uint32_t phase)
{
  g_bknative_loader_trace[1] = sched_getcpu();
  if (phase == 110)
    {
      g_bknative_loader_trace[2]++;
    }

  __atomic_store_n(&g_bknative_loader_trace[0], phase, __ATOMIC_RELEASE);
}
#else
#define bknative_loader_trace(phase) ((void)0)
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int bknative_readat(int fd, off_t offset, void *buffer, size_t size)
{
  uint8_t *cursor = buffer;
  ssize_t count;

  if (lseek(fd, offset, SEEK_SET) != offset)
    {
      return -errno;
    }

  while (size > 0)
    {
      count = read(fd, cursor, size);
      if (count < 0)
        {
          return -errno;
        }

      if (count == 0)
        {
          return -ENOEXEC;
        }

      cursor += count;
      size -= count;
    }

  return 0;
}

static int bknative_loader_require_cpu0(void)
{
#ifdef CONFIG_SMP
  return sched_getcpu() == 0 ? 0 : -EXDEV;
#else
  return 0;
#endif
}

static bool bknative_range_valid(off_t file_size, uint64_t offset,
                                 uint64_t size)
{
  return file_size >= 0 && offset <= (uint64_t)file_size &&
         size <= (uint64_t)file_size - offset;
}

static int bknative_validate_symbols(int fd, off_t file_size,
                                     const Elf32_Shdr *sections,
                                     Elf32_Half section_count,
                                     Elf32_Sym *entry)
{
  Elf32_Sym symbol;
  Elf32_Half i;
  Elf32_Word j;
  bool found = false;

  for (i = 0; i < section_count; i++)
    {
      const Elf32_Shdr *section = &sections[i];

      if ((section->sh_type != SHT_SYMTAB &&
           section->sh_type != SHT_DYNSYM) ||
          section->sh_entsize != sizeof(Elf32_Sym) ||
          section->sh_size % sizeof(Elf32_Sym) != 0 ||
          !bknative_range_valid(file_size, section->sh_offset,
                                section->sh_size))
        {
          if (section->sh_type == SHT_SYMTAB ||
              section->sh_type == SHT_DYNSYM)
            {
              return -ENOEXEC;
            }

          continue;
        }

      if (section->sh_link >= section_count)
        {
          return -ENOEXEC;
        }

      const Elf32_Shdr *strings = &sections[section->sh_link];
      if (strings->sh_type != SHT_STRTAB ||
          !bknative_range_valid(file_size, strings->sh_offset,
                                strings->sh_size))
        {
          return -ENOEXEC;
        }

      for (j = 1; j < section->sh_size / sizeof(Elf32_Sym); j++)
        {
          int ret = bknative_readat(fd,
                                    section->sh_offset +
                                    j * sizeof(Elf32_Sym),
                                    &symbol, sizeof(symbol));
          if (ret < 0)
            {
              return ret;
            }

          /* This ABI deliberately has no imports.  Requiring every regular
           * symbol to be defined prevents libelf from binding against the
           * base firmware's symbol table.
           */

          if (symbol.st_shndx == SHN_UNDEF)
            {
              return -ENOEXEC;
            }

          if (symbol.st_name >= strings->sh_size)
            {
              return -ENOEXEC;
            }

          if (strings->sh_size - symbol.st_name >=
              sizeof(BKNATIVE_EXPORT_NAME))
            {
              char name[sizeof(BKNATIVE_EXPORT_NAME)];
              ret = bknative_readat(fd, strings->sh_offset + symbol.st_name,
                                     name, sizeof(name));
              if (ret < 0)
                {
                  return ret;
                }

              if (!memcmp(name, BKNATIVE_EXPORT_NAME, sizeof(name)))
                {
                  if (found)
                    {
                      return -ENOEXEC;
                    }

                  *entry = symbol;
                  found = true;
                }
            }
        }
    }

  return found ? 0 : -ENOEXEC;
}

static int bknative_validate_elf(const char *path, Elf32_Sym *entry)
{
  Elf32_Ehdr header;
  Elf32_Shdr *sections = NULL;
  struct stat st;
  int fd;
  int ret;
  size_t section_bytes;
  Elf32_Half i;

  bknative_loader_trace(111);
  fd = open(path, O_RDONLY);
  if (fd < 0)
    {
      return -errno;
    }

  bknative_loader_trace(112);
  if (fstat(fd, &st) < 0)
    {
      ret = -errno;
      goto out;
    }

  bknative_loader_trace(113);
  ret = bknative_readat(fd, 0, &header, sizeof(header));
  if (ret < 0)
    {
      goto out;
    }

  if (header.e_ident[EI_MAG0] != ELFMAG0 ||
      header.e_ident[EI_MAG1] != ELFMAG1 ||
      header.e_ident[EI_MAG2] != ELFMAG2 ||
      header.e_ident[EI_MAG3] != ELFMAG3 ||
      header.e_ident[EI_CLASS] != ELFCLASS32 ||
      header.e_ident[EI_DATA] != ELFDATA2LSB ||
      header.e_type != ET_REL || header.e_machine != EM_ARM ||
      header.e_version != EV_CURRENT || header.e_ehsize != sizeof(header) ||
      header.e_shentsize != sizeof(Elf32_Shdr) || header.e_shnum == 0 ||
      header.e_shnum > BKNATIVE_MAX_SECTIONS)
    {
      ret = -ENOEXEC;
      goto out;
    }

  section_bytes = (size_t)header.e_shnum * sizeof(*sections);
  if (!bknative_range_valid(st.st_size, header.e_shoff, section_bytes))
    {
      ret = -ENOEXEC;
      goto out;
    }

  bknative_loader_trace(114);
  sections = malloc(section_bytes);
  if (sections == NULL)
    {
      ret = -ENOMEM;
      goto out;
    }

  bknative_loader_trace(115);
  ret = bknative_readat(fd, header.e_shoff, sections, section_bytes);
  if (ret < 0)
    {
      goto out;
    }

  for (i = 0; i < header.e_shnum; i++)
    {
      if ((sections[i].sh_type == SHT_INIT_ARRAY ||
           sections[i].sh_type == SHT_FINI_ARRAY ||
           sections[i].sh_type == SHT_PREINIT_ARRAY) &&
          sections[i].sh_size != 0)
        {
          ret = -ENOEXEC;
          goto out;
        }
    }

  bknative_loader_trace(116);
  ret = bknative_validate_symbols(fd, st.st_size, sections,
                                   header.e_shnum, entry);

out:
  bknative_loader_trace(117);
  free(sections);
  bknative_loader_trace(118);
  close(fd);
  bknative_loader_trace(119);
  return ret;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bknative_loader_open(const char *path, void **handle,
                         const struct bknative_program_s **program)
{
  struct bknative_module_s *loaded;
  Elf_Sym symbol;
  bknative_entry_t entry;
  const struct bknative_program_s *candidate;
  uintptr_t address;
  bool initialized = false;
  bool loaded_image = false;
  int ret;

  if (path == NULL || handle == NULL || program == NULL)
    {
      return -EINVAL;
    }

  *handle = NULL;
  *program = NULL;
  memset(&symbol, 0, sizeof(symbol));
  ret = bknative_loader_require_cpu0();
  if (ret < 0)
    {
      return ret;
    }

  bknative_loader_trace(110);
  ret = bknative_validate_elf(path, &symbol);
  if (ret < 0)
    {
      return ret;
    }

  bknative_loader_trace(120);
  loaded = malloc(sizeof(*loaded));
  if (loaded == NULL)
    {
      return -ENOMEM;
    }

  memset(loaded, 0, sizeof(*loaded));
  /* libelf_initialize may have opened the file before reporting a malformed
   * header, so pair every attempt with libelf_uninitialize on failure.
   */

  initialized = true;
  bknative_loader_trace(130);
  ret = libelf_initialize(path, &loaded->loadinfo);
  if (ret < 0)
    {
      goto errout;
    }

  bknative_loader_trace(140);
  ret = libelf_load(&loaded->loadinfo);
  if (ret < 0)
    {
      goto errout;
    }

  loaded_image = true;
  /* Use the symbol identified by the bounded preflight. The pinned
   * libelf_findsymbol compares an unallocated name buffer for STN_UNDEF.
   */

  if (symbol.st_shndx >= loaded->loadinfo.ehdr.e_shnum ||
      ELF_ST_BIND(symbol.st_info) != STB_GLOBAL ||
      ELF_ST_TYPE(symbol.st_info) != STT_FUNC ||
      ELF_ST_VISIBILITY(symbol.st_other) != STV_DEFAULT ||
      (loaded->loadinfo.shdr[symbol.st_shndx].sh_flags & SHF_EXECINSTR) == 0)
    {
      ret = -ENOEXEC;
      goto errout;
    }

  if (!(symbol.st_value & 1u) ||
      (symbol.st_value & ~1u) >=
      loaded->loadinfo.shdr[symbol.st_shndx].sh_size)
    {
      ret = -ENOEXEC;
      goto errout;
    }

  /* The preflight forbids undefined symbols, so binding with no base
   * firmware export table is both sufficient and an ABI enforcement point.
   * libelf_bind also applies relocations and makes instruction/data caches
   * coherent before this code becomes callable.
   */

  bknative_loader_trace(150);
  ret = libelf_bind(&loaded->module, &loaded->loadinfo, NULL, 0);
  if (ret < 0)
    {
      goto errout;
    }

  address = loaded->loadinfo.shdr[symbol.st_shndx].sh_addr + symbol.st_value;
  /* The control worker opens the file, while the product loop may unload
   * later. Close that worker's descriptor and release parsing buffers now;
   * libelf_unload retains only the owned text/data allocation information.
   */

  bknative_loader_trace(160);
  ret = libelf_uninitialize(&loaded->loadinfo);
  if (ret < 0)
    {
      goto errout;
    }

  initialized = false;
  entry = (bknative_entry_t)address;
  bknative_loader_trace(170);
  candidate = entry();
  if (candidate == NULL || candidate->abi != BKNATIVE_ABI ||
      candidate->state_size > BKNATIVE_STATE_MAX ||
      candidate->start == NULL || candidate->step == NULL)
    {
      ret = -EPROTO;
      goto errout;
    }

  *handle = loaded;
  *program = candidate;
  bknative_loader_trace(180);
  return 0;

errout:
  if (loaded_image)
    {
      libelf_unload(&loaded->loadinfo);
    }

  if (initialized)
    {
      libelf_uninitialize(&loaded->loadinfo);
    }

  free(loaded);
  return ret;
}

int bknative_loader_close(void *handle)
{
  struct bknative_module_s *loaded = handle;
  int ret;

  if (loaded == NULL)
    {
      return -EINVAL;
    }

  ret = bknative_loader_require_cpu0();
  if (ret < 0)
    {
      return ret;
    }

  bknative_loader_trace(210);
  ret = libelf_unload(&loaded->loadinfo);
  if (ret == 0)
    {
      bknative_loader_trace(220);
      ret = libelf_uninitialize(&loaded->loadinfo);
    }

  if (!ret)
    {
      bknative_loader_trace(230);
      free(loaded);
    }

  bknative_loader_trace(240);
  return ret;
}

#else

int bknative_loader_open(const char *path, void **handle,
                         const struct bknative_program_s **program)
{
  (void)path;
  if (handle != NULL)
    {
      *handle = NULL;
    }

  if (program != NULL)
    {
      *program = NULL;
    }

  return -ENOSYS;
}

int bknative_loader_close(void *handle)
{
  (void)handle;
  return -ENOSYS;
}

#endif
