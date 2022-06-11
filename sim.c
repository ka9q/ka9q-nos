/* Simulate a network path by introducing delay (propagation and queuing),
 * bandwidth (delay as a function of packet size), duplication and loss.
 * Intended for use with the loopback interface
 */
#include "global.h"
#include "mbuf.h"
#include "timer.h"
#include "iface.h"
#include "ip.h"

static void simfunc(void *p);

struct pkt {
	struct timer timer;
	struct mbuf *bp;
};

struct {
	int32 fixed;	/* Fixed prop delay, ms */
	int32 xmit;	/* Xmit time, ms/byte */
	int32 maxq;	/* Max queueing delay, ms */
	int pdup;	/* Probability of duplication, *0.1% */
	int ploss;	/* Probability of loss, *0.1% */
} Simctl = {
	0,0,1000,0,0 };
int
dosim(argc,argv,p)
int argc;
char *argv;
void *p;
{
	return 0;
}

void
net_sim(bp)
struct mbuf *bp;
{
	struct pkt *pkt;
	int32 delay;

	if(urandom(1000) < Simctl.ploss){
		if(Loopback.trfp)
			fprintf(Loopback.trfp,"packet lost\n");
		free_p(&bp);	/* Packet is lost */
		return;
	}
	if(urandom(1000) < Simctl.pdup){
		struct mbuf *dbp;
		if(Loopback.trfp)
			fprintf(Loopback.trfp,"packet duped\n");
		dup_p(&dbp,bp,0,len_p(bp));
		net_sim(dbp);	/* Packet is duplicated */
	}
	/* The simulated network delay for this packet is the sum
	 * of three factors: a fixed propagation delay, a transmission
	 * delay proportional to the packet size, and an evenly
	 * distributed random queuing delay up to some maximum
	 */
	delay = Simctl.fixed + len_p(bp)*Simctl.xmit + urandom(Simctl.maxq);
	if(Loopback.trfp)
		fprintf(Loopback.trfp,"packet delayed %ld ms\n",delay);
	if(delay == 0){
		/* No delay, return immediately */
		net_route(&Loopback,&bp);
		return;
	}
	pkt = (struct pkt *)mallocw(sizeof(struct pkt));
	pkt->bp = bp;
	set_timer(&pkt->timer,delay);
	pkt->timer.func = simfunc;
	pkt->timer.arg = pkt;
	start_timer(&pkt->timer);
}
/* Put packet on hopper after delay */
static void
simfunc(p)
void *p;
{
	struct pkt *pkt = (struct pkt *)p;
	struct mbuf *bp = pkt->bp;

	stop_timer(&pkt->timer);	/* shouldn't be necessary */
	net_route(&Loopback,&bp);
	free(pkt);
}
