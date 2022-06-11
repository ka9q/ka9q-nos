#ifndef	_AHDLC_H
#define	_AHDLC_H
#include "global.h"
#include "mbuf.h"

/* Asynch HDLC receiver control block */
struct ahdlc {
	int escaped;		/* Escape char (0x7d) just seen */
	int hunt;		/* Flushing input until next flag */
	struct mbuf *inframe;	/* Current frame being reassembled */
	int maxsize;		/* Maximum packet size */
	uint16 fcs;		/* current CRC value */
	int32 rxframes;		/* Valid frames received */
	int32 aborts;		/* Aborts seen */
	int32 toobigs;		/* Frames larger than maxsize */
	int32 crcerrs;		/* Frames with CRC errors */
	int32 runts;		/* Frames shorter than 2 bytes */
};
#define	HDLC_ESC_ASYNC	0x7d	/* Escapes special chars (0x7d, 0x7e) */
#define	HDLC_FLAG	0x7e	/* Ends each frame */
#define	HDLC_ESC_COMPL	0x20	/* XORed with special chars in data */

void init_hdlc(struct ahdlc *,int);
struct mbuf *ahdlcrx(struct ahdlc *,uint8);
struct mbuf *ahdlctx(struct mbuf *);

#endif	/* _AHDLC_H */

