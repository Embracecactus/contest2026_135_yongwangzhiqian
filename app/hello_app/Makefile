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

ifeq ($(CONFIG_BK7258_AP_CONTROL),y)
PROGNAME  += apctl
PRIORITY  += SCHED_PRIORITY_DEFAULT
STACKSIZE += 2048
MAINSRC   += bk7258_apctl_main.c
endif

MODULE = y

include $(APPDIR)/Application.mk
