#ifndef _LZW_H
#define _LZW_H

/* a string entry */
struct zentry {
	uint16 code;	/* codeword of the prefix string */
	char data;	/* character to add to the prefix string */
};
struct zfast {		/* fast version of string entry */
	uint16 owncode;	/* own codeword */
	uint16 code;	/* codeword of prefix string */
	char data;	/* character to add to prefix string */
};
#define ZCC		256	/* clear code table codeword */
#define ZFLUSH		257	/* codeword that signals a break in coding */

struct lzw {
	uint16 codebits;		/* significant bits in each codeword */
	int maxbits;		/* maximum number of bits per codeword */
#define LZWBITS		9	/* initial number of bits in each codeword */
	int32 prefix;		/* last processed codeword */
	char mode;		/* Compact or fast compression mode */
#define LZWCOMPACT	0
#define LZWFAST		1
	union {
		struct zentry **tbl;	/* compact table */
#define LZWBLK		130		/* size of entry arrays to allocate */
		struct mbuf **bpp;	/* mbuf version of table */
#define ZHASH		256	/* hash table size */
		void *p;	/* generic table pointer */
	} tu;			/* table of entries */
	int nextbit;		/* next bit to process in code stream */
	int version;		/* version number of sender */
#define ZVERSION	3	/* version number */
	int32 cnt;		/* count of processed bytes */
	int32 code;		/* temporary storage for coding in progress */
	int32 next;		/* next code to be added to the table */
	int flushbit;		/* next bit of the ZFLUSH codeword to send */
	/* the following is used by the decoder only */
	struct mbuf *buf;	/* decoded buffer */
};

struct usock;		/* To please Turbo C++ */
void lzwencode(int s,char c);
void lzwinit(int s,int bits,int mode);
void lzwfree(struct usock *up);
void lzwflush(struct usock *up);
int lzwdecode(struct usock *up);

#endif  /* _LZW_H */
