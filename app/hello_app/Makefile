############################################################################
# Contest 2026 team 135 hello app - Makefile
############################################################################

include $(APPDIR)/Make.defs

ifeq ($(CONFIG_LVX_USE_DEMO_CONTEST2026_135_HELLO_APP),y)
PROGNAME  += hello_app
PRIORITY  += SCHED_PRIORITY_DEFAULT
STACKSIZE += 2048
MAINSRC   += hello_app_main.c
endif

ifeq ($(CONFIG_BK7258_SDK_IRQ_TIMER_TEST),y)
PROGNAME  += bkirqtest
PRIORITY  += SCHED_PRIORITY_DEFAULT
STACKSIZE += 2048
MAINSRC   += bk7258_sdk_irq_test_main.c
endif

ifeq ($(CONFIG_BK7258_SDK_TIMER_SELFTEST),y)
PROGNAME  += bktimertest
PRIORITY  += SCHED_PRIORITY_DEFAULT
STACKSIZE += 2048
MAINSRC   += bk7258_sdk_timer_selftest_main.c
endif

ifeq ($(CONFIG_BK7258_GPIO_FOUNDATION_TEST),y)
PROGNAME  += bkgpioc0
PRIORITY  += SCHED_PRIORITY_DEFAULT
STACKSIZE += 2048
MAINSRC   += bk7258_gpio_foundation_test_main.c
endif

ifeq ($(CONFIG_BK7258_GPIO_IRQ_TEST),y)
PROGNAME  += bkgpioirq
PRIORITY  += SCHED_PRIORITY_DEFAULT
STACKSIZE += 2048
MAINSRC   += bk7258_gpio_irq_test_main.c
endif

ifeq ($(CONFIG_BK7258_AP_CONTROL),y)
PROGNAME  += apctl
PRIORITY  += SCHED_PRIORITY_DEFAULT
STACKSIZE += 2048
MAINSRC   += bk7258_apctl_main.c
endif

ifeq ($(CONFIG_BK7258_RPMSG_TEST),y)
ifneq ($(CONFIG_BK7258_AP_CORE),y)
PROGNAME  += bkrpmsgtest
PRIORITY  += SCHED_PRIORITY_DEFAULT
STACKSIZE += 3072
MAINSRC   += bk7258_rpmsg_test_main.c
endif
endif

ifeq ($(CONFIG_BK7258_RPMSGFS_TEST),y)
ifneq ($(CONFIG_BK7258_AP_CORE),y)
PROGNAME  += bkrpmsgfstest
PRIORITY  += SCHED_PRIORITY_DEFAULT
STACKSIZE += 3072
MAINSRC   += bk7258_rpmsgfs_test_main.c
endif
endif

ifeq ($(CONFIG_BK7258_BT_IPC_TEST),y)
ifneq ($(CONFIG_BK7258_AP_CORE),y)
PROGNAME  += bkbttest
PRIORITY  += SCHED_PRIORITY_DEFAULT
STACKSIZE += 3072
MAINSRC   += bk7258_bt_test_main.c
endif
endif

ifeq ($(CONFIG_BK7258_WIFI_VNET),y)
ifneq ($(CONFIG_BK7258_AP_CORE),y)
PROGNAME  += bkwifi
PRIORITY  += SCHED_PRIORITY_DEFAULT
STACKSIZE += 4096
MAINSRC   += bk7258_wifi_main.c
endif
endif

ifeq ($(CONFIG_BK7258_PSRAM_TEST),y)
ifneq ($(CONFIG_BK7258_AP_CORE),y)
PROGNAME  += bkpsramtest
PRIORITY  += SCHED_PRIORITY_DEFAULT
STACKSIZE += 3072
MAINSRC   += bk7258_psram_test_main.c
endif
endif

ifeq ($(CONFIG_BK7258_OTA_VALIDATION),y)
ifneq ($(CONFIG_BK7258_AP_CORE),y)
PROGNAME  += bkota
PRIORITY  += SCHED_PRIORITY_DEFAULT
STACKSIZE += 8192
MAINSRC   += bk7258_ota_main.c
endif
endif

MODULE = y

include $(APPDIR)/Application.mk
