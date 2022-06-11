/* Functions for level 3 net/rom support
 * Copyright 1989 Dan Frank, W9NK
 */
#include "global.h"
#include "mbuf.h"
#include "ax25.h"
#include "netrom.h"
#include "lapb.h"
#include <ctype.h>

/* Convert a net/rom network header to host format structure
 * Return -1 if error, 0 if OK
 */

int
ntohnr3(hdr,bpp)
register struct nr3hdr *hdr;	/* output structure */
struct mbuf **bpp;
{
	int ttl;
	
	if(pullup(bpp,hdr->source,AXALEN) < AXALEN)
		return -1;

	if(pullup(bpp,hdr->dest,AXALEN) < AXALEN)
		return -1;

	if((ttl = PULLCHAR(bpp)) == -1)
		return -1;

	hdr->ttl = ttl;

	return 0;
}

/* Convert a host-format net/rom level 3 header into an mbuf ready
 * for transmission.
 */

struct mbuf *
htonnr3(hdr)
register struct nr3hdr *hdr;
{
	struct mbuf *rbuf;
	register uint8 *cp;

	if(hdr == (struct nr3hdr *) NULL)
		return NULL;

	/* Allocate space for return buffer */
	if((rbuf = alloc_mbuf(NR3HLEN)) == NULL)
		return NULL;

	rbuf->cnt = NR3HLEN;

	/* Now convert */
	cp = rbuf->data;

	memcpy(cp,hdr->source,AXALEN);
	cp[ALEN] &= ~E;	/* source E-bit is always off */
	cp += AXALEN;
	memcpy(cp,hdr->dest,AXALEN);
	cp[ALEN] |= E;		/* destination E-bit always set */
	cp += AXALEN;
	*cp = hdr->ttl;

	return rbuf;
}

/* Convert a net/rom routing broadcast destination subpacket from
 * network format to a host format structure.  Return -1 if error,
 * 0 if OK.
 */
int
ntohnrdest(ds,bpp)
register struct nr3dest *ds;
struct mbuf **bpp;
{
	int quality;

	/* get destination callsign */
	if(pullup(bpp,ds->dest,AXALEN) < AXALEN)
		return -1;

	/* get destination alias */
	if(pullup(bpp,ds->alias,ALEN) < ALEN)
		return -1;
	ds->alias[ALEN] = '\0';

	/* get best neighbor callsign */
	if(pullup(bpp,ds->neighbor,AXALEN) < AXALEN)
		return -1;

	/* get route quality */
	if((quality = PULLCHAR(bpp)) == -1)
		return -1;
	ds->quality = quality;

	return 0;
}

/* Convert a host-format net/rom destination subpacket into an
 * mbuf ready for transmission as part of a route broadcast
 * packet.
 */
struct mbuf *
htonnrdest(ds)
register struct nr3dest *ds;
{
	struct mbuf *rbuf;
	register uint8 *cp;

	if(ds == (struct nr3dest *) NULL)
		return NULL;

	/* Allocate space for return buffer */
	if((rbuf = alloc_mbuf(NRRTDESTLEN)) == NULL)
		return NULL;

	rbuf->cnt = NRRTDESTLEN;

	cp = rbuf->data;

	memcpy(cp,ds->dest,AXALEN);
	cp += AXALEN;

	memcpy(cp,ds->alias,ALEN);
	cp += ALEN;

	memcpy(cp,ds->neighbor,AXALEN);
	cp += AXALEN;

	*cp = ds->quality;

	return rbuf;
}

