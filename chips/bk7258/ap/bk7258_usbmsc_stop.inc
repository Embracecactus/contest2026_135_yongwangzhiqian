/* SPDX-License-Identifier: Apache-2.0 */
int bk7258_usbmsc_uninitialize(void)
{
    struct inode *inode;
    irqstate_t flags;
    int close_ret;
    int ret;

    ret = nxmutex_lock(&g_bk7258_usbmsc_worker_lock);
    if (ret < 0)
    {
        return ret;
    }

    ret = nxmutex_lock(&g_bk7258_usbmsc_lock);
    if (ret < 0)
    {
        nxmutex_unlock(&g_bk7258_usbmsc_worker_lock);
        return ret;
    }

    if (g_bk7258_usbmsc_inode == NULL)
    {
        nxmutex_unlock(&g_bk7258_usbmsc_lock);
        nxmutex_unlock(&g_bk7258_usbmsc_worker_lock);
        return -ENODEV;
    }

    /* Close callback admission before detaching. No sleeping block or USB
     * teardown operation runs with interrupts held off.
     */
    flags = enter_critical_section();
    s_msc_storage_init = 0;
    thread_op = 0;
    leave_critical_section(flags);
    bk7258_usbmsc_soft_disconnect();
    ret = usbd_deinitialize();
    if (ret < 0)
    {
        /* Admission stays closed, but the driver may still reference storage.
         * Keep the inode until an explicit teardown retry succeeds.
         */
        nxmutex_unlock(&g_bk7258_usbmsc_lock);
        nxmutex_unlock(&g_bk7258_usbmsc_worker_lock);
        return ret;
    }
    inode = g_bk7258_usbmsc_inode;
    g_bk7258_usbmsc_inode = NULL;
    memset(&g_bk7258_usbmsc_geometry, 0,
           sizeof(g_bk7258_usbmsc_geometry));
    usbd_set_status(0);
    close_ret = close_blockdriver(inode);

    nxmutex_unlock(&g_bk7258_usbmsc_lock);
    nxmutex_unlock(&g_bk7258_usbmsc_worker_lock);
    return ret < 0 ? ret : close_ret;
}
