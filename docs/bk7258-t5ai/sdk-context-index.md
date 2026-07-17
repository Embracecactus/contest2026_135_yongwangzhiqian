# Hardware Context Index — bk_avdk_smp

Generated: 2026-07-17T09:03:31Z
SDK path: /home/lijian/project/armino/bk_avdk_smp

> Detected layout: **armino-embedded**

## Register / CMSIS Headers

| File | Key Defines | Path |
|------|-------------|------|
| qspi_reg.h | QSPI_R_BASE=SOC_QSPI0_REG_BASE,QSPI_DCACHE_BASE=SOC_QSPI0_DATA_BASE,0x4000000= | cp/middleware/soc/bk7257/soc/qspi_reg.h |
| spi_reg.h | SPI_R_BASE=SOC_SPI_REG_BASE,0x1010000= | cp/middleware/soc/bk7257/soc/spi_reg.h |
| qspi_reg.h | QSPI_R_BASE=SOC_QSPI0_REG_BASE,QSPI_DCACHE_BASE=SOC_QSPI0_DATA_BASE,0x4000000= | cp/middleware/soc/bk7258/soc/qspi_reg.h |
| spi_reg.h | SPI_R_BASE=SOC_SPI_REG_BASE,0x1010000= | cp/middleware/soc/bk7258/soc/spi_reg.h |
| reg_base.h | SOC_ITCM_DATA_BASE=0x00000000,SOC_DTCM_DATA_BASE=0x20000000,SOC_FLASH_DATA_BASE=0x02000000,SOC_ROM_DATA_BASE=0x06000000,SOC_PSRAM_DATA_BASE=0x60000000,SOC_SYS_REG_BASE=0x44010000 | cp/include/soc/bk7257/reg_base.h |
| reg_base.h | SOC_ITCM_DATA_BASE=0x00000000,SOC_DTCM_DATA_BASE=0x20000000,SOC_FLASH_DATA_BASE=0x02000000,SOC_ROM_DATA_BASE=0x06000000,SOC_PSRAM_DATA_BASE=0x60000000,SOC_SYS_REG_BASE=0x44010000 | cp/include/soc/bk7258/reg_base.h |
| qspi_reg.h | QSPI_R_BASE=SOC_QSPI0_REG_BASE,QSPI_DCACHE_BASE=SOC_QSPI0_DATA_BASE,0x4000000= | ap/middleware/soc/bk7257_ap/soc/qspi_reg.h |
| spi_reg.h | SPI_R_BASE=SOC_SPI_REG_BASE,0x1010000= | ap/middleware/soc/bk7257_ap/soc/spi_reg.h |
| qspi_reg.h | QSPI_R_BASE=SOC_QSPI0_REG_BASE,QSPI_DCACHE_BASE=SOC_QSPI0_DATA_BASE,0x4000000= | ap/middleware/soc/bk7258_ap/soc/qspi_reg.h |
| spi_reg.h | SPI_R_BASE=SOC_SPI_REG_BASE,0x1010000= | ap/middleware/soc/bk7258_ap/soc/spi_reg.h |
| reg_base.h | SOC_ITCM_DATA_BASE=0x00000000,SOC_DTCM_DATA_BASE=0x20000000,SOC_FLASH_DATA_BASE=0x02000000,SOC_ROM_DATA_BASE=0x06000000,SOC_PSRAM_DATA_BASE=0x60000000,SOC_SYS_REG_BASE=0x44010000 | ap/include/soc/bk7257/reg_base.h |
| reg_base.h | SOC_ITCM_DATA_BASE=0x00000000,SOC_DTCM_DATA_BASE=0x20000000,SOC_FLASH_DATA_BASE=0x02000000,SOC_ROM_DATA_BASE=0x06000000,SOC_PSRAM_DATA_BASE=0x60000000,SOC_SYS_REG_BASE=0x44010000 | ap/include/soc/bk7258/reg_base.h |

## Startup / System Init

| File | Key Functions | Path |
|------|---------------|------|
| startup_bk7236.c | Reset_Handler,SystemInit,_start,entry_main | cp/components/cmsis/CMSIS_5/Device/Beken/bk7236xx/Source/startup_bk7236.c |
| startup_cpu0.c | SystemInitCpu0,_start,entry_main | cp/components/cmsis/CMSIS_5/Device/Beken/bk7236xx/Source/smp/startup_cpu0.c |
| startup_cpu2.c | SystemInitCpu2,multicore_launch_core2 | cp/components/cmsis/CMSIS_5/Device/Beken/bk7236xx/Source/smp/startup_cpu2.c |
| startup_cpu1.c | SystemInitCpu1,multicore_launch_core1 | cp/components/cmsis/CMSIS_5/Device/Beken/bk7236xx/Source/smp/startup_cpu1.c |
| system_main.c | entry_main,start_cpu1_core,start_cpu2_core | cp/components/bk_startup/system_main.c |
| startup_bk7236.c | Reset_Handler,SystemInit,_start,entry_main | ap/components/cmsis/CMSIS_5/Device/Beken/bk7236xx/Source/startup_bk7236.c |
| startup_cpu0.c | SystemInitCpu0,_start,entry_main | ap/components/cmsis/CMSIS_5/Device/Beken/bk7236xx/Source/smp/startup_cpu0.c |
| startup_cpu2.c | SystemInitCpu2,multicore_launch_core2 | ap/components/cmsis/CMSIS_5/Device/Beken/bk7236xx/Source/smp/startup_cpu2.c |
| startup_cpu1.c | SystemInitCpu1,multicore_launch_core1 | ap/components/cmsis/CMSIS_5/Device/Beken/bk7236xx/Source/smp/startup_cpu1.c |
| system_main.c | entry_main,start_cpu1_core,start_cpu2_core | ap/components/bk_startup/system_main.c |

## Linker Scripts / Memory Layout

| File | Path |
|------|------|
| bk7257_bsp.ld | cp/middleware/soc/bk7257/bk7257_bsp.ld |
| bk7258_bsp.ld | cp/middleware/soc/bk7258/bk7258_bsp.ld |
| bk7257_ap_bsp.ld | ap/middleware/soc/bk7257_ap/bk7257_ap_bsp.ld |
| bk7258_ap_bsp.ld | ap/middleware/soc/bk7258_ap/bk7258_ap_bsp.ld |
| bk7258_bsp.ld | projects/wifi/ipv6/cp/bk7258_bsp.ld |
| bk7258_ap_bsp.ld | projects/wifi/ipv6/bk7258_ap_bsp.ld |
| bk7258_bsp.ld | projects/wifi/ipv6/bk7258_bsp.ld |
| bk7258_bsp.ld | projects/wifi/bridge/cp/bk7258_bsp.ld |
| bk7258_ap_bsp.ld | projects/wifi/bridge/bk7258_ap_bsp.ld |
| bk7258_bsp.ld | projects/wifi/bridge/bk7258_bsp.ld |
| bk7258_bsp.ld | projects/wifi/iperf/cp/bk7258_bsp.ld |
| bk7258_ap_bsp.ld | projects/wifi/iperf/bk7258_ap_bsp.ld |
| bk7258_bsp.ld | projects/wifi/iperf/bk7258_bsp.ld |

## Drivers / Middleware

| Driver | Key Files | Path |
|--------|-----------|------|
| aon_wdt (ap) | aon_wdt_driver.c | ap/middleware/driver/aon_wdt/ |
| audio (ap) | audio_ring_buff.c | ap/middleware/driver/audio/ |
| bk7257_ap (ap) | interrupt.c,mailbox_driver.c,interrupt_statis.c | ap/middleware/driver/bk7257_ap/ |
| bk7258_ap (ap) | interrupt.c,mailbox_driver.c,interrupt_statis.c | ap/middleware/driver/bk7258_ap/ |
| calendar (ap) | calendar_driver.c | ap/middleware/driver/calendar/ |
| can (ap) | can_test.c,can_driver.c,can_demo.c | ap/middleware/driver/can/ |
| chip_support (ap) | chip_support.c | ap/middleware/driver/chip_support/ |
| ckmn (ap) | ckmn_driver.c | ap/middleware/driver/ckmn/ |
| common (ap) | driver.c,drv_model.c,dd.c | ap/middleware/driver/common/ |
| device (ap) | device.c | ap/middleware/driver/device/ |
| dma2d (ap) | dma2d_driver.c | ap/middleware/driver/dma2d/ |
| dsp (ap) | dsp.c | ap/middleware/driver/dsp/ |
| eth (ap) | eth_mac_ex.c,lan8742.c,eth_mac.c | ap/middleware/driver/eth/ |
| fft (ap) | fft.c,fft_driver.c | ap/middleware/driver/fft/ |
| flash (ap) | flash_shared_lock.c,flash_notify.c,flash_bypass.c | ap/middleware/driver/flash/ |
| general_dma (ap) | dma_driver.c | ap/middleware/driver/general_dma/ |
| gpio (ap) | cli_gpio_api.c,gpio_driver_base.c | ap/middleware/driver/gpio/ |
| h264 (ap) | h264_driver.c | ap/middleware/driver/h264/ |
| hw_rotate (ap) | rott_driver.c | ap/middleware/driver/hw_rotate/ |
| hw_scale (ap) | hw_scale_driver.c | ap/middleware/driver/hw_scale/ |
| i2c (ap) | i2c_statis.c,i2c_unified.c,cli_i2c_api.c | ap/middleware/driver/i2c/ |
| i2s (ap) | i2s_driver.c | ap/middleware/driver/i2s/ |
| icu (ap) | icu_driver.c,interrupt_base.c | ap/middleware/driver/icu/ |
| include (ap) | (dir) | ap/middleware/driver/include/ |
| irda (ap) | irda.c | ap/middleware/driver/irda/ |
| jpeg_dec (ap) | jpeg_dec_driver.c | ap/middleware/driver/jpeg_dec/ |
| jpeg_enc (ap) | jpeg_statis.c,jpeg_driver.c | ap/middleware/driver/jpeg_enc/ |
| lcd (ap) | lcd_spi_driver.c,lcd_driver.c,lcd_qspi_driver.c | ap/middleware/driver/lcd/ |
| lin (ap) | lin_statis.c,lin_driver.c | ap/middleware/driver/lin/ |
| mailbox (ap) | mb_chnl_buff.c,bk_api_ipc.c,mbox0_adapter.c | ap/middleware/driver/mailbox/ |
| otp (ap) | otp_driver_v1_1.c | ap/middleware/driver/otp/ |
| phy (ap) | (dir) | ap/middleware/driver/phy/ |
| pmu (ap) | aon_pmu_driver.c | ap/middleware/driver/pmu/ |
| port (ap) | mem_port.c,os_port.c | ap/middleware/driver/port/ |
| psram (ap) | psram_driver.c | ap/middleware/driver/psram/ |
| pwm (ap) | cli_pwm_api.c | ap/middleware/driver/pwm/ |
| pwr_clk (ap) | rosc_32k.c,pm_ap_demo.c,pm_ap_misc.c | ap/middleware/driver/pwr_clk/ |
| qspi (ap) | qspi_flash.c,qspi_driver.c,qspi_statis.c | ap/middleware/driver/qspi/ |
| reset_reason (ap) | reset_reason.c | ap/middleware/driver/reset_reason/ |
| rtc (ap) | aon_rtc_driver_64bit.c | ap/middleware/driver/rtc/ |
| saradc (ap) | saradc_client.c,saradc_example.c,saradc_notify.c | ap/middleware/driver/saradc/ |
| sbc (ap) | sbc_driver.c | ap/middleware/driver/sbc/ |
| scr (ap) | scr_driver_v1_26.c | ap/middleware/driver/scr/ |
| sd_card (ap) | sd_card_driver.c,sdcard_test.c | ap/middleware/driver/sd_card/ |
| sdcard (ap) | sdcard.c,sdcard_test.c,sdio_driver.c | ap/middleware/driver/sdcard/ |
| sdio (ap) | (dir) | ap/middleware/driver/sdio/ |
| sdio_host (ap) | sdio_host_driver.c | ap/middleware/driver/sdio_host/ |
| sdmadc (ap) | sdmadc_driver.c | ap/middleware/driver/sdmadc/ |
| slcd (ap) | slcd_driver.c | ap/middleware/driver/slcd/ |
| spi (ap) | spi_statis.c,cli_spi_api.c,spi_driver.c | ap/middleware/driver/spi/ |
| spinlock (ap) | spinlock.c,amp_res_lock.c | ap/middleware/driver/spinlock/ |
| sys_ctrl (ap) | sys_clock_driver.c,sys_psram_driver.c,sys_wifi_driver.c | ap/middleware/driver/sys_ctrl/ |
| timer (ap) | timer_driver.c | ap/middleware/driver/timer/ |
| touch (ap) | touch_driver_v1_1.c,touch_driver.c | ap/middleware/driver/touch/ |
| tp (ap) | tp_driver.c,drv_tp.c,bk_queue.c | ap/middleware/driver/tp/ |
| trng (ap) | trng_driver.c | ap/middleware/driver/trng/ |
| uart (ap) | cli_uart_api.c,printf.c,uart_driver.c | ap/middleware/driver/uart/ |
| wdt (ap) | wdt_driver.c | ap/middleware/driver/wdt/ |
| yuv_buf (ap) | yuv_buf_driver.c | ap/middleware/driver/yuv_buf/ |
| aon_wdt (cp) | aon_wdt_driver.c | cp/middleware/driver/aon_wdt/ |
| audio (cp) | audio_ring_buff.c,uac_driver.c | cp/middleware/driver/audio/ |
| bk7257 (cp) | interrupt.c,mailbox_driver.c,interrupt_statis.c | cp/middleware/driver/bk7257/ |
| bk7258 (cp) | interrupt.c,mailbox_driver.c,interrupt_statis.c | cp/middleware/driver/bk7258/ |
| calendar (cp) | calendar_driver.c | cp/middleware/driver/calendar/ |
| can (cp) | (dir) | cp/middleware/driver/can/ |
| chip_support (cp) | chip_support.c | cp/middleware/driver/chip_support/ |
| ckmn (cp) | ckmn_driver.c | cp/middleware/driver/ckmn/ |
| common (cp) | driver.c,drv_model.c,dd.c | cp/middleware/driver/common/ |
| device (cp) | device.c | cp/middleware/driver/device/ |
| dsp (cp) | dsp.c | cp/middleware/driver/dsp/ |
| efuse (cp) | efuse_driver.c | cp/middleware/driver/efuse/ |
| eth (cp) | eth_mac_ex.c,lan8742.c,eth_mac.c | cp/middleware/driver/eth/ |
| fatfs (cp) | (dir) | cp/middleware/driver/fatfs/ |
| fft (cp) | fft.c,fft_driver.c | cp/middleware/driver/fft/ |
| flash (cp) | flash_shared_lock.c,flash_notify.c,flash_bypass.c | cp/middleware/driver/flash/ |
| general_dma (cp) | dma_driver.c | cp/middleware/driver/general_dma/ |
| gpio (cp) | cli_gpio_api.c,gpio_driver_base.c | cp/middleware/driver/gpio/ |
| i2s (cp) | i2s_driver.c | cp/middleware/driver/i2s/ |
| icu (cp) | icu_driver.c,interrupt_base.c | cp/middleware/driver/icu/ |
| include (cp) | (dir) | cp/middleware/driver/include/ |
| irda (cp) | irda.c | cp/middleware/driver/irda/ |
| lin (cp) | lin_statis.c,lin_driver.c | cp/middleware/driver/lin/ |
| mailbox (cp) | mb_chnl_buff.c,bk_api_ipc.c,mbox0_adapter.c | cp/middleware/driver/mailbox/ |
| otp (cp) | otp_driver_v1_1.c | cp/middleware/driver/otp/ |
| phy (cp) | (dir) | cp/middleware/driver/phy/ |
| pmu (cp) | aon_pmu_driver.c | cp/middleware/driver/pmu/ |
| port (cp) | mem_port.c,os_port.c | cp/middleware/driver/port/ |
| psram (cp) | psram_driver.c | cp/middleware/driver/psram/ |
| pwr_clk (cp) | rosc_32k.c,rosc_ppm.c,pwr_clk.c | cp/middleware/driver/pwr_clk/ |
| qspi (cp) | qspi_flash.c,qspi_driver.c,qspi_statis.c | cp/middleware/driver/qspi/ |
| reset_reason (cp) | reset_reason.c | cp/middleware/driver/reset_reason/ |
| rtc (cp) | aon_rtc_driver_64bit.c,aon_rtc_driver.c | cp/middleware/driver/rtc/ |
| saradc (cp) | cli_adc_api.c,adc_driver.c,cli_sadc_api.c | cp/middleware/driver/saradc/ |
| sbc (cp) | sbc_driver.c | cp/middleware/driver/sbc/ |
| scr (cp) | scr_driver_v1_26.c | cp/middleware/driver/scr/ |
| sd_card (cp) | sd_card_driver.c,sdcard_test.c | cp/middleware/driver/sd_card/ |
| sdcard (cp) | sdcard.c,sdcard_test.c,sdio_driver.c | cp/middleware/driver/sdcard/ |
| sdio (cp) | (dir) | cp/middleware/driver/sdio/ |
| sdio_host (cp) | sdio_host_driver.c | cp/middleware/driver/sdio_host/ |
| sdmadc (cp) | sdmadc_driver.c | cp/middleware/driver/sdmadc/ |
| slcd (cp) | slcd_driver.c | cp/middleware/driver/slcd/ |
| spi (cp) | spi_statis.c,cli_spi_api.c,spi_driver.c | cp/middleware/driver/spi/ |
| spinlock (cp) | spinlock.c,amp_res_lock.c | cp/middleware/driver/spinlock/ |
| sys_ctrl (cp) | sys_clock_driver.c,sys_psram_driver.c,sys_wifi_driver.c | cp/middleware/driver/sys_ctrl/ |
| timer (cp) | timer_driver.c | cp/middleware/driver/timer/ |
| touch (cp) | touch_driver_v1_1.c,touch_driver.c | cp/middleware/driver/touch/ |
| trng (cp) | trng_driver.c | cp/middleware/driver/trng/ |
| uart (cp) | cli_uart_api.c,printf.c,uart_driver.c | cp/middleware/driver/uart/ |
| wdt (cp) | cli_wdt_api.c,wdt_driver.c | cp/middleware/driver/wdt/ |

## Bootloader

| File | Size | Path |
|------|------|------|
| bootloader.bin | 52352 | cp/components/bk_libs/bk7257/bootloader/normal_bootloader/bootloader.bin |
| bootloader.bin | 18720 | cp/components/bk_libs/bk7257/bootloader/ab_bootloader/bootloader.bin |
| bootloader.bin | 52352 | cp/components/bk_libs/bk7258/bootloader/normal_bootloader/bootloader.bin |
| bootloader.bin | 18720 | cp/components/bk_libs/bk7258/bootloader/ab_bootloader/bootloader.bin |

## Documentation

| File | Description | Path |
|------|-------------|------|
| CMakeLists.txt | include($ENV{ARMINO_TOOLS_PATH}/build_tools/build_ | cp/CMakeLists.txt |
| CMakeLists.txt | armino_component_register(INCLUDE_DIRS . | cp/include/CMakeLists.txt |
| requirements.txt |  | cp/requirements.txt |
| CMakeLists.txt | include($ENV{ARMINO_TOOLS_PATH}/build_tools/build_ | ap/CMakeLists.txt |
| CMakeLists.txt | armino_component_register(INCLUDE_DIRS . | ap/include/CMakeLists.txt |
| requirements.txt |  | ap/requirements.txt |
| README_CN.md |  | README_CN.md |
| README.md |  | README.md |
| app.rst | .. _project_app: | projects/pm_ap_powerdown/app.rst |
| CMakeLists.txt | The following lines of boilerplate have to be in y | projects/pm_ap_powerdown/CMakeLists.txt |
| README_CN.md | 语音识别服务示例工程 | projects/asr_service_example/README_CN.md |
| README.md | Automatic Speech Recognition Service Example Proje | projects/asr_service_example/README.md |
| app.rst | .. _project_app: | projects/asr_service_example/app.rst |
| CMakeLists.txt | The following lines of boilerplate have to be in y | projects/asr_service_example/CMakeLists.txt |
| README_CN.md | # Encoder示例工程 | projects/encoder_example/README_CN.md |
| README.md | # Encoder Sample Project | projects/encoder_example/README.md |
| CMakeLists.txt | The following lines of boilerplate have to be in y | projects/encoder_example/CMakeLists.txt |
| README_CN.md | UVC示例工程 | projects/uvc_example/README_CN.md |
| README.md | UVC Sample Project | projects/uvc_example/README.md |
| app.rst | .. _project_app: | projects/uvc_example/app.rst |
