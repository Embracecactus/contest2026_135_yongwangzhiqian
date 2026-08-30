# Contest 2026 team 135 hello app
if(CONFIG_LVX_USE_DEMO_CONTEST2026_135_HELLO_APP)
  nuttx_add_application(
    NAME hello_app
    SRCS hello_app_main.c
    STACKSIZE 2048)
endif()
