#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Real driver ioctl/register/antenna paths, external register transport only."""
import subprocess
import tempfile
import unittest
from pathlib import Path
from test_mfrc522_read_errors import extract_function

ROOT = Path(__file__).resolve().parents[3]


class RfDriverTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix="shaniu-rf-")
        cls.addClassCleanup(cls.tmp.cleanup)
        directory = Path(cls.tmp.name)
        source = (ROOT / "nuttx/drivers/contactless/mfrc522_rf.c").read_text()
        code = r"""
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define FAR
#define OK 0
#define DEBUGASSERT assert
#define ctlserr(...) ((void)0)
#define ctlsinfo(...) ((void)0)
#define MFRC522_TX_CTRL_REG (0x14 << 1)
#define MFRC522_TX1_RF_EN 1
#define MFRC522_TX2_RF_EN 2
#define MFRC522IOC_SET_RF 0x240f
#define MFRC522IOC_GET_PICC_UID 0x2401
#define MFRC522IOC_GET_STATE 0x2402
#define CLIOC_READ_MIFARE_DATA 0x240c
#define PICC_TYPE_NOT_COMPLETE 4
#define MFRC522_STATE_NOT_INIT 0
#define MFRC522_STATE_IDLE 1
struct spi_dev_s { int unused; };
struct mfrc522_dev_s { struct spi_dev_s *spi; int state; };
struct inode { void *i_private; };
struct file { struct inode *f_inode; };
struct picc_uid_s { uint8_t size, uid_data[10], sak; };
struct mifare_tag_data_s { uint8_t data[16], address; };
static uint8_t rf;
static int reads, writes, stuck, registrations, frees;
static int g_mfrc522fops;
static void *kmm_malloc(size_t n) { return calloc(1,n); }
static void kmm_free(void *p) { frees++; free(p); }
static uint8_t mfrc522_readu8(struct mfrc522_dev_s *d, uint8_t reg) {
 (void)d; assert(reg==MFRC522_TX_CTRL_REG); reads++; return rf;
}
static void mfrc522_writeu8(struct mfrc522_dev_s *d, uint8_t reg, uint8_t v) {
 (void)d; assert(reg==MFRC522_TX_CTRL_REG); writes++; if(!stuck) rf=v;
}
static int mfrc522_picc_request_a(struct mfrc522_dev_s *d, uint8_t *b, uint8_t n) {
 (void)d;(void)b;(void)n;abort();
}
static int mfrc522_picc_select(struct mfrc522_dev_s *d, struct picc_uid_s *u,
 uint8_t v) { (void)d;(void)u;(void)v;abort(); }
static int mfrc522_mifare_read(struct mfrc522_dev_s *d,
 struct mifare_tag_data_s *t) { (void)d;(void)t;abort(); }
static void mfrc522_init(struct mfrc522_dev_s *d) { (void)d;rf=0x83; }
static uint8_t mfrc522_getfwversion(struct mfrc522_dev_s *d) { (void)d;return 0x92; }
static int register_driver(const char *path, void *ops, int mode, void *dev) {
 (void)path;(void)ops;(void)mode;assert((rf&3)==0);registrations++;free(dev);return 0;
}
"""
        for marker in (
            "void mfrc522_enableantenna(",
            "void mfrc522_disableantenna(",
            "static int mfrc522_set_rf(",
            "static int mfrc522_ioctl(",
            "int mfrc522_register(",
        ):
            code += extract_function(source, marker) + "\n"
        code += r"""
int main(int argc,char **argv) {
 struct mfrc522_dev_s d={0};struct inode inode={&d};struct file f={&inode};
 assert(argc==2);
 if(!strcmp(argv[1],"switch")) {
  rf=0x80;
  assert(mfrc522_ioctl(&f,MFRC522IOC_SET_RF,1)==0 && rf==0x83);
  int old=writes;
  assert(mfrc522_ioctl(&f,MFRC522IOC_SET_RF,1)==0 && writes==old);
  assert(mfrc522_ioctl(&f,MFRC522IOC_SET_RF,0)==0 && rf==0x80);
  old=reads;int oldwrite=writes;
  assert(mfrc522_ioctl(&f,MFRC522IOC_SET_RF,2)==-EINVAL);
  assert(reads==old && writes==oldwrite);
  assert(mfrc522_ioctl(&f,999,0)==-ENOTTY);
 } else if(!strcmp(argv[1],"stuck")) {
  stuck=1;rf=0x83;
  assert(mfrc522_ioctl(&f,MFRC522IOC_SET_RF,0)==-EIO && (rf&3)==3);
  rf=0x80;assert(mfrc522_ioctl(&f,MFRC522IOC_SET_RF,1)==-EIO);
 } else if(!strcmp(argv[1],"register")) {
  assert(mfrc522_register("/dev/test",NULL)==0);
  assert(registrations==1 && rf==0x80);
 } else {
  assert(!strcmp(argv[1],"register-fail"));stuck=1;
  assert(mfrc522_register("/dev/test",NULL)==-EIO);
  assert(registrations==0 && frees==1);
 }
 return 0;
}
"""
        (directory / "test.c").write_text(code)
        cls.binary = directory / "test"
        subprocess.run(
            [
                "cc",
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                str(directory / "test.c"),
                "-o",
                str(cls.binary),
            ],
            check=True,
        )

    def test_switch(self):
        subprocess.run([self.binary, "switch"], check=True)

    def test_stuck(self):
        subprocess.run([self.binary, "stuck"], check=True)

    def test_registration(self):
        subprocess.run([self.binary, "register"], check=True)

    def test_registration_failure(self):
        subprocess.run([self.binary, "register-fail"], check=True)


if __name__ == "__main__":
    unittest.main()
