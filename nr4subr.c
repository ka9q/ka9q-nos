/*
 * nr4subr.c:  subroutines for net/rom transport layer.
 * Copyright 1989 by Daniel M. Frank, W9NK.  Permission granted for
 * non-commercial distribution only.
 */
 
#include <stdio.h>
#include "global.h"
#include "mbuf.h"
#include "timer.h"
#include "ax25.h"
#include "netrom.h"
#include "nr4.h"
#include "lapb.h"
#include <ctype.h>



/* Get a free circuit table entry, and allocate a circuit descriptor.
 * Initialize control block circuit number and ID fields.
 * Return a pointer to the circuit control block if successful,
 * NULL if not.
 */

struct nr4cb *
new_n4circ()
{
	int i ;
	struct nr4cb *cb ;

	for (i = 0 ; i <  NR4MAXCIRC ; i++)		/* find a free circuit */
		if (Nr4circuits[i].ccb == NULL)
			break ;

	if (i == NR4MAXCIRC)	/* no more circuits */
		return NULL ;

	cb = Nr4circuits[i].ccb =
		 (struct nr4cb *)callocw(1,sizeof(struct nr4cb));
	cb->mynum = i ;
	cb->myid = Nr4circuits[i].cid ;
	return cb ;
}


/* Set the window size for a circuit and allocate the buffers for
 * the transmit and receive windows.  Set the control block window
 * parameter.  Return 0 if successful, -1 if not.
 */

int
init_nr4window(cb, window)
struct nr4cb *cb ;
unsigned window ;
{
	
	if (window == 0 || window > NR4MAXWIN) /* reject silly window sizes */
		return -1 ;
		
	cb->txbufs =
		 (struct nr4txbuf *)callocw(window,sizeof(struct nr4txbuf));

	cb->rxbufs =
		 (struct nr4rxbuf *)callocw(window,sizeof(struct nr4rxbuf));

	cb->window = window ;
	
	return 0 ;
}


/* Free a circuit.  Deallocate the control block and buffers, and
 * increment the circuit ID.  No return value.
 */

void
free_n4circ(cb)
struct nr4cb *cb ;
{
	unsigned circ ;

	if (cb == NULL)
		return ;

	circ = cb->mynum ;
	
	if (cb->txbufs != (struct nr4txbuf *)0)
		free(cb->txbufs) ;

	if (cb->rxbufs != (struct nr4rxbuf *)0)
		free(cb->rxbufs) ;

	/* Better be safe than sorry: */

	free_q(&cb->txq) ;
	free_q(&cb->rxq) ;
	
	free(cb) ;

	if (circ > NR4MAXCIRC)		/* Shouldn't happen. */
		return ;
		
	Nr4circuits[circ].ccb = NULL ;

	Nr4circuits[circ].cid++ ;
}

/* See if any open circuit matches the given parameters.  This is used
 * to prevent opening multiple circuits on a duplicate connect request.
 * Returns the control block address if a match is found, or NULL
 * otherwise.
 */

struct nr4cb *
match_n4circ(index, id, user, node)
int index ;					/* index of remote circuit */
int id ;					/* id of remote circuit */
uint8 *user ;	/* address of remote user */
uint8 *node ;	/* address of originating node */
{
	int i ;
	struct nr4cb *cb ;

	for (i = 0 ; i < NR4MAXCIRC ; i++) {
		if ((cb = Nr4circuits[i].ccb) == NULL)
			continue ;		/* not an open circuit */
		if (cb->yournum == index && cb->yourid == id
		    && addreq(cb->remote.user,user)
		    && addreq(cb->remote.node,node))
			return cb ;
	}
	/* if we get to here, we didn't find a match */

	return NULL ;
}

/* Validate the index and id of a local circuit, returning the control
 * block if it is valid, or NULL if it is not.
 */

struct nr4cb *
get_n4circ(index, id)
int index ;				/* local circuit index */
int id ;				/* local circuit id */
{
	struct nr4cb *cb ;

	if (index >= NR4MAXCIRC)
		return NULL ;

	if ((cb = Nr4circuits[index].ccb) == NULL)
		return NULL ;

	if (cb->myid == id)
		return cb ;
	else
		return NULL ;
}

/* Return 1 if b is "between" (modulo the size of an unsigned char)
 * a and c, 0 otherwise.
 */

int
nr4between(a, b, c)
unsigned a, b, c ;
{
	if ((a <= b && b < c) || (c < a && a <= b) || (b < c && c < a))
		return 1 ;
	else
		return 0 ;
}

/* Set up default timer values, etc., in newly connected control block.
 */

void
nr4defaults(cb)
struct nr4cb *cb ;
{
	int i ;
	struct timer *t ;

	if (cb == NULL)
		return ;

	/* Set up the ACK and CHOKE timers */
	
	set_timer(&cb->tack,Nr4acktime) ;
	cb->tack.func = nr4ackit ;
	cb->tack.arg = cb ;

	set_timer(&cb->tchoke,Nr4choketime) ;
	cb->tchoke.func = nr4unchoke ;
	cb->tchoke.arg = cb ;

	cb->rxpastwin = cb->window ;

	/* Don't actually set the timers, since this is done */
	/* in nr4sbuf */
	
	for (i = 0 ; i < cb->window ; i++) {
		t = &cb->txbufs[i].tretry ;
		t->func = nr4txtimeout ;
		t->arg = cb ;
	}
}

/* See if this control block address is valid */

int
nr4valcb(cb)
struct nr4cb *cb ;
{
	int i ;

	if (cb == NULL)
		return 0 ;
		
	for (i = 0 ; i < NR4MAXCIRC ; i++)
		if (Nr4circuits[i].ccb == cb)
			return 1 ;

	return 0 ;
}

void
nr_garbage(red)
int red;
{
	int i;
	struct nr4cb *ncp;

	for(i=0;i<NR4MAXCIRC;i++){
		ncp = Nr4circuits[i].ccb;
		if(ncp != NULL)
			mbuf_crunch(&ncp->rxq);
	}
}
