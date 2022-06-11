/* PC DMA support functions. Copyright 1992 Phil Karn
 *
 * The functions dma_map() and dma_unmap()
 * support the virtual->physical address translations required for
 * PC DMA, either in a real mode machine or on a 386/486 running
 * a memory manager with VDS (Virtual DMA Services) like Quarterdeck's
 * QEMM386.
 *
 * Only the "lock" and "unlock" VDS calls are used, and only to
 * check the suitability of the user's buffer for DMA. If it is not
 * suitable, the automatic DMA buffer allocation/copy mechanism in VDS,
 * as nice as it is, is *not* used since QEMM seems to have only one such
 * buffer. It would be begging for deadlock were we to use it heavily in
 * multitasking, especially for HDLC receive operations that always have
 * to have a buffer allocated.
 *
 * So we implement our own dynamic DMA buffering, using conventional
 * memory on the heap that is checked for DMA "correctness", and
 * without limiting the number of simultaneous DMA operations.
 */

#include <stdio.h>
#include <dos.h>
#include "global.h"
#include "dma.h"
#include "nospc.h"

/* I/O port addresses for DMA page registers on the PC/AT */
static uint16 Page_regs[] = {
	0x87,0x83,0x81,0x82,0x8f,0x8b,0x89,0x8a
};

/* Address to check to see if QEMM or other VDS manager is running.
 * God, I hate peeking at absolute memory, but Microsoft says I have to...
 */
char *Vds_run = MK_FP(0x40,0x7b);

/* Structure used to communicate with Virtual DMA Service (VDS) in
 * QEMM, etc
 */
struct dds {
	unsigned long rsize;
	unsigned long offset;
	unsigned short seg;
	unsigned short buf_id;
	unsigned long physaddr;
};

/* Descriptor used to keep track of the auxiliary buffer when the user's
 * own buffer isn't suitable for hardware DMA
 */
struct dma {
	struct dma *next;	/* Linked list pointers */
	struct dma *prev;
	void *p;		/* virtual address of user's original buffer */
	unsigned short len;
	void *aux;		/* Virtual address of aux buffer, if any */
	unsigned long physaddr;	/* Physical address of actual buffer */
};
#define	NULLDMA	(struct dma *)0
struct dma *Dma;	/* List of active DMA descriptors */

/* Allocate a block of memory suitable for DMA */
void *
dma_malloc(physaddr,len)
int32 *physaddr;	/* Return physical address thru here */
unsigned short len;
{
	void *bufs[20],*aux;
	int i,tries;

	for(tries=0;tries<20;tries++){
		if((aux = bufs[tries] = malloc(len)) == NULL)
			break;
		if((*physaddr = dmalock(aux,len)) != 0)
			break;	/* Good buffer */
		if((*physaddr >> 24) == 0){
			/* Good buffer outside bus-accessible memory
			 * (can this happen?)
			 */
			dmaunlock(*physaddr,len);
			*physaddr = 0;
		}
	}
	/* At this point, we've either got a good buffer (physaddr != 0) or
	 * we gave up or failed (physaddr == 0). Free all unsuitable buffers.
	 */
	for(i=0;i<tries;i++)
		free(bufs[i]);

	if(*physaddr == 0){
		/* Failure, clean up */
		free(aux);
		return NULL;
	}
	/* Success */
	return aux;
}

/* Convert user's virtual address to a physical address suitable for DMA */
unsigned long
dma_map(p,len,copy)
void *p;		/* User's virtual address */
unsigned short len;	/* Length of user's buffer */
int copy;	/* !0 => Copy user buffer to aux buffer if allocated */
{
	void *bufs[20],*aux;
	int i,tries;
	unsigned long physaddr;
	struct dma *dmap;

	/* Create descriptor */
	dmap = calloc(1,sizeof(struct dma));
	dmap->p = p;
	dmap->len = len;
	dmap->next = Dma;
	if(Dma->next != NULL)
		Dma->next->prev = dmap;
	Dma = dmap;

	if((physaddr = dmalock(p,len)) != 0){
		dmap->physaddr = physaddr;
		return physaddr;	/* User's buffer is OK for DMA */
	}
	/* Try to malloc a suitable buffer. Hold onto the unsuitable
	 * ones to make sure malloc doesn't give them right back to us.
	 */
	for(tries=0;tries<20;tries++){
		if((aux = bufs[tries] = malloc(len)) == NULL)
			break;
		if((physaddr = dmalock(aux,len)) != 0)
			break;	/* Good buffer */
	}
	/* At this point, we've either got a good buffer (physaddr != 0) or
	 * we gave up or failed (physaddr == 0). Free all unsuitable buffers.
	 */
	for(i=0;i<tries;i++)
		free(bufs[i]);

	if(physaddr == 0){
		/* Failure, clean up */
		free(aux);
		if(dmap->next != NULL)
			dmap->next->prev = dmap->prev;
		if(dmap->prev != NULL)
			dmap->prev->next = dmap->next;
		else
			Dma = dmap->next;
		free(dmap);
		return 0;
	}
	/* Success */
	if(copy)
		memcpy(aux,p,len);

	dmap->aux = aux;
	dmap->physaddr = physaddr;
	return physaddr;
}
/* When done with DMA, the user calls this function, again with his
 * buffer's virtual address. We free any auxiliary buffers, after copying them
 * out (if requested) and then if QEMM is running, call the unlock function.
 */
void
dma_unmap(p,copy)
void *p;
int copy;	/* !0 => Copy aux buffer to user buffer, if mapped */
{
	struct dma *dmap;

	for(dmap = Dma;dmap != NULLDMA;dmap = dmap->next){
		if(dmap->p == p)
			break;
	}
	if(dmap == NULLDMA)
		return;	/* Unknown */

	if((*Vds_run & 0x20) != 0)
		dmaunlock(dmap->physaddr,dmap->len);	/* VDS call */

	/* Copy aux buffer to user's buffer, if requested, and free */
	if(dmap->aux != NULL){
		if(copy)
			memcpy(dmap->p,dmap->aux,dmap->len);
		free(dmap->aux);
	}
	/* Free DMA descriptor */
	if(dmap->next != NULLDMA)
		dmap->next->prev = dmap->prev;
	if(dmap->prev != NULLDMA)
		dmap->prev->next = dmap->next;
	else
		Dma = dmap->next;
	free(dmap);
}

/* Translate a virtual address and length into a physical address for DMA.
 * If QEMM or another VDS service is available, use it. Otherwise
 * assume real mode and do the translation ourselves.
 *
 * If the physical buffer crosses a physical 64K memory
 * boundary, or if it does not sit in contiguous 4K 386/486 pages, then
 * return an error.
 *
 * Since we set the flag that says "don't remap", we don't need to keep
 * the DDS structure as modified by the lock call. The only information
 * needed to unlock is the address and length, which the user will supply.
 */
unsigned long
dmalock(p,len)
void *p;
unsigned short len;
{
	struct dds ddsp;
	union REGS regs;
	struct SREGS segregs;
	unsigned long physaddr;

	if((*Vds_run & 0x20) == 0){
		/* VDS not available, assume real mode. Convert
		 * to flat address and check for 64K alignment
		 */
		physaddr = ((unsigned long)FP_SEG(p) << 4) + FP_OFF(p);
		if((((physaddr + len) ^ physaddr) & ~0xffff) != 0)
			physaddr = 0;	/* NFG, crosses 64K segment */
		return physaddr;
	}
	/* VDS available, use it */
	ddsp.rsize = len;
	ddsp.offset = FP_OFF(p);
	ddsp.seg = FP_SEG(p);

	regs.x.ax = 0x8103;
	/* Region must not cross 64K, be contiguous in physical memory,
	 * and do *not* allocate DMA buffer if it fails
	 */
	regs.x.dx = 20;
	segregs.es = FP_SEG(&ddsp);
	regs.x.di = FP_OFF(&ddsp);
	int86x(0x4b,&regs,&regs,&segregs);
	if(regs.x.cflag)
		return 0;

	return ddsp.physaddr;
}
/* Release memory that has been locked for DMA */
unsigned long
dmaunlock(physaddr,len)
unsigned long physaddr;
unsigned short len;
{
	union REGS regs;
	struct SREGS segregs;
	struct dds dds;

	if((*Vds_run & 0x20) == 0)
		return 0;	/* Nothing required */
	dds.rsize = len;
	dds.physaddr = physaddr;
	dds.buf_id = 0;
	dds.seg = 0;	/* We assume these are don't cares? */
	dds.offset = 0;

	regs.x.ax = 0x8104;
	regs.x.dx = 0;
	segregs.es = FP_SEG(&dds);
	regs.x.di = FP_OFF(&dds);
	int86x(0x4b,&regs,&regs,&segregs);
	if(regs.x.cflag)
		return -1;

	return 0;
}
/* Disable QEMM DMA translation */
int
dis_dmaxl(chan)
int chan;	/* DMA channel number */
{
	union REGS regs;
	struct SREGS segregs;

	if((*Vds_run & 0x20) == 0)
		return 0;	/* QEMM not running */
	regs.x.ax = 0x810b;
	regs.x.bx = chan;
	regs.x.dx = 0;
	int86x(0x4b,&regs,&regs,&segregs);
	if(regs.x.cflag)
		return -1;

	return 0;
}

/* Re-enable QEMM DMA translation */
int
ena_dmaxl(chan)
int chan;
{
	union REGS regs;
	struct SREGS segregs;

	if((*Vds_run & 0x20) == 0)
		return 0;	/* QEMM not running */
	regs.x.ax = 0x810c;
	regs.x.bx = chan;
	regs.x.dx = 0;
	int86x(0x4b,&regs,&regs,&segregs);
	if(regs.x.cflag)
		return -1;

	return 0;
}

/* Set up a 8237 DMA controller channel to point to a specified buffer */
int
setup_dma(chan,physaddr,length,mode)
int chan;
int32 physaddr;
uint16 length;
int mode;	/* Read/write, etc */
{
	int dmaport;
	int i_state;

	if(length == 0 || chan < 0 || chan > 7 || chan == 4)
		return -1;

	i_state = dirps();
	dma_disable(chan);
	outportb(Page_regs[chan],physaddr >> 16); /* Store in 64K DMA page */
	if(chan < 4){
		/* 8-bit DMA */
		length--;
		outportb(DMA1BASE+DMA_MODE,mode|chan);	/* Select mode */
		outportb(DMA1BASE+DMA_RESETFF,0);	 /* reset byte pointer flipflop */

		/* Output buffer start (dest) address */
		dmaport = DMA1BASE + 2*chan;
		outportb(dmaport,(uint8)physaddr);
		outportb(dmaport,(uint8)(physaddr >> 8));

		/* output DMA maximum byte count */
		dmaport++;
		outportb(dmaport,(uint8)length);
		outportb(dmaport,(uint8)(length >> 8));
	} else {
		/* 16-bit DMA */
		length >>= 1;	/* count is 16-bit words */
		length--;
		physaddr >>= 1;

		outportb(DMA2BASE+2*DMA_MODE,mode|(chan & 3));/* Select mode */
		outportb(DMA2BASE+2*DMA_RESETFF,0);	 /* reset byte pointer flipflop */

		/* Output buffer start (dest) address */
		dmaport = DMA2BASE + 4*(chan & 3);
		outportb(dmaport,(uint8)physaddr);
		outportb(dmaport,(uint8)(physaddr >> 8));

		/* output DMA maximum byte count */
		dmaport += 2;
		outportb(dmaport,(uint8)length);
		outportb(dmaport,(uint8)(length >> 8));
	}
	/* Unmask channel (start DMA) */
	dma_enable(chan);
	restore(i_state);
	return 0;
}

/* Return current count on specified DMA channel */
uint16
dma_cnt(chan)
int chan;
{
	int dmaport;
	uint16 bytecount;

	if(chan < 4){
		outportb(DMA1BASE+DMA_RESETFF,0); /* reset firstlast ff */
		dmaport = DMA1BASE + 2*chan + 1;
	} else {
		outportb(DMA2BASE+2*DMA_RESETFF,0);
		dmaport = DMA2BASE + 4*(chan&3) + 2;
	}
	bytecount = inportb(dmaport);
	bytecount += inportb(dmaport) << 8;
	return bytecount;
}

/* Disable DMA on specified channel, return previous status */
int
dma_disable(chan)
int chan;
{
	if(chan < 4){
		outportb(DMA1BASE+DMA_MASK, DMA_DISABLE|chan);
	} else {
		outportb(DMA2BASE+2*DMA_MASK, DMA_DISABLE|(chan & 3));
	}
	return 0;
}
/* Enable DMA on specified channel */
int
dma_enable(chan)
int chan;
{
	if(chan < 4){
		outportb(DMA1BASE+DMA_MASK, DMA_ENABLE|chan);
	} else {
		outportb(DMA2BASE+2*DMA_MASK,DMA_ENABLE|(chan & 3));
	}
	return 0;
}
