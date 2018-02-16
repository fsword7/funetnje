/* LOAD_DMF_MAIN.C  V1.2
 |  Link with READ_CONFIG.C
 | In the meantime, load the DMF routine and print its memory address.
 |
 | V1.1 - Read the name of the DMF file from the configuration file.
 | V1.2 - Add dummy routine add_gone_user() and the structures IOLINES and
 |        the variable LogLevel in order to link smoothly.
 */

#include	<stdio.h>
#define	MAIN
#include "consts.h"

INTERNAL struct	LINE	IoLines[MAX_LINES];
INTERNAL int	LogLevel;

main()
{
	long	status, LOAD_DMF();
	FILE	*fd;

	*ADDRESS_FILE = '\0';
	read_configuration();
	if(*ADDRESS_FILE == '\0') {
		printf("LOAD_DMF: No DMF-FILE command in configuration file\n");
		exit(44);
	}
	status = LOAD_DMF();
	if(status == 0)
		exit(44);	/* error */

	printf("NJE-LOAD: DMF framing routine loaded, memory address=x^%x\n",
		status);

	if((fd = fopen(ADDRESS_FILE, "w")) == NULL) {
		printf("Can't create ADDRESS.DAT !!!\n");
		exit(1);
	}
	fprintf(fd, "%x\n", status);
	fclose(fd);
}

/* Dummy routine */
logger()
{
}

/*
 |  Case insensitive strings comparisons. Return 0 only if they have the same
 |  length.
*/
#define	TO_UPPER(c)	(((c >= 'a') && (c <= 'z')) ? (c - ' ') : c)
compare(a, b)
char	*a, *b;
{
	register char	*p, *q;

	p = a; q = b;

	for(; TO_UPPER(*p) == TO_UPPER(*q); p++,q++)
		if((*p == '\0') || (*q == '\0')) break;

	if((*p == '\0') && (*q == '\0'))	/* Both strings done = Equal */
		return 0;

/* Not equal */
	return(TO_UPPER(*p) - TO_UPPER(*q));
}

add_gone_user()
{
}
