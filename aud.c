#include <stdio.h>
#include <math.h>
#define	DMASIZE	16384
main()
{
	unsigned char buf[DMASIZE];
	double x,y;
	unsigned samp;
	int i;
	FILE *fp;

	fp = fopen("test.aud","wb");

	for(i=0;i<4096;i++){
		x = 2*3.141592653589*(i*256.)/4096.;
		samp = 32767 * cos(x);
		buf[4*i] = samp;
		buf[4*i+1] = samp >> 8;
		x = 2*3.141592653589*(i*192.)/4096.;
		samp = 32767 * cos(x);
		buf[4*i+2] = samp;
		buf[4*i+3] = samp >> 8;
	}
	fwrite(buf,1,DMASIZE,fp);
	fclose(fp);
}

