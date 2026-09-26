#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Actual bounded RF exchange and observation logic; external bus/clock/UID selection."""
import re
import subprocess
import tempfile
import unittest
from pathlib import Path
from test_mfrc522_read_errors import extract_function
ROOT = Path(__file__).resolve().parents[3]

class ObservationTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix="shaniu-nfc-observe-")
        cls.addClassCleanup(cls.tmp.cleanup)
        source = (ROOT / "nuttx/drivers/contactless/mfrc522_rf.c").read_text()
        functions = "\n".join(extract_function(source, name) for name in
            ("int mfrc522_calc_crc(", "static int mfrc522_comm_picc_ex(", "static int mfrc522_observe(", "static int mfrc522_ioctl("))
        header = (ROOT / "nuttx/drivers/contactless/mfrc522.h").read_text()
        definitions = {m.group(1): m.group(0) for m in re.finditer(
            r"^#\s*define\s+((?:MFRC522(?:IOC)?|PICC)_\w+)\s+[^\n]+", header, re.M)}
        definitions["MFRC522IOC_OBSERVE"] = "#define MFRC522IOC_OBSERVE 0x2410"
        definitions["MFRC522IOC_SET_RF"] = "#define MFRC522IOC_SET_RF 0x240f"
        definitions["MFRC522IOC_GET_PICC_UID"] = "#define MFRC522IOC_GET_PICC_UID 0x2401"
        definitions["MFRC522IOC_GET_STATE"] = "#define MFRC522IOC_GET_STATE 0x2402"
        definitions["PICC_TYPE_NOT_COMPLETE"] = "#define PICC_TYPE_NOT_COMPLETE 4"
        needed = set(re.findall(r"\b(?:MFRC522(?:IOC)?|PICC)_\w+", functions))
        while True:
            expanded = needed | set(re.findall(r"\b(?:MFRC522(?:IOC)?|PICC)_\w+", "\n".join(definitions[n] for n in needed)))
            if expanded == needed: break
            needed = expanded
        code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#define FAR
#define OK 0
#include <stdlib.h>
#define DEBUGASSERT assert
#define ctlsinfo(...) ((void)0)
#define ctlserr(...) ((void)0)
#define CLIOC_READ_MIFARE_DATA 0x240c
struct mfrc522_dev_s { int state; };
struct inode { void *i_private; };
struct file { struct inode *f_inode; };
struct mifare_tag_data_s { int unused; };
struct picc_uid_s { uint8_t size, uid_data[10], sak; };
struct mfrc522_observation_s { uint32_t present; struct picc_uid_s uid; };
static int mode, selections, writes, irq_reads;
static unsigned clocks;
static void clock_systime_timespec(struct timespec *t) {
 assert(clocks<3);t->tv_sec=10;t->tv_nsec=clocks++*100000000;
}
'''
        code += "\n".join(definitions[n] for n in sorted(needed)) + "\n"
        code += r'''
static uint8_t mfrc522_readu8(struct mfrc522_dev_s *d,uint8_t reg) {
 (void)d;
 if(reg==MFRC522_TX_CTRL_REG)return mode==1?0:3;
 if(reg==MFRC522_COM_IRQ_REG) {
   if(++irq_reads==1)return 0;
   if(mode==3)return 0; /* software deadline, no chip timer */
   return mode==2||mode==4||mode==10?MFRC522_TIMER_IRQ:MFRC522_RX_IRQ;
 }
 if(reg==MFRC522_ERROR_REG)return mode==10?4:mode==4?MFRC522_PARITY_ERR:mode==9?MFRC522_COLL_ERR:0;
 if(reg==MFRC522_FIFO_LEVEL_REG)return mode==5?1:2;
 return 0;
}
static void mfrc522_writeu8(struct mfrc522_dev_s *d,uint8_t r,uint8_t v) {(void)d;(void)r;(void)v;writes++;}
static void mfrc522_writeblk(struct mfrc522_dev_s *d,uint8_t r,uint8_t *b,int n) {(void)d;(void)r;assert(*b==PICC_CMD_REQA && n==1);}
static void mfrc522_readblk(struct mfrc522_dev_s *d,uint8_t r,uint8_t *b,int n,uint8_t a) {(void)d;(void)r;(void)a;memset(b,0,n);}
static int mfrc522_picc_select(struct mfrc522_dev_s *d,struct picc_uid_s *uid,uint8_t bits) {
 (void)d;assert(bits==0);selections++;uid->size=4;memset(uid->uid_data,1,4);uid->sak=0;
 if(mode==6)return -ETIMEDOUT;
 if(mode==7)uid->size=3;
 return 0;
}
'''
        code += r'''
static int mfrc522_set_rf(struct mfrc522_dev_s *d,unsigned long a) {(void)d;(void)a;abort();}
static int mfrc522_picc_request_a(struct mfrc522_dev_s *d,uint8_t *b,uint8_t n) {(void)d;(void)b;(void)n;abort();}
static int mfrc522_mifare_read(struct mfrc522_dev_s *d,struct mifare_tag_data_s *t) {(void)d;(void)t;abort();}
'''
        code += functions
        code += r'''
int main(int argc,char **argv) {
 assert(argc==2);struct mfrc522_dev_s d={0};struct mfrc522_observation_s o;
 memset(&o,0xa5,sizeof(o));
 struct inode inode={&d};struct file file={&inode};
 if(!strcmp(argv[1],"off"))mode=1;
 if(!strcmp(argv[1],"quiet"))mode=2;
 if(!strcmp(argv[1],"watchdog"))mode=3;
 if(!strcmp(argv[1],"timer-error"))mode=4;
 if(!strcmp(argv[1],"partial"))mode=5;
 if(!strcmp(argv[1],"selection-timeout"))mode=6;
 if(!strcmp(argv[1],"invalid-uid"))mode=7;
 if(!strcmp(argv[1],"collision"))mode=9;
 if(!strcmp(argv[1],"residual-error"))mode=10;
 if(!strcmp(argv[1],"null")) {assert(mfrc522_ioctl(&file,MFRC522IOC_OBSERVE,0)==-EINVAL && writes==0);return 0;}
 int ret=mfrc522_ioctl(&file,MFRC522IOC_OBSERVE,(unsigned long)&o);
 if(mode==0 || mode==9) {assert(ret==0 && o.present==1 && o.uid.size==4 && selections==1);}
 else {
  assert(ret==(mode==1?-ESHUTDOWN:mode==2?0:mode==3||mode==6?-ETIMEDOUT:(mode==4||mode==10)?-EIO:-EPROTO));
  struct mfrc522_observation_s empty={0};assert(memcmp(&o,&empty,sizeof(o))==0);
  assert(selections==(mode==6||mode==7?1:0));
 }
 if(mode==1)assert(writes==0);
 return 0;
}
'''
        path=Path(cls.tmp.name)/"driver.c";path.write_text(code)
        cls.binary=Path(cls.tmp.name)/"driver"
        subprocess.run(["cc","-std=c11","-Wall","-Wextra","-Werror","-fsanitize=undefined",
                        "-fno-sanitize-recover=all",str(path),"-o",str(cls.binary)],check=True)
    def run_case(self,name):
        result=subprocess.run([str(self.binary),name],capture_output=True,text=True,timeout=3)
        self.assertEqual(result.returncode,0,result.stderr)
    def test_off(self): self.run_case("off")
    def test_quiet(self): self.run_case("quiet")
    def test_watchdog(self): self.run_case("watchdog")
    def test_timer_error(self): self.run_case("timer-error")
    def test_partial(self): self.run_case("partial")
    def test_selection_timeout(self): self.run_case("selection-timeout")
    def test_invalid_uid(self): self.run_case("invalid-uid")
    def test_collision(self): self.run_case("collision")
    def test_present(self): self.run_case("present")
    def test_residual_error(self): self.run_case("residual-error")
    def test_null(self): self.run_case("null")
if __name__ == "__main__": unittest.main()
