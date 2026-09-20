#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Script peer frames to verify APDU ordering, WTX bounds and failure cleanup."""
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
HARNESS = r"""
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <nuttx/contactless/isodep.h>
struct step {uint8_t tx[62],rx[62];size_t nt,nr;int error;uint32_t wait;};
static struct step steps[12];static unsigned current,count;static uint64_t clock_ms;static int released;
static int exchange(void *arg,const uint8_t *tx,size_t nt,uint8_t *rx,size_t *nr,uint32_t wait) {
 (void)arg;assert(current<count);struct step *s=&steps[current++];
 assert(nt==s->nt&&!memcmp(tx,s->tx,nt));assert(wait==s->wait&&*nr==62);
 memcpy(rx,s->rx,s->nr);*nr=s->nr;clock_ms+=1;return s->error;
}
static int delay(void *arg,uint32_t us){(void)arg;(void)us;return 0;}
static void release(void *arg){(void)arg;released++;}
static uint64_t now_ms(void *arg){(void)arg;return clock_ms;}
static const struct isodep_transport_s ops={exchange,delay,release,now_ms};
static void init(struct isodep_session_s *s) {
 memset(steps,0,sizeof(steps));current=count=0;clock_ms=0;released=0;
 *s=(struct isodep_session_s){.transport=&ops,.fwt_us=4834,.fsc=64,.active=true};
}
static struct step *add(const uint8_t *tx,size_t nt,const uint8_t *rx,size_t nr,uint32_t wait) {
 struct step *s=&steps[count++];memcpy(s->tx,tx,nt);memcpy(s->rx,rx,nr);s->nt=nt;s->nr=nr;s->wait=wait;return s;
}
int main(void) {
 struct isodep_session_s s;uint8_t command[]={0,0xa4,4,0,8,0xf0,0x53,0x48,0x41,0x4e,0x49,0x55,1};
 uint8_t tx[62]={2};memcpy(tx+1,command,sizeof(command));
 uint8_t ok[]={2,0x53,0x48,0x4e,1,0x90,0};uint8_t out[100];size_t n;
 init(&s);add(tx,14,ok,7,5);n=sizeof(out);
 assert(!isodep_transceive(&s,command,sizeof(command),out,&n,100));
 assert(n==6&&!memcmp(out,ok+1,6)&&s.block==1&&released==0);
 tx[0]=3;ok[0]=3;add(tx,14,ok,7,5);n=sizeof(out);
 assert(!isodep_transceive(&s,command,sizeof(command),out,&n,100)&&s.block==0);
 tx[0]=2;ok[0]=2;
 // WTX then normal response; block numbering must not change for WTX.
 init(&s);uint8_t wtx[]={0xf2,3};add(tx,14,wtx,2,5);add(wtx,2,ok,7,15);n=sizeof(out);
 assert(!isodep_transceive(&s,command,sizeof(command),out,&n,100)&&n==6);
 // Two received fragments, including a repeated first fragment after lost ACK.
 init(&s);uint8_t first[]={0x12,1,2},ack[]={0xa3},last[]={3,3,4};
 add(tx,14,first,3,5);add(ack,1,first,3,5);add(ack,1,last,3,5);n=sizeof(out);
 assert(!isodep_transceive(&s,command,sizeof(command),out,&n,100)&&n==4);
 assert(out[0]==1&&out[1]==2&&out[2]==3&&out[3]==4&&s.block==0);
 // Minimum FSC forces command chaining. First fragment is retried verbatim.
 init(&s);s.fsc=16;uint8_t longcmd[20];memset(longcmd,0x55,20);
 uint8_t a[14]={0x12},b[8]={3},nak[]={0xb2};memset(a+1,0x55,13);memset(b+1,0x55,7);
 add(a,14,nak,1,5);add(a,14,ack,1,5);ok[0]=3;add(b,8,ok,7,5);n=sizeof(out);
 assert(!isodep_transceive(&s,longcmd,20,out,&n,100)&&n==6&&s.block==0);ok[0]=2;
 // The original overall budget caps subsequent WTX, and rejects a late reply.
 init(&s);wtx[1]=59;add(tx,14,wtx,2,2);add(wtx,2,ok,7,1);n=sizeof(out);memset(out,0xaa,sizeof(out));
 assert(isodep_transceive(&s,command,sizeof(command),out,&n,2)==-ETIMEDOUT);
 assert(!s.active&&released==1&&n==0);for(unsigned i=0;i<sizeof(out);i++)assert(out[i]==0);
 // Bad WTX multipliers and framing, output overflow, I-block sequence mismatch.
 uint8_t invalid[][3]={{0xf2,0,0},{0xf2,60,0},{0xfa,0,1},{3,0,0},{2,1,2}};
 for(unsigned i=0;i<5;i++) {
  init(&s);add(tx,14,invalid[i],i==2?3:2,5);n=i==4?0:sizeof(out);
  if(i==4){n=1;steps[0].nr=3;}
  int ret=isodep_transceive(&s,command,sizeof(command),out,&n,100);
  assert(ret==(i==4?-EMSGSIZE:-EPROTO)&&!s.active&&released==1&&n==0);
 }
 init(&s);struct step *e=add(tx,14,ok,7,5);e->error=-EIO;n=sizeof(out);
 assert(isodep_transceive(&s,command,sizeof(command),out,&n,100)==-EIO&&released==1);
 return 0;
}
"""
with tempfile.TemporaryDirectory(prefix="isodep-apdu-") as tmp:
    p = Path(tmp)
    (p / "test.c").write_text(HARNESS)
    subprocess.run(
        [
            "cc",
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-fsanitize=undefined",
            "-I" + str(ROOT / "nuttx/include"),
            str(p / "test.c"),
            str(ROOT / "nuttx/drivers/contactless/isodep.c"),
            "-o",
            str(p / "test"),
        ],
        check=True,
    )
    subprocess.run([str(p / "test")], check=True)
print(
    "PASS: APDU sequence, bidirectional chaining, retransmit, WTX deadline and failure cleanup"
)
