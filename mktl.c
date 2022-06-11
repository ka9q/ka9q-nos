/* Create command files for Borland TLIB command. Reads list of
 * object file names from stdin in free format
 *
 * June 1992 P. Karn
 */
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
int
main()
{
	int c;
	int prev = 0;

	for(;;){
		/* Skip leading white space */
		while(c = getchar(),isspace(c))
			;
		if(c == EOF)
			break;
		if(prev)
			printf(" &\n");
		/* Print "+token" */
		printf("+%c",c);
		while(c = getchar(),!isspace(c) && c != EOF)
			putchar(c);
		if(c == EOF)
			break;
		prev = 1;
	}
	putchar('\n');	/* Final line is empty */
	return 0;
}
