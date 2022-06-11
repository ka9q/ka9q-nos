int setup_dma(int chan, int32 physaddr, uint16 length,int mode);
uint16 dma_cnt(int chan);
int dma_disable(int chan);
int dma_enable(int chan);
void *dma_malloc(int32 *,unsigned short);
int dis_dmaxl(int);
int ena_dmaxl(int);

#define	DMA1BASE	0	/* Base address of 1st (8-bit) DMA controller */
#define	DMA2BASE	0xc0	/* Base address of 2nd (16-bit) DMA controller */
#define DMAEN	0x4	/* Offset off DMA Enable register */

/* DMA controller registers */
#define DMA_STAT	8	/* DMA controller status register */
#define DMA_MASK	0xa	/* DMA controller mask register	*/
#define DMA_MODE	0xb	/* DMA controller mode register	*/
#define DMA_RESETFF	0xc	/* DMA controller first/last flip flop	*/
/* DMA data */
#define DMA_DISABLE	4	/* Disable channel n */
#define DMA_ENABLE	0	/* Enable channel n */
/* Single transfers, incr. address, auto init, writes, ch. n */
#define DMA_RX_MODE	0x54
/* Single transfers, incr. address, no auto init, reads, ch. n */
#define DMA_TX_MODE	0x48

