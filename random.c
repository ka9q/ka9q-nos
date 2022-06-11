/* Maintain source of random numbers for cryptographic keys, etc
 * This is inherently machine dependent code
 */
#include "stdio.h"
#include <dos.h>
#include "global.h"
#include "timer.h"
#include "md5.h"
#include "nospc.h"
static unsigned char Randstate[16];
static unsigned long Randcntr;

/* Spawned at startup to initialize random generator state.
 * Rapidly reads counter/timer bits a few thousand times, relying
 * on the phase jitter between the CPU clock and the counter/timer
 * clock. THIS IS NOT RELIABLE! It works on some motherboards and
 * not others. It's fast, so we might as well do it, but it must
 * not be the only source of randomness.
 */
void
rand_init(n,v1,v2)
int n;
void *v1,*v2;
{
	MD5_CTX context;
	int i,j;
	unsigned short values[200];

	for(i = 0;i< 16;i++){
		for(j=0;j<200;j++)
			values[j] = clockbits();
		MD5Init(&context);
		MD5Update(&context,Randstate,sizeof(Randstate));
		MD5Update(&context,(unsigned char *)values,sizeof(values));
		MD5Final(Randstate,&context);
		kwait(NULL);	/* Don't hog the machine */
	}
}
/* Fill user buffer with random data. This is done by hashing the random
 * state with a counter so that the random state is not directly
 * revealed.
 */
void
getrand(buf,len)
unsigned char *buf;
int len;
{
	uint16 t;
	MD5_CTX context;
	uint8 digest[16];
	int cnt;

	/* Fold the current time into the random state */
	t = clockbits();
	MD5Init(&context);
	MD5Update(&context,Randstate,sizeof(Randstate));
	MD5Update(&context,(unsigned char *)&t,sizeof(t));
	MD5Final(Randstate,&context);

	/* Now combine the random state and the counter to provide as
	 * much random data as needed
	 */
	while(len != 0){
		MD5Init(&context);
		MD5Update(&context,Randstate,sizeof(Randstate));
		MD5Update(&context,(unsigned char *)&Randcntr,sizeof(Randcntr));
		MD5Final(digest,&context);
		Randcntr++;
		cnt = min(len,sizeof(digest));
		memcpy(buf,digest,cnt);
		len -= cnt;
		buf += cnt;
	}
}

/* Called with a 16-bit scan code whenever a key is hit. Fold the current
 * state, the typed character and the current clock into the new
 * random state.
 */
void
rtype(c)
uint16 c;
{
	uint16 t;
	MD5_CTX context;

	MD5Init(&context);
	MD5Update(&context,Randstate,sizeof(Randstate));
	t = clockbits();
	MD5Update(&context,(unsigned char *)&t,sizeof(t));
	MD5Update(&context,(unsigned char *)&c,sizeof(c));
	MD5Final(Randstate,&context);
}
/* Generate a uniformly-distributed random number between 0 and n-1
 * Uses rejection method
 */
int
urandom(n)
unsigned int n;
{
	uint32 k,i;

#define	MAXRANDOM	((unsigned long)0x7fffffff)
	k = MAXRANDOM - (MAXRANDOM+1) % n;
	do {
		getrand((unsigned char *)&i,sizeof(i));
		i &= ~0x80000000;	/* Make i positive as signed long */
	} while(i > k);
	return i % n;
}
