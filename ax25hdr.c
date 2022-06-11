/* AX25 header conversion routines
 * Copyright 1991 Phil Karn, KA9Q
 */
#include "global.h"
#include "mbuf.h"
#include "ax25.h"

/* Convert a host-format AX.25 header into a mbuf ready for transmission */
void
htonax25(
struct ax25 *hdr,
struct mbuf **bpp
){
	register uint8 *cp;
	register uint16 i;

	if(hdr == (struct ax25 *)NULL || hdr->ndigis > MAXDIGIS || bpp == NULL)
		return;

	/* Allocate space for return buffer */
	i = AXALEN * (2 + hdr->ndigis);
	pushdown(bpp,NULL,i);

	/* Now convert */
	cp = (*bpp)->data;		/* cp -> dest field */

	/* Generate destination field */
	memcpy(cp,hdr->dest,AXALEN);
	if(hdr->cmdrsp == LAPB_COMMAND)
		cp[ALEN] |= C;	/* Command frame sets C bit in dest */
	else
		cp[ALEN] &= ~C;
	cp[ALEN] &= ~E;	/* Dest E-bit is always off */

	cp += AXALEN;		/* cp -> source field */

	/* Generate source field */
	memcpy(cp,hdr->source,AXALEN);
	if(hdr->cmdrsp == LAPB_RESPONSE)
		cp[ALEN] |= C;
	else
		cp[ALEN] &= ~C;
	/* Set E bit on source address if no digis */
	if(hdr->ndigis == 0){
		cp[ALEN] |= E;
		return;
	}

	cp += AXALEN;		/* cp -> first digi field */

	/* All but last digi get copied with E bit off */
	for(i=0; i < hdr->ndigis; i++){
		memcpy(cp,hdr->digis[i],AXALEN);
		if(i < hdr->ndigis - 1)
			cp[ALEN] &= ~E;
		else
			cp[ALEN] |= E;	/* Last digipeater has E bit set */
		if(i < hdr->nextdigi)
			cp[ALEN] |= REPEATED;
		else
			cp[ALEN] &= ~REPEATED;
		cp += AXALEN;		/* cp -> next digi field */
	}
}
/* Convert a network-format AX.25 header into a host format structure
 * Return -1 if error, number of addresses if OK
 */
int
ntohax25(
register struct ax25 *hdr,	/* Output structure */
struct mbuf **bpp
){
	register uint8 *axp;

	if(pullup(bpp,hdr->dest,AXALEN) < AXALEN)
		return -1;

	if(pullup(bpp,hdr->source,AXALEN) < AXALEN)
		return -1;

	/* Process C bits to get command/response indication */
	if((hdr->source[ALEN] & C) == (hdr->dest[ALEN] & C))
		hdr->cmdrsp = LAPB_UNKNOWN;
	else if(hdr->source[ALEN] & C)
		hdr->cmdrsp = LAPB_RESPONSE;
	else
		hdr->cmdrsp = LAPB_COMMAND;

	hdr->ndigis = 0;
	hdr->nextdigi = 0;
	if(hdr->source[ALEN] & E)
		return 2;	/* No digis */

	/* Count and process the digipeaters */
	axp = hdr->digis[0];
	while(hdr->ndigis < MAXDIGIS && pullup(bpp,axp,AXALEN) == AXALEN){
		hdr->ndigis++;
		if(axp[ALEN] & REPEATED)
			hdr->nextdigi++;
		if(axp[ALEN] & E)	/* Last one */
			return hdr->ndigis + 2;			
		axp += AXALEN;
	}
	return -1;	/* Too many digis */
}

