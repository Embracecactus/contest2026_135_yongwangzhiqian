/* SPDX-License-Identifier: Apache-2.0 */
int bk7258_usbmsc_initialize(const char *blockdev)
{
    struct usbd_interface *intf;
    struct inode *inode = NULL;
    struct geometry geometry;
    irqstate_t flags;
    int ret;

    if (blockdev == NULL || blockdev[0] == '\0')
    {
        return -EINVAL;
    }

    ret = nxmutex_lock(&g_bk7258_usbmsc_worker_lock);
    if (ret < 0)
    {
        return ret;
    }

    ret = nxmutex_lock(&g_bk7258_usbmsc_lock);
    if (ret < 0)
    {
        syslog(LOG_ERR, "BK7258 USBMSC START stage=lock-fail ret=%d\n",
               ret);
        nxmutex_unlock(&g_bk7258_usbmsc_worker_lock);
        return ret;
    }


    if (s_msc_storage_init || g_bk7258_usbmsc_inode != NULL)
    {
        nxmutex_unlock(&g_bk7258_usbmsc_lock);
        nxmutex_unlock(&g_bk7258_usbmsc_worker_lock);
        return -EBUSY;
    }

    ret = open_blockdriver(blockdev, 0, &inode);
    if (ret < 0)
    {
        syslog(LOG_ERR,
               "BK7258 USBMSC START stage=block-open-fail dev=%s ret=%d\n",
               blockdev, ret);
        nxmutex_unlock(&g_bk7258_usbmsc_lock);
        nxmutex_unlock(&g_bk7258_usbmsc_worker_lock);
        return ret;
    }


    if (inode == NULL || inode->u.i_bops == NULL ||
        inode->u.i_bops->geometry == NULL ||
        inode->u.i_bops->read == NULL || inode->u.i_bops->write == NULL) {
        ret = -ENODEV;
        goto errout_with_inode;
    }

    ret = inode->u.i_bops->geometry(inode, &geometry);
    if (ret < 0 || !geometry.geo_available || geometry.geo_nsectors == 0 ||
        geometry.geo_sectorsize == 0 ||
        geometry.geo_sectorsize > CONFIG_USBDEV_MSC_BLOCK_SIZE ||
        (CONFIG_USBDEV_MSC_BLOCK_SIZE % geometry.geo_sectorsize) != 0 ||
        geometry.geo_nsectors > UINT32_MAX ||
        geometry.geo_sectorsize > UINT16_MAX) {
        ret = ret < 0 ? ret : -ENODEV;
        syslog(LOG_ERR,
               "BK7258 USBMSC START stage=geometry-fail ret=%d "
               "available=%u sectors=%lu size=%u\n",
               ret, geometry.geo_available ? 1u : 0u,
               (unsigned long)geometry.geo_nsectors,
               (unsigned int)geometry.geo_sectorsize);
        goto errout_with_inode;
    }


    g_bk7258_usbmsc_inode = inode;
    memcpy(&g_bk7258_usbmsc_geometry, &geometry, sizeof(geometry));
    flags = enter_critical_section();
    thread_op = 0;
    s_msc_storage_init = 1;
    leave_critical_section(flags);

    memset(&gs_intf0, 0, sizeof(gs_intf0));
    memset(mass_ep_data, 0, sizeof(mass_ep_data));
    usbd_desc_register(msc_storage_descriptor);
    intf = usbd_msc_init_intf(&gs_intf0, MSC_OUT_EP, MSC_IN_EP);
    if (intf == NULL)
    {
        ret = -ENOMEM;
        syslog(LOG_ERR,
               "BK7258 USBMSC START stage=class-fail ret=%d\n", ret);
        goto errout_with_state;
    }

    usbd_add_interface(intf);
    ret = usbd_initialize();
    if (ret < 0)
    {
        syslog(LOG_ERR,
               "BK7258 USBMSC START stage=controller-fail ret=%d\n", ret);
        goto errout_with_state;
    }


    syslog(LOG_INFO,
           "BK7258 USBMSC: ready blockdev=%s sectors=%lu size=%u\n",
           blockdev, (unsigned long)geometry.geo_nsectors,
           (unsigned int)geometry.geo_sectorsize);
    nxmutex_unlock(&g_bk7258_usbmsc_lock);
    nxmutex_unlock(&g_bk7258_usbmsc_worker_lock);
    return OK;

errout_with_state:
    flags = enter_critical_section();
    s_msc_storage_init = 0;
    thread_op = 0;
    leave_critical_section(flags);
    bk7258_usbmsc_soft_disconnect();
    if (usbd_deinitialize() < 0)
    {
        /* Keep ownership for explicit stop retry; never overlap a new class. */
        nxmutex_unlock(&g_bk7258_usbmsc_lock);
        nxmutex_unlock(&g_bk7258_usbmsc_worker_lock);
        return ret;
    }
    g_bk7258_usbmsc_inode = NULL;
    memset(&g_bk7258_usbmsc_geometry, 0,
           sizeof(g_bk7258_usbmsc_geometry));
errout_with_inode:
    (void)close_blockdriver(inode);
    nxmutex_unlock(&g_bk7258_usbmsc_lock);
    nxmutex_unlock(&g_bk7258_usbmsc_worker_lock);
    return ret;
}
