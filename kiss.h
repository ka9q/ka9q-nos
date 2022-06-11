#ifndef	_KISS_H
#define	_KISS_H

#ifndef	_MBUF_H
#include "mbuf.h"
#endif

#ifndef	_IFACE_H
#include "iface.h"
#endif

/* In kiss.c: */
int kiss_free(struct iface *ifp);
int kiss_raw(struct iface *iface,struct mbuf **data);
void kiss_recv(struct iface *iface,struct mbuf **bp);
int kiss_init(struct iface *ifp);
int32 kiss_ioctl(struct iface *iface,int cmd,int set,int32 val);
void kiss_recv(struct iface *iface,struct mbuf **bp);

#endif	/* _KISS_H */
