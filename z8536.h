#ifndef	_8536_H
#define	_8536_H

/* 8536 register definitions */

#define CIO_MICR	0x00	/* Master interrupt control register */
#define CIO_MCCR	0x01	/* Master configuration control register */
#define CIO_CTMS1	0x1c	/* Counter/timer mode specification #1 */
#define CIO_CTMS2	0x1d	/* Counter/timer mode specification #2 */
#define CIO_CTMS3	0x1e	/* Counter/timer mode specification #3 */
#define CIO_IVR 	0x04	/* Interrupt vector register */

#define CIO_CSR1	0x0a	/* Command and status register CTC #1 */
#define CIO_CSR2	0x0b	/* Command and status register CTC #2 */
#define CIO_CSR3	0x0c	/* Command and status register CTC #3 */

#define CIO_CT1MSB	0x16	/* CTC #1 Timer constant - MSB */
#define CIO_CT1LSB	0x17	/* CTC #1 Timer constant - LSB */
#define CIO_CT2MSB	0x18	/* CTC #2 Timer constant - MSB */
#define CIO_CT2LSB	0x19	/* CTC #2 Timer constant - LSB */
#define CIO_CT3MSB	0x1a	/* CTC #3 Timer constant - MSB */
#define CIO_CT3LSB	0x1b	/* CTC #3 Timer constant - LSB */
#define CIO_PDCA	0x23	/* Port A data direction control */
#define CIO_PDCB	0x2b	/* Port B data direction control */

#define CIO_GCB 	0x04	/* CTC Gate command bit */
#define CIO_TCB 	0x02	/* CTC Trigger command bit */
#define CIO_IE		0xc0	/* CTC Interrupt enable (set) */
#define CIO_CIP 	0x20	/* CTC Clear interrupt pending */
#define CIO_IP		0x20	/* CTC Interrupt pending */

#endif	/* _8536_H */
