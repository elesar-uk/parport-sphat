#ifndef	PARPORT_SPHAT_H
#define	PARPORT_SPHAT_H

/* Product id */
#define ELESAR_SPHAT_PID        20       /* P/N 8067660000020 */
#define ELESAR_VENDOR_STR       "Elesar Limited"

/* HAT pin assignments */
#define HAT_CONTROL_NSTROBE     9        /* Outputs */
#define HAT_CONTROL_NAUTOLF     10
#define HAT_CONTROL_INIT        11
#define HAT_CONTROL_NSELECT     12
#define HAT_CONTROL_BIDI        13       
#define HAT_CONTROL_MAP(k)      (((((k) & 0xF) << 9) | (((k) & 0x20) << 8)) ^ 0x2800)
#define HAT_CONTROL_UNMAP(k)    (((((k) >> 9) & 0xF) | (((k) >> 8) & 0x20)) ^ 0x24)
#define HAT_CONTROL_MASK        (0x1F<<HAT_CONTROL_NSTROBE)
#define HAT_DATA_SHIFT          20       /* Data */
#define HAT_DATA_MASK           (0xFF<<HAT_DATA_SHIFT)
#define HAT_DATA_BITS           8
#define HAT_STATUS_ERROR        4        /* Inputs */
#define HAT_STATUS_SELECT       5
#define HAT_STATUS_PAPEROUT     6
#define HAT_STATUS_ACK          7
#define HAT_STATUS_NBUSY        8
#define HAT_STATUS_UNMAP(k)     ((((k) >> 1) & 0xF8) ^ 0x80)
#define HAT_STATUS_MASK         (0x1F<<HAT_STATUS_NERROR)
#define HAT_DETECT              19       /* Loop back from HAT_CONTROL_BIDI */

/* Original PC parallel register layouts */
#define LPT_REG_DATA            0
#define LPT_REG_STATUS          1
#define LPT_REG_STATUS_NBUSY    (1<<7)   /* 1=ready; 0=busy */
#define LPT_REG_STATUS_ACK      (1<<6)   /* 1=ack line high; 0=ack line low */
#define LPT_REG_STATUS_PAPEROUT (1<<5)   /* 1=no paper; 0=have paper */
#define LPT_REG_STATUS_SELECT   (1<<4)   /* 1=printer online; 0=offline */
#define LPT_REG_STATUS_ERROR    (1<<3)   /* 1=no error; 0=error */
#define LPT_REG_STATUS_NIRQ     (1<<2)   /* 1=no IRQ; 0=IRQ */
#define LPT_REG_STATUS_MASK     0xFC
#define LPT_REG_CONTROL         2
#define LPT_REG_CONTROL_BIDI    (1<<5)   /* 1=input; 0=output */
#define LPT_REG_CONTROL_IRQEN   (1<<4)   /* 1=enabled; 0=disabled */
#define LPT_REG_CONTROL_NSELECT (1<<3)   /* 1=select printer; 0=offline */
#define LPT_REG_CONTROL_INIT    (1<<2)   /* 1=normal; 0=reset */
#define LPT_REG_CONTROL_NAUTOLF (1<<1)   /* 1=auto LF; 0=no auto LF */
#define LPT_REG_CONTROL_NSTROBE (1<<0)   /* 1=strobe; 0=no strobe */
#define LPT_REG_CONTROL_DEFAULT (LPT_REG_CONTROL_INIT | LPT_REG_CONTROL_NSELECT)
#define LPT_REG_CONTROL_MASK    0x3F

#endif
