#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Real REQA/selection admission/ioctl; external RF exchange and anticollision."""
import subprocess
import tempfile
import unittest
from pathlib import Path
from test_mfrc522_read_errors import extract_function

ROOT = Path(__file__).resolve().parents[3]


class SelectionTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix="shaniu-nfc-selection-")
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
#define ctlsinfo(...) ((void)0)
#define ctlserr(...) ((void)0)
#define MFRC522_COLL_REG 0x1c
#define MFRC522_VALUES_AFTER_COLL 0x80
#define PICC_CMD_REQA 0x26
#define PICC_TYPE_NOT_COMPLETE 4
#define MFRC522IOC_SET_RF 0x240f
#define MFRC522IOC_GET_PICC_UID 0x2401
#define MFRC522IOC_GET_STATE 0x2402
#define CLIOC_READ_MIFARE_DATA 0x240c
struct mfrc522_dev_s { int state; };
struct inode { void *i_private; };
struct file { struct inode *f_inode; };
struct picc_uid_s { uint8_t size, uid_data[10], sak; };
struct mifare_tag_data_s { int unused; };
static int probe_result, select_result, exchanges, selections, registers;
static uint8_t reply_length=2, reply_bits, uid_size=4, uid_sak;
static uint8_t mfrc522_readu8(struct mfrc522_dev_s *d,uint8_t reg) {
 (void)d;assert(reg==MFRC522_COLL_REG);registers++;return 0;
}
static void mfrc522_writeu8(struct mfrc522_dev_s *d,uint8_t reg,uint8_t v) {
 (void)d;(void)v;assert(reg==MFRC522_COLL_REG);registers++;
}
static int mfrc522_transcv_data(struct mfrc522_dev_s *d,uint8_t *tx,uint8_t n,
 uint8_t *rx,uint8_t *length,uint8_t *bits,uint8_t align,bool crc) {
 (void)d;assert(*tx==PICC_CMD_REQA && n==1 && *length==2 && *bits==7);
 assert(align==0 && !crc);exchanges++;*length=reply_length;*bits=reply_bits;
 rx[0]=4;rx[1]=0;return probe_result;
}
static int mfrc522_picc_select(struct mfrc522_dev_s *d,struct picc_uid_s *uid,
 uint8_t bits) {
 (void)d;assert(bits==0);selections++;
 uid->size=uid_size;uid->sak=uid_sak;
 for(int i=0;i<10;i++) uid->uid_data[i]=(uint8_t)(i+1);
 return select_result;
}
static int mfrc522_set_rf(struct mfrc522_dev_s *d,unsigned long on) {
 (void)d;(void)on;abort();
}
static int mfrc522_mifare_read(struct mfrc522_dev_s *d,struct mifare_tag_data_s *t) {
 (void)d;(void)t;abort();
}
"""
        markers = [
            "int mfrc522_picc_reqa_wupa(",
            "int mfrc522_picc_request_a(",
            "int mfrc522_picc_detect(",
        ]
        if "static int mfrc522_select_uid(" in source:
            markers.append("static int mfrc522_select_uid(")
        markers.append("static int mfrc522_ioctl(")
        for marker in markers:
            code += extract_function(source, marker) + "\n"
        code += r"""
static int get(struct file *f,struct picc_uid_s *uid) {
 return mfrc522_ioctl(f,MFRC522IOC_GET_PICC_UID,(unsigned long)uid);
}
static void cleared(struct picc_uid_s *uid) {
 struct picc_uid_s zero={0};assert(!memcmp(uid,&zero,sizeof(zero)));
}
int main(int argc,char **argv) {
 struct mfrc522_dev_s d={0};struct inode inode={&d};struct file f={&inode};
 struct picc_uid_s uid;memset(&uid,0xa5,sizeof(uid));assert(argc==2);
 if(!strcmp(argv[1],"probe-error")) {
  probe_result=-EIO;assert(get(&f,&uid)==-EIO);assert(selections==0);cleared(&uid);
 } else if(!strcmp(argv[1],"timeout")) {
  probe_result=-ETIMEDOUT;assert(get(&f,&uid)==-ETIMEDOUT);
  assert(selections==0);cleared(&uid);
 } else if(!strcmp(argv[1],"malformed")) {
  reply_length=1;assert(get(&f,&uid)==-EPROTO);assert(selections==0);cleared(&uid);
  reply_length=2;reply_bits=3;assert(get(&f,&uid)==-EPROTO);
  assert(selections==0);cleared(&uid);
 } else if(!strcmp(argv[1],"select-error")) {
  select_result=-EIO;assert(get(&f,&uid)==-EIO);assert(selections==1);cleared(&uid);
 } else if(!strcmp(argv[1],"invalid")) {
  assert(get(&f,NULL)==-EINVAL);assert(exchanges==0 && registers==0);
  uid_size=5;assert(get(&f,&uid)==-EPROTO);cleared(&uid);
  uid_size=4;uid_sak=4;assert(get(&f,&uid)==-EPROTO);cleared(&uid);
  uid_sak=0;select_result=1;assert(get(&f,&uid)==-EPROTO);cleared(&uid);
 } else if(!strcmp(argv[1],"valid")) {
  const uint8_t sizes[]={4,7,10};
  for(unsigned i=0;i<3;i++) {
   uid_size=sizes[i];probe_result=i==1?-EBUSY:0;
   assert(get(&f,&uid)==0 && uid.size==sizes[i] && uid.sak==0);
   for(int j=0;j<uid.size;j++) assert(uid.uid_data[j]==j+1);
  }
  assert(selections==3 && exchanges==3);
 } else {assert(0);}
 return 0;
}
"""
        (directory / "test.c").write_text(code)
        cls.binary = directory / "test"
        subprocess.run(
            [
                "cc",
                "-Wall",
                "-Wextra",
                "-Werror",
                str(directory / "test.c"),
                "-o",
                str(cls.binary),
            ],
            check=True,
        )

    def run_case(self, case):
        subprocess.run([str(self.binary), case], check=True)

    def test_probe_error(self):
        self.run_case("probe-error")

    def test_timeout(self):
        self.run_case("timeout")

    def test_malformed(self):
        self.run_case("malformed")

    def test_select_error(self):
        self.run_case("select-error")

    def test_invalid(self):
        self.run_case("invalid")

    def test_valid(self):
        self.run_case("valid")


if __name__ == "__main__":
    unittest.main()
