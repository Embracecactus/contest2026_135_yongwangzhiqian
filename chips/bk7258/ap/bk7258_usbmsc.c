/*
 * Copyright (c) 2022, sakumisu
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 CherryUSB MSC transport adapted for the NuttX block-driver ABI.
 * The BOT/SCSI state machine is derived from the Beken v3.1.1.9 CherryUSB
 * source.  Storage ownership is deliberately kept in NuttX: this file never
 * initializes or calls the SDK bk_sd_card_* stack.
 */

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_USBMSC

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <syslog.h>

#include <nuttx/fs/fs.h>
#include <nuttx/irq.h>
#include <nuttx/mutex.h>
#include <nuttx/signal.h>
#include <nuttx/spinlock.h>

#include <arch/chip/bk7258_usbmsc.h>

/* The SDK bundle contains CherryUSB core and its OS adapter, but not the MSC
 * class object.  Build the class state machine here and force block I/O onto
 * its worker so no SDIO operation runs from a USB endpoint callback.
 */

#define CONFIG_USBDEV_MSC_THREAD    1
#define CONFIG_USBDEV_MSC_BLOCK_SIZE CONFIG_BK7258_USBMSC_BUFFER_SIZE
#define CONFIG_USBDEV_MSC_STACKSIZE CONFIG_BK7258_USBMSC_STACKSIZE
#define CONFIG_USBDEV_MSC_PRIO      CONFIG_BK7258_USBMSC_PRIORITY

#include <components/cherryusb/usbd_core.h>
#include <components/cherryusb/usbd_msc.h>
#include <components/cherryusb/usb_scsi.h>
#ifdef CONFIG_USBDEV_MSC_THREAD
#include <components/cherryusb/usb_osal.h>
#endif

#define MSC_THREAD_OP_READ_MEM   1
#define MSC_THREAD_OP_WRITE_MEM  2
#define MSC_THREAD_OP_WRITE_DONE 3
#define MSC_THREAD_OP_RESET      4
#define MSC_THREAD_OP_SUSPEND    5


#define MSD_OUT_EP_IDX 0
#define MSD_IN_EP_IDX  1

/* Describe EndPoints configuration */
static struct usbd_endpoint mass_ep_data[2];

/* MSC Bulk-only Stage */
enum Stage {
    MSC_READ_CBW = 0, /* Command Block Wrapper */
    MSC_DATA_OUT = 1, /* Data Out Phase */
    MSC_DATA_IN = 2,  /* Data In Phase */
    MSC_SEND_CSW = 3, /* Command Status Wrapper */
    MSC_WAIT_CSW = 4, /* Command Status Wrapper */
};

/* Device data structure */
/* USB_NOCACHE_RAM_SECTION */
struct usbd_msc_cfg_priv {
    /* state of the bulk-only state machine */
    enum Stage stage;
    USB_MEM_ALIGNX struct CBW cbw;
    USB_MEM_ALIGNX struct CSW csw;

    bool readonly;
    uint8_t sKey; /* Sense key */
    uint8_t ASC;  /* Additional Sense Code */
    uint8_t ASQ;  /* Additional Sense Qualifier */
    uint8_t max_lun;
    uint32_t start_sector;
    uint32_t nsectors;
    uint16_t scsi_blk_size;
    uint32_t scsi_blk_nbr;

    uint8_t block_buffer[CONFIG_USBDEV_MSC_BLOCK_SIZE];
} usbd_msc_cfg;

#ifdef CONFIG_USBDEV_MSC_THREAD
static volatile uint8_t thread_op;
static beken_semaphore_t msc_sem;
static usb_osal_thread_t msc_thread = NULL;
static volatile uint32_t current_byte_read;
#endif

static bool s_msc_storage_init;
static struct inode *g_bk7258_usbmsc_inode;
static struct geometry g_bk7258_usbmsc_geometry;
/* Lock order is worker -> block. The worker lock covers both block I/O and
 * its USB completion, so teardown cannot deinitialize endpoints between
 * the two. Endpoint callbacks never take either sleeping mutex. Their short
 * NuttX critical sections also serialize admission with the inactive flag;
 * the SDK's usb_osal critical-section hooks are no-ops in this bundle.
 */
static mutex_t g_bk7258_usbmsc_worker_lock = NXMUTEX_INITIALIZER;
static mutex_t g_bk7258_usbmsc_lock = NXMUTEX_INITIALIZER;

#define MSC_IN_EP  0x81
#define MSC_OUT_EP 0x02

#define USBD_VID           CONFIG_BK7258_USBMSC_VID
#define USBD_PID           CONFIG_BK7258_USBMSC_PID
#define USBD_MAX_POWER     100
#define USBD_LANGID_STRING 1033

#define USB_CONFIG_SIZE (9 + MSC_DESCRIPTOR_LEN)

#ifdef CONFIG_USB_HS
#define MSC_MAX_MPS 512
#else
#define MSC_MAX_MPS 64
#endif

#define UI_SECTORS (64*1024*2) /* 64M */
#define UI_USE_BEGIN_SECTOR 0
/* volatile static uint8_t gs_ui_sector_display_enable = 0; */
volatile static uint8_t gs_status = 0;
static void usbd_set_status(uint8_t status);

const uint8_t msc_storage_descriptor[] __attribute__((aligned(4))) = {
    /* Keep the control-plane descriptor deliberately small, just like the
     * proven CDC path.  Product identity for a mass-storage disk comes from
     * SCSI INQUIRY; optional USB strings only add EP0 transactions during the
     * sensitive CDC-to-MSC re-enumeration window.
     */
    0x12,                       /* bLength */
    USB_DESCRIPTOR_TYPE_DEVICE, /* bDescriptorType */
    0x00, 0x02,                 /* bcdUSB 2.0 */
    0x00,                       /* bDeviceClass */
    0x00,                       /* bDeviceSubClass */
    0x00,                       /* bDeviceProtocol */
    0x40,                       /* bMaxPacketSize0 */
    USBD_VID & 0xff, (USBD_VID >> 8) & 0xff,
    USBD_PID & 0xff, (USBD_PID >> 8) & 0xff,
    0x00, 0x02,                 /* bcdDevice 2.0 */
    0x00,                       /* iManufacturer */
    0x00,                       /* iProduct */
    0x00,                       /* iSerialNumber */
    0x01,                       /* bNumConfigurations */
    USB_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_SIZE, 0x01, 0x01, USB_CONFIG_BUS_POWERED, USBD_MAX_POWER),
    MSC_DESCRIPTOR_INIT(0x00, MSC_OUT_EP, MSC_IN_EP, 0x00),
    0x00
};

_Static_assert(sizeof(msc_storage_descriptor) == 18u + USB_CONFIG_SIZE + 1u,
               "BK7258 MSC descriptor length mismatch");

static struct usbd_interface gs_intf0;

#define BK7258_MUSB_POWER_REG        0x46000001u
#define BK7258_MUSB_POWER_SOFTCONN   (1u << 6)
#define BK7258_USB_DETACH_US         10000u

static void bk7258_usbmsc_soft_disconnect(void)
{
    volatile uint8_t *power = (volatile uint8_t *)BK7258_MUSB_POWER_REG;

    *power &= (uint8_t)~BK7258_MUSB_POWER_SOFTCONN;
    (void)nxsig_usleep(BK7258_USB_DETACH_US);
}


static void usbd_msc_reset(void)
{
    usbd_msc_cfg.stage = MSC_READ_CBW;
    usbd_msc_cfg.readonly = false;
}

static int msc_storage_class_interface_request_handler(struct usb_setup_packet *setup, uint8_t **data, uint32_t *len)
{
    irqstate_t flags = enter_critical_section();
    int ret = 0;

    if (!s_msc_storage_init)
    {
        leave_critical_section(flags);
        return -ENODEV;
    }

    USB_LOG_VBS("MSC Class request: "
                "bRequest 0x%02x\r\n",
                setup->bRequest);

    switch (setup->bRequest)
    {
        case MSC_REQUEST_RESET:
            USB_LOG_DBG("MSC_REQUEST_RESET\r\n");
            usbd_msc_reset();
            break;

        case MSC_REQUEST_GET_MAX_LUN:
            *data = (uint8_t *)(&usbd_msc_cfg.max_lun);
            *len = 1;
            break;

        default:
            USB_LOG_DBG("Unhandled MSC Class bRequest 0x%02x\r\n", setup->bRequest);
            ret = -1;
            break;
    }

    leave_critical_section(flags);
    return ret;
}

void msc_storage_notify_handler(uint8_t event, void *arg)
{
    irqstate_t flags = enter_critical_section();

    if (!s_msc_storage_init)
    {
        leave_critical_section(flags);
        return;
    }

    switch (event)
    {
        case USBD_EVENT_ERROR:
            break;
        case USBD_EVENT_RESET:
            USB_LOG_VBS("USBD_EVENT_RESET\r\n");
            usbd_msc_reset();
            break;
        case USBD_EVENT_CONNECTED:
        case USBD_EVENT_DISCONNECTED:
            break;
        case USBD_EVENT_CONFIGURED:
            USB_LOG_VBS("Start reading cbw\r\n");
            usbd_ep_start_read(mass_ep_data[MSD_OUT_EP_IDX].ep_addr, (uint8_t *)&usbd_msc_cfg.cbw, USB_SIZEOF_MSC_CBW);
            break;
        case USBD_EVENT_SUSPEND:
            USB_LOG_VBS("USBD_EVENT_SUSPEND\r\n");
            break;
        case USBD_EVENT_RESUME:

            USB_LOG_VBS("USBD_EVENT_RESUME\r\n");
            break;
        default:
            break;
    }
    leave_critical_section(flags);
}

static void usbd_msc_bot_abort(void)
{
    if ((usbd_msc_cfg.cbw.bmFlags == 0) && (usbd_msc_cfg.cbw.dDataLength != 0))
    {
        usbd_ep_set_stall(mass_ep_data[MSD_OUT_EP_IDX].ep_addr);
    }
    usbd_ep_set_stall(mass_ep_data[MSD_IN_EP_IDX].ep_addr);
    usbd_ep_start_read(mass_ep_data[0].ep_addr, (uint8_t *)&usbd_msc_cfg.cbw, USB_SIZEOF_MSC_CBW);
}

static void usbd_msc_send_csw(uint8_t CSW_Status)
{
    usbd_msc_cfg.csw.dSignature = MSC_CSW_Signature;
    usbd_msc_cfg.csw.bStatus = CSW_Status;

    /* updating the State Machine , so that we wait CSW when this
	 * transfer is complete, ie when we get a bulk in callback
	 */
    usbd_msc_cfg.stage = MSC_WAIT_CSW;

    USB_LOG_VBS("Send csw\r\n");
    usbd_ep_start_write(mass_ep_data[MSD_IN_EP_IDX].ep_addr, (uint8_t *)&usbd_msc_cfg.csw, sizeof(struct CSW));
}

static void usbd_msc_send_info(uint8_t *buffer, uint8_t size)
{
    size = MIN(size, usbd_msc_cfg.cbw.dDataLength);

    /* updating the State Machine , so that we send CSW when this
	 * transfer is complete, ie when we get a bulk in callback
	 */
    usbd_msc_cfg.stage = MSC_SEND_CSW;

    usbd_ep_start_write(mass_ep_data[MSD_IN_EP_IDX].ep_addr, buffer, size);

    usbd_msc_cfg.csw.dDataResidue -= size;
    usbd_msc_cfg.csw.bStatus = CSW_STATUS_CMD_PASSED;
}

static bool SCSI_processWrite(uint32_t nbytes);
static bool SCSI_processRead(void);

/**
* @brief  SCSI_SetSenseData
*         Load the last error code in the error list
* @param  sKey: Sense Key
* @param  ASC: Additional Sense Code
* @retval none

*/
static void SCSI_SetSenseData(uint32_t KCQ)
{
    usbd_msc_cfg.sKey = (uint8_t)(KCQ >> 16);
    usbd_msc_cfg.ASC = (uint8_t)(KCQ >> 8);
    usbd_msc_cfg.ASQ = (uint8_t)(KCQ);
}

/**
 * @brief SCSI Command list
 *
 */

static bool SCSI_testUnitReady(uint8_t **data, uint32_t *len)
{
    if (usbd_msc_cfg.cbw.dDataLength != 0U)
    {
        SCSI_SetSenseData(SCSI_KCQIR_INVALIDCOMMAND);
        return false;
    }
    *data = NULL;
    *len = 0;
    return true;
}

static bool SCSI_requestSense(uint8_t **data, uint32_t *len)
{
    uint8_t data_len = SCSIRESP_FIXEDSENSEDATA_SIZEOF;
    if (usbd_msc_cfg.cbw.dDataLength == 0U)
    {
        SCSI_SetSenseData(SCSI_KCQIR_INVALIDCOMMAND);
        return false;
    }

    if (usbd_msc_cfg.cbw.CB[4] < SCSIRESP_FIXEDSENSEDATA_SIZEOF)
    {
        data_len = usbd_msc_cfg.cbw.CB[4];
    }

    uint8_t request_sense[SCSIRESP_FIXEDSENSEDATA_SIZEOF] = {
        0x70,
        0x00,
        0x00, /* Sense Key */
        0x00,
        0x00,
        0x00,
        0x00,
        SCSIRESP_FIXEDSENSEDATA_SIZEOF - 8,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00, /* Additional Sense Code */
        0x00, /* Additional Sense Request */
        0x00,
        0x00,
        0x00,
        0x00,
    };

    request_sense[2] = usbd_msc_cfg.sKey;
    request_sense[12] = usbd_msc_cfg.ASC;
    request_sense[13] = usbd_msc_cfg.ASQ;
#if 0
    request_sense[ 2] = 0x06;           /* UNIT ATTENTION */
    request_sense[12] = 0x28;           /* Additional Sense Code: Not ready to ready transition */
    request_sense[13] = 0x00;           /* Additional Sense Code Qualifier */
#endif
#if 0
    request_sense[ 2] = 0x02;           /* NOT READY */
    request_sense[12] = 0x3A;           /* Additional Sense Code: Medium not present */
    request_sense[13] = 0x00;           /* Additional Sense Code Qualifier */
#endif
#if 0
    request_sense[ 2] = 0x05;         /* ILLEGAL REQUEST */
    request_sense[12] = 0x20;         /* Additional Sense Code: Invalid command */
    request_sense[13] = 0x00;         /* Additional Sense Code Qualifier */
#endif
#if 0
    request_sense[ 2] = 0x00;         /* NO SENSE */
    request_sense[12] = 0x00;         /* Additional Sense Code: No additional code */
    request_sense[13] = 0x00;         /* Additional Sense Code Qualifier */
#endif

    memcpy(*data, (uint8_t *)request_sense, data_len);
    *len = data_len;
    return true;
}

static bool SCSI_inquiry(uint8_t **data, uint32_t *len)
{
    uint8_t data_len = SCSIRESP_INQUIRY_SIZEOF;

    uint8_t inquiry00[6] = {
        0x00,
        0x00,
        0x00,
        (0x06 - 4U),
        0x00,
        0x80
    };

    /* USB Mass storage VPD Page 0x80 Inquiry Data for Unit Serial Number */
    uint8_t inquiry80[8] = {
        0x00,
        0x80,
        0x00,
        0x08,
        0x20, /* Put Product Serial number */
        0x20,
        0x20,
        0x20
    };

    uint8_t inquiry[SCSIRESP_INQUIRY_SIZEOF] = {
        /* 36 */

        /* LUN 0 */
        0x00,
        0x80,
        0x02,
        0x02,
        (SCSIRESP_INQUIRY_SIZEOF - 5),
        0x00,
        0x00,
        0x00,
        ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', /* Manufacturer : 8 bytes */
        ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', /* Product      : 16 Bytes */
        ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
        ' ', ' ', ' ', ' ' /* Version      : 4 Bytes */
    };

    memcpy(&inquiry[8], CONFIG_USBDEV_MSC_MANUFACTURER_STRING, strlen(CONFIG_USBDEV_MSC_MANUFACTURER_STRING));
    memcpy(&inquiry[16], CONFIG_USBDEV_MSC_PRODUCT_STRING, strlen(CONFIG_USBDEV_MSC_PRODUCT_STRING));
    memcpy(&inquiry[32], CONFIG_USBDEV_MSC_VERSION_STRING, strlen(CONFIG_USBDEV_MSC_VERSION_STRING));

    if (usbd_msc_cfg.cbw.dDataLength == 0U)
    {
        SCSI_SetSenseData(SCSI_KCQIR_INVALIDCOMMAND);
        return false;
    }

    if ((usbd_msc_cfg.cbw.CB[1] & 0x01U) != 0U) { /* Evpd is set */
        if (usbd_msc_cfg.cbw.CB[2] == 0U) {       /* Request for Supported Vital Product Data Pages*/
            data_len = 0x06;
            memcpy(*data, (uint8_t *)inquiry00, data_len);
        } else if (usbd_msc_cfg.cbw.CB[2] == 0x80U) { /* Request for VPD page 0x80 Unit Serial Number */
            data_len = 0x08;
            memcpy(*data, (uint8_t *)inquiry80, data_len);
        } else { /* Request Not supported */
            SCSI_SetSenseData(SCSI_KCQIR_INVALIDFIELDINCBA);
            return false;
        }
    }
    else
    {
        if (usbd_msc_cfg.cbw.CB[4] < SCSIRESP_INQUIRY_SIZEOF)
        {
            data_len = usbd_msc_cfg.cbw.CB[4];
        }
        memcpy(*data, (uint8_t *)inquiry, data_len);
    }

    *len = data_len;
    return true;
}

static bool SCSI_startStopUnit(uint8_t **data, uint32_t *len)
{
    if (usbd_msc_cfg.cbw.dDataLength != 0U)
    {
        SCSI_SetSenseData(SCSI_KCQIR_INVALIDCOMMAND);
        return false;
    }

    if ((usbd_msc_cfg.cbw.CB[4] & 0x3U) == 0x1U) /* START=1 */
    {
        /* SCSI_MEDIUM_UNLOCKED; */
    } else if ((usbd_msc_cfg.cbw.CB[4] & 0x3U) == 0x2U) /* START=0 and LOEJ Load Eject=1 */
    {
        /* SCSI_MEDIUM_EJECTED; */
    } else if ((usbd_msc_cfg.cbw.CB[4] & 0x3U) == 0x3U) /* START=1 and LOEJ Load Eject=1 */
    {
        /* SCSI_MEDIUM_UNLOCKED; */
    }
    else
    {
    }

    *data = NULL;
    *len = 0;
    return true;
}

static bool SCSI_preventAllowMediaRemoval(uint8_t **data, uint32_t *len)
{
    if (usbd_msc_cfg.cbw.dDataLength != 0U)
    {
        SCSI_SetSenseData(SCSI_KCQIR_INVALIDCOMMAND);
        return false;
    }
    if (usbd_msc_cfg.cbw.CB[4] == 0U)
    {
        /* SCSI_MEDIUM_UNLOCKED; */
    }
    else
    {
        /* SCSI_MEDIUM_LOCKED; */
    }
    *data = NULL;
    *len = 0;
    return true;
}

static bool SCSI_modeSense6(uint8_t **data, uint32_t *len)
{
    uint8_t data_len = 4;
    if (usbd_msc_cfg.cbw.dDataLength == 0U)
    {
        SCSI_SetSenseData(SCSI_KCQIR_INVALIDCOMMAND);
        return false;
    }
    if (usbd_msc_cfg.cbw.CB[4] < SCSIRESP_MODEPARAMETERHDR6_SIZEOF)
    {
        data_len = usbd_msc_cfg.cbw.CB[4];
    }

    uint8_t sense6[SCSIRESP_MODEPARAMETERHDR6_SIZEOF] = { 0x03, 0x00, 0x00, 0x00 };

    if (usbd_msc_cfg.readonly)
    {
        sense6[2] = 0x80;
    }
    memcpy(*data, (uint8_t *)sense6, data_len);
    *len = data_len;
    return true;
}

static bool SCSI_modeSense10(uint8_t **data, uint32_t *len)
{
    uint8_t data_len = 27;
    if (usbd_msc_cfg.cbw.dDataLength == 0U)
    {
        SCSI_SetSenseData(SCSI_KCQIR_INVALIDCOMMAND);
        return false;
    }

    if (usbd_msc_cfg.cbw.CB[8] < 27)
    {
        data_len = usbd_msc_cfg.cbw.CB[8];
    }

    uint8_t sense10[27] = {
        0x00,
        0x26,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x08,
        0x12,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00
    };

    memcpy(*data, (uint8_t *)sense10, data_len);
    *len = data_len;
    return true;
}

static bool SCSI_readFormatCapacity(uint8_t **data, uint32_t *len)
{
    if (usbd_msc_cfg.cbw.dDataLength == 0U)
    {
        SCSI_SetSenseData(SCSI_KCQIR_INVALIDCOMMAND);
        return false;
    }
    uint8_t format_capacity[SCSIRESP_READFORMATCAPACITIES_SIZEOF] = {
        0x00,
        0x00,
        0x00,
        0x08, /* Capacity List Length */
        (uint8_t)((usbd_msc_cfg.scsi_blk_nbr >> 24) & 0xff),
        (uint8_t)((usbd_msc_cfg.scsi_blk_nbr >> 16) & 0xff),
        (uint8_t)((usbd_msc_cfg.scsi_blk_nbr >> 8) & 0xff),
        (uint8_t)((usbd_msc_cfg.scsi_blk_nbr >> 0) & 0xff),

        0x02, /* Descriptor Code: Formatted Media */
        0x00,
        (uint8_t)((usbd_msc_cfg.scsi_blk_size >> 8) & 0xff),
        (uint8_t)((usbd_msc_cfg.scsi_blk_size >> 0) & 0xff),
    };

    memcpy(*data, (uint8_t *)format_capacity, SCSIRESP_READFORMATCAPACITIES_SIZEOF);
    *len = SCSIRESP_READFORMATCAPACITIES_SIZEOF;
    return true;
}

static bool SCSI_readCapacity10(uint8_t **data, uint32_t *len)
{
    if (usbd_msc_cfg.cbw.dDataLength == 0U)
    {
        SCSI_SetSenseData(SCSI_KCQIR_INVALIDCOMMAND);
        return false;
    }

    uint8_t capacity10[SCSIRESP_READCAPACITY10_SIZEOF] = {
        (uint8_t)(((usbd_msc_cfg.scsi_blk_nbr - 1) >> 24) & 0xff),
        (uint8_t)(((usbd_msc_cfg.scsi_blk_nbr - 1) >> 16) & 0xff),
        (uint8_t)(((usbd_msc_cfg.scsi_blk_nbr - 1) >> 8) & 0xff),
        (uint8_t)(((usbd_msc_cfg.scsi_blk_nbr - 1) >> 0) & 0xff),

        (uint8_t)((usbd_msc_cfg.scsi_blk_size >> 24) & 0xff),
        (uint8_t)((usbd_msc_cfg.scsi_blk_size >> 16) & 0xff),
        (uint8_t)((usbd_msc_cfg.scsi_blk_size >> 8) & 0xff),
        (uint8_t)((usbd_msc_cfg.scsi_blk_size >> 0) & 0xff),
    };

    memcpy(*data, (uint8_t *)capacity10, SCSIRESP_READCAPACITY10_SIZEOF);
    *len = SCSIRESP_READCAPACITY10_SIZEOF;
    return true;
}

static bool SCSI_read10(uint8_t **data, uint32_t *len)
{
    if (((usbd_msc_cfg.cbw.bmFlags & 0x80U) != 0x80U) || (usbd_msc_cfg.cbw.dDataLength == 0U))
    {
        SCSI_SetSenseData(SCSI_KCQIR_INVALIDCOMMAND);
        return false;
    }

    usbd_msc_cfg.start_sector = GET_BE32(&usbd_msc_cfg.cbw.CB[2]); /* Logical Block Address of First Block */
    USB_LOG_VBS("lba: 0x%04x\r\n", usbd_msc_cfg.start_sector);

    usbd_msc_cfg.nsectors = GET_BE16(&usbd_msc_cfg.cbw.CB[7]); /* Number of Blocks to transfer */
    USB_LOG_VBS("nsectors: 0x%02x\r\n", usbd_msc_cfg.nsectors);

    if ((usbd_msc_cfg.start_sector + usbd_msc_cfg.nsectors) > usbd_msc_cfg.scsi_blk_nbr)
    {
        SCSI_SetSenseData(SCSI_KCQIR_LBAOUTOFRANGE);
        USB_LOG_ERR("LBA out of range\r\n");
        return false;
    }

    if (usbd_msc_cfg.cbw.dDataLength != (usbd_msc_cfg.nsectors * usbd_msc_cfg.scsi_blk_size))
    {
        USB_LOG_ERR("scsi_blk_len does not match with dDataLength\r\n");
        return false;
    }
    usbd_msc_cfg.stage = MSC_DATA_IN;
    return SCSI_processRead();
}

static bool SCSI_read12(uint8_t **data, uint32_t *len)
{
    if (((usbd_msc_cfg.cbw.bmFlags & 0x80U) != 0x80U) || (usbd_msc_cfg.cbw.dDataLength == 0U))
    {
        SCSI_SetSenseData(SCSI_KCQIR_INVALIDCOMMAND);
        return false;
    }

    usbd_msc_cfg.start_sector = GET_BE32(&usbd_msc_cfg.cbw.CB[2]); /* Logical Block Address of First Block */
    USB_LOG_VBS("lba: 0x%04x\r\n", usbd_msc_cfg.start_sector);

    usbd_msc_cfg.nsectors = GET_BE32(&usbd_msc_cfg.cbw.CB[6]); /* Number of Blocks to transfer */
    USB_LOG_VBS("nsectors: 0x%02x\r\n", usbd_msc_cfg.nsectors);

    if ((usbd_msc_cfg.start_sector + usbd_msc_cfg.nsectors) > usbd_msc_cfg.scsi_blk_nbr)
    {
        SCSI_SetSenseData(SCSI_KCQIR_LBAOUTOFRANGE);
        USB_LOG_ERR("LBA out of range\r\n");
        return false;
    }

    if (usbd_msc_cfg.cbw.dDataLength != (usbd_msc_cfg.nsectors * usbd_msc_cfg.scsi_blk_size))
    {
        USB_LOG_ERR("scsi_blk_len does not match with dDataLength\r\n");
        return false;
    }
    usbd_msc_cfg.stage = MSC_DATA_IN;
    return SCSI_processRead();
}

static bool SCSI_write10(uint8_t **data, uint32_t *len)
{
    uint32_t data_len = 0;
    if (((usbd_msc_cfg.cbw.bmFlags & 0x80U) != 0x00U) || (usbd_msc_cfg.cbw.dDataLength == 0U))
    {
        SCSI_SetSenseData(SCSI_KCQIR_INVALIDCOMMAND);
        return false;
    }

    usbd_msc_cfg.start_sector = GET_BE32(&usbd_msc_cfg.cbw.CB[2]); /* Logical Block Address of First Block */
    USB_LOG_VBS("lba: 0x%04x\r\n", usbd_msc_cfg.start_sector);

    usbd_msc_cfg.nsectors = GET_BE16(&usbd_msc_cfg.cbw.CB[7]); /* Number of Blocks to transfer */
    USB_LOG_VBS("nsectors: 0x%02x\r\n", usbd_msc_cfg.nsectors);

    data_len = usbd_msc_cfg.nsectors * usbd_msc_cfg.scsi_blk_size;
    if ((usbd_msc_cfg.start_sector + usbd_msc_cfg.nsectors) > usbd_msc_cfg.scsi_blk_nbr)
    {
        USB_LOG_ERR("LBA out of range\r\n");
        return false;
    }

    if (usbd_msc_cfg.cbw.dDataLength != data_len)
    {
        return false;
    }
    usbd_msc_cfg.stage = MSC_DATA_OUT;
    data_len = MIN(data_len, CONFIG_USBDEV_MSC_BLOCK_SIZE);
    usbd_ep_start_read(mass_ep_data[MSD_OUT_EP_IDX].ep_addr, usbd_msc_cfg.block_buffer, data_len);
    return true;
}

static bool SCSI_write12(uint8_t **data, uint32_t *len)
{
    uint32_t data_len = 0;
    if (((usbd_msc_cfg.cbw.bmFlags & 0x80U) != 0x00U) || (usbd_msc_cfg.cbw.dDataLength == 0U))
    {
        SCSI_SetSenseData(SCSI_KCQIR_INVALIDCOMMAND);
        return false;
    }

    usbd_msc_cfg.start_sector = GET_BE32(&usbd_msc_cfg.cbw.CB[2]); /* Logical Block Address of First Block */
    USB_LOG_VBS("lba: 0x%04x\r\n", usbd_msc_cfg.start_sector);

    usbd_msc_cfg.nsectors = GET_BE32(&usbd_msc_cfg.cbw.CB[6]); /* Number of Blocks to transfer */
    USB_LOG_VBS("nsectors: 0x%02x\r\n", usbd_msc_cfg.nsectors);

    data_len = usbd_msc_cfg.nsectors * usbd_msc_cfg.scsi_blk_size;
    if ((usbd_msc_cfg.start_sector + usbd_msc_cfg.nsectors) > usbd_msc_cfg.scsi_blk_nbr)
    {
        USB_LOG_ERR("LBA out of range\r\n");
        return false;
    }

    if (usbd_msc_cfg.cbw.dDataLength != data_len)
    {
        return false;
    }
    usbd_msc_cfg.stage = MSC_DATA_OUT;
    data_len = MIN(data_len, CONFIG_USBDEV_MSC_BLOCK_SIZE);
    usbd_ep_start_read(mass_ep_data[MSD_OUT_EP_IDX].ep_addr, usbd_msc_cfg.block_buffer, data_len);
    return true;
}
/* do not use verify to reduce code size */
#if 0
static bool SCSI_verify10(uint8_t **data, uint32_t *len)
{
    /* Logical Block Address of First Block */
    uint32_t lba = 0;
    uint32_t blk_num = 0;

    if ((usbd_msc_cfg.cbw.CB[1] & 0x02U) == 0x00U)
    {
        return true;
    }

    if (((usbd_msc_cfg.cbw.bmFlags & 0x80U) != 0x00U) || (usbd_msc_cfg.cbw.dDataLength == 0U))
    {
        SCSI_SetSenseData(SCSI_KCQIR_INVALIDCOMMAND);
        return false;
    }

    if ((usbd_msc_cfg.cbw.CB[1] & 0x02U) == 0x02U)
    {
        SCSI_SetSenseData(SCSI_KCQIR_INVALIDFIELDINCBA);
        return false; /* Error, Verify Mode Not supported*/
    }

    lba = GET_BE32(&usbd_msc_cfg.cbw.CB[2]);
    USB_LOG_VBS("lba: 0x%x\r\n", lba);

    usbd_msc_cfg.scsi_blk_addr = lba * usbd_msc_cfg.scsi_blk_size;

    /* Number of Blocks to transfer */
    blk_num = GET_BE16(&usbd_msc_cfg.cbw.CB[7]);

    USB_LOG_VBS("num (block) : 0x%x\r\n", blk_num);
    usbd_msc_cfg.scsi_blk_len = blk_num * usbd_msc_cfg.scsi_blk_size;

    if ((lba + blk_num) > usbd_msc_cfg.scsi_blk_nbr)
    {
        USB_LOG_ERR("LBA out of range\r\n");
        return false;
    }

    if (usbd_msc_cfg.cbw.dDataLength != usbd_msc_cfg.scsi_blk_len)
    {
        return false;
    }

    usbd_msc_cfg.stage = MSC_DATA_OUT;
    return true;
}
#endif

static bool SCSI_processRead(void)
{
    uint32_t transfer_len;

    USB_LOG_VBS("read lba:%d\r\n", usbd_msc_cfg.start_sector);

    transfer_len = MIN(usbd_msc_cfg.nsectors * usbd_msc_cfg.scsi_blk_size, CONFIG_USBDEV_MSC_BLOCK_SIZE);

    /* Start reading one sector */
#ifdef CONFIG_USBDEV_MSC_THREAD
    thread_op = MSC_THREAD_OP_READ_MEM;
    (void)rtos_set_semaphore(&msc_sem);
    return true;
#else
    if (usbd_msc_sector_read(usbd_msc_cfg.start_sector, usbd_msc_cfg.block_buffer, transfer_len) != 0)
    {
        SCSI_SetSenseData(SCSI_KCQHE_UREINRESERVEDAREA);
        return false;
    }
#endif
    usbd_ep_start_write(mass_ep_data[MSD_IN_EP_IDX].ep_addr, usbd_msc_cfg.block_buffer, transfer_len);

    usbd_msc_cfg.start_sector += (transfer_len / usbd_msc_cfg.scsi_blk_size);
    usbd_msc_cfg.nsectors -= (transfer_len / usbd_msc_cfg.scsi_blk_size);
    usbd_msc_cfg.csw.dDataResidue -= transfer_len;

    if (usbd_msc_cfg.nsectors == 0)
    {
        usbd_msc_cfg.stage = MSC_SEND_CSW;
    }

    return true;
}

#ifdef CONFIG_USBDEV_MSC_THREAD
static void usbd_msc_thread_memory_read_done(void)
{
    irqstate_t flags;
    uint32_t transfer_len;

    flags = enter_critical_section();

    transfer_len = MIN(usbd_msc_cfg.nsectors * usbd_msc_cfg.scsi_blk_size, CONFIG_USBDEV_MSC_BLOCK_SIZE);

    usbd_ep_start_write(mass_ep_data[MSD_IN_EP_IDX].ep_addr,
                        usbd_msc_cfg.block_buffer, transfer_len);

    usbd_msc_cfg.start_sector += (transfer_len / usbd_msc_cfg.scsi_blk_size);
    usbd_msc_cfg.nsectors -= (transfer_len / usbd_msc_cfg.scsi_blk_size);
    usbd_msc_cfg.csw.dDataResidue -= transfer_len;

    if (usbd_msc_cfg.nsectors == 0)
    {
        usbd_msc_cfg.stage = MSC_SEND_CSW;
    }
    leave_critical_section(flags);
}
#endif

static bool SCSI_processWrite(uint32_t nbytes)
{
    uint32_t data_len = 0;
    USB_LOG_VBS("write lba:%d\r\n", usbd_msc_cfg.start_sector);

    /* Start writing one sector */
#ifdef CONFIG_USBDEV_MSC_THREAD
    thread_op = MSC_THREAD_OP_WRITE_MEM;
    current_byte_read = nbytes;
    (void)rtos_set_semaphore(&msc_sem);
    return true;
#else
    if (usbd_msc_sector_write(usbd_msc_cfg.start_sector, usbd_msc_cfg.block_buffer, nbytes) != 0)
    {
        SCSI_SetSenseData(SCSI_KCQHE_WRITEFAULT);
        return false;
    }
#endif

    usbd_msc_cfg.start_sector += (nbytes / usbd_msc_cfg.scsi_blk_size);
    usbd_msc_cfg.nsectors -= (nbytes / usbd_msc_cfg.scsi_blk_size);
    usbd_msc_cfg.csw.dDataResidue -= nbytes;

    if (usbd_msc_cfg.nsectors == 0)
    {
        usbd_msc_send_csw(CSW_STATUS_CMD_PASSED);
    }
    else
    {
        data_len = MIN(usbd_msc_cfg.nsectors * usbd_msc_cfg.scsi_blk_size, CONFIG_USBDEV_MSC_BLOCK_SIZE);
        usbd_ep_start_read(mass_ep_data[MSD_OUT_EP_IDX].ep_addr, usbd_msc_cfg.block_buffer, data_len);
    }

    return true;
}

#ifdef CONFIG_USBDEV_MSC_THREAD
static void usbd_msc_thread_memory_write_done(void)
{
    irqstate_t flags;
    uint32_t data_len = 0;

    flags = enter_critical_section();

    usbd_msc_cfg.start_sector += (current_byte_read / usbd_msc_cfg.scsi_blk_size);
    usbd_msc_cfg.nsectors -= (current_byte_read / usbd_msc_cfg.scsi_blk_size);
    usbd_msc_cfg.csw.dDataResidue -= current_byte_read;

    if (usbd_msc_cfg.nsectors == 0)
    {
        usbd_msc_send_csw(CSW_STATUS_CMD_PASSED);
    }
    else
    {
        data_len = MIN(usbd_msc_cfg.nsectors * usbd_msc_cfg.scsi_blk_size, CONFIG_USBDEV_MSC_BLOCK_SIZE);
        usbd_ep_start_read(mass_ep_data[MSD_OUT_EP_IDX].ep_addr, usbd_msc_cfg.block_buffer, data_len);
    }

    leave_critical_section(flags);
}
#endif

static bool SCSI_CBWDecode(uint32_t nbytes)
{
    uint8_t *buf2send = usbd_msc_cfg.block_buffer;
    uint32_t len2send = 0;
    bool ret = false;

    if (nbytes != sizeof(struct CBW))
    {
        USB_LOG_ERR("size != sizeof(cbw)\r\n");
        SCSI_SetSenseData(SCSI_KCQIR_INVALIDCOMMAND);
        return false;
    }

    usbd_msc_cfg.csw.dTag = usbd_msc_cfg.cbw.dTag;
    usbd_msc_cfg.csw.dDataResidue = usbd_msc_cfg.cbw.dDataLength;

    if ((usbd_msc_cfg.cbw.bLUN > 1) || (usbd_msc_cfg.cbw.dSignature != MSC_CBW_Signature) || (usbd_msc_cfg.cbw.bCBLength < 1) || (usbd_msc_cfg.cbw.bCBLength > 16))
    {
        SCSI_SetSenseData(SCSI_KCQIR_INVALIDCOMMAND);
        return false;
    }
    else
    {
        USB_LOG_VBS("Decode CB:0x%02x\r\n", usbd_msc_cfg.cbw.CB[0]);
        switch (usbd_msc_cfg.cbw.CB[0])
        {
            case SCSI_CMD_TESTUNITREADY:
                ret = SCSI_testUnitReady(&buf2send, &len2send);
                break;
            case SCSI_CMD_REQUESTSENSE:
                ret = SCSI_requestSense(&buf2send, &len2send);
                break;
            case SCSI_CMD_INQUIRY:
                ret = SCSI_inquiry(&buf2send, &len2send);
                break;
            case SCSI_CMD_STARTSTOPUNIT:
                ret = SCSI_startStopUnit(&buf2send, &len2send);
                break;
            case SCSI_CMD_PREVENTMEDIAREMOVAL:
                ret = SCSI_preventAllowMediaRemoval(&buf2send, &len2send);
                break;
            case SCSI_CMD_MODESENSE6:
                ret = SCSI_modeSense6(&buf2send, &len2send);
                break;
            case SCSI_CMD_MODESENSE10:
                ret = SCSI_modeSense10(&buf2send, &len2send);
                break;
            case SCSI_CMD_READFORMATCAPACITIES:
                ret = SCSI_readFormatCapacity(&buf2send, &len2send);
                break;
            case SCSI_CMD_READCAPACITY10:
                ret = SCSI_readCapacity10(&buf2send, &len2send);
                break;
            case SCSI_CMD_READ10:
                ret = SCSI_read10(NULL, 0);
                break;
            case SCSI_CMD_READ12:
                ret = SCSI_read12(NULL, 0);
                break;
            case SCSI_CMD_WRITE10:
                ret = SCSI_write10(NULL, 0);
                break;
            case SCSI_CMD_WRITE12:
                ret = SCSI_write12(NULL, 0);
                break;
            case SCSI_CMD_SYNCHCACHE10:
            case SCSI_CMD_VERIFY10:
                ret = SCSI_testUnitReady(&buf2send, &len2send);
                break;

            default:
                SCSI_SetSenseData(SCSI_KCQIR_INVALIDCOMMAND);
                USB_LOG_DBG("unsupported cmd:0x%02x\r\n", usbd_msc_cfg.cbw.CB[0]);
                ret = false;
                break;
        }
    }
    if (ret)
    {
        if (usbd_msc_cfg.stage == MSC_READ_CBW)
        {
            if (len2send)
            {
                USB_LOG_VBS("Send info len:%d\r\n", len2send);
                usbd_msc_send_info(buf2send, len2send);
            }
            else
            {
                usbd_msc_send_csw(CSW_STATUS_CMD_PASSED);
            }
        }
    }
    return ret;
}

void mass_storage_bulk_out(uint8_t ep, uint32_t nbytes)
{
    irqstate_t flags = enter_critical_section();

    if (!s_msc_storage_init)
    {
        leave_critical_section(flags);
        return;
    }

    switch (usbd_msc_cfg.stage)
    {
        case MSC_READ_CBW:
            if (SCSI_CBWDecode(nbytes) == false)
            {
                USB_LOG_ERR("Command:0x%02x decode err\r\n", usbd_msc_cfg.cbw.CB[0]);
                usbd_msc_bot_abort();
                leave_critical_section(flags);
                return;
            }
            break;
        case MSC_DATA_OUT:
            switch (usbd_msc_cfg.cbw.CB[0])
            {
                case SCSI_CMD_WRITE10:
                case SCSI_CMD_WRITE12:
                    if (SCSI_processWrite(nbytes) == false)
                    {
                        usbd_msc_send_csw(CSW_STATUS_CMD_FAILED); /* send fail status to host,and the host will retry*/
                    }
                    break;
                default:
                    break;
            }
            break;
        default:
            break;
    }
    leave_critical_section(flags);
}

void mass_storage_bulk_in(uint8_t ep, uint32_t nbytes)
{
    irqstate_t flags = enter_critical_section();

    if (!s_msc_storage_init)
    {
        leave_critical_section(flags);
        return;
    }

    switch (usbd_msc_cfg.stage)
    {
        case MSC_DATA_IN:
            switch (usbd_msc_cfg.cbw.CB[0])
            {
                case SCSI_CMD_READ10:
                case SCSI_CMD_READ12:
                    if (SCSI_processRead() == false)
                    {
                        usbd_msc_send_csw(CSW_STATUS_CMD_FAILED); /* send fail status to host,and the host will retry*/
                        leave_critical_section(flags);
                        return;
                    }
                    break;
                default:
                    break;
            }
            break;
        /* the device has to send a CSW */
        case MSC_SEND_CSW:
            usbd_msc_send_csw(CSW_STATUS_CMD_PASSED);
            break;

        /* the host has received the CSW */
        case MSC_WAIT_CSW:
            usbd_msc_cfg.stage = MSC_READ_CBW;
            USB_LOG_VBS("Start reading cbw\r\n");
            usbd_ep_start_read(mass_ep_data[MSD_OUT_EP_IDX].ep_addr, (uint8_t *)&usbd_msc_cfg.cbw, USB_SIZEOF_MSC_CBW);
            break;

        default:
            break;
    }
    leave_critical_section(flags);
}

#ifdef CONFIG_USBDEV_MSC_THREAD
static void usbd_msc_thread(void *argument)
{
    uint32_t data_len = 0;
    uint8_t operation;
    irqstate_t flags;

    (void)argument;

    while (1)
    {
        if (rtos_get_semaphore(&msc_sem, 0xffffffff) != BK_OK)
        {
            rtos_delay_milliseconds(1);
            continue;
        }

        if (nxmutex_lock(&g_bk7258_usbmsc_worker_lock) < 0)
        {
            continue;
        }

        /* Consume the one outstanding BOT operation exactly once. A wake
         * left over from an earlier USB mode is harmless after init clears
         * thread_op, even if the next mode has already queued new work.
         */
        flags = enter_critical_section();
        operation = thread_op;
        thread_op = 0;
        leave_critical_section(flags);
        if (!s_msc_storage_init)
        {
            nxmutex_unlock(&g_bk7258_usbmsc_worker_lock);
            continue;
        }

        switch (operation)
        {
            case MSC_THREAD_OP_READ_MEM:
                data_len = MIN(usbd_msc_cfg.nsectors * usbd_msc_cfg.scsi_blk_size, CONFIG_USBDEV_MSC_BLOCK_SIZE);
                if (usbd_msc_sector_read(usbd_msc_cfg.start_sector, usbd_msc_cfg.block_buffer, data_len) != 0)
                {
                    flags = enter_critical_section();
                    SCSI_SetSenseData(SCSI_KCQHE_UREINRESERVEDAREA);
                    if (s_msc_storage_init)
                    {
                        usbd_msc_send_csw(CSW_STATUS_CMD_FAILED);
                    }
                    leave_critical_section(flags);
                    break;
                }
                if (s_msc_storage_init)
                {
                    usbd_msc_thread_memory_read_done();
                }
                break;
            case MSC_THREAD_OP_WRITE_MEM:
                data_len = current_byte_read;
                if (usbd_msc_sector_write(usbd_msc_cfg.start_sector, usbd_msc_cfg.block_buffer, data_len) != 0)
                {
                    flags = enter_critical_section();
                    SCSI_SetSenseData(SCSI_KCQHE_WRITEFAULT);
                    if (s_msc_storage_init)
                    {
                        usbd_msc_send_csw(CSW_STATUS_CMD_FAILED);
                    }
                    leave_critical_section(flags);
                    break;
                }
                if (s_msc_storage_init)
                {
                    usbd_msc_thread_memory_write_done();
                }
                break;
            case MSC_THREAD_OP_RESET:
                usbd_set_status(1);
                usbd_msc_get_cap(0, &usbd_msc_cfg.scsi_blk_nbr, &usbd_msc_cfg.scsi_blk_size);
                break;
            case MSC_THREAD_OP_SUSPEND:
                break;
            default:
                break;
        }
        nxmutex_unlock(&g_bk7258_usbmsc_worker_lock);
    }
}
#endif

struct usbd_interface *usbd_msc_init_intf(struct usbd_interface *intf, const uint8_t out_ep, const uint8_t in_ep)
{
    intf->class_interface_handler = msc_storage_class_interface_request_handler;
    intf->class_endpoint_handler = NULL;
    intf->vendor_handler = NULL;
    intf->notify_handler = msc_storage_notify_handler;

    mass_ep_data[MSD_OUT_EP_IDX].ep_addr = out_ep;
    mass_ep_data[MSD_OUT_EP_IDX].ep_cb = mass_storage_bulk_out;
    mass_ep_data[MSD_IN_EP_IDX].ep_addr = in_ep;
    mass_ep_data[MSD_IN_EP_IDX].ep_cb = mass_storage_bulk_in;

    usbd_add_endpoint(&mass_ep_data[MSD_OUT_EP_IDX]);
    usbd_add_endpoint(&mass_ep_data[MSD_IN_EP_IDX]);

    memset((uint8_t *)&usbd_msc_cfg, 0, sizeof(struct usbd_msc_cfg_priv));

    usbd_msc_get_cap(0, &usbd_msc_cfg.scsi_blk_nbr, &usbd_msc_cfg.scsi_blk_size);

    if (usbd_msc_cfg.scsi_blk_size > CONFIG_USBDEV_MSC_BLOCK_SIZE)
    {
        USB_LOG_ERR("msc block buffer overflow\r\n");
        return NULL;
    }
#ifdef CONFIG_USBDEV_MSC_THREAD
    if (msc_thread == NULL)
    {
        if (rtos_init_semaphore(&msc_sem, 1) != BK_OK)
        {
            USB_LOG_ERR("failed to create msc semaphore\r\n");
            return NULL;
        }
        msc_thread = usb_osal_thread_create("usbd_msc", CONFIG_USBDEV_MSC_STACKSIZE, CONFIG_USBDEV_MSC_PRIO, usbd_msc_thread, NULL);
        if (msc_thread == NULL)
        {
            USB_LOG_ERR("no enough memory to alloc msc thread\r\n");
            (void)rtos_deinit_semaphore(&msc_sem);
            msc_sem = NULL;
            return NULL;
        }
    }
#endif

    return intf;
}

void usbd_msc_set_readonly(bool readonly)
{
    usbd_msc_cfg.readonly = readonly;
}

int usbd_ms_media_get_status(void)
{
	return gs_status;
}

static void usbd_set_status(uint8_t status)
{
   gs_status = status;
}

void usbd_msc_get_cap(uint8_t lun, uint32_t *block_num, uint16_t *block_size)
{
    (void)lun;

    if (block_num == NULL || block_size == NULL)
    {
        return;
    }

    if (!s_msc_storage_init || g_bk7258_usbmsc_inode == NULL)
    {
        *block_num = 0;
        *block_size = 0;
        return;
    }

    *block_num = (uint32_t)g_bk7258_usbmsc_geometry.geo_nsectors;
    *block_size = (uint16_t)g_bk7258_usbmsc_geometry.geo_sectorsize;
}

int usbd_msc_sector_read(uint32_t sector, uint8_t *buffer, uint32_t length)
{
    struct inode *inode;
    uint32_t sectorsize;
    size_t nsectors;
    ssize_t nread;
    int ret;

    sectorsize = g_bk7258_usbmsc_geometry.geo_sectorsize;
    if (buffer == NULL || sectorsize == 0 || length == 0 ||
        (length % sectorsize) != 0) {
        return -EINVAL;
    }

    nsectors = length / sectorsize;
    if (sector >= g_bk7258_usbmsc_geometry.geo_nsectors ||
        nsectors > g_bk7258_usbmsc_geometry.geo_nsectors - sector) {
        return -ERANGE;
    }

    ret = nxmutex_lock(&g_bk7258_usbmsc_lock);
    if (ret < 0)
    {
        return ret;
    }

    inode = g_bk7258_usbmsc_inode;
    if (!s_msc_storage_init || inode == NULL || inode->u.i_bops == NULL ||
        inode->u.i_bops->read == NULL) {
        nxmutex_unlock(&g_bk7258_usbmsc_lock);
        return -ENODEV;
    }

    nread = inode->u.i_bops->read(inode, buffer, sector, nsectors);
    nxmutex_unlock(&g_bk7258_usbmsc_lock);
    return nread == (ssize_t)nsectors ? OK :
           (nread < 0 ? (int)nread : -EIO);
}

int usbd_msc_sector_write(uint32_t sector, uint8_t *buffer, uint32_t length)
{
    struct inode *inode;
    uint32_t sectorsize;
    size_t nsectors;
    ssize_t nwritten;
    int ret;

    sectorsize = g_bk7258_usbmsc_geometry.geo_sectorsize;
    if (buffer == NULL || sectorsize == 0 || length == 0 ||
        (length % sectorsize) != 0) {
        return -EINVAL;
    }

    nsectors = length / sectorsize;
    if (sector >= g_bk7258_usbmsc_geometry.geo_nsectors ||
        nsectors > g_bk7258_usbmsc_geometry.geo_nsectors - sector) {
        return -ERANGE;
    }

    ret = nxmutex_lock(&g_bk7258_usbmsc_lock);
    if (ret < 0)
    {
        return ret;
    }

    inode = g_bk7258_usbmsc_inode;
    if (!s_msc_storage_init || inode == NULL || inode->u.i_bops == NULL ||
        inode->u.i_bops->write == NULL) {
        nxmutex_unlock(&g_bk7258_usbmsc_lock);
        return -ENODEV;
    }

    nwritten = inode->u.i_bops->write(inode, buffer, sector, nsectors);
    nxmutex_unlock(&g_bk7258_usbmsc_lock);
    return nwritten == (ssize_t)nsectors ? OK :
           (nwritten < 0 ? (int)nwritten : -EIO);
}

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


    if (s_msc_storage_init)
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
    g_bk7258_usbmsc_inode = NULL;
    memset(&g_bk7258_usbmsc_geometry, 0,
           sizeof(g_bk7258_usbmsc_geometry));
    (void)usbd_deinitialize();
errout_with_inode:
    (void)close_blockdriver(inode);
    nxmutex_unlock(&g_bk7258_usbmsc_lock);
    nxmutex_unlock(&g_bk7258_usbmsc_worker_lock);
    return ret;
}

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

    if (!s_msc_storage_init || g_bk7258_usbmsc_inode == NULL)
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

#endif /* CONFIG_BK7258_USBMSC */
