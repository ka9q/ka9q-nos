/* HDLC envelope constants */
#define HDLC_ENVLEN	8	/* Max bytes for HDLC envelope (outgoing) */

#define HDLC_FLAG	0x7e	/* HDLC async start/stop flag */
#define HDLC_ESC_ASYNC	0x7d	/* HDLC transparency escape flag for async */
#define HDLC_ESC_COMPL	0x20	/* HDLC transparency bit complement mask */

#define HDLC_FCS_START	0xffff	/* Starting bit string for FCS calculation */
#define HDLC_FCS_FINAL	0xf0b8	/* FCS when summed over frame and sender FCS */
#define PPP_IP_PROTOCOL		0x0021	/* Internet Protocol */
#define PPP_COMPR_PROTOCOL	0x002d	/* Van Jacobson Compressed TCP/IP */
#define PPP_UNCOMP_PROTOCOL	0x002f	/* Van Jacobson Uncompressed TCP/IP */

void sppp_rx(int dev,void *p1,void *p2);
int sppp_send(struct mbuf **bpp,struct iface *ifp,int32 gateway,uint8 tos);
int sppp_output(struct iface *iface,int protocol,struct mbuf **bpp);
int sppp_init(struct iface *ifp);
int sppp_free(struct iface *ifp);
