#ifndef _PPPPAP_H
#define _PPPPAP_H
					/* PAP config packet codes */
/* PAP control block */
struct pap_s {
	char *username;			/* Username for REQ */
	char *password;			/* Password for REQ */
	char *message;			/* message from last ACK/NAK */
	struct proc *pp;		/* user/password process */
};

#define PAP_REQ_TRY	100		/* REQ attempts */
#define PAP_FAIL_MAX	5		/* # attempts from remote */
#define PAP_TIMEOUT	3		/* Seconds to wait for response */


int pap_local(struct ppp_s *ppp_p);
int pap_remote(struct ppp_s *ppp_p);
void pap_down(struct fsm_s *fsm_p);
void pap_proc(struct fsm_s *fsm_p, struct mbuf **bpp);

int doppp_pap(int argc, char *argv[], void *p);
void pap_init(struct ppp_s *ppp_p);

#endif /* _PPPPAP_H */
