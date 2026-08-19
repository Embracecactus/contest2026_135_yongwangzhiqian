/****************************************************************************
 * tests/mocks/driver/jpeg_enc.h
 *
 * Host mirror of the v3.1.1.9 SDK <driver/jpeg_enc.h> ABI.  Only the
 * driver-init root referenced by bk7258_media_root.c is needed.
 ****************************************************************************/

#ifndef __MOCK_DRIVER_JPEG_ENC_H
#define __MOCK_DRIVER_JPEG_ENC_H

#include <common/bk_err.h>

bk_err_t bk_jpeg_enc_driver_init(void);

#endif /* __MOCK_DRIVER_JPEG_ENC_H */