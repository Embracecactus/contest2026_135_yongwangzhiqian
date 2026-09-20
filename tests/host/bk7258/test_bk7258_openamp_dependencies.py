#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Verify the manifest-managed OpenAMP and libmetal dependency lock.

This test never copies an expanded dependency tree.  It streams ``git archive``
from each clean, manifest-managed checkout at the commit in dependencies.lock.json
and compiles the public-header ABI probe from those archives.
"""

from __future__ import annotations

import json
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
import unittest


CONTEST = Path(__file__).resolve().parents[3]
WORKSPACE = CONTEST.parent
LOCK_PATH = CONTEST / "nuttx" / "dependencies.lock.json"


def command(*args: str, cwd: Path | None = None) -> str:
    return subprocess.run(
        args,
        cwd=cwd,
        check=True,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    ).stdout.strip()


def archive_checkout(source: Path, commit: str, destination: Path) -> None:
    destination.mkdir(parents=True)
    archive = subprocess.Popen(
        ("git", "-C", str(source), "archive", "--format=tar", commit),
        stdout=subprocess.PIPE,
    )
    assert archive.stdout is not None
    with tarfile.open(fileobj=archive.stdout, mode="r|") as contents:
        for member in contents:
            target = (destination / member.name).resolve()
            if (
                target != destination.resolve()
                and destination.resolve() not in target.parents
            ):
                raise RuntimeError(f"archive member escapes destination: {member.name}")
            contents.extract(member, destination)
    archive.stdout.close()
    if archive.wait() != 0:
        raise RuntimeError(f"git archive failed for {source} at {commit}")


class OpenampDependenciesTest(unittest.TestCase):
    def test_locked_archives_supply_nuttx_openamp_abi(self) -> None:
        lock = json.loads(LOCK_PATH.read_text(encoding="utf-8"))
        self.assertEqual(lock["format"], "bk7258.openvela-dependencies/1")
        projects = {project["path"]: project for project in lock["projects"]}
        expected = {
            "nuttx/openamp/open-amp",
            "nuttx/openamp/libmetal",
            "nuttx/fs/littlefs/littlefs",
        }
        self.assertEqual(set(projects), expected)

        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            extracted: dict[str, Path] = {}
            for path, project in projects.items():
                source = WORKSPACE / path
                self.assertEqual(
                    command("git", "status", "--porcelain", cwd=source), ""
                )
                self.assertEqual(
                    command("git", "rev-parse", "HEAD", cwd=source), project["commit"]
                )
                self.assertEqual(
                    command("git", "rev-parse", "HEAD^{tree}", cwd=source),
                    project["tree"],
                )
                destination = root / Path(path).name
                archive_checkout(source, project["commit"], destination)
                extracted[path] = destination

            generated = root / "generated" / "metal"
            generated.mkdir(parents=True)
            stubs = {
                "compiler.h": "#define METAL_PACKED_BEGIN\n#define METAL_PACKED_END __attribute__((packed))\n#define metal_align(n) __attribute__((aligned(n)))\n#define __deprecated __attribute__((deprecated))\n",
                "io.h": "#include <stdint.h>\n#include <metal/list.h>\ntypedef uintptr_t metal_phys_addr_t;\nstruct metal_io_region;\nvoid *metal_io_phys_to_virt(struct metal_io_region *, metal_phys_addr_t);\nmetal_phys_addr_t metal_io_virt_to_phys(struct metal_io_region *, void *);\n",
                "mutex.h": "typedef int metal_mutex_t;\n",
                "list.h": "#ifndef TEST_METAL_LIST_H\n#define TEST_METAL_LIST_H\nstruct metal_list { struct metal_list *next; struct metal_list *prev; };\n#endif\n",
                "sleep.h": "static inline void metal_sleep_usec(unsigned int value) { (void)value; }\n",
                "utilities.h": "#define metal_bitmap_longs(bits) (((bits) + (8 * sizeof(long)) - 1) / (8 * sizeof(long)))\n",
                "spinlock.h": "struct metal_spinlock { int value; };\n",
                "errno.h": "#include <errno.h>\n",
                "cache.h": "static inline void metal_cache_flush(void *p, unsigned long n) { (void)p; (void)n; }\nstatic inline void metal_cache_invalidate(void *p, unsigned long n) { (void)p; (void)n; }\n",
                "alloc.h": "#include <stddef.h>\nvoid *metal_allocate_memory(size_t);\n",
                "log.h": "#define metal_log(level, ...) do { (void)(level); } while (0)\n",
                "assert.h": "#define metal_assert(expr) ((void)sizeof(char[(expr) ? 1 : -1]))\n",
            }
            for name, body in stubs.items():
                (generated / name).write_text(body, encoding="utf-8")
            probe = root / "openamp_abi.c"
            probe.write_text(
                "#include <stddef.h>\n"
                "#include <openamp/remoteproc.h>\n"
                "#include <openamp/rpmsg.h>\n"
                "#include <openamp/virtio.h>\n"
                "#include <openamp/virtio_ring.h>\n"
                '_Static_assert(sizeof(struct fw_rsc_config) == 64, "config ABI");\n'
                '_Static_assert(offsetof(struct rpmsg_endpoint, priority) > 0, "priority ABI");\n'
                "typedef int (*alloc_fn)(struct virtio_device *, void **, size_t, size_t);\n"
                '_Static_assert(__builtin_types_compatible_p(__typeof__(&virtio_alloc_buf), alloc_fn), "alloc ABI");\n'
                "int main(void) { struct vring_used_elem used = {0}; return (int)used.u.id; }\n",
                encoding="utf-8",
            )
            compiler = shutil.which("cc")
            self.assertIsNotNone(compiler, "host C compiler is required")
            compiled = subprocess.run(
                [
                    compiler,
                    "-std=c11",
                    "-Wall",
                    "-Werror",
                    "-DFAR=",
                    "-fsyntax-only",
                    "-I",
                    str(root / "generated"),
                    "-I",
                    str(extracted["nuttx/openamp/open-amp"] / "lib/include"),
                    str(probe),
                ],
                check=False,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )
            self.assertEqual(compiled.returncode, 0, compiled.stderr)


if __name__ == "__main__":
    unittest.main()
