#include <stdio.h>
main()
{
	int i,j;

	printf("char DHprime[] = {\n");
	for(;;){
		for(i=0;i<8;i++){
			if(scanf("%2x",&j) != 1)
				goto done;
			printf("0x%02x, ",j);
		}
		printf("\n");
	}
done:
	printf("};\n");
}
