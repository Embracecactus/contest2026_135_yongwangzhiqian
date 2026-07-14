/****************************************************************************
 * NuttX - RV1126B BSP
 ****************************************************************************/

#ifndef __BOARD_HARDWARE_RV1126B_UART_H
#define __BOARD_HARDWARE_RV1126B_UART_H

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* UART register offsets (DesignWare 8250-compatible) */

#define RV1126B_UART_RBR_OFFSET       0x0000  /* Receive Buffer Register (read) */
#define RV1126B_UART_THR_OFFSET       0x0000  /* Transmit Holding Register (write) */
#define RV1126B_UART_DLL_OFFSET       0x0000  /* Divisor Latch Low (when DLAB=1) */
#define RV1126B_UART_DLH_OFFSET       0x0004  /* Divisor Latch High (when DLAB=1) */
#define RV1126B_UART_IER_OFFSET       0x0004  /* Interrupt Enable Register (when DLAB=0) */
#define RV1126B_UART_IIR_OFFSET       0x0008  /* Interrupt Identification Register (read) */
#define RV1126B_UART_FCR_OFFSET       0x0008  /* FIFO Control Register (write) */
#define RV1126B_UART_LCR_OFFSET       0x000C  /* Line Control Register */
#define RV1126B_UART_MCR_OFFSET       0x0010  /* Modem Control Register */
#define RV1126B_UART_LSR_OFFSET       0x0014  /* Line Status Register (read) */
#define RV1126B_UART_MSR_OFFSET       0x0018  /* Modem Status Register (read) */
#define RV1126B_UART_SCR_OFFSET       0x001C  /* Scratch Register */
#define RV1126B_UART_SRBR_OFFSET      0x0030  /* Shadow Receive Buffer Register */
#define RV1126B_UART_STHR_OFFSET      0x0030  /* Shadow Transmit Holding Register */
#define RV1126B_UART_FAR_OFFSET       0x0070  /* FIFO Access Register */
#define RV1126B_UART_TFR_OFFSET       0x0074  /* Transmit FIFO Read (read) */
#define RV1126B_UART_RFW_OFFSET       0x0078  /* Receive FIFO Write (write) */
#define RV1126B_UART_USR_OFFSET       0x007C  /* UART Status Register (read) */
#define RV1126B_UART_TFL_OFFSET       0x0080  /* Transmit FIFO Level (read) */
#define RV1126B_UART_RFL_OFFSET       0x0084  /* Receive FIFO Level (read) */
#define RV1126B_UART_SRR_OFFSET       0x0088  /* Software Reset Register (write) */
#define RV1126B_UART_SRTS_OFFSET      0x008C  /* Shadow Request to Send */
#define RV1126B_UART_SBCR_OFFSET      0x0090  /* Shadow Break Control Register */
#define RV1126B_UART_SDMAM_OFFSET     0x0094  /* Shadow DMA Mode */
#define RV1126B_UART_SFE_OFFSET       0x0098  /* Shadow FIFO Enable */
#define RV1126B_UART_SRT_OFFSET       0x009C  /* Shadow RCVR Trigger */
#define RV1126B_UART_STET_OFFSET      0x00A0  /* Shadow TX Empty Trigger */
#define RV1126B_UART_HTX_OFFSET       0x00A4  /* Halt TX */
#define RV1126B_UART_DMASA_OFFSET     0x00A8  /* DMA Software Acknowledge (write) */
#define RV1126B_UART_CPR_OFFSET       0x00F4  /* Component Parameter Register (read) */
#define RV1126B_UART_UCV_OFFSET       0x00F8  /* UART Component Version (read) */
#define RV1126B_UART_CTR_OFFSET       0x00FC  /* Component Type Register (read) */

/* IER bit definitions */

#define RV1126B_UART_IER_ERBFI        (1 << 0)  /* Enable Received Data Available Interrupt */
#define RV1126B_UART_IER_ETBEI        (1 << 1)  /* Enable Transmit Holding Register Empty Int */
#define RV1126B_UART_IER_ELSI         (1 << 2)  /* Enable Receiver Line Status Interrupt */
#define RV1126B_UART_IER_EDSSI        (1 << 3)  /* Enable Modem Status Interrupt */
#define RV1126B_UART_IER_ELCOLR       (1 << 4)  /* Enable Low-power Setting Interrupt */
#define RV1126B_UART_IER_PTIME        (1 << 7)  /* Programmable THRE Interrupt Mode Enable */

/* IIR bit definitions */

#define RV1126B_UART_IIR_NONE         0x01      /* No interrupt pending */
#define RV1126B_UART_IIR_ID_MASK      0x0e      /* Interrupt ID mask */
#define RV1126B_UART_IIR_MSI          0x00      /* Modem Status */
#define RV1126B_UART_IIR_THRE         0x02      /* Transmit Holding Register Empty */
#define RV1126B_UART_IIR_RDA          0x04      /* Received Data Available */
#define RV1126B_UART_IIR_RLS          0x06      /* Receiver Line Status */
#define RV1126B_UART_IIR_BUSY         0x07      /* Busy Detect */
#define RV1126B_UART_IIR_CTI          0x0C      /* Character Timeout */
#define RV1126B_UART_IIR_FEFLAG       0x80      /* FIFOs Enable Flag */

/* FCR bit definitions */

#define RV1126B_UART_FCR_FIFOE        (1 << 0)  /* FIFO Enable */
#define RV1126B_UART_FCR_RFIFOR       (1 << 1)  /* Receiver FIFO Reset */
#define RV1126B_UART_FCR_XFIFOR       (1 << 2)  /* Transmitter FIFO Reset */
#define RV1126B_UART_FCR_TFT_HALF     (0 << 4)  /* TX FIFO Trigger: Half */
#define RV1126B_UART_FCR_TFT_QUARTER  (1 << 4)  /* TX FIFO Trigger: Quarter (not empty) */
#define RV1126B_UART_FCR_TFT_TWO      (2 << 4)  /* TX FIFO Trigger: 2 entries */
#define RV1126B_UART_FCR_TFT_EMPTY    (3 << 4)  /* TX FIFO Trigger: Empty */
#define RV1126B_UART_FCR_RT_ONE       (0 << 6)  /* RX FIFO Trigger: 1 entry */
#define RV1126B_UART_FCR_RT_QUARTER   (1 << 6)  /* RX FIFO Trigger: Quarter */
#define RV1126B_UART_FCR_RT_HALF      (2 << 6)  /* RX FIFO Trigger: Half */
#define RV1126B_UART_FCR_RT_TWO_LESS  (3 << 6)  /* RX FIFO Trigger: 2 less than full */

/* LCR bit definitions */

#define RV1126B_UART_LCR_DLS_5        0x00      /* Data Length: 5 bits */
#define RV1126B_UART_LCR_DLS_6        0x01      /* Data Length: 6 bits */
#define RV1126B_UART_LCR_DLS_7        0x02      /* Data Length: 7 bits */
#define RV1126B_UART_LCR_DLS_8        0x03      /* Data Length: 8 bits */
#define RV1126B_UART_LCR_STOP_1       (0 << 2)  /* 1 Stop Bit */
#define RV1126B_UART_LCR_STOP_2       (1 << 2)  /* 1.5 or 2 Stop Bits */
#define RV1126B_UART_LCR_PEN          (1 << 3)  /* Parity Enable */
#define RV1126B_UART_LCR_EPS          (1 << 4)  /* Even Parity Select */
#define RV1126B_UART_LCR_SP           (1 << 5)  /* Stick Parity */
#define RV1126B_UART_LCR_BC           (1 << 6)  /* Break Control */
#define RV1126B_UART_LCR_DLAB         (1 << 7)  /* Divisor Latch Access Bit */

/* MCR bit definitions */

#define RV1126B_UART_MCR_DTR          (1 << 0)  /* Data Terminal Ready */
#define RV1126B_UART_MCR_RTS          (1 << 1)  /* Request to Send */
#define RV1126B_UART_MCR_OUT1         (1 << 2)  /* Out 1 */
#define RV1126B_UART_MCR_OUT2         (1 << 3)  /* Out 2 */
#define RV1126B_UART_MCR_LOOPBACK     (1 << 4)  /* Loopback Mode */
#define RV1126B_UART_MCR_AFCE         (1 << 5)  /* Auto Flow Control Enable */
#define RV1126B_UART_MCR_SIRE         (1 << 6)  /* SIR Mode Enable */

/* LSR bit definitions */

#define RV1126B_UART_LSR_DR           (1 << 0)  /* Data Ready */
#define RV1126B_UART_LSR_OE           (1 << 1)  /* Overrun Error */
#define RV1126B_UART_LSR_PE           (1 << 2)  /* Parity Error */
#define RV1126B_UART_LSR_FE           (1 << 3)  /* Framing Error */
#define RV1126B_UART_LSR_BI           (1 << 4)  /* Break Interrupt */
#define RV1126B_UART_LSR_THRE         (1 << 5)  /* Transmit Holding Register Empty */
#define RV1126B_UART_LSR_TEMT         (1 << 6)  /* Transmitter Empty */
#define RV1126B_UART_LSR_RFE          (1 << 7)  /* Receiver FIFO Error */

/* USR bit definitions */

#define RV1126B_UART_USR_BUSY         (1 << 0)  /* UART Busy */
#define RV1126B_UART_USR_TFNF         (1 << 1)  /* Transmit FIFO Not Full */
#define RV1126B_UART_USR_TFE          (1 << 2)  /* Transmit FIFO Empty */
#define RV1126B_UART_USR_RFNE         (1 << 3)  /* Receive FIFO Not Empty */
#define RV1126B_UART_USR_RFF          (1 << 4)  /* Receive FIFO Full */

/* SRR bit definitions */

#define RV1126B_UART_SRR_UR           (1 << 0)  /* UART Reset */
#define RV1126B_UART_SRR_RFR          (1 << 1)  /* Receiver FIFO Reset */
#define RV1126B_UART_SRR_XFR          (1 << 2)  /* Transmitter FIFO Reset */

#endif /* __BOARD_HARDWARE_RV1126B_UART_H */
