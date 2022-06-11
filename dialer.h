/* Dynamic dialup params */
struct asydialer {
	char *actfile;		/* Script to activate line */
	char *dropfile;		/* Script to drop line */
	char *ansfile;		/* Script to answer incoming call */
	struct timer idle;	/* Idle timeout timer */
	long originates;	/* Count of times we bring up the link */
	long answers;		/* Count of incoming calls */
	long localdrops;	/* Count of times we dropped the link */
	int dev;
};

int sd_init(struct iface *,int32,int,char **);
int sd_stat(struct iface *);
