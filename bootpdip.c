/*
 * Center for Information Technology Integration
 *           The University of Michigan
 *                    Ann Arbor
 *
 * Dedicated to the public domain.
 * Send questions to info@citi.umich.edu
 * BOOTP is documented in RFC 951 and RFC 1048
 *
 * Delinted, ANSIfied and reformatted - 5/30/91 P. Karn
 */



/* Dynamic Ip Assignment for a Bootp Server
 * Called when a client request is received and the bootp server doesnt' have a
 * record for it.
 *
 * Design goals:
 *   Assign an IP address
 *   Separation/Identification of IP addresses assigned and not assigned
 *   Time out mechanism to reclaim IP address
 *	Timer, and arp on address with little activity
 *   Reassignment to same machine if possible.
 */     
#include <stdio.h>
#include <time.h>	

#include "global.h"
#include "arp.h"
#include "iface.h"
#include "mbuf.h"
#include "netuser.h"
#include "pktdrvr.h"
#include "timer.h"
#include "bootpd.h"


#define E_NOMEM 3101
#define ERR_NOIPADDRESS	3103		/* No IP address available. */

#define THRESH_ON            20    /* (%) When to turn on reclaimation of IP addresses. */
#define THRESH_CRITICAL       2    /* (#) */
#define THRESH_OFF           50    /* (%) */

#define R_OFF                   0x01    /* Reclaimation is off. */
#define R_RECLAIM               0x02    /* Reclaimation is on. */
#define R_CRITICAL              0x04    /* Reclaimation is operating in critical state. */
#define R_DONE                  0x08    /* Reclaimation is finishing up. */
#define V_SWAIT                 0x10    /* Reclaimation is wait to start verif cycle. */
#define V_VERIFY                0x20    /* Reclaimation is in verification cycle. */

#define TIME_RWAIT		(5) 	/* Time between running reclm da_task */
#define TIME_SWAIT              (30)    /* Time between cycles of starting address rec */
#define TIME_VWAIT              (10)    /* Time to wait between sending ARPs to verify add
resses. */
#define TIME_ADDRRETRY          (4 * 600)       /* Time to wait before trying to reclaim a
n address. */
#define TIME_ADDRRECLAIM        (900)   /* Time for which an address must be in the reclai
mation */
                                        /* queue before being moved to the free list. */

#define RECLAIM_QUEUE_MAX       15      /* Maximum number of addresses in reclaimation queue. */



/*      dynamic_ip.c
 *
 * This file contains code to manage a range of dynamic IP addresses on a network.
 */
	


/* Queue structures  */

struct  q_elt {
        struct  q_elt *next;
};
struct  q {
        char *head;
        char *tail;
};

/* Dynamic IP structures */
struct daddr {
        struct daddr	*da_next;       /* Queue link. */
        int32		da_addr;        /* IP address. */
        time_t		da_time;        /* last time this address was answered for. */
	uint8		da_hwaddr[1];   /* Hardware address, variable length. */
};

struct drange_desc {
        struct drange_desc *dr_next;    /* Queue link. */
        struct iface    *dr_iface;      /* Pointer to network information. */
	struct timer	timer;		/* Timer for reclaiming */
        int32    	dr_start;       /* First IP address in range. */
        int32    	dr_end;         /* Last IP address in range. */
        uint16           dr_acount;      /* Number of IP addresses in range. */
        uint16           dr_fcount;      /* Number of IP addresses in free. */
        uint16           dr_rcount;      /* Number of IP addresses on reclmation queue  */
        uint16           dr_thon;        /* Threshold for turning on reclaimation. */
        uint16           dr_thcritical;  /* Threshold for critical reclaimation. */
        uint16           dr_thoff;       /* Threshold for turning off reclaimation. */
        int32           dr_time_addrretry;      /* Time to wait before retrying addresses.
						   Varies with state. */
        uint16           dr_hwaddrlen;   /* Length of hardware address. */
	uint8   dr_rstate;      /* Reclaimation state. */
	uint8   dr_vstate;      /* Verification state. */
        time_t          dr_rtime;       /* Time stamp for reclaimation. */
        struct daddr    *dr_raddr;      /* Address being verified. */
        struct daddr    *dr_table;      /* Pointer to table of addresses. */
        struct q        dr_usedq;       /* Pointer to list of used addresses. */
        struct q        dr_reclaimq;    /* Pointer to list of addrs being reclaimed.  */
        struct q        dr_freeq;       /* Pointer to list of free addresses. */
};

#define da_structlen(dr)        (sizeof (struct daddr) + dr->dr_hwaddrlen)
#define da_getnext(dr,da)       ((struct daddr *) ((unsigned char *)da + da_structlen(dr)))


/*
 * Globals.
 */

int ifaceToArpMap[] = {
        0, 		                        /* CL_NONE */
        ARP_ETHER, 	                        /* CL_ETHERNET */
        ARP_PRONET, 	                        /* CL_PRONET_10 */
        ARP_IEEE802,	                        /* CL_IEEE8025 */
        0, 		                        /* CL_OMNINET */
        ARP_APPLETALK,                          /* CL_APPLETALK */
        0, 	          	                /* CL_SERIAL_LINE */
        0, 		                        /* CL_STARLAN */
        ARP_ARCNET, 	                        /* CL_ARCNET */
        ARP_AX25,                               /* CL_AX25 */
        0,                                      /* CL_KISS */
        0,                                      /* CL_IEEE8023 */
        0,                                      /* CL_FDDI */
        0,                                      /* CL_INTERNET_X25 */
        0,                                      /* CL_LANSTAR */
        0,                                      /* CL_SLFP */
        ARP_NETROM,                             /* CL_NETROM */
        0                                       /* NCLASS */
};



static struct q                 rtabq;
struct timer			da_timer;
char				bp_ascii[128];

static void da_runtask(void *arg);
struct q_elt *q_dequeue(struct q *queue);
static void da_closeup(struct drange_desc *dr);
static void dprint_addresses(struct drange_desc *dr);
static int q_remove(struct q *source_queue,struct q_elt *qel);
static void iptoa(int32 ipaddr,char ipstr[16]);
static void da_task(void);
static int da_fill_reclaim(struct drange_desc *dr);
static void da_do_verify(struct drange_desc *dr,int pendtime);
static void da_enter_reclaim(struct drange_desc *dr);
static void da_enter_done(struct drange_desc *dr);
static void da_enter_off(struct drange_desc *dr);
static void q_enqueue(struct q 	*queue,struct q_elt *elem);
static int da_get_old_addr(struct drange_desc *dr,uint8 *hwaddr,struct daddr **dap);
static int da_get_free_addr(struct drange_desc *dr,struct daddr **dap);
static void da_enter_critical(struct drange_desc *dr);
static void q_init(struct q *queue);

extern int bp_ReadingCMDFile;



/*
 * Shutdown routines.
 */

/*
 * Done serving a network.
 */
da_done_net(iface)
struct iface *iface;
{
        struct drange_desc *dr;

        /* Find the network table */
        for(dr = (struct drange_desc *) rtabq.head; dr != NULL; dr = dr->dr_next){
                if(iface == dr->dr_iface)
                        break;
        }

        if(dr == NULL){
		bp_log("Range for interface '%s' not found.\n", iface->name);
		return -1;
	}

        da_closeup(dr);
	bp_log("Range removed for iface %s\n", iface->name);
        return 0;
}




/*
 * Print the status of the da structures.
 */
void
da_status(iface)
struct iface *iface;
{
        struct drange_desc *dr;

	/* If no interface was specified, print all the range information */
	if(iface == NULL){
        	for(dr = (struct drange_desc *) rtabq.head; dr != NULL; 
		 dr = dr->dr_next) 
			dprint_addresses(dr);
	
	} else {
		/* Print the specified range's information */
		/* Find the specified interface */
        	for(dr = (struct drange_desc *) rtabq.head; 
		 (dr != NULL) && (dr->dr_iface != iface); 
		 dr = dr->dr_next)
			; 
		
		/* If network not found, return */
		if(dr == NULL){
			printf("Range for interface '%s' not found.\n", iface->name);
			return;	
		}
		/* The range has been found.  Print it. */
		dprint_addresses(dr);
	}
}



/*
 * Finish up service.  Close up on each of the address ranges.
 */
void
da_shut()
{
        struct drange_desc *dr;

	stop_timer(&da_timer);
        while((dr = (struct drange_desc *)q_dequeue (&rtabq)) != NULL)
                da_closeup(dr);
}


/*
 * Release resource for a network.
 */
static void
da_closeup(dr)
struct drange_desc *dr;
{
        free(dr->dr_table);			/* Free the address table. */
        q_remove(&rtabq, (struct q_elt *)dr);	/* Dequeue the range descriptor. */
        free(dr);				/* Free the range descriptor. */
}



/* This is only called from a command */
static void
dprint_addresses(dr)
struct drange_desc *dr;
{
        struct daddr *da;
        char ipa[16];
	char ipb[16];
	struct arp_type	*at;
	
	at = &Arp_type[dr->dr_iface->iftype->type];

	iptoa(dr->dr_start, ipa);
	iptoa(dr->dr_end, ipb);
	printf("Interface %s range: %s - %s\n", dr->dr_iface->name, ipa, ipb);

        da = (struct daddr *) dr->dr_freeq.head;
	printf("Free address queue\n");
        while(da){
                iptoa(da->da_addr, ipa);
                printf("    %s  last used by %s\n", ipa,(*at->format)(bp_ascii, da->da_hwaddr));
                da = da->da_next;
        }

        da = (struct daddr *) dr->dr_usedq.head;
        printf("\nUsed address queue\n");
        while(da){
                iptoa(da->da_addr, ipa);
                printf("    %s  in use by %s\n", ipa, (*at->format)(bp_ascii, da->da_hwaddr));
                da = da->da_next;
        }

        da =(struct daddr *) dr->dr_reclaimq.head;
        printf("\nReclaimation address queue\n");
        while(da){
                iptoa(da->da_addr, ipa);
                printf("    %s  in use by %s?\n", ipa, (*at->format)(bp_ascii, da->da_hwaddr));
                da = da->da_next;
        }
        printf("\n");
}



/*
 * Reclaimation routines.
 */
static void
da_runtask(p)
void *p;
{
	stop_timer(&da_timer);
	da_task();
	set_timer(&da_timer,TIME_RWAIT*1000L);
	start_timer(&da_timer);
}

/*
 * Called periodically to run reclaimation.
 */
static void
da_task()
{
	struct drange_desc *dr;
	time_t now;
	int arpHardware, arpPendtime;

	now = time(NULL);

	for(dr = (struct drange_desc *)rtabq.head; dr != NULL; dr = dr->dr_next){

		arpHardware = ifaceToArpMap [dr->dr_iface->iftype->type];
		arpPendtime = Arp_type[arpHardware].pendtime;

		if(!(dr->dr_rstate & R_OFF)){	/* If doing reclaimation on this range. */
			if(dr->dr_vstate == V_SWAIT){	/* If in wait sub-state. */
				/* Doing reclaimation on this range and am waiting to 
				 * start a cycle of address
				 * verification.  Check if it is time to start the 
				 * cycle. */

				if(now - dr->dr_rtime > TIME_SWAIT){
					/* Start the cycle.  */
					if(!(dr->dr_rstate & R_DONE))
						da_fill_reclaim(dr);

					dr->dr_vstate = V_VERIFY; /* verify sub-state. */
					dr->dr_raddr = NULL; /* start at beginning */
				}
			}
			/* If in the verify state (may have just been changed above), and 
			 * enough time has passed since last lookup, check it and start
			 * the next lookup. */

			if(dr->dr_vstate == V_VERIFY){
				if(now - dr->dr_rtime > arpPendtime){
					da_do_verify(dr, arpPendtime); /* Verify address. */
					dr->dr_rtime = time(NULL); /* Set time stamp. */
					if(dr->dr_raddr == NULL){ /* If at end... */
						dr->dr_vstate = V_SWAIT; /* Q empty; enter wait sub-state. */
					}
				}
			}
			
			/* 
			 * State transitions.  May have moved some addresses to free list.
			 * If so, I may be able to move to a "lower" state.
			 */
			switch(dr->dr_rstate){
			/* case R_OFF: Not handled. */
			case R_CRITICAL:
				/* Have conditions droped below critical threshhold? */
				if(dr->dr_fcount > dr->dr_thcritical)	
					da_enter_reclaim(dr);
				/* Fall through. */
			case R_RECLAIM:
				/* Have I reclaimed enough addresses? */
				if(dr->dr_fcount > dr->dr_thoff)	
					da_enter_done(dr);
				/* Fall through. */
			case R_DONE:
				/* Am I in the done state and have exausted the reclaimation queue? */
				if((dr->dr_rstate & R_DONE) && dr->dr_reclaimq.head == NULL) 
					da_enter_off(dr);
				break;
			}
		}
	}
}




/* 
 * Enter the DONE state.  Can't get to the done state from the off state.
 */
static void
da_enter_done(dr)
struct drange_desc *dr;
{
	char ipa[16], ipb[16];
	
	iptoa(dr->dr_start, ipa);
	iptoa(dr->dr_end, ipb);

	if((dr->dr_rstate & R_OFF) == 0){ 
		dr->dr_rstate = R_DONE;
		dr->dr_time_addrretry = TIME_ADDRRETRY;		/* Wait a while before retrying addresses. */
	}
}


/* 
 * Enter the OFF state.
 */
static void
da_enter_off(dr)
struct drange_desc *dr;
{
	char ipa[16], ipb[16];
	
	iptoa(dr->dr_start, ipa);
	iptoa(dr->dr_end, ipb);

	dr->dr_rstate = R_OFF;
}


/*
 * Verify addresses.
 * To avoid flodding the network and our address resolution queue I only send
 * out one ARP at a time.  This routine is called periodically to step through
 * the reclaimation queue.  The first step is to check for a responce to the
 * ARP that was sent out previously.  If there is a responce I move the address
 * to the used queue. The next step is to send out an ARP for the next address
 * on the recliamation queue. After a suitable intervel (TIME_VTIME) I'll be
 * called again.
 */
static void
da_do_verify(dr, pendtime)
struct drange_desc *dr;
int pendtime;
{
	struct daddr *da, *dn;
	struct iface *iface;
	long now;
	struct arp_tab *ap;
	uint16 arpType;
	
 	now = time(NULL);
 	iface = dr->dr_iface;
	arpType = ifaceToArpMap[iface->iftype->type];

	/*
 	 * If I sent an ARP for an address, check if that ARP has been responded to.
	 * If dr_raddr points to an address record, I have previously sent an
	 * ARP for that address.  Check the ARP cache for a responce.
	 * If dr_raddr is NULL then I am to start at the head of the reclaim queue.
 	 */

	if(dr->dr_raddr != NULL){
		/* ARP has been sent for dr_raddr.  Check the ARP cache for a responce. */
		da = dr->dr_raddr;
		dn = da->da_next;

		ap = arp_lookup(arpType, da->da_addr);

		if((ap != NULL) && (ap->state == ARP_VALID)){
			/* Host responded to arp.  Place address on used queue.
			 * Copy in physical address of host using address to
			 * make sure our info is up to date.  
			 * I could verify that physical address of host
			 * responding to  ARP matches the physical address of
			 * the host I think owns the address.  If don't match
			 * someone is probably using an incorrect address.
			 */

			q_remove(&dr->dr_reclaimq, (struct q_elt *)da);
			--dr->dr_rcount;
			da->da_time = now;		/* Time tested. */
			memcpy(da->da_hwaddr, ap->hw_addr, Arp_type[ap->hardware].hwalen);
			q_enqueue(&dr->dr_usedq, (struct q_elt *)da);

		} else {
			/* Host did not respond to ARP.  If addr on reclaim
			 * queue long enough, move it to the free queue.
			 */
			if(now - da->da_time >= pendtime){
				q_remove(&dr->dr_reclaimq, (struct q_elt *)da);
				--dr->dr_rcount;
				q_enqueue(&dr->dr_freeq,(struct q_elt *)da);
				++dr->dr_fcount;
				bp_log("Reclaimed address %s on net %s.\n", 
					inet_ntoa(da->da_addr), dr->dr_iface->name);
			}
		}
	} else {
		/* Use first addr in reclaimq. */
		dn = (struct daddr *) dr->dr_reclaimq.head;
	}
	/*
 	 * Now move to the next entry in the queue and ARP for it.
 	 */
 	da = dn;
	if(da != NULL){
		ap = arp_lookup(arpType, da->da_addr);
		if(ap != NULL) arp_drop(ap);
		res_arp(iface, arpType, da->da_addr, NULL);
	}
	dr->dr_raddr = da;	/* Verify this address next time around. */
	dr->dr_rtime = time(NULL);
}


/*
 * Fill the reclaimation list from the used list.  Take addresses off the head
 * of the used queue until the reclaim queue is full, the used queue is empty,
 * or the address at the head of the used queue has been verified (responded
 * to an ARP) within dr_time_addrretry tocks.
 */
static int
da_fill_reclaim(dr)
struct drange_desc *dr;
{
        struct daddr *da;
        long now;

        now = time(NULL);

        while(dr->dr_rcount < RECLAIM_QUEUE_MAX){
		/* Look at first address on used queue. */
                da = (struct daddr *) dr->dr_usedq.head;
                if(da == NULL)
			return 0;    /* If used queue is empty, done filling. */
                if(now - da->da_time < dr->dr_time_addrretry)
                        return 0;

		/* If the first element has responded to in ARP recently.
		 * I am done filling.
		 */
		/* Get first address on used queue. */
                da = (struct daddr *) q_dequeue(&dr->dr_usedq);
		/* Mark time addr put in reclaim queue. */
                da->da_time = now;
		/* Put it at end of reclaim queue. */
                q_enqueue(&dr->dr_reclaimq,(struct q_elt *)da);
                ++dr->dr_rcount;
        }
        return 0;
}


/*
 * Address assignment routines.
 */

/*
 * Assign an address.
 */
int
da_assign(iface, hwaddr, ipaddr)
struct iface *iface;	/* -> Pointer to lnet struct of net on which to assign addr. */
uint8	*hwaddr;	/* -> Pointer to hardware address of hosts. */
int32	*ipaddr;	/* <- Address assigned to host. */
{
	struct drange_desc *dr;	
	struct daddr *da;	
	int status;
	struct arp_type	*at;

	/* Find the network table */
	for(dr = (struct drange_desc *) rtabq.head; dr != NULL; dr = dr->dr_next){
		if(iface == dr->dr_iface)
			break;	
	}

	if(dr == NULL){
		*ipaddr = 0;
		return ERR_NOIPADDRESS;
	}

	/* If this host had an address assigned previously, try to reassign
	 * that. If no previous address, assign a new one.
	 */
	status = da_get_old_addr(dr, hwaddr, &da);
	if(status != 0) 
		status = da_get_free_addr(dr, &da);
	
	/* If I got an address, assign it and link it in to the use list. */
	if(status == 0){
		memcpy(da->da_hwaddr, hwaddr, dr->dr_hwaddrlen);
		*ipaddr = da->da_addr;
		da->da_time = time(NULL);	/* Time assigned */
		q_enqueue(&dr->dr_usedq,(struct q_elt *)da);
		at = &Arp_type[dr->dr_iface->iftype->type];
		bp_log("IP addr %s assigned to %s on network %s\n",
		 inet_ntoa(*ipaddr),
		 (*at->format)(bp_ascii, hwaddr), dr->dr_iface->name);
	}

        switch(dr->dr_rstate){
        case R_OFF:
        case R_DONE:
                if(dr->dr_fcount <= dr->dr_thon) 
			da_enter_reclaim(dr);
                /* Fall through. */
        case R_RECLAIM:
                if(dr->dr_fcount <= dr->dr_thcritical) 
			da_enter_critical(dr);
                break;
        /* case R_CRITICAL: is not handled. */
        }
        return status;
}


/*
 * Enter the reclaimation state.
 */
static void
da_enter_reclaim(dr)
struct drange_desc *dr;
{
        char ipa[16], ipb[16];

        iptoa(dr->dr_start, ipa);
        iptoa(dr->dr_end, ipb);

        if(dr->dr_rstate & R_OFF){
                dr->dr_vstate = V_SWAIT;  /* da_enter_reclaim: R_OFF */
                dr->dr_rtime = 0;
        }
        dr->dr_rstate = R_RECLAIM;
        dr->dr_time_addrretry = TIME_ADDRRETRY;         /* Wait a while before retrying addresses. */
}

/*
 * Search for hwaddr on the used list, the reclaimation list, and the free list.
 */
static int
da_get_free_addr(dr, dap)
struct drange_desc *dr;
struct daddr **dap;
{
        *dap = (struct daddr *) q_dequeue(&(dr->dr_freeq));
        if(*dap == NULL) 
		return ERR_NOIPADDRESS;
        --dr->dr_fcount;

        return 0;
}

/*
 * Search for hwaddr on the used list, the reclaimation list, and the free list.
 */
static int
da_get_old_addr(dr, hwaddr, dap)
struct drange_desc *dr;
uint8  	*hwaddr;
struct daddr **dap;
{
        struct daddr *da;

	/* Search the used queue */
        for(da = (struct daddr *) dr->dr_usedq.head; da != NULL; da = da->da_next){
                if(memcmp(da->da_hwaddr, hwaddr, dr->dr_hwaddrlen) == 0){
                        q_remove(&dr->dr_usedq,(struct q_elt *)da);
                        *dap = da;
                        return 0;
                }
        }

	/* Search the relaimq queue */
        for(da = (struct daddr *) dr->dr_reclaimq.head; da != NULL; 
		da = da->da_next){
                if(memcmp(da->da_hwaddr, hwaddr, dr->dr_hwaddrlen) == 0){
                        /* Here is the address.  I have to be carefull in removing it from
			 * reclaim queue, I may be verifying this address.
                         * If I am, I have to fix up the pointers before removing this
                         * element.
                         */
                        if((dr->dr_rstate & R_OFF) == 0 && dr->dr_vstate == V_VERIFY 
				&& dr->dr_raddr == da){ 
				/* I am verifying this very address.  */
                                /* Start over.  
				 * This should happen very infrequently at most. */
                                dr->dr_vstate = V_SWAIT;  /* get_old_addr */
                        }
                        q_remove(&dr->dr_reclaimq,(struct q_elt *)da);
                        *dap = da;
                        return 0;
                }
        }

	/* Search the free queue */
        for(da = (struct daddr *) dr->dr_freeq.head; da != NULL; da = da->da_next){
                if(memcmp(da->da_hwaddr, hwaddr, dr->dr_hwaddrlen) == 0){
                        q_remove(&dr->dr_freeq,(struct q_elt *)da);
                        --dr->dr_fcount;
                        *dap = da;
                        return 0;
                }
        }
        return ERR_NOIPADDRESS;
}

#ifdef	notdef
static void
dprint_dr_record(dr)
struct drange_desc *dr;
{
        bp_log("Queue link   x%lx\n", dr->dr_next);
        bp_log("Pointer to network information         x%lx\n", dr->dr_iface);
        bp_log("First IP address in range              x%lx\n", dr->dr_start);
        bp_log("Last IP address in range               x%lx\n", dr->dr_end);
        bp_log("Number of IP addresses in range        %d\n", dr->dr_acount);
        bp_log("Number of IP addresses in free         %d\n", dr->dr_fcount);
        bp_log("Number of IP addresses on reclaimation queue %d\n", 
		dr->dr_rcount);
        bp_log("Threshold for turning on reclaimation  %d\n", dr->dr_thon);
        bp_log("Threshold for critical reclaimation    %d\n", dr->dr_thcritical);
        bp_log("Threshold for turning off reclaimation %d\n", dr->dr_thoff);
        bp_log("Time to wait before retrying addresses %ld\n", 
		dr->dr_time_addrretry);
        bp_log("Length of hardware address             %d\n", dr->dr_hwaddrlen);
        bp_log("Reclaimation state                     %d\n",(int)dr->dr_rstate);
        bp_log("Verification state                     %d\n",(int)dr->dr_vstate);
        bp_log("Time stamp for reclaimation            %ld\n", dr->dr_rtime);
        bp_log("Address being verified                 x%lx\n", dr->dr_raddr);
        bp_log("Pointer to table of addresses          x%lx\n", dr->dr_table);
        bp_log("uesdq x%lx  reclaimq                   x%lx  freeq x%lx\n", dr->dr_usedq, 
		dr->dr_reclaimq, dr->dr_freeq);
}
#endif


/*
 * Enter the critical reclaimation state.
 */
static void
da_enter_critical(dr)
struct drange_desc *dr;
{
        char ipa[16], ipb[16];
	char *ipc;

	ipc = inet_ntoa(dr->dr_start);
	strcpy(ipa, ipc);
	ipc = inet_ntoa(dr->dr_end);
	strcpy(ipb, ipc);

        if((dr->dr_rstate & R_OFF) == 0){
                dr->dr_vstate = V_SWAIT;	/* Enter critical, & R_OFF */
                dr->dr_rtime = 0;
        }
        dr->dr_rstate = R_CRITICAL;
        dr->dr_time_addrretry = 0;      /* Retry addresses as fast as possible. */
}

/*
 * Initialization	
 */
/*
 * Initialize the Dynamic address assignment module.
 */
int
da_init()
{
        q_init(&rtabq);
        return 0;
}

/*
 * Begin dynamic address service for a network.
 */
int
da_serve_net(iface, rstart, rend)
struct iface *iface;		/* Pointer to lnet record. */
int32 rstart;			/* First address in range. */
int32 rend;			/* Last address in range. */
{
	struct drange_desc *dr;	/* Pointer to the range descriptor. */
	struct daddr *da;	/* Pointer to an address structure. */
	int32 rcount;		/* Number of addresses range. */
	time_t now;		/* Current time. */
	uint16 i;
	char ipc[16], ipd[16];

        /* Find the network table */
        for(dr = (struct drange_desc *) rtabq.head; dr != NULL;
	 dr = dr->dr_next){
                if(iface == dr->dr_iface)
                        break;
        }

	
	if(dr == NULL){
		/* If there is no network table, allocate a new one
		 *
 		 * Allocate the memory I need.
 		 */
		dr = (struct drange_desc *) calloc(1, sizeof(*dr));
		if(dr == NULL) 
			return E_NOMEM;
	} else if((dr->dr_start != rstart) || (dr->dr_end != rend)) 
		/* If the range is different, create a new range */
		free(dr->dr_table);
	else
		return 0; /* There is no change, return */


	rcount = (rend - rstart) + 1;
	da = (struct daddr *) calloc(1,(sizeof (*da) + iface->iftype->hwalen) * rcount);
	if(da == NULL) 
		return E_NOMEM;

	/* 
	 * Got the memory, fill in the structures.
	 */
	dr->dr_iface = iface;
	dr->dr_start = rstart;
	dr->dr_end = rend;
	dr->dr_acount = rcount;
	dr->dr_fcount = 0;
	dr->dr_rcount = 0;
	dr->dr_thon = (rcount * THRESH_ON) / 100;
	dr->dr_thcritical = THRESH_CRITICAL;
	dr->dr_thoff = (rcount * THRESH_OFF) / 100;
	dr->dr_time_addrretry = 0;
        dr->dr_hwaddrlen = iface->iftype->hwalen;
	dr->dr_rstate = R_OFF;
	dr->dr_vstate = V_SWAIT;			/* Initialize */
	dr->dr_rtime = 0;
	dr->dr_raddr = NULL;
	dr->dr_table = da;

	/* 
	 * Fill in the table and link them all onto the used list.
	 */
	time(&now);
	for(i = 0, da = dr->dr_table; i < dr->dr_acount; ++i, da = da_getnext(dr, da)){
		da->da_addr = rstart++;
		da->da_time = 0;		/* Initiallize at 0, only here */
		q_enqueue(&dr->dr_usedq,(struct q_elt *)da);
	}
	/* and set up the timer stuff */
	if(rtabq.head == NULL){
		set_timer(&da_timer,TIME_RWAIT*1000L);
       		da_timer.func = da_runtask;
	       	da_timer.arg = (void *) 0;
		start_timer(&da_timer);
	}
	q_enqueue(&rtabq,(struct q_elt *)dr);
	da_enter_critical(dr);	/* Start reclaiming some of these addresses. */

	iptoa(dr->dr_start, ipc);
	iptoa(dr->dr_end, ipd);
	bp_log("DynamicIP range: %s - %s\n", ipc, ipd);
	return 0;
}


/*
 * Routines to implement a simple forward linked queue.
 */

/*
 *      q_init()
 *      Initialize simple Q descriptor
 */
static void
q_init(queue)
struct q *queue;
{
        queue->head = 0;
        queue->tail = 0;
}


/*
 *      q_enqueue()
 *              Enqueue an element in a simple Q.
 */
void
q_enqueue(queue, elem)
struct q *queue;
struct q_elt *elem;
{
        struct q_elt *last;

        if(queue->tail != NULL){  /* If not empty Q... */
                last = (struct q_elt *) queue->tail;
                last->next = elem;
        }
        else
		queue->head = (char *) elem;

        queue->tail = (char *) elem;
        elem->next = NULL;
}


/*
 *      q_dequeue       ()
 *      Pull an element off of the head of a Q.
 */
struct q_elt *
q_dequeue(queue)
struct q *queue;
{
        struct q_elt *elem;

        if(queue->head == NULL)
		return NULL; /* return NULL when empty Q */
        elem = (struct q_elt *) queue->head;
        queue->head = (char *) elem->next;
        elem->next = NULL;
        if(queue->head == NULL)
		queue->tail = NULL;
        return elem;
}


/*
 *      Remove an element from the middle of a queue.  Note that
 *      there is no mutex here, so this shouldn't be used on
 *      critical Qs
 */

static int
q_remove(source_queue, qel)
struct q *source_queue;
struct q_elt *qel;
{
        struct q_elt *prev, *e;

        /*   Case : removing first in Q */

        if(qel == (struct q_elt *) source_queue->head){
                source_queue->head = (char *)qel->next;  /* trying to remove first in queue... */
                if(source_queue->head == NULL)  	/* nothing left... */
                        source_queue->tail = NULL; 	/* blank out the Q */
                else if(source_queue->head == source_queue->tail){ /* One thing left */
                        e = (struct q_elt *) source_queue->head; /* As insurance, set it's next to NULL. */
                        e->next = NULL;
                }
                return 0;
        }

        /* find Q element before qel, so that we can link around qel */
        for(prev = (struct q_elt *) source_queue->head; prev->next != qel; prev = prev->next)
                if(prev == NULL)
                        return 1;

        /* Case : Removing last in Q */

        if(qel == (struct q_elt *) source_queue->tail){     /* trying to remove last one in queue... */
                prev->next = NULL;   /* there is a prev elt, since we return on first */
                source_queue->tail = (char *) prev;
                return 0;
        }

        /*  else, removing a queue element in the middle...  */
        prev->next = qel->next;
        return 0;
}

/*
 * Support Routines
 */

static void
iptoa(ipaddr, ipstr)
int32 ipaddr;
char ipstr[16];
{
	char *tmpStr;

	tmpStr = inet_ntoa(ipaddr);
	strcpy(ipstr, tmpStr);
}


#ifdef	notdef
static  void
build_hex_string(fromstr, len, tostr)
char *fromstr; int  len;
char *tostr;
{
	int i;

	for(i=0; i < len; i++){
		sprintf(tostr, "%02x", fromstr[i]);
		tostr++;
		tostr++;
	}
	fromstr[len] = 0;
}
#endif
