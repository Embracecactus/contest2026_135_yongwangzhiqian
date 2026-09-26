#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""MOT-01: complete production lower half; external bus/OS/clock seams only."""
import re
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]


class SamplingTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix="shaniu-accel-")
        cls.addClassCleanup(cls.tmp.cleanup)
        directory = Path(cls.tmp.name)

        # Replace platform includes, not any driver function or state machine.
        def body(path):
            return re.sub(r"^#include[^\n]*$", "", path.read_text(), flags=re.M)

        code = r"""
#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define FAR
#define CODE
#define OK 0
#define CONFIG_SENSORS_SC7A20 1
#define SENSOR_TYPE_ACCELEROMETER 1
struct file { int unused; };
typedef int mutex_t;
static int nxmutex_init(mutex_t *m) {*m=0;return 0;}
static int nxmutex_lock(mutex_t *m) {assert(!*m);*m=1;return 0;}
static int nxmutex_unlock(mutex_t *m) {assert(*m);*m=0;return 0;}
static void nxmutex_destroy(mutex_t *m) {assert(!*m);}
#define kmm_zalloc(n) calloc(1,n)
#define kmm_free free
struct sensor_lowerhalf_s;
struct sensor_ops_s {
 int (*activate)(struct sensor_lowerhalf_s *,struct file *,bool);
 int (*set_interval)(struct sensor_lowerhalf_s *,struct file *,uint32_t *);
 int (*fetch)(struct sensor_lowerhalf_s *,struct file *,char *,size_t);
};
struct sensor_lowerhalf_s {int type,nbuffer;const struct sensor_ops_s *ops;};
struct sensor_accel {uint64_t timestamp;float x,y,z,temperature;int32_t status;};
static struct sensor_lowerhalf_s *registered;
static int sensor_register(struct sensor_lowerhalf_s *s,int n) {assert(n==0);registered=s;return 0;}
static uint64_t stamp=100;
static int clock_calls;
static uint64_t sensor_get_timestamp(void) {clock_calls++;return ++stamp;}
static unsigned char regs[256];
static int status_error,data_error,data_reads,status_reads;
static int bus_read(void *arg,uint8_t reg,uint8_t *b,size_t n) {
 (void)arg;
 if(reg==0x0f) {assert(n==1);*b=0x11;return 0;}
 if(reg==0x27) {assert(n==1);status_reads++;if(status_error)return -status_error;*b=regs[reg];return 0;}
 assert(reg==0xa8 && n==6);data_reads++;
 if(data_error)return -data_error;
 const uint8_t raw[]={0x00,0x40,0x00,0xc0,0x80,0x00};
 memcpy(b,raw,6);regs[0x27]=0;return 0;
}
static int bus_write(void *arg,uint8_t reg,uint8_t val) {(void)arg;regs[reg]=val;return 0;}
"""
        code += body(ROOT / "nuttx/include/nuttx/sensors/sc7a20.h")
        code += body(ROOT / "nuttx/drivers/sensors/sc7a20.c")
        code += r"""
int main(int argc,char **argv) {
 assert(argc==2);struct sc7a20_config_s config={NULL,bus_read,bus_write};
 assert(sc7a20_register(0,&config)==0 && registered);
 struct sensor_accel sample,untouched;memset(&sample,0x5a,sizeof(sample));untouched=sample;
 if(!strcmp(argv[1],"register")) {assert(regs[0x23]==0x80);goto done;}
 if(!strcmp(argv[1],"inactive")) {
  assert(registered->ops->fetch(registered,NULL,(char *)&sample,sizeof(sample))==-EAGAIN);
  assert(!status_reads && !data_reads && !clock_calls);goto unchanged;
 }
 assert(registered->ops->activate(registered,NULL,true)==0);
 if(!strcmp(argv[1],"invalid")) {
  assert(registered->ops->fetch(registered,NULL,NULL,sizeof(sample))==-EINVAL);
  assert(registered->ops->fetch(registered,NULL,(char *)&sample,sizeof(sample)-1)==-EINVAL);
  assert(!status_reads && !data_reads && !clock_calls);goto unchanged;
 }
 if(!strcmp(argv[1],"not_ready") || !strcmp(argv[1],"partial")) {
  regs[0x27]=!strcmp(argv[1],"partial")?0x07:0;
  assert(registered->ops->fetch(registered,NULL,(char *)&sample,sizeof(sample))==-EAGAIN);
  assert(status_reads==1 && !data_reads && !clock_calls);goto unchanged;
 }
 regs[0x27]=0x08;
 if(!strcmp(argv[1],"status_error"))status_error=EIO;
 if(!strcmp(argv[1],"data_error"))data_error=EREMOTEIO;
 if(status_error || data_error) {
  assert(registered->ops->fetch(registered,NULL,(char *)&sample,sizeof(sample))==-(status_error?EIO:EREMOTEIO));
  assert(status_reads==1 && data_reads==(data_error?1:0) && !clock_calls);goto unchanged;
 }
 assert(!strcmp(argv[1],"fresh"));
 assert(registered->ops->fetch(registered,NULL,(char *)&sample,sizeof(sample))==sizeof(sample));
 assert(fabsf(sample.x-10.04201f)<0.0001f && fabsf(sample.y+10.04201f)<0.0001f);
 assert(fabsf(sample.z-0.0784532f)<0.000001f && sample.timestamp==101 && sample.status==0);
 untouched=sample;
 assert(registered->ops->fetch(registered,NULL,(char *)&sample,sizeof(sample))==-EAGAIN);
 assert(!memcmp(&sample,&untouched,sizeof(sample)) && data_reads==1 && clock_calls==1);
 regs[0x27]=0x08; /* Identical acceleration with a genuinely new conversion. */
 assert(registered->ops->fetch(registered,NULL,(char *)&sample,sizeof(sample))==sizeof(sample));
 assert(sample.timestamp==102 && sample.x==untouched.x && data_reads==2);goto done;
unchanged:
 assert(!memcmp(&sample,&untouched,sizeof(sample)));
done:
 assert(registered->ops->activate(registered,NULL,false)==0);
 free(registered);puts("CONTRACT_PASS");return 0;
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
                "-fsanitize=undefined",
                "-fno-sanitize-recover=all",
                str(directory / "test.c"),
                "-lm",
                "-o",
                str(cls.binary),
            ],
            check=True,
        )

    def run_case(self, name):
        subprocess.run([str(self.binary), name], check=True)


for case in (
    "register",
    "inactive",
    "invalid",
    "not_ready",
    "partial",
    "status_error",
    "data_error",
    "fresh",
):
    setattr(SamplingTest, "test_" + case, lambda self, name=case: self.run_case(name))

if __name__ == "__main__":
    unittest.main()
