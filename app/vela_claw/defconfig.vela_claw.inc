###############################################################################
# Vela-Claw — app-layer enablement fragment
#
# Normally CONFIG_VELA_CLAW_UI / CONFIG_VELA_CLAW_NET already `select` every
# required kernel + userspace symbol, so you only need to enable those two
# switches in menuconfig. This file is a fallback: append its contents to
#   boards/bk7258/t5ai_core/configs/t5ai_core_ap_base/defconfig
# (or your board's AP defconfig) to turn Vela-Claw + screen UI + networking
# on without menuconfig.
#
# NOTE: BK7258_WIFI_VNET requires the CP-side vnet proxy and a base config
# that already enables the RPTUN mailbox; it only takes effect on the AP core.
###############################################################################

CONFIG_BK7258_APP_VELA_CLAW=y
CONFIG_BK7258_APP_VELA_CLAW_PROGNAME="vela_claw"
CONFIG_BK7258_APP_VELA_CLAW_STACKSIZE=8192
CONFIG_BK7258_APP_VELA_CLAW_PRIORITY=100

# ---- Screen UI: LVGL + openvela UIKit on the t5_board LCD + GT1151 touch ----
CONFIG_VELA_CLAW_UI=y
CONFIG_GRAPHICS_LVGL=y
CONFIG_UIKIT=y
CONFIG_BK7258_LCD=y
CONFIG_BK7258_GT1151=y
CONFIG_INPUT_GT9XX=y
CONFIG_I2C_BITBANG=y

# ---- Networking: real cloud LLM over Wi-Fi STA (curl transport) ----
CONFIG_VELA_CLAW_NET=y
CONFIG_NET=y
CONFIG_BK7258_WIFI_VNET=y
CONFIG_LIBCURL4NX=y
