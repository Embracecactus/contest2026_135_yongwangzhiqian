#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Production CRC/communication waits; substitute register bus and monotonic clock."""
import re
import subprocess
import tempfile
import unittest
from pathlib import Path
from test_mfrc522_read_errors import extract_function
ROOT = Path(__file__).resolve().parents[3]

class DeadlineTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix="shaniu-nfc-deadline-")
        cls.addClassCleanup(cls.tmp.cleanup)
        source = (ROOT / "nuttx/drivers/contactless/mfrc522_rf.c").read_text()
        functions = "\n".join(extract_function(source, name) for name in
                             ("int mfrc522_calc_crc(", "static int mfrc522_comm_picc_ex(", "int mfrc522_comm_picc("))
        header = (ROOT / "nuttx/drivers/contactless/mfrc522.h").read_text()
        definitions = {m.group(1): m.group(0) for m in re.finditer(
            r"^#\s*define\s+(MFRC522_\w+)\s+[^\n]+", header, re.M)}
        needed = set(re.findall(r"\bMFRC522_\w+", functions))
        while True:
            expanded = needed | set(re.findall(r"\bMFRC522_\w+", "\n".join(definitions[n] for n in needed)))
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
struct mfrc522_dev_s { int unused; };
static uint64_t origin, offsets[3];
static unsigned clocks, irq_reads;
static int mode;
static void clock_systime_timespec(struct timespec *t) {
 assert(clocks < 3); /* finite evidence: no uncontrolled hang */
 uint64_t ns=origin+offsets[clocks++];t->tv_sec=ns/1000000000;t->tv_nsec=ns%1000000000;
}
'''
        code += "\n".join(definitions[n] for n in sorted(needed)) + "\n"
        code += r'''
static uint8_t mfrc522_readu8(struct mfrc522_dev_s *d,uint8_t reg) {
 (void)d;
 if(reg==MFRC522_DIV_IRQ_REG || reg==MFRC522_COM_IRQ_REG) {
   /* comm clears IRQ before starting; exclude that read from completion stimulus. */
   irq_reads++;
   unsigned index=irq_reads-(reg==MFRC522_COM_IRQ_REG?1:0);
   if(mode==1 && index==2)return reg==MFRC522_DIV_IRQ_REG?MFRC522_CRC_IRQ:0x30;
   if(mode==2 && index==1)return MFRC522_TIMER_IRQ;
   if(mode==3 && index==1)return 0x30;
   return 0;
 }
 if(reg==MFRC522_ERROR_REG)return mode==3?MFRC522_PROTO_ERR:0;
 return 0;
}
static void mfrc522_writeu8(struct mfrc522_dev_s *d,uint8_t reg,uint8_t value) {(void)d;(void)reg;(void)value;}
static void mfrc522_writeblk(struct mfrc522_dev_s *d,uint8_t reg,uint8_t *b,int n) {(void)d;(void)reg;(void)b;(void)n;}
static void mfrc522_readblk(struct mfrc522_dev_s *d,uint8_t reg,uint8_t *b,int n,uint8_t align) {(void)d;(void)reg;(void)b;(void)n;(void)align;}
'''
        code += functions
        code += r'''
int main(int argc,char **argv) {
 assert(argc==2);struct mfrc522_dev_s d={0};uint8_t data=0,result[2]={0};
 bool crc=!strncmp(argv[1],"crc",3);
 origin=strstr(argv[1],"wrap")?10950000000ULL:10200000000ULL;
 offsets[0]=0;offsets[1]=199000000;offsets[2]=200000000;
 if(strstr(argv[1],"success"))mode=1;
 if(!strcmp(argv[1],"hardware"))mode=2;
 if(!strcmp(argv[1],"protocol"))mode=3;
 int ret=crc?mfrc522_calc_crc(&d,&data,1,result):
 mfrc522_comm_picc(&d,MFRC522_TRANSCV_CMD,0x30,&data,1,NULL,NULL,NULL,0,false);
 assert(ret==(mode==1?0:mode==3?-EPROTO:-ETIMEDOUT));
 assert(clocks==(mode==1?2:mode==2||mode==3?1:3));
 return 0;
}
'''
        path = Path(cls.tmp.name) / "driver.c"; path.write_text(code)
        cls.binary = Path(cls.tmp.name) / "driver"
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-fsanitize=undefined",
                        "-fno-sanitize-recover=all", str(path), "-o", str(cls.binary)], check=True)
    def run_case(self, case):
        run = subprocess.run([str(self.binary), case], capture_output=True, text=True, timeout=3)
        self.assertEqual(run.returncode, 0, run.stderr)
    def test_crc_deadline(self): self.run_case("crc-deadline")
    def test_crc_wrap(self): self.run_case("crc-wrap")
    def test_crc_success(self): self.run_case("crc-success")
    def test_comm_deadline(self): self.run_case("comm-deadline")
    def test_comm_wrap(self): self.run_case("comm-wrap")
    def test_comm_success(self): self.run_case("comm-success")
    def test_hardware(self): self.run_case("hardware")
    def test_protocol(self): self.run_case("protocol")
if __name__ == "__main__": unittest.main()
