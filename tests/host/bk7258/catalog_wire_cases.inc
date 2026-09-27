/* SPDX-License-Identifier: Apache-2.0 */
static void catalog_capture(unsigned nonce, uint8_t out[608])
{
  uint8_t query[20] = {0}; query[4] = nonce;
  for (unsigned off = 0; off < 608; off += 16)
    {
      put32(query, (18u << 16) | off);
      assert(packet(BKCONTROL_CONFIG_READ, query, sizeof(query)) == 0);
      assert(get32(response + 20) == 608);
      memcpy(out + off, response + 24, 16);
    }
}
static void catalog_wire_case(struct bkdisplay_service_s *service,
                               const char *mode)
{
  uint8_t epoch[16] = {7}, record[96] = {0}, page[608], before[608];
  unsigned io = mounts, stored = writes, painted = frames;
  wire_kind = 18;
#ifdef TEST_PHONE_SELECTION
  phone_scope_error = 0;
#elif defined(TEST_SELECTION_PRODUCT)
  g_pc_usb_owner.pair = &pair_fixture;
  g_pc_usb_owner.usb.lease.open = true;
  g_pc_usb_owner.usb.lease.binding = 1;
  g_pc_usb_owner.usb.lease.revision = 2;
  g_pc_usb_owner.usb.lease.capabilities = BKPC_CAP_RESOURCES;
#endif
  assert(bkselection_control_bind(&selection_control, epoch) == 0);
#ifdef TEST_SELECTION_PRODUCT
  g_pc_selection_binding = 1; g_pc_selection_grant = 2;
#endif
  connect_wire();
  catalog_capture(1, before);
  assert(!memcmp(before,"ECL1",4) && get32(before+24)==0 && get32(before+64)==0);
  assert(mounts==io && writes==stored && frames==painted);
  memcpy(record,"ECC1",4); put32(record+4,1);
  memcpy(record+8,epoch,16); record[24]=1;
  if (!strcmp(mode,"catalog-wire-invalid"))
    {
      record[8]^=1;assert(wire_apply(record,96)==-ESTALE);record[8]^=1;
      record[48]=1;assert(wire_apply(record,96)==-EINVAL);record[48]=0;
      memcpy(record+56,"../a.bkep",10);assert(wire_apply(record,96)==-EINVAL);
      memset(record+56,0,40);record[24]=0;assert(wire_apply(record,96)==-EINVAL);
      assert(mounts==io && writes==stored && frames==painted);return;
    }
#ifdef TEST_SELECTION_PRODUCT
  if (!strcmp(mode,"catalog-wire-product"))
    {
      g_pc_usb_owner.usb.lease.capabilities=BKPC_CAP_SCENES;
      assert(wire_apply(record,96)==-EACCES);
      g_pc_usb_owner.usb.lease.capabilities=BKPC_CAP_RESOURCES;
    }
#endif
  assert(wire_apply(record,96)==0);
  assert(wire_apply(record,96)==0);
  record[95]=1;assert(wire_apply(record,96)==-EINVAL);record[95]=0;
  put32(record+40,1);assert(wire_apply(record,96)==-EEXIST);put32(record+40,0);
  catalog_capture(2, before);
  assert(get32(before+4)==BKDISPLAY_SELECTION_PENDING && get32(before+24)==1);
  assert(get32(before+36)==1 && get32(before+64)==0);
  assert(mounts==io && frames==painted && writes==stored);
#ifdef TEST_PHONE_SELECTION
  if (!strcmp(mode,"catalog-wire-phone-revoke"))
    {
      phone_scope_error=-EACCES;product_phone_selection_step();
      assert(!bkdisplay_selection_step(service,true));
      assert(wire_apply(record,96)==-EACCES);
      assert(mounts==io && writes==stored && frames==painted);return;
    }
#endif
  if (!strcmp(mode,"catalog-wire-revoke"))
    {
      bkselection_control_invalidate(&selection_control);
      assert(!bkdisplay_selection_step(service,true));
      assert(wire_apply(record,96)==-EACCES);
      epoch[0]++;assert(bkselection_control_bind(&selection_control,epoch)==0);
      assert(wire_apply(record,96)==-ESTALE);return;
    }
  if (!strcmp(mode,"catalog-wire-cancel"))
    {
      record[24]=2;put32(record+4,2);put32(record+40,1);
      assert(wire_apply(record,96)==0);
      assert(!bkdisplay_selection_step(service,true));
      catalog_capture(3,page);
      assert(get32(page+4)==BKDISPLAY_SELECTION_CANCELED && get32(page+64)==0);
      assert(mounts==io);return;
    }
  if (!strcmp(mode,"catalog-wire-recovery")) fail_unmount=true;
  assert(bkdisplay_selection_step(service,true));
  catalog_capture(2,page);assert(!memcmp(page,before,608));
  catalog_capture(3,page);
  if (!strcmp(mode,"catalog-wire-recovery"))
    {
      assert(get32(page+4)==BKDISPLAY_SELECTION_UNKNOWN && get32(page+64)==0);
      assert((int32_t)get32(page+32)==-EIO);
      record[24]=2;put32(record+4,3);put32(record+40,1);
      assert(wire_apply(record,96)==0);assert(bkdisplay_selection_recover_step(service));
      catalog_capture(4,page);
      assert(get32(page+4)==BKDISPLAY_SELECTION_UNKNOWN && get32(page+32)==0 && get32(page+64)==0);
    }
  else
    {
      assert(get32(page+4)==BKDISPLAY_SELECTION_DONE && get32(page+36)==5);
      assert(get32(page+64)==2 && !strcmp((char *)page+96,"shaniu-default-v1.bkep"));
      assert(!strcmp((char *)page+224,"shaniu-upload-v1.bkep"));
      assert(get32(page+168)==1 && get32(page+184)==10494);
      assert(page[174]==0 && page[175]==160 && page[176]==0 && page[177]==160);
      for (unsigned i=352;i<608;i++) assert(page[i]==0);
      uint8_t query[20]={0};query[4]=9;put32(query,17u<<16);
      assert(packet(BKCONTROL_CONFIG_READ,query,20)==0);
      put32(query,(17u<<16)|32);assert(packet(BKCONTROL_CONFIG_READ,query,20)==0);
      assert(get32(response+28)==32); /* Shared latest job explicitly catalog. */
    }
  assert(writes==stored && frames==painted && mounts==unmounts);
}
