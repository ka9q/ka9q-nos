#define	SB_RESET	6
#define	SB_READ_DATA	0xa
#define	SB_WB		0xc
#define	SB_WB_STAT	0xc
#define	SB_RB_STAT	0xe
#define	SB_MIX_INDEX	4
#define	SB_MIX_DATA	5

#define	SB_IS		0x82	/* Interrupt status */

#define	SB_MV		0x30
#define	SB_DAC		0x32
#define	SB_MIDI		0x34
#define	SB_CD		0x36
#define	SB_LINE		0x38
#define	SB_MIKE		0x3a
#define	SB_PCSPKR	0x3b
#define	SB_OUTMIX	0x3c
#define	SB_INMIXL	0x3d
#define	SB_INMIXR	0x3e
#define	SB_ADC		0x3f
#define	SB_OUTG		0x41
#define	SB_MIKAGC	0x43
#define	SB_TREB		0x44
#define	SB_BASS		0x46

void sbshut(void);

