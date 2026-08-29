/****************************************************************************
 * boards/bk7258/aidk_ai_toy/include/bk7258_board_config.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Board-layer facts for the AIDK AI Toy board (BK7258 AI Demo schematic
 * V1.0).  Pin routes below are derived from the owner-supplied schematic;
 * hardware verification is still pending, so unconfirmed routes remain
 * unavailable.
 ****************************************************************************/

#ifndef __BOARD_BK7258_AIDK_AI_TOY_CONFIG_H
#define __BOARD_BK7258_AIDK_AI_TOY_CONFIG_H

#define BK7258_BOARD_VARIANT_ID                 "aidk_ai_toy"
#define BK7258_BOARD_VARIANT_NAME               "AIDK AI Toy"
#define BK7258_BOARD_HARDWARE_VERSION           "schematic-v1.0"
#define BK7258_BOARD_SCHEMATIC                   "AIDK_AI玩具开发板_原理图.pdf"

/* UART0 is the sole documented console/download binding. */

#define BK7258_BOARD_CONSOLE_UART_ID             0
#define BK7258_BOARD_CONSOLE_BAUD                115200u
#define BK7258_BOARD_CONSOLE_DATA_BITS          8
#define BK7258_BOARD_CONSOLE_PARITY             0 /* none */
#define BK7258_BOARD_CONSOLE_STOP_BITS           1
#define BK7258_BOARD_CONSOLE_FLOW_CONTROL        0
#define BK7258_BOARD_CONSOLE_RTS_RESET           0
#define BK7258_BOARD_PORT_IDENTITY               "dynamic-usb-serial"

/* Debug/reset controls are deliberately not claimed by this binding. */

#define BK7258_BOARD_HAS_SWD                     0
#define BK7258_BOARD_BOOT_HOLD                   0
#define BK7258_BOARD_HAS_RTT                     0

/* Board capabilities from BK7258 AI Demo schematic V1.0 plus the current
 * assembly state supplied by the owner.  CN5's single-screen module and
 * CN10's motor are not connected, so their routes must not be initialized.
 */

#define BK7258_BOARD_HAS_USB_UART                1  /* CH340E -> UART0 (P10/P11) */
#define BK7258_BOARD_HAS_AUDIO                   1  /* HT6873 PA, AUDLP/AUDLN */
#define BK7258_BOARD_HAS_BATTERY                 1  /* ETA4322 + 4.2V VBAT */
#define BK7258_BOARD_HAS_TF_CARD                 0
#define BK7258_BOARD_HAS_SD_NAND                 1  /* 1GB SD NAND, SDIO P14-P19 */
#define BK7258_BOARD_HAS_RGB_LCD_CONNECTOR       0
#define BK7258_BOARD_HAS_SPI_LCD_CONNECTOR       0
#define BK7258_BOARD_HAS_QSPI_LCD_CONNECTOR      1  /* CN5, QSPI P2-P7 */
#define BK7258_BOARD_HAS_QSPI_LCD                0  /* CN5 module disconnected */
#define BK7258_BOARD_HAS_DVP_CONNECTOR           1  /* GC2145 24pin DVP */
#define BK7258_BOARD_HAS_CAMERA                  1
#define BK7258_BOARD_HAS_MOTOR                   0  /* CN10 disconnected */
#define BK7258_BOARD_HAS_MFRC522                 1  /* NFC UART1 (P0/P1) */
#define BK7258_BOARD_HAS_SC7A20                  1  /* SoC I2C0 (P20/P21) */
#define BK7258_BOARD_HAS_USB0                    1  /* Type-C DP/DM to chip */

#define BK7258_BOARD_CN5_DISPLAY_CONNECTED       0
#define BK7258_BOARD_CN10_MOTOR_CONNECTED        0
#define BK7258_BOARD_XTAL_32768_FITTED           0

/* Audio capture topology (schematic sheet 5 plus FAE confirmation).
 *
 * MIC1 is the physical microphone.  The MIC2 connector path is not fitted
 * (C53/C59 are NC); populated C60/C61 and R83/R84 instead feed AUDLN/AUDLP
 * back into MIC2N/MIC2P as the AEC reference.  The SoC must therefore expose
 * both ADC inputs in channel order MIC1, MIC2, while applications must not
 * treat channel 2 as a second microphone.
 */

#define BK7258_BOARD_CAPTURE_CHANNELS             2
#define BK7258_BOARD_MIC1_IS_PRIMARY_MIC           1
#define BK7258_BOARD_MIC2_IS_AEC_REFERENCE         1
#define BK7258_BOARD_MIC2_IS_PHYSICAL_MIC          0

/* GPIO lower-half binding (LED1/KEY3 as the user-visible pair). */

#define BK7258_BOARD_USER_LED_GPIO               40
#define BK7258_BOARD_USER_LED_ACTIVE_HIGH        1
#define BK7258_BOARD_USER_LED_CONSOLE_SHARED     0
#define BK7258_BOARD_USER_BUTTON_GPIO             8
#define BK7258_BOARD_USER_BUTTON_ACTIVE_LOW      1

/* HT6873 PA enable: PA_SD active high, 10 ms on / 30 ms off. */

#define BK7258_BOARD_SPEAKER_CONTROL_GPIO        50
#define BK7258_BOARD_SPEAKER_ACTIVE_HIGH          1
#define BK7258_BOARD_SPEAKER_ON_DELAY_MS         10u
#define BK7258_BOARD_SPEAKER_OFF_DELAY_MS        30u

/* SD NAND on SDIO map mode 1 (P14-P19), soldered, no card detect. */

#define BK7258_BOARD_SDIO_MAP_MODE                1
#define BK7258_BOARD_SDIO_CLK_GPIO               14
#define BK7258_BOARD_SDIO_CMD_GPIO               15
#define BK7258_BOARD_SDIO_D0_GPIO                16
#define BK7258_BOARD_SDIO_D1_GPIO                17
#define BK7258_BOARD_SDIO_D2_GPIO                18
#define BK7258_BOARD_SDIO_D3_GPIO                19
#define BK7258_BOARD_SDIO_CARD_DETECT_AVAILABLE  0
#define BK7258_BOARD_SDIO_CARD_DETECT_GPIO       0
#define BK7258_BOARD_SDIO_CARD_DETECT_ACTIVE_LOW 1
#define BK7258_BOARD_SDIO_MEDIA_POLL_MS          0

/* GC2145 control and DVP routes.  The schematic's IIC2 net name maps to
 * BK7258 hardware I2C1 map mode 1 at P42/P43; R38/R36 provide the external
 * 10 kOhm SCCB pull-ups.  The alternative I2C1 route on P38/P39 is
 * unavailable because those pins carry DVP D6/D7.  R39 pulls active-low
 * reset high and R31 pulls active-high power control low by default.
 *
 * Phase 0 uses only P49 power, P28 active-low reset, P27 MCLK and SCCB.  It
 * reads the two identity registers and does not initialize the DVP datapath.
 * PWDNB is hard-wired on the camera board and has no MCU GPIO.
 */

#define BK7258_BOARD_DVP_I2C_BUS                  1
#define BK7258_BOARD_DVP_I2C_MAP_MODE             1
#define BK7258_BOARD_DVP_I2C_FREQUENCY           100000u
#define BK7258_BOARD_DVP_I2C_SCL_GPIO            42
#define BK7258_BOARD_DVP_I2C_SDA_GPIO            43
#define BK7258_BOARD_DVP_POWER_GPIO              49
#define BK7258_BOARD_DVP_POWER_ACTIVE_HIGH        1
#define BK7258_BOARD_DVP_RESET_GPIO              28
#define BK7258_BOARD_DVP_RESET_ACTIVE_LOW         1
#define BK7258_BOARD_DVP_PWDN_AVAILABLE           0
#define BK7258_BOARD_DVP_PWDN_GPIO               0xff
#define BK7258_BOARD_DVP_MCLK_GPIO               27
#define BK7258_BOARD_DVP_PCLK_GPIO               29
#define BK7258_BOARD_DVP_HSYNC_GPIO              30
#define BK7258_BOARD_DVP_VSYNC_GPIO              31
#define BK7258_BOARD_DVP_D0_GPIO                 32
#define BK7258_BOARD_DVP_D1_GPIO                 33
#define BK7258_BOARD_DVP_D2_GPIO                 34
#define BK7258_BOARD_DVP_D3_GPIO                 35
#define BK7258_BOARD_DVP_D4_GPIO                 36
#define BK7258_BOARD_DVP_D5_GPIO                 37
#define BK7258_BOARD_DVP_D6_GPIO                 38
#define BK7258_BOARD_DVP_D7_GPIO                 39

/* P49 simultaneously enables U4 (ME6211C28M5G-N, 2.8 V AVDD) and U5
 * (RS9236-ADJ8YF5, R28=75 kOhm, 1.8 V DVDD for the fitted GC2145).
 */

#define BK7258_BOARD_CAMERA_AVDD_MV              2800
#define BK7258_BOARD_CAMERA_DVDD_MV              1800
#define BK7258_BOARD_GC2145_I2C_ADDRESS         0x3c
#define BK7258_BOARD_GC2145_ID_HIGH_REG         0xf0
#define BK7258_BOARD_GC2145_ID_HIGH_VALUE       0x21
#define BK7258_BOARD_GC2145_ID_LOW_REG          0xf1
#define BK7258_BOARD_GC2145_ID_LOW_VALUE        0x45
#define BK7258_BOARD_GC2145_ID                  0x2145
#define BK7258_BOARD_CAMERA_DEVPATH              "/dev/video0"
#define BK7258_BOARD_CAMERA_WIDTH                640u
#define BK7258_BOARD_CAMERA_HEIGHT               480u
#define BK7258_BOARD_CAMERA_FPS                  30u
#define BK7258_BOARD_CAMERA_FRAME_COUNT          2u
#define BK7258_BOARD_CAMERA_DMA_ALIGNMENT        32u

/* SC7A20H accelerometer on I2C0.  The schematic ties SDO low through fitted
 * R35 (0 Ohm), selecting 7-bit address 0x18.  G_VDD is supplied directly
 * from VDDGPIO through fitted R37, so Phase 0 needs no power-control GPIO.
 * It reads only WHO_AM_I and leaves the sensor in its reset power-down mode.
 */

#define BK7258_BOARD_SC7A20_I2C_BUS               0
#define BK7258_BOARD_SC7A20_I2C_FREQUENCY    100000u
#define BK7258_BOARD_SC7A20_I2C_SCL_GPIO          20
#define BK7258_BOARD_SC7A20_I2C_SDA_GPIO          21
#define BK7258_BOARD_SC7A20_I2C_ADDRESS          0x18
#define BK7258_BOARD_SC7A20_WHO_AM_I_REG         0x0f
#define BK7258_BOARD_SC7A20_WHO_AM_I_VALUE       0x11
#define BK7258_BOARD_SC7A20_POWER_ALWAYS_ON         1

/* Schematic-derived pin map (BK7258 pin -> net). */

#define BK7258_BOARD_PIN_UART1_TXD               0
#define BK7258_BOARD_PIN_UART1_RXD               1
#define BK7258_BOARD_PIN_QSPI1_CLK              2
#define BK7258_BOARD_PIN_QSPI1_CS               3
#define BK7258_BOARD_PIN_QSPI1_D0               4
#define BK7258_BOARD_PIN_QSPI1_D1               5
#define BK7258_BOARD_PIN_QSPI1_D2               6
#define BK7258_BOARD_PIN_QSPI1_D3               7
#define BK7258_BOARD_PIN_KEY3                   8
#define BK7258_BOARD_PIN_MOTOR                  9
#define BK7258_BOARD_PIN_UART0_RXD              10
#define BK7258_BOARD_PIN_UART0_TXD              11
#define BK7258_BOARD_PIN_KEY2                   12
#define BK7258_BOARD_PIN_KEY1                   13
#define BK7258_BOARD_PIN_SD_CLK                 14
#define BK7258_BOARD_PIN_SD_CMD                 15
#define BK7258_BOARD_PIN_SD_D0                  16
#define BK7258_BOARD_PIN_SD_D1                  17
#define BK7258_BOARD_PIN_SD_D2                  18
#define BK7258_BOARD_PIN_SD_D3                  19
#define BK7258_BOARD_PIN_I2C0_SCL               20
#define BK7258_BOARD_PIN_I2C0_SDA               21
#define BK7258_BOARD_PIN_LCD_BL_PWM             25
#define BK7258_BOARD_PIN_FULL_DET               26
#define BK7258_BOARD_PIN_DVP_MCLK               27
#define BK7258_BOARD_PIN_DVP_RST                28
#define BK7258_BOARD_PIN_DVP_PCLK               29
#define BK7258_BOARD_PIN_DVP_HSYNC              30
#define BK7258_BOARD_PIN_DVP_VSYNC              31
#define BK7258_BOARD_PIN_DVP_D0                 32
#define BK7258_BOARD_PIN_DVP_D1                 33
#define BK7258_BOARD_PIN_DVP_D2                 34
#define BK7258_BOARD_PIN_DVP_D3                 35
#define BK7258_BOARD_PIN_DVP_D4                 36
#define BK7258_BOARD_PIN_DVP_D5                 37
#define BK7258_BOARD_PIN_DVP_D6                 38
#define BK7258_BOARD_PIN_DVP_D7                 39
#define BK7258_BOARD_PIN_LED1                   40
#define BK7258_BOARD_PIN_LED2                   41
#define BK7258_BOARD_PIN_I2C1_SCL               42
#define BK7258_BOARD_PIN_I2C1_SDA               43
#define BK7258_BOARD_PIN_LCD_TE                 44
#define BK7258_BOARD_PIN_LCD_RST                45
#define BK7258_BOARD_PIN_TP_INT                 46
#define BK7258_BOARD_PIN_TP_CS                  47
#define BK7258_BOARD_PIN_DVP_PWR_CTL            49
#define BK7258_BOARD_PIN_PA_SD                  50
#define BK7258_BOARD_PIN_5V_DET                 51
#define BK7258_BOARD_PIN_LDO33_EN               52
#define BK7258_BOARD_PIN_NFC_IRQ                53
#define BK7258_BOARD_PIN_NFC_MX                 54
#define BK7258_BOARD_PIN_NFC_DTRQ               55

#define BK7258_BOARD_MINIMAL_BRINGUP             0
#define BK7258_BOARD_HARDWARE_VERIFIED           0

/* Schematic conflict records; no route is enabled from these facts. */

#define BK7258_BOARD_CONFLICT_P20_P21_SC7A20_SWD 1
#define BK7258_BOARD_CONFLICT_P0_P1_MFRC522_CN1  1
#define BK7258_BOARD_CONFLICT_P38_P39_DVP_I2C1   1
#define BK7258_BOARD_CONFLICT_USB0_UNKNOWN       0

#endif /* __BOARD_BK7258_AIDK_AI_TOY_CONFIG_H */
