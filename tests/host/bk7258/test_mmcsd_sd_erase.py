#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
from pathlib import Path
import subprocess
import sys
import tempfile

s = Path(sys.argv[1]).read_text()
a = s.index("static bool mmcsd_erase_iocmd_valid")
b = s.index(
    "\n/****************************************************************************\n * Name: mmcsd_iocmd",
    a,
)
c = s.index("static int mmcsd_multi_iocmd", b)
d = s.index("\n#endif", c)
body = s[a:b] + "\n" + s[c:d]
prefix = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
typedef long clock_t;
typedef uint32_t blkcnt_t;
#define FAR
#define OK 0
#define MMC_IOC_MAX_CMDS 8
#define MCSD_SZ_512 512u
#define MMCSD_ERASE_TIMEOUT_MS 30000u
#define MMCSD_CCC_ERASE (1u << 5)
#define MMCSD_CMDIDX13 13u
#define MMCSD_CMD13 13u
#define SD_CMDIDX32 32u
#define SD_CMD32 32u
#define SD_CMDIDX33 33u
#define SD_CMD33 33u
#define MMCSD_CMDIDX38 38u
#define MMCSD_CMD38 38u
#define MMCSD_CMDIDX_MASK 0x3fu
#define MMCSD_R1_ERRORMASK 0xfdffe088u
#define MMCSD_R1_CARDISLOCKED (1u << 25)
#define MMCSD_R1_STATE_TRAN (4u << 9)
#define MMCSD_R1_STATE_RCV (6u << 9)
#define MMCSD_R1_STATE_PRG (7u << 9)
#define MMCSD_R1_READYFORDATA (1u << 8)
#define MMCSD_CARDTYPE_UNKNOWN 0
#define MMCSD_CARDTYPE_MMC 1
#define MMCSD_CARDTYPE_SDSC 4
#define MMCSD_CARDTYPE_SDHC 12
#define IS_EMPTY(p) ((p)->type == MMCSD_CARDTYPE_UNKNOWN)
#define IS_SD(t) (((t)&(2|4)) != 0)
#define IS_BLOCK(t) (((t)&8) != 0)
#define IS_STATE(r,s) (((r)&(0xfu<<9))==(s))
#define SDIO_PRESENT(d) ((d)!=NULL)
#define MSEC2TICK(x) ((clock_t)(x))
#define MMCSD_USLEEP(x) do { (void)(x); now++; } while (0)
#define DEBUGASSERT(x) assert(x)
struct sdio_dev_s { int present; };
struct mmcsd_state_s;
struct mmcsd_part_s { struct mmcsd_state_s *priv; uint32_t nblocks; };
struct mmcsd_state_s { struct sdio_dev_s *dev; unsigned type,partnum; bool wrbusy,wrprotect,locked; uint32_t blocksize,selblocklen,rca,csd[4]; struct mmcsd_part_s part[2]; };
struct mmc_ioc_cmd { uint32_t opcode,arg,flags,write_flag,is_acmd,blksz,blocks,postsleep_min_us,postsleep_max_us,data_timeout_ns,pad,data_ptr,cmd_timeout_ms,response[4]; };
struct mmc_ioc_multi_cmd { int num_of_cmds; struct mmc_ioc_cmd cmds[8]; };
static clock_t now; static unsigned sent,status_i,status_n; static uint32_t sent_cmd[8],sent_arg[8],status_seq[8]; static int send_fail,recv_fail;
static int mmcsd_wrprotected(struct mmcsd_state_s *p){return p->wrprotect;}
static int mmcsd_sendcmdpoll(struct mmcsd_state_s *p,uint32_t c,uint32_t a){(void)p;assert(sent<64);sent_cmd[sent]=c;sent_arg[sent++]=a;return send_fail&&c==38?-EIO:OK;}
static int mock_recvr1(struct sdio_dev_s *d,uint32_t c,uint32_t *r)
  { (void)d; if (recv_fail && c==38) return -EIO; if (c==13) { *r=status_i<status_n?status_seq[status_i++]:MMCSD_R1_STATE_PRG; } else *r=0; return OK; }
#define SDIO_RECVR1(d,c,r) mock_recvr1((d),(c),(r))
static clock_t clock_systime_ticks(void){return now++;}
static void ferr(const char *f,...){(void)f;}
"""
prefix += r"""
static int mmcsd_iocmd(struct mmcsd_part_s *p, struct mmc_ioc_cmd *c){(void)p;(void)c;return -EINVAL;}
"""
suffix = r"""
static void initp(struct mmcsd_state_s *p, struct sdio_dev_s *d)
{
  memset(p,0,sizeof(*p)); p->dev=d; p->type=4; p->blocksize=p->selblocklen=512; p->csd[0]=0x007f0032; p->csd[1]=0x535a803c; p->csd[2]=0x6ebbff9f; p->csd[3]=0x00168000; p->partnum=0; p->part[0].priv=p; p->part[0].nblocks=1000000; p->wrbusy=true; now=sent=status_i=0; status_n=2; send_fail=recv_fail=0; status_seq[0]=status_seq[1]=MMCSD_R1_STATE_TRAN|MMCSD_R1_READYFORDATA;
}
static struct mmc_ioc_multi_cmd makecmd(uint32_t lba){struct mmc_ioc_multi_cmd m;memset(&m,0,sizeof(m));m.num_of_cmds=3;m.cmds[0].opcode=32;m.cmds[0].arg=lba;m.cmds[1].opcode=33;m.cmds[1].arg=lba+255;m.cmds[2].opcode=38;m.cmds[2].cmd_timeout_ms=10;return m;}
int main(void)
{
  struct mmcsd_state_s p; struct sdio_dev_s d={1}; struct mmc_ioc_multi_cmd m;
  initp(&p,&d); m=makecmd(1234); assert(mmcsd_multi_iocmd(&p.part[0],&m)==OK&&!p.wrbusy); assert(sent==5&&sent_cmd[1]==32&&sent_cmd[2]==33&&sent_cmd[3]==38&&sent_arg[1]==1234u*512u&&sent_arg[2]==(1234u+255u)*512u);
  initp(&p,&d); p.type=12;m=makecmd(1234);assert(mmcsd_multi_iocmd(&p.part[0],&m)==OK&&sent_arg[1]==1234&&sent_arg[2]==1489);
  initp(&p,&d);m=makecmd(1);m.num_of_cmds=2;assert(mmcsd_multi_iocmd(&p.part[0],&m)==-EINVAL&&sent==0);
  initp(&p,&d);m=makecmd(1);m.cmds[1].flags=1;assert(mmcsd_multi_iocmd(&p.part[0],&m)==-EINVAL&&sent==0);
  initp(&p,&d);p.type=1;m=makecmd(1);assert(mmcsd_multi_iocmd(&p.part[0],&m)==-EINVAL&&sent==0);
  initp(&p,&d);p.part[1].priv=&p;m=makecmd(1);assert(mmcsd_multi_iocmd(&p.part[1],&m)==-EINVAL&&sent==0);
  initp(&p,&d);m=makecmd(1);m.cmds[2].arg=1;assert(mmcsd_multi_iocmd(&p.part[0],&m)==-EINVAL&&sent==0);
  initp(&p,&d);p.csd[1]=0;m=makecmd(1);assert(mmcsd_multi_iocmd(&p.part[0],&m)==-EINVAL&&sent==0);
  initp(&p,&d);m=makecmd(1);m.cmds[0].arg=UINT32_MAX;assert(mmcsd_multi_iocmd(&p.part[0],&m)==-EINVAL&&sent==0);
  initp(&p,&d);p.part[0].nblocks=UINT32_MAX;m=makecmd(UINT32_MAX/512u+1u);m.cmds[1].arg=m.cmds[0].arg+255u;assert(mmcsd_multi_iocmd(&p.part[0],&m)==-EINVAL&&sent==0);
  initp(&p,&d);m=makecmd(1);send_fail=1;assert(mmcsd_multi_iocmd(&p.part[0],&m)<0&&p.wrbusy);
  initp(&p,&d);m=makecmd(1);recv_fail=1;assert(mmcsd_multi_iocmd(&p.part[0],&m)<0&&p.wrbusy);
  initp(&p,&d);m=makecmd(1);status_seq[0]=MMCSD_R1_STATE_TRAN;status_seq[1]=MMCSD_R1_STATE_PRG;status_seq[2]=MMCSD_R1_STATE_TRAN|MMCSD_R1_READYFORDATA;status_seq[3]=status_seq[2];status_n=4;assert(mmcsd_multi_iocmd(&p.part[0],&m)==OK&&!p.wrbusy);
  initp(&p,&d);m=makecmd(1);status_seq[1]=MMCSD_R1_STATE_PRG;status_n=2;m.cmds[2].cmd_timeout_ms=2;assert(mmcsd_multi_iocmd(&p.part[0],&m)==-ETIMEDOUT&&p.wrbusy);
  puts("MMCSD_SD_ERASE_HOST_TEST_PASS");return 0;
}
"""
with tempfile.TemporaryDirectory(prefix="mmcsd-sd-erase-") as d:
    root = Path(d)
    c = root / "test.c"
    c.write_text(prefix + body + suffix)
    subprocess.run(
        [
            "cc",
            "-std=gnu11",
            "-Wall",
            "-Wextra",
            "-Werror",
            str(c),
            "-o",
            str(root / "test"),
        ],
        check=True,
    )
    subprocess.run([str(root / "test")], check=True)
