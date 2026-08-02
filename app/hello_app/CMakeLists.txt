# Contest 2026 team 135 hello app
if(CONFIG_LVX_USE_DEMO_CONTEST2026_135_HELLO_APP)
  nuttx_add_application(
    NAME hello_app
    SRCS hello_app_main.c
    STACKSIZE 2048)
endif()

if(CONFIG_BK7258_SDK_IRQ_TIMER_TEST)
  nuttx_add_application(
    NAME bkirqtest
    SRCS bk7258_sdk_irq_test_main.c
    STACKSIZE 2048)
endif()

if(CONFIG_BK7258_GPIO_FOUNDATION_TEST)
  nuttx_add_application(
    NAME bkgpioc0
    SRCS bk7258_gpio_foundation_test_main.c
    STACKSIZE 2048)
endif()

if(CONFIG_BK7258_GPIO_IRQ_TEST)
  nuttx_add_application(
    NAME bkgpioirq
    SRCS bk7258_gpio_irq_test_main.c
    STACKSIZE 2048)
endif()

if(CONFIG_BK7258_AP_CONTROL)
  nuttx_add_application(
    NAME apctl
    SRCS bk7258_apctl_main.c
    STACKSIZE 2048)
endif()

if(CONFIG_BK7258_RPMSG_TEST AND NOT CONFIG_BK7258_AP_CORE)
  nuttx_add_application(
    NAME bkrpmsgtest
    SRCS bk7258_rpmsg_test_main.c
    STACKSIZE 3072)
endif()

if(CONFIG_BK7258_RPMSGFS_TEST AND NOT CONFIG_BK7258_AP_CORE)
  nuttx_add_application(
    NAME bkrpmsgfstest
    SRCS bk7258_rpmsgfs_test_main.c
    STACKSIZE 3072)
endif()
