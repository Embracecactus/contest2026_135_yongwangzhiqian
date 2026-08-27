/* Host shim for the optional OTA-over-RPMsg service.
 *
 * The RPTUN core includes the public header unconditionally, while the
 * service API is used only when CONFIG_BK7258_OTA_RPMSG is enabled.  This
 * fixture deliberately leaves that option disabled so the transport unit
 * tests do not acquire OTA manager/source dependencies.
 */

#ifndef __TESTS_BK7258_OTA_RPMSG_H
#define __TESTS_BK7258_OTA_RPMSG_H

#endif /* __TESTS_BK7258_OTA_RPMSG_H */
