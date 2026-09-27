/* SPDX-License-Identifier: Apache-2.0 */
#define main storage_fixture_main
#include "test_pc_storage.c"
#undef main
#include "bk7258_pc_authorization_owner.h"
#include "bk7258_provision_owner.h"
#include "bk7258_provision_settings.h"
#include "bk7258_provision_gatt.h"
#include "bk7258_provision_scan.h"
static mbedtls_x509_crt certificate;
static mbedtls_pk_context identity_key;
bool bkprov_gatt_idle(void) { return true; }
bool bkprov_scan_busy(void) { return false; }
static int execute(void *c,enum bkcontrol_command_e cmd,uint32_t value,struct bkcontrol_status_s *s)
{ (void)c;(void)cmd;(void)value;(void)s;return 0; }
static uint8_t bundle[BKPROV_BUNDLE_MAX];
static size_t bundle_size;
static void encode(const uint8_t *key)
{
 struct bkprov_settings_s settings={.control_key=key,.deferred=true,.utc=1750000000};
 assert(bkprov_settings_encode(&settings,bundle,sizeof(bundle),&bundle_size)==0);
}
static int prepare(uint64_t rev)
{
 int ret=-EAGAIN;
 for(int i=0;i<3000 && ret==-EAGAIN;i++)
  {ret=bkpc_authorization_prepare(rev,bundle,bundle_size);if(ret==-EAGAIN)tick();}
 return ret;
}
static int read_current(void)
{
 struct bkcontrol_status_s status={0};
 return bkpc_authorization_current(BKCONTROL_CONFIG_READ,0,NULL,0,&status);
}
int main(int argc,char **argv)
{
 assert(argc==3 && bkprov_storage_start(argv[2])==0 && receipt(config_tx)==0);
 encode(phone);assert(bkprov_storage_commit(0,config_tx,bundle,bundle_size)==-EAGAIN);
 assert(receipt(config_tx)==1);
 assert(bkprov_owner_bind(&certificate,&identity_key,owner2,NULL,NULL)==0);
 assert(read_current()<0);
 assert(bkpc_authorization_prepare(1,bundle,bundle_size)==-EACCES);
 assert(bkprov_owner_control(phone,execute,NULL)==0);
 assert(prepare(1)==0 && read_current()==0);
 assert(set(1,0,tx,3)==0);
 if(!strcmp(argv[1],"revision"))
  {
   assert(bkprov_storage_commit(1,next,bundle,bundle_size)==-EAGAIN);
   assert(receipt(next)==1 && read_current()==-ESTALE);
   assert(prepare(1)==-ESTALE && prepare(2)==0 && read_current()==0);
  }
 else if(!strcmp(argv[1],"owner"))
  {
   encode(owner2);
   assert(bkprov_storage_commit(1,next,bundle,bundle_size)==-EAGAIN);
   assert(receipt(next)==1 && prepare(2)==-EACCES && read_current()==-EACCES);
   assert(bkprov_owner_control(owner2,execute,NULL)==0);
   assert(prepare(2)==0 && read_current()==0);
   struct bkprov_pc_snapshot_s view;
   assert(bkprov_storage_pc_snapshot(2,&view)==0 && view.capabilities==0);
  }
 else if(!strcmp(argv[1],"invalid"))
  {
   bundle[0]='X';assert(prepare(1)<0 && read_current()<0);
   encode(phone);assert(prepare(1)==0 && read_current()==0);
  }
 else if(!strcmp(argv[1],"unbind"))
  {
   bkpc_authorization_unbind();assert(read_current()==-ENOKEY);
   assert(prepare(1)==0 && read_current()==0);
  }
 else if(!strcmp(argv[1],"source"))
  {
   struct bkprov_pc_snapshot_s view;
   uint64_t binding = 0;
   assert(bkpc_authorization_snapshot(NULL, &binding, &view) == 0);
   assert(binding == 1 && view.revision == 1 && view.capabilities == 3);
   assert(!memcmp(view.client, client, 16) && !memcmp(view.key, pc, 32));
   memset(&view, 0xa5, sizeof(view));
   bkpc_authorization_unbind();
   assert(bkpc_authorization_snapshot(NULL, &binding, &view) == -ENOKEY);
   assert(binding == 0);
   for(size_t i=0;i<sizeof(view);i++) assert(((uint8_t *)&view)[i] == 0);
   assert(prepare(1)==0);
  }
 else if(!strcmp(argv[1],"source-revision"))
  {
   struct bkprov_pc_snapshot_s view;
   uint64_t binding = 0;
   assert(bkpc_authorization_snapshot(NULL, &binding, &view) == 0 && binding == 1);
   uint64_t grant = view.revision;
   assert(bkprov_storage_commit(1,next,bundle,bundle_size)==-EAGAIN);
   assert(receipt(next)==1 && prepare(2)==0);
   assert(bkpc_authorization_snapshot(NULL, &binding, &view) == 0);
   assert(binding == 2 && view.revision == grant && !memcmp(view.key,pc,32));
  }
 else assert(!strcmp(argv[1],"offline"));
 bkpc_authorization_unbind();
 assert(bkprov_storage_stop()==0);puts("CONTRACT_PASS");return 0;
}
