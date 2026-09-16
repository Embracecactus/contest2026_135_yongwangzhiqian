/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include "bk7258_provision_settings.h"
#include "bk7258_voice_config.h"
#include "bk7258_cloud_config.h"
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <mbedtls/platform_util.h>

static uint32_t get32(const uint8_t *p)
{ return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3]; }
static void put16(uint8_t *p,size_t n) { p[0]=n>>8;p[1]=n; }

int bkprov_settings_decode(struct bkprov_settings_s *settings,
                            const void *bundle, size_t size)
{
  const uint8_t *p=bundle;
  size_t ssid,password,host,ca,cloud,control;
  uint64_t utc=0;
  if(settings==NULL) return -EINVAL;
  mbedtls_platform_zeroize(settings,sizeof(*settings));
  if(p==NULL || size<32 || (memcmp(p,"SCB1",4) && memcmp(p,"SCB2",4) &&
     memcmp(p,"SCB3",4)) ||
     p[7] || p[10] || p[11]) return -EBADMSG;
  cloud=get32(p+28);
  control=p[3]=='3' ? BKPROV_CONTROL_KEY_BYTES : 0;
  if((p[3]=='1' && cloud) || (p[3]!='1' && (cloud<24 || cloud>BKCLOUD_CONFIG_MAX)))
    return -EBADMSG;
  ssid=p[4];password=p[5];host=p[6];ca=get32(p+24);
  for(int i=16;i<24;i++) utc=(utc<<8)|p[i];
  if(ssid==0 || ssid>32 || (password!=0 && (password<8 || password>64)) ||
     host==0 || host>127 || ca==0 || ca>4096 ||
     size!=32+ssid+password+host+ca+cloud+control || (p[8]==0 && p[9]==0) ||
     p[12]==0 || p[12]==127 || p[12]>=224 ||
     utc<UINT64_C(1704067200) || utc>UINT64_C(4133980799)) return -EBADMSG;
  if(control)
    {
      uint8_t nonzero=0;
      for(size_t i=size-control;i<size;i++)nonzero|=p[i];
      if(!nonzero)return -EBADMSG;
    }
  if(memchr(p+32,0,ssid+password) ||
     !bkvoice_config_host_valid(p+32+ssid+password,host)) return -EBADMSG;
  if(password==64)
    {
      for(size_t i=0;i<64;i++)
        {
          uint8_t ch=p[32+ssid+i];
          if(!((ch>='0' && ch<='9') || (ch>='a' && ch<='f') ||
               (ch>='A' && ch<='F'))) return -EBADMSG;
        }
    }
  if(cloud)
    {
      struct bkcloud_config_s *config=malloc(sizeof(*config));
      if(!config) return -ENOMEM;
      const uint8_t *record=p+32+ssid+password+host+ca;
      int ret=bkcloud_config_decode(config,record,cloud);
      if(ret==0 && (strlen(config->host)!=host ||
          memcmp(config->host,p+32+ssid+password,host) ||
          config->port!=(((uint16_t)p[8]<<8)|p[9]))) ret=-EBADMSG;
      bkcloud_config_clear(config);free(config);
      if(ret<0) return ret;
      settings->cloud=record;settings->cloud_size=cloud;
    }
  memcpy(settings->ssid,p+32,ssid);
  memcpy(settings->password,p+32+ssid,password);
  memcpy(settings->host,p+32+ssid+password,host);
  memcpy(settings->address,p+12,4);
  settings->port=((uint16_t)p[8]<<8)|p[9];settings->utc=utc;
  settings->ca=p+32+ssid+password+host;settings->ca_size=ca;
  settings->control_key=control ? p+size-control : NULL;
  return 0;
}

int bkprov_settings_voice(const struct bkprov_settings_s *settings,
                          const uint8_t *certificate, size_t certificate_size,
                          const uint8_t *key, size_t key_size,
                          void *output, size_t capacity, size_t *size)
{
  uint8_t *p=output;
  size_t host,total;
  uint64_t utc;
  if(settings==NULL || certificate==NULL || key==NULL || output==NULL ||
     size==NULL || settings->ca==NULL || settings->ca_size==0 ||
     settings->ca_size>4096 || certificate_size==0 || certificate_size>4096 ||
     key_size==0 || key_size>4096) return -EINVAL;
  *size=0;host=strnlen(settings->host,sizeof(settings->host));
  if(!bkvoice_config_host_valid((const uint8_t *)settings->host,host)) return -EINVAL;
  total=32+host+settings->ca_size+certificate_size+key_size;
  if(total>BKVOICE_CONFIG_MAX_BYTES || total>capacity) return -ENOSPC;
  memset(p,0,32);memcpy(p,"BVC1",4);p[5]=1;
  utc=settings->utc;
  for(int i=15;i>=8;i--) {p[i]=utc;utc>>=8;}
  put16(p+16,host);put16(p+18,settings->port);memcpy(p+20,settings->address,4);
  put16(p+24,settings->ca_size);put16(p+26,certificate_size);put16(p+28,key_size);
  p+=32;memcpy(p,settings->host,host);p+=host;
  memcpy(p,settings->ca,settings->ca_size);p+=settings->ca_size;
  memcpy(p,certificate,certificate_size);p+=certificate_size;
  memcpy(p,key,key_size);*size=total;
  return 0;
}
