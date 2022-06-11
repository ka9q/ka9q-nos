unsigned long decode(unsigned char *data,signed char *symbols,
 unsigned int nbits,int mettab[2][3],int delta,unsigned long maxcycles);
int encode(unsigned char *symbols,unsigned char *data,unsigned int nbytes);
void expsym(signed char *,unsigned char *,unsigned int);
double gen_met(int mettab[2][3],double ber,double nc);

extern unsigned char Partab[];
extern unsigned char Bitcnt[]; 
extern unsigned char Revtab[];

/* PN symbol scrambler definitions
 * The 23-degree polynomial is the CCITT standard for V.29, V.32 and V.33
 */
#define	PN_START	1
#define	PN_SCRAMBLE(X)	Partab[((X) & 0x420000) >> 17]
/*#define	PN_SCRAMBLE(X)	Partab[((X) & 0x60) >> 5] */


#define	ILEAVE	64

/* Convolutional coding polynomials. All are rate 1/2, K=32 */
#define	LL 1

#ifdef	NASA_STANDARD
/* "NASA standard" code by Massey & Costello
 * Nonsystematic, quick look-in, dmin=11, dfree=23
 * used on Pioneer 10-12, Helios A,B
 */
#define	POLY1	0xbbef6bb7
#define	POLY2	0xbbef6bb5
#endif

#ifdef	MJ
/* Massey-Johannesson code
 * Nonsystematic, quick look-in, dmin=13, dfree>=23
 * Purported to be more computationally efficient than Massey-Costello
 */
#define	POLY1	0xb840a20f
#define POLY2	0xb840a20d
#endif

#ifdef	LL
/* Layland-Lushbaugh code
 * Nonsystematic, non-quick look-in, dmin=?, dfree=?
 */
#define	POLY1	0xf2d05351
#define	POLY2	0xe4613c47
#endif

#define	ENCODE(sym,encstate){\
	unsigned long _tmp;\
\
	_tmp = (encstate) & POLY1;\
	_tmp ^= _tmp >> 16;\
	(sym) = Partab[(_tmp ^ (_tmp >> 8)) & 0xff];\
	_tmp = (encstate) & POLY2;\
	_tmp ^= _tmp >> 16;\
	(sym) |= Partab[(_tmp ^ (_tmp >> 8)) & 0xff] << 1;\
}


